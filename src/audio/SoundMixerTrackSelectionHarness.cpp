// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
//
// Deterministic producer tests cover muted selection and queued fades using real
// game tracks. Device/preview lifecycle cases use SDL's dummy driver and run in
// the isolated engine-test registry. The opt-in stress case reports callback and
// command latency under externally supplied CPU load.

#include <Environment.h>
#include "Glob2Test.h"
#include "ScopedEnvironment.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <SDL3/SDL.h>

#include "FileManager.h"
#include "Toolkit.h"

#include "MusicTrack.h"
#include "SoundMixer.h"
#include "MusicProducer.h"

// Drive the extracted decoder with the former callback byte-count convention.
void mixaudio(void *value, Uint8 *stream, int len)
{
	auto &p = *static_cast<Music::Producer *>(value);
	auto *samples = reinterpret_cast<std::int16_t *>(stream);
	for (unsigned frames = unsigned(len) / 4; frames;)
	{
		auto count = std::min(frames, Music::Chunk);
		p.render(samples, count);
		samples += count * 2;
		frames -= count;
	}
}

namespace
{
// Shared producer constants preserve the original fade duration at 48 kHz.
// DEVICE_FRAME_COUNT frames of stereo Sint16, and the number of such
// callbacks one FADE_SAMPLE_COUNT fade spans.
const int kDeviceFrameCount = 1024;
const int kCallbackBytes = kDeviceFrameCount * 2 /*channels*/ * 2 /*Sint16*/;
const int kCallbacksPerFade =
	(SoundMixer::FadeSampleCount + kDeviceFrameCount * 2 - 1) / (kDeviceFrameCount * 2);

void check(bool ok, const char *what)
{
	CHECK_MESSAGE(ok, (what));
}

void checkTrack(int got, MusicTrack expected, const char *what)
{
	CHECK_MESSAGE(got == static_cast<int>(expected),
				  what << " (expected " << static_cast<int>(expected) << ", got " << got << ")");
}

//! Run the callback `count` times over a scratch buffer.
void pump(Music::Producer &mix, int count)
{
	static Uint8 buffer[kCallbackBytes];
	for (int i = 0; i < count; i++)
		mixaudio(&mix, buffer, kCallbackBytes);
}

//! Put the mixer into a running crossfade from `from` to `to`, as
//! setNextTrack(to, true) would once the device is open.
void beginCrossfade(Music::Producer &mix, MusicTrack from, MusicTrack to)
{
	mix.actTrack = static_cast<int>(from);
	mix.nextTrack = static_cast<int>(to);
	mix.fadePos = 0;
	mix.pendingTrack = -1;
	mix.mode = Music::Producer::MODE_EARLY_CHANGE;
}
} // namespace

