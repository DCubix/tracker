#include <iostream>
#include <cmath>
#include <array>
#include <vector>

#include "SDL2/SDL.h"

#define SR 44100.0f
#define BPM 120.0f
#define SAMPLE(x) (Uint8(((x) * 0.5f + 0.5f) * 255.0f))
#define LERP(a, b, t) ((1.0f - (t)) * (a) + (b) * (t))
#define MIX(a, b) ((a) + (b) - (a) * (b))
#define HERTZ (1.0 / 60.0)
#define CHANNELS 7

class Phase {
public:

	virtual float get(float freq) {
		float p = m_phase;
		m_phase += (M_PI * 2.0f * freq) / SR;
		if (m_phase >= M_PI * 2.0f) {
			m_phase -= M_PI * 2.0f;
		}
		return p;
	}

	float norm(float freq) {
		return get(freq) / (M_PI * 2.0f);
	}

	void reset() {
		m_phase = 0.0f;
	}

protected:
	float m_phase{ 0.0f };
};

class WaveTable : public Phase {
public:
	enum BasicWaveForm {
		Sine = 0,
		Saw,
		Square,
		Triangle
	};

	float sample(float freq) {
		float t = norm(freq);
		const int i = int(std::floor(t * m_waveTable.size())) % m_waveTable.size();
		const int n = (i + 1) % m_waveTable.size();
		return LERP(m_waveTable[i], m_waveTable[n], t);
	}

	float& operator [](unsigned int i) { return m_waveTable[i % 16]; }
	const float& operator [](unsigned int i) const { return m_waveTable[i % 16]; }

	void setWaveForm(BasicWaveForm wf) {
		float step = 1.0f / m_waveTable.size();
		float t = 0.0;

		for (size_t i = 0; i < m_waveTable.size(); i++) {
			float v = 0.0f;
			switch (wf) {
				case BasicWaveForm::Sine: v = std::sin(t * M_PI * 2.0f); break;
				case BasicWaveForm::Saw: v = t * 2.0f - 1.0f; break;
				case BasicWaveForm::Square: v = t >= 0.5f ? 1.0f : -1.0f; break;
				case BasicWaveForm::Triangle: v = std::acos(std::sin(t * M_PI * 2.0f)) / M_PI_2; break;
			}
			m_waveTable[i] = v;
			t += step;
		}
	}

private:
	std::array<float, 24> m_waveTable{};
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

enum ArpeggioType {
	Off = 0,
	Major,
	Minor,
	Maj7,
	Min7,
	Sus4,
	Sus2
};

struct Sound {
	int note{ Note::C + 24 };
	bool vibrato{ false }, slide{ false };
	float fine{ 0.0f };

	ArpeggioType arpeggio{ ArpeggioType::Off };

	float frequency(int noteOffset = 0) const {
		const int n = note + noteOffset;
		return NOTE[n % 12] * std::pow(2, n / 12) + fine;
	}
};

class Channel {
public:
	Channel() {
		m_waveTable.setWaveForm(WaveTable::Sine);
		m_amplitudeEnv.attack(0.002f);
		m_amplitudeEnv.decay(0.0f);
		m_amplitudeEnv.sustain(1.0f);
		m_amplitudeEnv.release(0.5f);
	}

	float sample() {
		const float delay = (60000.0f / BPM) / 1000.0f;

		int noff = 0;
		switch (m_current.arpeggio) {
			default: noff = 0; break;
			case ArpeggioType::Major: {
				const int offs[3] = { 0, 4, 7 };
				noff = offs[int(m_phase.norm(HERTZ * BPM * 8) * 3)];
			} break;
			case ArpeggioType::Minor: {
				const int offs[3] = { 0, 3, 7 };
				noff = offs[int(m_phase.norm(HERTZ * BPM * 8) * 3)];
			} break;
			case ArpeggioType::Sus4: {
				const int offs[3] = { 0, 5, 7 };
				noff = offs[int(m_phase.norm(HERTZ * BPM * 8) * 3)];
			} break;
			case ArpeggioType::Sus2: {
				const int offs[3] = { 0, 2, 7 };
				noff = offs[int(m_phase.norm(HERTZ * BPM * 8) * 3)];
			} break;
			case ArpeggioType::Maj7: {
				const int offs[4] = { 0, 4, 7, 10 };
				noff = offs[int(m_phase.norm(HERTZ * BPM * 6) * 4)];
			} break;
			case ArpeggioType::Min7: {
				const int offs[4] = { 0, 3, 7, 10 };
				noff = offs[int(m_phase.norm(HERTZ * BPM * 6) * 4)];
			} break;
		}

		float freq = m_current.frequency(noff);
		if (m_current.slide && (m_bar % 4) == 0 && m_sliding) {
			float t = m_time / delay;
			freq = LERP(m_previous.frequency(noff), freq, t);
			if (t >= 1.0f - 1e-4f) {
				m_sliding = false;
			}
		}

		if (m_current.vibrato) {
			freq += std::sin(m_phase.get(HERTZ * BPM * 4)) * (NOTE[4] - NOTE[0]);
		}

		m_time += (1.0f / SR) * 8.0f;
		if (m_time >= delay) {
			m_bar++;
			m_time -= delay;
		}

		return m_waveTable.sample(freq) * m_amplitudeEnv.sample(SR) * 0.18f;
	}

	void play(
		Note note,
		int octave = 1,
		bool vibrato = false,
		bool slide = false,
		ArpeggioType arpeggio = ArpeggioType::Off,
		float fineTune = 0.0f
	) {
		if (m_playing) {
			m_previous.arpeggio = m_current.arpeggio;
			m_previous.note = m_current.note;
			m_previous.slide = m_current.slide;
			m_previous.vibrato = m_current.vibrato;
			m_previous.fine = m_current.fine;
		}
		m_current.arpeggio = arpeggio;
		m_current.note = note + 12 * octave;
		m_current.slide = slide;
		m_current.vibrato = vibrato;
		m_current.fine = fineTune;
		m_playing = true;
		m_sliding = slide;
		m_amplitudeEnv.gate(true);
		m_phase.reset();
	}

	void silence() {
		m_amplitudeEnv.gate(false);
		m_sliding = false;
	}

private:
	WaveTable m_waveTable{};
	ADSR m_amplitudeEnv{};

	Phase m_phase{};

	float m_time{ 0.0f };

	Sound m_current{}, m_previous{};
	bool m_playing{ false }, m_sliding{ false };
	int m_bar{ 0 };
};

class Mixer {
public:
	Mixer() {
		for (int i = 0; i < CHANNELS; i++)
			m_channels[i] = Channel();
	}

	float sample() {
		float mix = 0.0f;
		for (int i = 0; i < CHANNELS; i+=2) {
			float m = MIX(m_channels[i].sample(), m_channels[i + 1].sample());
			mix = MIX(mix, m);
		}
		return mix;
	}

	float masterVolume() const { return m_masterVolume; }
	void masterVolume(float v) { m_masterVolume = v; }

	std::array<Channel, CHANNELS>& channels() { return m_channels; }
private:
	std::array<Channel, CHANNELS> m_channels{};
	float m_masterVolume{ 1.0f };
};

static Mixer p{};

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
	spec.freq = 44100;
	spec.samples = 1024;
	spec.callback = callback;

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		return -1;
	}
	SDL_PauseAudio(0);

	p.channels()[0].play(Note::C, 4, false, false);
	p.channels()[1].play(Note::E, 4, false, false);
	
	SDL_Delay(500);
	SDL_CloseAudio();
	return 0;
}