#include <iostream>
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>

#include "SDL2/SDL.h"

#define PI 3.141592654f
#define HALF_PI (PI / 2.0f)
#define SR 22050.0f
#define BPM 120.0f
#define SAMPLE(x) (Uint8((std::clamp((x), -1.0f, 1.0f) * 0.5f + 0.5f) * 255.0f))
#define LERP(a, b, t) ((1.0f - (t)) * (a) + (b) * (t))
#define MIX(a, b) ((a) + (b) - ((a) * (b)))
#define HERTZ (1.0 / 60.0)
#define CHANNELS 8
#define MAX_CHORUS_VOICES 3

class Phase {
public:

	virtual float get(float freq, float sampleRate) {
		float p = m_phase;
		m_phase += (PI * 2.0f * freq) / sampleRate;
		if (m_phase >= PI * 2.0f) {
			m_phase -= PI * 2.0f;
		}
		return p;
	}

	float norm(float freq, float sampleRate) {
		return get(freq, sampleRate) / (PI * 2.0f);
	}

	void reset() {
		m_phase = 0.0f;
	}

protected:
	float m_phase{ 0.0f };
};

class WaveTable {
public:
	enum BasicWaveForm {
		Sine = 0,
		Saw,
		Square,
		Triangle
	};

	WaveTable() {
		setWaveForm(BasicWaveForm::Sine);
	}

	float sample(float t) {
		if (m_noise) {
			if (t >= 0.5f) {
				m_lastNoise = (float(rand() % RAND_MAX) / RAND_MAX) * 2.0f - 1.0f;
			}
			return m_lastNoise;
		}
		const int i = int(std::floor(t * m_waveTable.size())) % m_waveTable.size();
		const int n = (i + 1) % m_waveTable.size();
		return LERP(m_waveTable[i], m_waveTable[n], t);
	}

	bool noise() const { return m_noise; }
	void noise(bool v) { m_noise = v; }

	float& operator [](unsigned int i) { return m_waveTable[i % 16]; }
	const float& operator [](unsigned int i) const { return m_waveTable[i % 16]; }

	void setWaveForm(BasicWaveForm wf) {
		float step = 1.0f / m_waveTable.size();
		float t = 0.0;

		for (size_t i = 0; i < m_waveTable.size(); i++) {
			float v = 0.0f;
			switch (wf) {
				case BasicWaveForm::Sine: v = std::sin(t * PI * 2.0f); break;
				case BasicWaveForm::Saw: v = t * 2.0f - 1.0f; break;
				case BasicWaveForm::Square: v = t >= 0.5f ? 1.0f : -1.0f; break;
				case BasicWaveForm::Triangle: v = std::acos(std::sin(t * PI * 2.0f)) / HALF_PI; break;
			}
			m_waveTable[i] = v;
			t += step;
		}
	}

private:
	std::array<float, 24> m_waveTable{};
	bool m_noise{ false };
	float m_lastNoise{ 0.0f };
};

class ADSR {
public:
	enum State {
		Idle = 0,
		Attack,
		Decay,
		Sustain,
		Release
	};

	ADSR()
		: m_attack(0.0f), m_decay(0.0f), m_sustain(1.0f), m_release(0.0f)
	{}

	ADSR(float a, float d, float s, float r)
	{
		attack(a);
		decay(d);
		sustain(s);
		release(r);
	}

	float attack() const { return m_attack; }
	void attack(float v) { m_attack = v; }

	float decay() const { return m_decay; }
	void decay(float v) { m_decay = v; }

	float release() const { return m_release; }
	void release(float v) { m_release = v; }

	float sustain() const { return m_sustain; }
	void sustain(float v) { m_sustain = v; }

	void gate(bool g) {
		if (g) {
			m_state = Attack;
		} else if (m_state != Idle) {
			m_state = Release;
		}
	}

	float sample(float rate) {
		switch (m_state) {
			default: break;
			case State::Attack: {
				m_out += (1.0f / m_attack) / rate;
				if (m_out >= 1.0f || m_attack <= 1e-5f) {
					m_state = State::Decay;
					m_out = 1.0f;
				}
			} break;
			case State::Decay: {
				m_out -= (1.0f / m_decay) / rate;
				if (m_out <= m_sustain || m_decay <= 1e-5f) {
					m_out = m_sustain;
					m_state = State::Sustain;
				}
			} break;
			case State::Release: {
				m_out -= (1.0f / m_release) / rate;
				if (m_out <= 1e-5f) {
					m_out = 0.0;
					m_state = State::Idle;
				}
			} break;
		}
		return m_out;
	}

	void reset() {
		m_state = State::Release;
		m_out = 0.0f;
	}

	float value() const { return m_out; }

private:
	State m_state;
	float m_attack,
		m_decay,
		m_sustain,
		m_release,
		m_out;
};

static const float NOTE[] = {
	32.70320f,
	34.64783f,
	36.70810f,
	38.89087f,
	41.20344f,
	43.65353f,
	46.24930f,
	48.99943f,
	51.91309f,
	55.00000f,
	58.27047f,
	61.73541f
};