TEST_SUITE("SoundMixerTrackSelection")
{
	TEST_CASE("track selection while closed and queued mid-fade changes")
	{
		const std::string dataDir = glob2test::sourceRoot().string();

		// The dummy driver gives a real SDL_OpenAudioDeviceStream without needing hardware, so
		// the muted-start / unmute path below is the production one.
		glob2test::ScopedEnvironment audio("SDL_AUDIODRIVER", "dummy");
		REQUIRE_MESSAGE(SDL_InitSubSystem(SDL_INIT_AUDIO), (SDL_GetError()));

		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(dataDir);

		{
			// A muted start, exactly as GlobalContainer builds it.
			Music::Producer mix;

			const char *names[] = {"data/zik/intro.opus", "data/zik/menu.opus",
								   "data/zik/original/a1.opus", "data/zik/original/a2.opus",
								   "data/zik/original/a3.opus"};
			for (int i = 0; i < 5; i++)
			{
				REQUIRE_MESSAGE(mix.load(dataDir + "/" + names[i], i) >= 0,
								"could not load " << names[i] << " from " << dataDir);
			}

			// --- 1. selection while the device is closed ---------------------
			// GlobalContainer::loadClient asks for Intro then Menu.
			mix.select(unsigned(MusicTrack::Intro), false, false);
			mix.select(unsigned(MusicTrack::Menu), false, false);
			checkTrack(mix.actTrack, MusicTrack::Menu,
					   "closed device: Intro then Menu selects Menu");
			checkTrack(mix.nextTrack, MusicTrack::Menu, "closed device: Menu is also what follows");

			// Engine::run asks for the in-game track when a game starts.
			mix.select(unsigned(MusicTrack::InGameDefault), true, false);
			checkTrack(mix.actTrack, MusicTrack::InGameDefault,
					   "closed device: entering a game selects the game track");

			// --- 3. a fade spans the full FADE_SAMPLE_COUNT ------------------
			beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
			pump(mix, kCallbacksPerFade - 1);
			check(mix.mode == Music::Producer::MODE_EARLY_CHANGE,
				  "crossfade still running one callback short of the end");
			pump(mix, 1);
			check(mix.mode == Music::Producer::MODE_NORMAL,
				  "crossfade lands after the expected callback count");
			checkTrack(mix.actTrack, MusicTrack::BuildingEvent,
					   "crossfade hands over to the incoming track");

			// --- 4. a change asked for mid-fade is queued, not restarted -----
			beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
			pump(mix, 4);
			const unsigned fadePosBefore = mix.fadePos;
			mix.select(unsigned(MusicTrack::WarEvent), true);
			checkTrack(mix.pendingTrack, MusicTrack::WarEvent, "mid-fade request is queued");
			checkTrack(mix.nextTrack, MusicTrack::BuildingEvent,
					   "mid-fade request does not displace the incoming track");
			checkTrack(mix.actTrack, MusicTrack::InGameDefault,
					   "mid-fade request does not displace the outgoing track");
			check(mix.fadePos == fadePosBefore,
				  "mid-fade request does not rewind the running fade");

			// --- 5. the queued change starts once the fade lands -------------
			pump(mix, kCallbacksPerFade - 4);
			checkTrack(mix.actTrack, MusicTrack::BuildingEvent,
					   "queued change: first fade still completes normally");
			checkTrack(mix.nextTrack, MusicTrack::WarEvent,
					   "queued change: the queued track becomes the incoming one");
			check(mix.mode == Music::Producer::MODE_EARLY_CHANGE,
				  "queued change: a second fade starts rather than MODE_NORMAL");
			check(mix.fadePos == 0, "queued change: the second fade starts at zero");
			check(mix.pendingTrack == -1, "queued change: the queue is emptied");

			pump(mix, kCallbacksPerFade);
			checkTrack(mix.actTrack, MusicTrack::WarEvent,
					   "queued change: the second fade lands on the queued track");
			check(mix.mode == Music::Producer::MODE_NORMAL,
				  "queued change: back to normal playback afterwards");

			const auto currentPosition = op_pcm_tell(mix.tracks[mix.actTrack]);
			mix.select(unsigned(MusicTrack::WarEvent), true);
			check(mix.mode == Music::Producer::MODE_NORMAL, "repeated mood keeps normal playback");
			check(op_pcm_tell(mix.tracks[mix.actTrack]) == currentPosition,
				  "repeated mood does not move the decoder");
			beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
			pump(mix, 4);
			const unsigned repeatedFadePosition = mix.fadePos;
			mix.select(unsigned(MusicTrack::WarEvent), true);
			mix.select(unsigned(MusicTrack::BuildingEvent), true);
			check(mix.pendingTrack == -1, "latest incoming mood cancels an older queued request");
			check(mix.fadePos == repeatedFadePosition,
				  "repeated incoming mood preserves fade progress");
			pump(mix, kCallbacksPerFade - 4);
			check(mix.mode == Music::Producer::MODE_NORMAL,
				  "repeated incoming mood does not start a self fade");

			// Vary the callback size, including a partial Opus packet and a large
			// device request. Fade progress and mood alignment use PCM samples.
			for (const unsigned frames : {127u, 1024u})
			{
				beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
				std::vector<Sint16> output(frames * 2);
				const unsigned calls =
					(SoundMixer::FadeSampleCount + frames * 2 - 1) / (frames * 2);
				for (unsigned call = 0; call < calls; ++call)
				{
					mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()),
							 output.size() * sizeof(Sint16));
					check(op_pcm_tell(mix.tracks[static_cast<int>(MusicTrack::InGameDefault)]) ==
							  op_pcm_tell(mix.tracks[static_cast<int>(MusicTrack::BuildingEvent)]),
						  "callback sizes preserve aligned mood positions");
					if (call + 1 < calls)
					{
						check(mix.mode == Music::Producer::MODE_EARLY_CHANGE,
							  "fade remains active before its sample budget");
						check(mix.fadePos == (call + 1) * frames * 2,
							  "fade advances by interleaved PCM samples");
					}
				}
				check(mix.mode == Music::Producer::MODE_NORMAL,
					  "fade lands after its sample budget across callback sizes");
			}

			// --- 6. stopping clears a queued change --------------------------
			beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
			pump(mix, 4);
			mix.select(unsigned(MusicTrack::WarEvent), true);
			mix.stop();
			check(mix.pendingTrack == -1, "stopMusic clears a queued change");
			check(mix.mode == Music::Producer::MODE_STOP, "stopMusic starts the fade out");
		}

		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
}

