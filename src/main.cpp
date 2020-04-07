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

struct Event {
	enum { NoteOn = 0, NoteOff } type{ NoteOff };
	int note{ 0 };
	int length{ 1 };
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

class Track {
public:
	Track() {
		m_waveTable.setWaveForm(WaveTable::Sine);
	}

	float sample() {
		const float delay = (60000.0f / BPM) / 1000.0f;
		m_time += (1.0f / SR) * 4.0f;
		if (m_time >= delay) {
			if (m_current == nullptr) {
				m_position %= m_events.size();
				m_current = &m_events[m_position++];
				m_timeUnits = m_current->length;
			} else {
				if (m_timeUnits-- <= 0) {
					m_current = nullptr;
				}
			}
			m_time -= delay;
		}

		if (m_current != nullptr) {
			return m_waveTable.sample(NOTE[m_current->note % 12] * std::pow(2, m_current->note / 12));
		}
		return 0.0f;
	}

	std::vector<Event>& events() { return m_events; }

private:
	WaveTable m_waveTable{};
	std::vector<Event> m_events{};
	float m_time{ 0.0f };

	Event* m_current{ nullptr };
	int m_timeUnits, m_position{ 0 };
};

static Track p{};

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

	Event ev{};
	ev.note = 32;
	ev.length = 1;
	p.events().push_back(ev);

	Event ev2{};
	ev2.note = 34;
	ev2.length = 1;
	p.events().push_back(ev2);

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		return -1;
	}
	SDL_PauseAudio(0);
	SDL_Delay(2000);
	SDL_CloseAudio();
	return 0;
}