enum Note {
	C = 0,
	Cs,
	D,
	Ds,
	E,
	F,
	Fs,
	G,
	Gs,
	A,
	As,
	B
};

class Instrument {
public:
	Instrument() {
		m_notes.fill(0);
		for (int i = 0; i < m_voicePhases.size(); i++) {
			m_voicePhases[i] = Phase();
		}
	}

	float sample(float freq, float sampleRate) {
		float sample = 0.0f;
		for (int v = 0; v < m_voices; v++) {
			sample += m_waveTable.sample(m_voicePhases[v].norm(freq, sampleRate));
			freq -= 1.0f;
		}
		sample /= m_voices;

		return sample * m_volume.sample(sampleRate);
	}

	WaveTable& waveTable() { return m_waveTable; }
	ADSR& volume() { return m_volume; }

	int voices() const { return m_voices; }
	void voices(int v) { m_voices = std::clamp(v, 1, MAX_CHORUS_VOICES); }

	std::array<int, 16>& notes() { return m_notes; }

private:
	int m_voices{ 1 };
	std::array<Phase, MAX_CHORUS_VOICES> m_voicePhases{};
	std::array<int, 16> m_notes{ 0 };
	WaveTable m_waveTable{};
	ADSR m_volume{};
};

enum class Effect : uint8_t {
	None = 0,
	Vibrato,
	Slide,
	Arpeggio
};

enum class Arpeggio : uint8_t {
	Major = 0,
	Minor,
	Maj7,
	Min7,
	Sus4,
	Sus2,
	Octave
};

struct Sound {
	uint8_t note{ Note::C + 24 };
	float volume{ 1.0f };
	Effect effect{ Effect::None };
	Arpeggio arpeggio{ Arpeggio::Major };
	float effectSpeed{ 1.0f };
	Instrument* instrument{ nullptr };

	float frequency(int noteOffset = 0) const {
		int n = note + noteOffset;
		n = n < 0 ? 0 : n;
		return NOTE[n % 12] * std::pow(2, n / 12);
	}
};

class Channel {
public:
	float sample(float bpm, float step, float sampleRate) {
		const float delay = (60000.0f / bpm) / 1000.0f;
		if (m_current.instrument == nullptr) return 0.0f;

		int noff = m_current.instrument->notes()[m_currentNote];

		float speed = m_current.effectSpeed * 0.25f;
		if (m_current.effect == Effect::Arpeggio) {
			switch (m_current.arpeggio) {
				default: noff = 0; break;
				case Arpeggio::Major: {
					const int offs[3] = { 0, 4, 7 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 3)];
				} break;
				case Arpeggio::Minor: {
					const int offs[3] = { 0, 3, 7 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 3)];
				} break;
				case Arpeggio::Sus4: {
					const int offs[3] = { 0, 5, 7 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 3)];
				} break;
				case Arpeggio::Sus2: {
					const int offs[3] = { 0, 2, 7 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 3)];
				} break;
				case Arpeggio::Maj7: {
					const int offs[4] = { 0, 4, 7, 10 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 4)];
				} break;
				case Arpeggio::Min7: {
					const int offs[4] = { 0, 3, 7, 10 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 4)];
				} break;
				case Arpeggio::Octave: {
					const int offs[2] = { 0, 12 };
					noff += offs[int(m_phase.norm(HERTZ * bpm * speed, sampleRate) * 2)];
				} break;
			}
		}

		float freq = m_current.frequency(noff);
		if (m_current.effect == Effect::Slide && (m_bar % 4) == 0 && m_sliding) {
			float t = m_time / delay;
			freq = LERP(m_previous.frequency(noff), freq, t);
			if (t >= 1.0f - 1e-3f) {
				m_sliding = false;
			}
		}

		if (m_current.effect == Effect::Vibrato) {
			float vt = std::sin(m_phase.get(HERTZ * bpm * speed, sampleRate) + PI) * 0.5f + 0.5f;
			freq -= vt * (NOTE[2] - NOTE[0]);
		}

		m_time += step;
		if (m_time >= delay) {
			m_bar++;
			m_time = 0.0f;
		}

		m_notesTime += step * 2.0f;
		if (m_notesTime >= delay / 2) {
			m_currentNote++;
			if (m_currentNote > 15) {
				m_currentNote = 15;
			}
			m_notesTime = 0;
		}

		return m_current.instrument->sample(freq, sampleRate) * m_current.volume;
	}

	void play(Sound sound) {
		if (m_playing) {
			m_previous.note = m_current.note;
			m_previous.effect = m_current.effect;
			m_previous.arpeggio = m_current.arpeggio;
			m_previous.instrument = m_current.instrument;
			m_previous.volume = m_current.volume;
			m_previous.effectSpeed = m_current.effectSpeed;
		}
		m_current.note = sound.note;
		m_current.effect = sound.effect;
		m_current.arpeggio = sound.arpeggio;
		m_current.instrument = sound.instrument;
		m_current.volume = sound.volume;
		m_current.effectSpeed = sound.effectSpeed;

		m_playing = true;
		m_sliding = sound.effect == Effect::Slide;

		m_phase.reset();
		m_currentNote = 0;
		if (m_current.instrument != nullptr)
			m_current.instrument->volume().gate(true);
	}

	void stop() {
		if (m_current.instrument != nullptr)
			m_current.instrument->volume().gate(false);
		m_sliding = false;
		m_playing = false;
	}