TEST_SUITE("SoundMixerTrackSelection")
{
	TEST_CASE("Opus trimmed timeline looping seeking and unavailable decoder")
	{
		const auto dataDir = glob2test::sourceRoot().string();
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		Music::Producer mix;

		REQUIRE(mix.load(dataDir + "/test/fixtures/audio/trimmed.opus", 0) == 0);
		REQUIRE(mix.load(dataDir + "/test/fixtures/audio/trimmed.opus", 1) == 1);
		CHECK(op_pcm_total(mix.tracks[0], -1) == 4813); // not a multiple of an Opus packet
		CHECK(op_pcm_seek(mix.tracks[0], 4800) == 0);
		mix.actTrack = mix.nextTrack = 0;
		mix.mode = Music::Producer::MODE_NORMAL;
		alignas(Sint16) std::array<Sint16, 2048> output;
		mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
		CHECK(op_pcm_tell(mix.tracks[0]) == (4800 + 1024) % 4813);
		CHECK(std::any_of(output.begin(), output.end(), [](Sint16 n) { return n != 0; }));
		REQUIRE(op_raw_seek(mix.tracks[0], 0) == 0);
		CHECK(op_pcm_seek(mix.tracks[0], 4700) == 0);
		// Incoming decoder has a partially consumed packet; alignment must reset it.
		opus_int16 partial[200];
		REQUIRE(op_read_stereo(mix.tracks[1], partial, 200) == 100);
		mix.nextTrack = 1;
		mix.mode = Music::Producer::MODE_EARLY_CHANGE;
		mix.fadePos = 0;
		mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
		CHECK(op_pcm_tell(mix.tracks[0]) == op_pcm_tell(mix.tracks[1]));
		CHECK(mix.load(dataDir + "/test/fixtures/audio/unsupported-vorbis.ogg", 0) == -2);
		CHECK(op_pcm_total(mix.tracks[0], -1) == 4813); // rejection leaves the old decoder
		const auto profile = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0));
		std::ofstream(profile / "empty.opus", std::ios::binary);
		CHECK(mix.load((profile / "empty.opus").string(), 0) == -2);
		{
			std::ifstream source(glob2test::sourceRoot() / "test/fixtures/audio/trimmed.opus",
								 std::ios::binary);
			char header[48];
			source.read(header, sizeof(header));
			std::ofstream(profile / "truncated.opus", std::ios::binary)
				.write(header, sizeof(header));
		}
		CHECK(mix.load((profile / "truncated.opus").string(), 0) == -2);
		REQUIRE(mix.load(dataDir + "/test/fixtures/audio/damaged-packet.opus", 2) == 2);
		mix.actTrack = mix.nextTrack = 2;
		mix.mode = Music::Producer::MODE_NORMAL;
		for (int i = 0; i < 32; ++i)
			mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
		CHECK(op_pcm_tell(mix.tracks[2]) >= 0);
		// Missing/failed decoder must fill the whole callback and return promptly.
		op_free(mix.tracks[0]);
		mix.tracks[0] = nullptr;
		mix.actTrack = mix.nextTrack = 0;
		mix.mode = Music::Producer::MODE_NORMAL;
		output.fill(12345);
		mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
		CHECK(std::all_of(output.begin(), output.end(), [](Sint16 n) { return n == 0; }));
	}
}

