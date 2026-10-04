// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Glob2Test.h"
#include <GameplayRecording.h>
#include "support/RecordingValidationProcess.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <array>

using namespace GAGCore::Recording;
namespace
{
std::string read(const std::filesystem::path &path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), {}};
}
std::filesystem::path output(const char *label)
{
	return glob2test::artifactDir() /
		   (std::string(label) + "-" + std::to_string(SDL_GetPerformanceCounter()) + ".mp4");
}
void draw(Recorder &recorder, SDL_Surface *pixels, int milliseconds)
{
	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
	std::int16_t samples[882];
	std::uint64_t index = 0;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (recorder.wantsFrame())
			recorder.frame(*pixels);
		for (int i = 0; i < 441; ++i)
		{
			auto sample =
				std::int16_t(std::sin(2 * 3.14159265358979323846 * 440 * index++ / 44100) * 12000);
			samples[2 * i] = samples[2 * i + 1] = sample;
		}
		recorder.audio(samples, 882);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

// Functional media checks wait for encoded output, rather than assuming a
// worker has run within a short wall-clock interval on a contended test host.
template<class Ready>
void drawUntil(Recorder &recorder, SDL_Surface *pixels, Ready ready)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	do { draw(recorder, pixels, 50); }
	while (!ready() && std::chrono::steady_clock::now() < deadline);
	INFO(recorder.status().error);
	REQUIRE(ready());
}

