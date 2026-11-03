// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicProducer.h"
#include <emscripten.h>
#include <nlohmann/json.hpp>
namespace
{
Music::Producer producer;
std::array<std::vector<unsigned char>, 3> pending;
std::array<std::int16_t, Music::Chunk * 2> pcm;
std::string snapshot;
} // namespace
extern "C"
{
	EMSCRIPTEN_KEEPALIVE int audio_load(int index, const unsigned char *bytes, int size)
	{
		return producer.loadMemory(bytes, size, index);
	}
	EMSCRIPTEN_KEEPALIVE void audio_stage(int index, const unsigned char *bytes, int size)
	{
		pending.at(index).assign(bytes, bytes + size);
	}
	EMSCRIPTEN_KEEPALIVE int audio_replace(int preview)
	{
		bool ok = preview ? producer.openPreviewMemory(pending) : producer.replaceMemory(pending);
		for (auto &bytes : pending)
			std::vector<unsigned char>().swap(bytes);
		return ok;
	}
	EMSCRIPTEN_KEEPALIVE void audio_select(int index, int early, int enabled)
	{
		producer.select(index, early, enabled);
	}
	EMSCRIPTEN_KEEPALIVE void audio_control(int command, double value)
	{
		producer.control(Music::Control(command), value);
	}
	EMSCRIPTEN_KEEPALIVE void audio_stop()
	{
		producer.stop();
	}
	EMSCRIPTEN_KEEPALIVE void audio_close_preview()
	{
		producer.preview.reset();
	}
	EMSCRIPTEN_KEEPALIVE const std::int16_t *audio_render()
	{
		producer.render(pcm.data(), Music::Chunk);
		return pcm.data();
	}
	EMSCRIPTEN_KEEPALIVE const char *audio_snapshot()
	{
		auto s = producer.snapshot();
		snapshot = nlohmann::json{{"track", s.track},       {"next", s.next},
								  {"pending", s.pending},   {"mode", s.mode},
								  {"preview", s.preview},   {"playing", s.playing},
								  {"audition", s.audition}, {"failed", s.failed},
								  {"position", s.position}, {"duration", s.duration},
								  {"weights", s.weights}}
					   .dump();
		return snapshot.c_str();
	}
}
