#include <iostream>
#include <cmath>
#include <array>
#include <vector>

#include "SDL2/SDL.h"

#define SR 44100.0f
#define BPM 120.0f
#define SAMPLE(x) (Uint8(((x) * 0.5f + 0.5f) * 255.0f))
#define LERP(a, b, t) ((1.0f - (t)) * (a) + (b) * (t))

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

struct Event {
	enum Type { NoteOn = 0, NoteOff };
	Type type{ NoteOff };
	int note{ 0 };
	int length{ 1 };
	bool vibrato{ false };
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

class Channel {
public:
	Channel() {
		m_waveTable.setWaveForm(WaveTable::Sine);
		m_amplitudeEnv.attack(0.05f);
		m_amplitudeEnv.decay(0.45f);
		m_amplitudeEnv.sustain(0.0f);
		m_amplitudeEnv.release(0.5f);
	}

	float sample() {
		if (m_events.empty()) return 0.0f;

		const float delay = (60000.0f / BPM) / 1000.0f;

		m_time += (1.0f / SR) * 8.0f;
		if (m_time >= delay) {
			if (m_timeUnits-- < 0) {
				if (m_current != nullptr) {
					float freq = NOTE[m_current->note % 12] * std::pow(2, m_current->note / 12);
					m_prevFreq = freq;
				}
				m_current = nullptr;
				if (m_amplitudeEnv.value() > 0.0f) {
					m_amplitudeEnv.gate(false);
				}
			}
			m_time = 0.0f;
		}

		if (m_current == nullptr && m_position < m_events.size()) {
			m_current = &m_events[m_position++];
			m_timeUnits = m_current->length;
			
			m_amplitudeEnv.gate(true);
		}

		if (m_current != nullptr && m_current->type == Event::NoteOn) {
			float freq = NOTE[m_current->note % 12] * std::pow(2, m_current->note / 12);

			if (m_portamento && m_timeUnits == m_current->length) {
				freq = LERP(m_prevFreq, freq, m_time / delay);
			}

			if (m_current->vibrato) {
				freq += std::cos(m_vibratoPhase.get((1.0f / 60.0f) * BPM * 4.0f)) * (NOTE[2] - NOTE[0]);
			}

			return m_waveTable.sample(freq) * m_amplitudeEnv.sample(SR);
		}
		return 0.0f;
	}

	void pushNote(int note, int length = 1, Event::Type type = Event::NoteOn, bool vibrato = false) {
		Event ev{};
		ev.note = note;
		ev.length = length;
		ev.type = type;
		ev.vibrato = vibrato;
		m_events.push_back(ev);
	}

private:
	WaveTable m_waveTable{};
	ADSR m_amplitudeEnv{};
	Phase m_vibratoPhase{};

	bool m_portamento{ true };

	std::vector<Event> m_events{};
	float m_time{ 0.0f }, m_prevFreq{ 0.0f };

	Event* m_current{ nullptr };
	int m_timeUnits, m_position{ 0 };
};

static Channel p{};

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

	p.pushNote(Note::C + 24, 2);
	p.pushNote(Note::D + 24, 2);
	p.pushNote(Note::E + 24, 2);
	p.pushNote(Note::F + 24, 2);
	p.pushNote(Note::G + 24, 2);
	p.pushNote(Note::A + 24, 2);
	p.pushNote(Note::B + 24, 2);
	p.pushNote(Note::C + 36, 2);
	// for (int i = 24; i < 34; i++) {
	// 	p.pushNote(i, 1);
	// }

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		return -1;
	}
	SDL_PauseAudio(0);
	SDL_Delay(3000);
	SDL_CloseAudio();
	return 0;
}