void awaitRecording(Recorder &recorder, SDL_Surface &pixels)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (recorder.status().state == State::Starting &&
		   std::chrono::steady_clock::now() < deadline)
	{
		if (recorder.wantsFrame())
			recorder.frame(pixels);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	INFO(recorder.status().error);
	REQUIRE(recorder.status().state == State::Recording);
}
} // namespace
TEST_SUITE("GameplayRecording")
{
	TEST_CASE("producer failure remains failed and a publication collision preserves foreign files "
			  "[recording]")
	{
		if (!supported()) return;
		auto pixels = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
			SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		REQUIRE(pixels);
		for (bool captureFailure : {true, false})
		{
			Recorder recorder;
			const auto path = output(captureFailure ? "capture-failure" : "publish-collision");
			REQUIRE(recorder.start(path.string()));
			awaitRecording(recorder, *pixels);
			if (captureFailure)
				recorder.abort("forced capture failure");
			else
				std::ofstream(path) << "foreign video";
			recorder.shutdown();
			CHECK(recorder.status().state == State::Failed);
			const auto manifest = read(path.string() + ".recording/manifest.json");
			CHECK(manifest.find("\"complete\":false") != std::string::npos);
			CHECK_FALSE(std::filesystem::exists(path.string() + ".json"));
			CHECK_FALSE(std::filesystem::exists(path.string() + ".events.jsonl"));
			if (captureFailure)
			{
				CHECK_FALSE(std::filesystem::exists(path));
				CHECK(recorder.status().error == "forced capture failure");
			}
			else
				CHECK(read(path) == "foreign video");
		}
	}

	TEST_CASE("split callback audio survives as consecutive decoded PCM [recording][artifacts]")
	{
		const char *encoder = SDL_getenv("GLOB2_TEST_FFMPEG");
		if (!encoder || !supported())
			return;
		Recorder recorder;
		const auto path = output("audio-burst");
		auto pixels = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
			SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		REQUIRE(pixels);
		REQUIRE(recorder.start(path.string()));
		const auto epochHint = timestamp();
		recorder.frame(*pixels);
		awaitRecording(recorder, *pixels);
		const auto callbackStart = timestamp();
		std::array<std::int16_t, 2048> samples;
		for (int chunk = 0; chunk < 4; ++chunk)
		{
			for (int i = 0; i < 1024; ++i)
			{
				const auto sample =
					std::int16_t(12000 * std::sin(2 * 3.14159265358979323846 * (400 + chunk * 200) *
												  i / AudioSampleRate));
				samples[2 * i] = samples[2 * i + 1] = sample;
			}
			recorder.audio(samples.data(), samples.size(),
						   callbackStart + std::int64_t(chunk) * 1024 * 1000000 / AudioSampleRate);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(130));
		recorder.shutdown();
		INFO(recorder.status().error);
		REQUIRE(recorder.status().state == State::Complete);
		const auto raw = path.string() + ".pcm";
		Process decode;
		// A slow encoder startup can leave a timestamp gap before the first AAC
		// packet. Decode against the video's zero origin, retaining that silence.
		decode.launch({encoder, "-v", "error", "-nostdin", "-n", "-i", path.string(), "-map",
					   "0:a:0", "-af", "aresample=first_pts=0", "-f", "s16le", "-ar", "44100", "-ac", "2", raw},
					  path.string() + ".decode.log", false);
		REQUIRE(decode.finish() == 0);
		const auto pcm = read(raw);
		const auto first = (callbackStart - epochHint) * AudioSampleRate / 1000000;
		REQUIRE(pcm.size() >= std::size_t(first + 4096) * 4);
		// The MP4's first video timestamp is zero. Detect the first audible window
		// after AAC decoding; priming/edit-list handling must retain clock alignment.
		std::int64_t onset = -1;
		for (std::size_t at=0; at+128 <= pcm.size()/4; at+=128)
		{
			double energy = 0;
			for (std::size_t i=at;i<at+128;++i)
			{
				const auto offset=i*4;
				const auto sample=std::int16_t(std::uint16_t(static_cast<unsigned char>(pcm[offset])) | (std::uint16_t(static_cast<unsigned char>(pcm[offset+1]))<<8));
				energy += std::abs(int(sample));
			}
			if (energy/128 > 2000) { onset=std::int64_t(at); break; }
		}
		REQUIRE(onset>=0); CHECK(std::abs(onset-first)*1000/AudioSampleRate <= 50);
		// Inspect each chunk's interior, away from codec priming/transition samples.
		for (int chunk = 0; chunk < 4; ++chunk)
		{
			double energy = 0;
			for (int i = 256; i < 768; ++i)
			{
				const auto offset = std::size_t(first + chunk * 1024 + i) * 4;
				const auto sample =
					std::int16_t(std::uint16_t(static_cast<unsigned char>(pcm[offset])) |
								 (std::uint16_t(static_cast<unsigned char>(pcm[offset + 1])) << 8));
				energy += std::abs(int(sample));
			}
			CHECK(energy / 512 > 3000);
		}
	}
	TEST_CASE("recording rejects invalid options and never overwrites output")
	{
		Recorder recorder;
		recorder.options.fps = 0;
		CHECK_FALSE(recorder.start(output("invalid").string()));
		CHECK(recorder.status().state == State::Failed);
		recorder.options.fps = 30;
		recorder.options.encoder = EncoderPreference::Software;
		auto path = output("existing");
		std::ofstream(path) << "preserve";
		CHECK_FALSE(recorder.start(path.string()));
		CHECK(read(path) == "preserve");
	}
	TEST_CASE("recording encodes audio, odd resized frames, and semantic chapters [recording]")
	{
		if (!supported()) return;
		auto path = output("recording-sections");
		Recorder recorder;
		recorder.options.fps = 30;
		recorder.options.encoder = EncoderPreference::Software;
		auto pixels = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
			SDL_CreateSurface(321, 181, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		REQUIRE(pixels);
		SDL_FillSurfaceRect(pixels.get(), nullptr,
							SDL_MapSurfaceRGBA(pixels.get(), 200, 20, 10, 255));
		recorder.screen("main_menu");
		REQUIRE(recorder.start(path.string()));
		const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (recorder.status().state == State::Starting &&
			   std::chrono::steady_clock::now() < readyDeadline)
			draw(recorder, pixels.get(), 50);
		REQUIRE(recorder.status().state == State::Recording);
		draw(recorder, pixels.get(), 350);
		recorder.screen("multiplayer_game");
		SDL_FillSurfaceRect(pixels.get(), nullptr,
							SDL_MapSurfaceRGBA(pixels.get(), 20, 200, 10, 255));
		draw(recorder, pixels.get(), 200);
		recorder.beginMatch("multiplayer", "chapter test", 2, 15000);
		recorder.matchFrame(15000, false, 40);
		const auto journal = path.string() + ".recording/events.jsonl";
		drawUntil(recorder, pixels.get(), [&] {
			return read(journal).find("\"era_start\":10000") != std::string::npos;
		});
		recorder.matchFrame(20000, false, 40);
		drawUntil(recorder, pixels.get(), [&] {
			return read(journal).find("\"era_start\":20000") != std::string::npos;
		});
		recorder.matchFrame(20005, true, 40);
		recorder.dialog("in_game_main");
		draw(recorder, pixels.get(), 200);
		pixels.reset(SDL_CreateSurface(400, 240, SDL_PIXELFORMAT_RGBA32));
		REQUIRE(pixels);
		SDL_FillSurfaceRect(pixels.get(), nullptr,
							SDL_MapSurfaceRGBA(pixels.get(), 10, 20, 200, 255));
		recorder.matchFrame(20005, false, 40);
		drawUntil(recorder, pixels.get(), [&] {
			return recorder.status().segment == 2 && recorder.status().state == State::Recording;
		});
		recorder.screen("end_game");
		drawUntil(recorder, pixels.get(), [&] {
			return read(recorder.status().path + ".recording/events.jsonl").find(
				"\"phase\":\"results\"") != std::string::npos;
		});
		recorder.stop();
		CHECK_FALSE(recorder.active());
		CHECK_FALSE(recorder.start(output("busy").string()));
		recorder.shutdown();
		INFO(recorder.status().error);
		REQUIRE(recorder.status().state == State::Complete);
		CHECK(std::filesystem::file_size(path) > 1000);
		CHECK_FALSE(std::filesystem::exists(path.string() + ".recording"));
		const auto manifest = read(path.string() + ".json");
		CHECK(manifest.find("\"width\":322") != std::string::npos);
		CHECK(manifest.find("\"height\":182") != std::string::npos);
		CHECK(manifest.find("\"era_start\":10000") != std::string::npos);
		CHECK(manifest.find("\"era_start\":20000") != std::string::npos);
		REQUIRE(recorder.status().outputs.size() == 2);
		CHECK(read(recorder.status().outputs.back()+".json").find("\"phase\":\"results\"") != std::string::npos);
		CHECK(manifest.find("\"screen\":\"multiplayer_game\"") != std::string::npos);
		CHECK(manifest.find("\"mode\":\"multiplayer\"") != std::string::npos);
		CHECK(manifest.find("\"team\":2") != std::string::npos);
		CHECK(read(path.string() + ".events.jsonl").find("\"kind\":\"resize\"") !=
			  std::string::npos);
	}
	TEST_CASE("stopping a recording before its first frame completes without hanging")
	{
		if (!supported())
			return;
		Recorder recorder;
		REQUIRE(recorder.start(output("no-frame").string()));
		recorder.stop();
		recorder.shutdown();
		CHECK(recorder.status().state == State::Failed);
	}
}
