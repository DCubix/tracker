#include <iostream>
#include <cmath>

#include "SDL2/SDL.h"

#define SR 44100.0f
#define SAMPLE(x) (Uint8(((x) * 0.5f + 0.5f) * 255.0f))

class Phase {
public:

	inline float sample(float freq) {
		float p = m_phase;
		m_phase += (M_PI * 2.0f * freq) / SR;
		if (m_phase >= M_PI * 2.0f) {
			m_phase -= M_PI * 2.0f;
		}
		return p;
	}

	inline float norm(float freq) {
		return sample(freq) / (M_PI * 2.0f);
	}

private:
	float m_phase{ 0.0f };
};

static Phase p{}, lfo{};

static float wavetable[] = {
	-1.0f, -0.875f, -0.75f, -0.625f, -0.5f, -0.375f, -0.25f, -0.125f, 0.0f, 0.125f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f
};

void callback(void* udata, Uint8* stream, int len) {
	for (int i = 0; i < len; i++) {
		float lfoVal = std::cosf(lfo.sample(5.0f)) * 0.5f + 0.5f;
		float t = p.norm(220.0f + lfoVal * 15.0f);
		int index = int(std::floor(t * 16.0f)) % 15;
		int next = (index + 1) % 16;
		t -= index;

		float wsample = (1.0f - t) * wavetable[index] + wavetable[next] * t;
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
	SDL_Delay(1000);
	SDL_CloseAudio();
	return 0;
}