// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicStream.h"
#include <emscripten/emscripten.h>
static Music::Preview preview;
static std::array<std::int16_t, Music::Chunk * 2> output;
extern "C"
{
	EMSCRIPTEN_KEEPALIVE int music_open(unsigned mood, const unsigned char *bytes, unsigned size)
	{
		return preview.openMemory(mood, bytes, size);
	}
	EMSCRIPTEN_KEEPALIVE const std::int16_t *music_render()
	{
		preview.render(output.data(), Music::Chunk);
		return output.data();
	}
	EMSCRIPTEN_KEEPALIVE double music_position()
	{
		return preview.position();
	}
	EMSCRIPTEN_KEEPALIVE int music_failed()
	{
		return preview.failed();
	}
	EMSCRIPTEN_KEEPALIVE double music_weight(unsigned mood)
	{
		return mood < 3 ? preview.weights()[mood] : 0;
	}
	EMSCRIPTEN_KEEPALIVE void music_command(unsigned command, double value)
	{
		switch (command)
		{
		case 0:
			preview.playing = value != 0;
			break;
		case 1:
			preview.setMood(unsigned(value));
			break;
		case 2:
			preview.seekTo(value);
			break;
		case 3:
			preview.setFade(value);
			break;
		case 4:
			preview.setBlend(value);
			break;
		case 5:
			preview.audition = value != 0;
			break;
		case 6:
			preview.reset();
			break;
		}
	}
}