private:
	Phase m_phase{};

	float m_time{ 0.0f }, m_notesTime{ 0.0f };

	Sound m_current{}, m_previous{};
	bool m_playing{ false }, m_sliding{ false };
	int m_bar{ 0 }, m_currentNote{ 0 };
};

class Tracker {
public:
	Tracker(float sampleRate = SR) {
		m_sampleRate = sampleRate;
		for (int i = 0; i < CHANNELS; i++) {
			m_channels[i] = Channel();
		}
	}

	float sample(float bpm = 120.0f, int bars = 4) {
		const float delay = (60000.0f / bpm) / 1000.0f;
		const float step = (1.0f / m_sampleRate) * (bars);
		
		float mix = 0.0f;
		for (int i = 0; i < CHANNELS; i++) {
			// MIX = a + b - a * b
			mix = MIX(mix, m_channels[i].sample(bpm, step, m_sampleRate));
		}

		m_time += step;
		if (m_time >= delay) {
			
			m_time = 0.0f;
		}
		return mix * m_masterVolume;
	}

	float masterVolume() const { return m_masterVolume; }
	void masterVolume(float v) { m_masterVolume = v; }

	std::array<Channel, CHANNELS>& channels() { return m_channels; }

private:
	std::array<Channel, CHANNELS> m_channels;

	float m_masterVolume{ 1.0f }, m_sampleRate{ SR };

	float m_time{ 99.0f };
	int m_position{ 0 };
};

static Tracker p{};

void callback(void* udata, Uint8* stream, int len) {
	for (int i = 0; i < len; i++) {
		float wsample = p.sample();
		stream[i] = SAMPLE(wsample);
	}
}

int main(int argc, char** argv) {
	SDL_AudioSpec spec;
	spec.channels = 1;
	spec.format = AUDIO_U8;
	spec.freq = int(SR);
	spec.samples = 1024;
	spec.callback = callback;

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		return -1;
	}
	SDL_PauseAudio(0);

	Instrument kick{};
	kick.volume().attack(0.005f);
	kick.volume().decay(0.25f);
	kick.volume().sustain(0.0f);
	kick.waveTable().setWaveForm(WaveTable::Triangle);
	for (int i = 0; i < 16; i++) {
		kick.notes()[i] = -15;
	}
	for (int i = 0; i < 16; i+=3) {
		kick.notes()[i/3] = -(i);
	}

	Instrument snare{};
	snare.volume().attack(0.005f);
	snare.volume().decay(0.22f);
	snare.volume().sustain(0.0f);
	snare.waveTable().noise(true);

	Instrument arp{};
	arp.voices(3);
	arp.volume().attack(0.01f);
	arp.volume().decay(3.0f);
	arp.volume().sustain(0.3f);
	arp.waveTable().setWaveForm(WaveTable::Saw);

	const int delayBeat = (60000 / int(BPM));
	for (int i = 0; i < 4; i++) {
		Sound kickSnd{};
		kickSnd.instrument = &kick;
		kickSnd.note = Note::F + 20;

		Sound snareSnd{};
		snareSnd.instrument = &snare;
		snareSnd.note = 56;
		snareSnd.volume = 0.4f;

		Sound arpSnd{};
		arpSnd.instrument = &arp;
		arpSnd.note = Note::A;
		arpSnd.effect = Effect::Arpeggio;
		arpSnd.arpeggio = Arpeggio::Octave;
		arpSnd.volume = 0.35f;
		arpSnd.effectSpeed = 2.0f;

		p.channels()[0].play(kickSnd);
		p.channels()[2].play(arpSnd);
		SDL_Delay(delayBeat);

		p.channels()[0].play(kickSnd);
		p.channels()[1].play(snareSnd);
		SDL_Delay(delayBeat);

		p.channels()[0].play(kickSnd);
		SDL_Delay(delayBeat);

		p.channels()[0].play(kickSnd);
		p.channels()[1].play(snareSnd);
		SDL_Delay(delayBeat);

		arpSnd.note = Note::F;
		p.channels()[2].play(arpSnd);
		p.channels()[0].play(kickSnd);
		SDL_Delay(delayBeat);

		p.channels()[0].play(kickSnd);
		p.channels()[1].play(snareSnd);
		SDL_Delay(delayBeat);

		arpSnd.note = Note::G;
		p.channels()[2].play(arpSnd);
		p.channels()[0].play(kickSnd);
		SDL_Delay(delayBeat);

		p.channels()[0].play(kickSnd);
		p.channels()[1].play(snareSnd);
		SDL_Delay(delayBeat / 2);

		p.channels()[0].play(kickSnd);
		SDL_Delay(delayBeat / 2);
	}
	
	SDL_CloseAudio();
	return 0;
}