TEST_SUITE("SoundMixerTrackSelection")
{
	TEST_CASE("buffered facade runs at ordinary priority and joins on shutdown")
	{
		glob2test::ScopedEnvironment audio("SDL_AUDIODRIVER", "dummy");
		glob2test::ScopedEnvironment priority("GLOB2_AUDIO_THREAD_PRIORITY", "0");
		REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		{
			SoundMixer mixer(255, 255, true);
			CHECK_FALSE(mixer.enabled());
			REQUIRE(mixer.loadTrack("test/fixtures/audio/trimmed.opus", 0) == 0);
			REQUIRE(mixer.loadTrack("test/fixtures/audio/trimmed.opus", 1) == 1);
			mixer.setNextTrack(0);
			mixer.setNextTrack(1);
			CHECK(mixer.playbackSnapshot().track == 1);
			mixer.setVolume(255, 255, false);
			REQUIRE(mixer.enabled());
			const auto deadline = SDL_GetTicks() + 5000;
			while (mixer.diagnostics().consumedFrames < 48000 && SDL_GetTicks() < deadline)
				SDL_Delay(10);
			CHECK(mixer.diagnostics().consumedFrames >= 48000);
			CHECK(mixer.playbackSnapshot().track == 1);
			CHECK(mixer.diagnostics().underruns == 0);
			const auto fixture =
				(glob2test::sourceRoot() / "test/fixtures/audio/trimmed.opus").string();
			const auto first = mixer.openPreview({fixture, fixture, fixture});
			REQUIRE(first != 0);
			const auto second = mixer.openPreview({fixture, fixture, fixture});
			REQUIRE(second != 0);
			CHECK(second != first);
			mixer.closePreview(first); // an old screen cannot close the new preview
			mixer.previewControl(first, Music::Control::Play, 1);
			mixer.setVolume(0, 0, true);
			const auto previewDeadline = SDL_GetTicks() + 2000;
			while (!mixer.playbackSnapshot().preview && SDL_GetTicks() < previewDeadline)
				SDL_Delay(10);
			CHECK(mixer.playbackSnapshot().preview);
			CHECK_FALSE(mixer.playbackSnapshot().playing);
			mixer.previewControl(second, Music::Control::Reset, 0);
			mixer.previewControl(second, Music::Control::Mood, 2);
			mixer.previewControl(second, Music::Control::Play, 1);
			const auto moodDeadline = SDL_GetTicks() + 5000;
			while (mixer.playbackSnapshot().weights[2] < .99 && SDL_GetTicks() < moodDeadline)
				SDL_Delay(10);
			CHECK(mixer.playbackSnapshot().weights[2] > .99);
			mixer.previewControl(second, Music::Control::Play, 0);
			const auto pauseDeadline = SDL_GetTicks() + 2000;
			while (mixer.playbackSnapshot().playing && SDL_GetTicks() < pauseDeadline)
				SDL_Delay(5);
			SDL_Delay(50); // Let the callback observe the transport gate.
			const auto pausedPosition = mixer.playbackSnapshot().position;
			const auto pausedFrames = mixer.diagnostics().consumedFrames;
			SDL_Delay(100);
			CHECK(mixer.diagnostics().consumedFrames == pausedFrames);
			CHECK(mixer.playbackSnapshot().position == doctest::Approx(pausedPosition));
			mixer.previewControl(second, Music::Control::Seek, .05);
			const auto seekDeadline = SDL_GetTicks() + 2000;
			while (std::abs(mixer.playbackSnapshot().position - .05) > .00001 &&
				   SDL_GetTicks() < seekDeadline)
				SDL_Delay(5);
			CHECK(mixer.playbackSnapshot().position == doctest::Approx(.05));
			CHECK_FALSE(mixer.playbackSnapshot().playing);
			mixer.previewControl(second, Music::Control::Play, 1);
			const auto resumeDeadline = SDL_GetTicks() + 2000;
			while (mixer.diagnostics().consumedFrames == pausedFrames &&
				   SDL_GetTicks() < resumeDeadline)
				SDL_Delay(5);
			CHECK(mixer.diagnostics().consumedFrames > pausedFrames);
			mixer.closePreview(second);
			mixer.setVolume(255, 255, false);
			for (unsigned n = 0; n < 100; ++n)
				mixer.setNextTrack(n % 2, true);
		}
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
}

TEST_SUITE("SoundMixerTrackSelection")
{
	TEST_CASE("muted preview controls and shutdown do not require a device callback")
	{
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		SoundMixer mixer(255, 255, true);
		const auto fixture =
			(glob2test::sourceRoot() / "test/fixtures/audio/trimmed.opus").string();
		const auto session = mixer.openPreview({fixture, fixture, fixture});
		REQUIRE(session != 0);
		mixer.previewControl(session, Music::Control::Play, 1);
		CHECK(mixer.playbackSnapshot().playing);
		mixer.previewControl(session, Music::Control::Play, 0);
		CHECK_FALSE(mixer.playbackSnapshot().playing);
		mixer.previewControl(session, Music::Control::Seek, .05);
		mixer.previewControl(session, Music::Control::Reset);
		const auto state = mixer.playbackSnapshot();
		CHECK_FALSE(state.playing);
		CHECK(state.position == doctest::Approx(.05));
		CHECK_FALSE(mixer.enabled());
		// Destruction with an active, paused preview must join without a consumer.
	}
	TEST_CASE("native music supply under external CPU contention [benchmark][slow]")
	{
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
		const char *duration = std::getenv("GLOB2_AUDIO_STRESS_SECONDS");
		const unsigned seconds =
			duration ? unsigned(std::clamp(std::atoi(duration), 1, 3600)) : 600;
		{
			SoundMixer mixer(255, 255, false);
			REQUIRE(mixer.enabled());
			REQUIRE(mixer.loadTrack("data/zik/original/a1.opus", 0) == 0);
			REQUIRE(mixer.loadTrack("data/zik/original/a2.opus", 1) == 1);
			mixer.setNextTrack(0);
			const auto ready = SDL_GetTicks() + 10000;
			while (mixer.diagnostics().consumedFrames < 48000 && SDL_GetTicks() < ready)
				SDL_Delay(10);
			REQUIRE(mixer.diagnostics().consumedFrames >= 48000);
			const auto before = mixer.diagnostics();
			const auto start = SDL_GetTicks();
			unsigned minimum = before.queuedFrames, mood = 0;
			while (SDL_GetTicks() - start < seconds * 1000)
			{
				const auto elapsed = SDL_GetTicks() - start;
				if (elapsed / 2000 > mood)
				{
					mood = unsigned(elapsed / 2000);
					mixer.setNextTrack(mood % 2, true);
				}
				minimum = std::min(minimum, mixer.diagnostics().queuedFrames);
				SDL_Delay(10);
			}
			const auto after = mixer.diagnostics();
			std::printf("AUDIO_STRESS seconds=%u min_queued=%u starvation=%u underruns=%u "
						"consumed=%u render_us=%u callback_us=%u command_us=%u\n",
						seconds, minimum, after.starvationFrames - before.starvationFrames,
						after.underruns - before.underruns,
						after.consumedFrames - before.consumedFrames, after.maxRenderUs,
						after.maxCallbackUs, after.maxCommandLatencyUs);
			CHECK(after.underruns == before.underruns);
			CHECK(after.starvationFrames == before.starvationFrames);
		}
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
}
