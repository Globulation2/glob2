// SPDX-License-Identifier: GPL-3.0-or-later
// Commands/assets cross to the UI only at control time. Playback never calls the game.
#include "BrowserMusic.h"
#include "MusicProducer.h"
#include <GameplayRecording.h>
#include <emscripten.h>
#include <fstream>
#include <nlohmann/json.hpp>
namespace
{
using Bytes = std::vector<unsigned char>;
Bytes read(const std::string &path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file || file.tellg() <= 0 || file.tellg() > 16 * 1024 * 1024)
		return {};
	Bytes bytes(size_t(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	return file ? bytes : Bytes{};
}
void send(const char *type, int index, double value = 0, const Bytes &bytes = {})
{
	MAIN_THREAD_EM_ASM(
		{
			Module.glob2Music.send({
				type : UTF8ToString($0),
				index : $1,
				value : $2,
				bytes : $4 ? HEAPU8.slice($3, $3 + $4).buffer : null
			});
		},
		type, index, value, bytes.data(), bytes.size());
}
bool trio(const std::array<std::string, 3> &paths, bool preview)
{
	std::array<Bytes, 3> bytes;
	for (unsigned i = 0; i < 3; ++i)
	{
		bytes[i] = read(paths[i]);
		if (bytes[i].empty())
			return false;
	}
	Music::Producer validation;
	if (!(preview ? validation.openPreviewMemory(bytes) : validation.replaceMemory(bytes)))
		return false;
	// Single message publishes the entire validated replacement atomically.
	MAIN_THREAD_EM_ASM(
		{
			Module.glob2Music.send({
				type : $0 ? 'preview' : 'replace',
				tracks : [
					HEAPU8.slice($1, $1 + $2).buffer, HEAPU8.slice($3, $3 + $4).buffer,
					HEAPU8.slice($5, $5 + $6).buffer
				]
			});
		},
		preview, bytes[0].data(), bytes[0].size(), bytes[1].data(), bytes[1].size(),
		bytes[2].data(), bytes[2].size());
	return true;
}
nlohmann::json status()
{
	char *text = reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
		const s = JSON.stringify(Module.glob2Music.status || {});
		const p = _malloc(lengthBytesUTF8(s) + 1);
		stringToUTF8(s, p, lengthBytesUTF8(s) + 1);
		return p;
	}));
	if (!text)
		return {};
	auto result = nlohmann::json::parse(text, nullptr, false);
	free(text);
	return result.is_object() ? result : nlohmann::json::object();
}
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE int glob2_audio_recording_active()
{
	return GAGCore::Recording::recorder().active();
}
extern "C" EMSCRIPTEN_KEEPALIVE void glob2_audio_capture(const std::int16_t *pcm, int count,
														 double time)
{
	GAGCore::Recording::recorder().audio(pcm, size_t(count), std::int64_t(time));
}
BrowserMusic::~BrowserMusic()
{
	command("destroy");
}
int BrowserMusic::load(const std::string &path, int index)
{
	auto bytes = read(path);
	if (bytes.empty())
		return -1;
	Music::Producer validation;
	if (validation.loadMemory(bytes.data(), bytes.size(), 0) < 0)
		return -2;
	if (index < 0)
		index = nextIndex;
	if (index > 255)
		return -2;
	nextIndex = std::max(nextIndex, index + 1);
	send("load", index, 0, bytes);
	return index;
}
bool BrowserMusic::replace(const std::array<std::string, 3> &paths)
{
	return trio(paths, false);
}
bool BrowserMusic::preview(const std::array<std::string, 3> &paths)
{
	return trio(paths, true);
}
void BrowserMusic::select(unsigned index, bool early)
{
	send("select", int(index), early);
}
void BrowserMusic::volume(unsigned music, unsigned, bool mute)
{
	MAIN_THREAD_EM_ASM(
		{ Module.glob2Music.volume($0, $1, $2); }, std::min(255u, music), mute,
		double(GAGCore::Recording::timestamp()));
}
void BrowserMusic::command(const char *type)
{
	send(type, 0);
}
void BrowserMusic::control(Music::Control command, double value)
{
	send("control", int(command), value);
}
bool BrowserMusic::enabled() const
{
	return MAIN_THREAD_EM_ASM_INT({ return !!Module.glob2Music.context; });
}
Music::Snapshot BrowserMusic::snapshot() const
{
	auto s = status();
	Music::Snapshot out;
	out.track = s.value("track", -1);
	out.next = s.value("next", -1);
	out.pending = s.value("pending", -1);
	out.mode = s.value("mode", 0);
	out.preview = s.value("preview", false);
	out.playing = s.value("playing", false);
	out.audition = s.value("audition", false);
	out.failed = s.value("failed", false);
	out.position = s.value("position", 0.0);
	out.duration = s.value("duration", 0.0);
	if (s.contains("weights"))
		out.weights = s["weights"].get<std::array<double, 3>>();
	return out;
}
Music::Diagnostics BrowserMusic::diagnostics() const
{
	auto s = status();
	return {s.value("queuedFrames", 0u),       s.value("starvationFrames", 0u),
			s.value("underruns", 0u),          s.value("consumedFrames", 0u),
			s.value("maxRenderUs", 0u),        0,
			s.value("maxCommandLatencyUs", 0u)};
}
