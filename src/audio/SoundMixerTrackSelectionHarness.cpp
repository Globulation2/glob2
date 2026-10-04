// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
//
// Standalone regression harness for SoundMixer's track selection and its fade
// state machine. Both changed when the device buffer shrank from 16384 frames
// to 1024 and a fade consequently grew from one callback to sixteen.
//
// Two regressions are covered:
//
//  1. While the device is closed -- a muted start, which no longer opens it --
//     setNextTrack kept `actTrack` pinned to the first track ever selected,
//     because it only moved `nextTrack` once actTrack was set. GlobalContainer
//     asks for Intro then Menu at startup, so on unmute setVolume() resumed
//     Intro and, since nextTrack had been overwritten with it too, looped it
//     forever: menu.opus never played. Unmuting inside a game played the menu
//     track rather than the game track. While nothing is playing there is no
//     "current" track, so the selection must move both.
//
//  2. A fade now spans sixteen callbacks, so a track change can arrive while
//     one is still running -- GameMusicController can emit on consecutive
//     40 ms ticks. Restarting the fade cut the incoming track off mid-mix,
//     jumping the output by whatever it had faded in so far. The request is
//     now queued in `pendingTrack` and started when the running fade lands.
//
// The mixer is driven directly rather than through SDL's audio thread so the
// callback count is deterministic; the thread is parked first. Ogg decoding is
// real -- the harness loads the game's own tracks -- so the fade paths run
// against actual PCM rather than silence.
//
// Usage: SoundMixerTrackSelectionHarness [dir containing data/zik]
// Defaults to "..", so it runs from test/ under CI's harness loop with no
// arguments. Exits 0 if every check passes, 1 if any fails.

#include <Environment.h>
#include "Glob2Test.h"
#include "ScopedEnvironment.h"

#include <cstdio>
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

// Defined in SoundMixer.cpp as the SDL audio callback; called directly here.
void mixaudio(void *voidMixer, Uint8 *stream, int len);

namespace
{
	// Mirrors of the SoundMixer.cpp constants, which are file-local #defines.
	// DEVICE_FRAME_COUNT frames of stereo Sint16, and the number of such
	// callbacks one FADE_SAMPLE_COUNT fade spans.
	const int kDeviceFrameCount = 1024;
	const int kCallbackBytes = kDeviceFrameCount * 2 /*channels*/ * 2 /*Sint16*/;
	const int kCallbacksPerFade = (SoundMixer::FadeSampleCount + kDeviceFrameCount * 2 - 1) / (kDeviceFrameCount * 2);

	void check(bool ok, const char *what)
	{
		CHECK_MESSAGE(ok, (what));
	}

	void checkTrack(int got, MusicTrack expected, const char *what)
	{
		CHECK_MESSAGE(got == static_cast<int>(expected), what << " (expected " << static_cast<int>(expected) << ", got " << got << ")");
	}

	//! Run the callback `count` times over a scratch buffer.
	void pump(SoundMixer &mix, int count)
	{
		static Uint8 buffer[kCallbackBytes];
		for (int i = 0; i < count; i++)
			mixaudio(&mix, buffer, kCallbackBytes);
	}

	//! Put the mixer into a running crossfade from `from` to `to`, as
	//! setNextTrack(to, true) would once the device is open.
	void beginCrossfade(SoundMixer &mix, MusicTrack from, MusicTrack to)
	{
		mix.actTrack = static_cast<int>(from);
		mix.nextTrack = static_cast<int>(to);
		mix.fadePos = 0;
		mix.pendingTrack = -1;
		mix.mode = SoundMixer::MODE_EARLY_CHANGE;
	}
}

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
		SoundMixer mix(255, 255, true);
		check(!mix.soundEnabled, "muted start leaves the device closed");

		const char *names[] = {
			"data/zik/intro.opus", "data/zik/menu.opus",
			"data/zik/original/a1.opus", "data/zik/original/a2.opus",
			"data/zik/original/a3.opus" };
		for (int i = 0; i < 5; i++)
		{
			REQUIRE_MESSAGE(mix.loadTrack(names[i], i) >= 0, "could not load " << names[i] << " from " << dataDir);
		}

		// --- 1. selection while the device is closed ---------------------
		// GlobalContainer::loadClient asks for Intro then Menu.
		mix.setNextTrack(MusicTrack::Intro);
		mix.setNextTrack(MusicTrack::Menu);
		checkTrack(mix.actTrack, MusicTrack::Menu,
			"closed device: Intro then Menu selects Menu");
		checkTrack(mix.nextTrack, MusicTrack::Menu,
			"closed device: Menu is also what follows");

		// Engine::run asks for the in-game track when a game starts.
		mix.setNextTrack(MusicTrack::InGameDefault, true);
		checkTrack(mix.actTrack, MusicTrack::InGameDefault,
			"closed device: entering a game selects the game track");

		// --- 2. unmuting resumes that selection --------------------------
		mix.setVolume(255, 255, false);
		check(mix.soundEnabled, "unmuting opens the device");
		checkTrack(mix.actTrack, MusicTrack::InGameDefault,
			"unmute resumes the game track, not the intro");
		checkTrack(mix.nextTrack, MusicTrack::InGameDefault,
			"unmute does not queue a stale track behind it");

		// Park SDL's callback thread so the fade checks below are the only
		// thing advancing the state machine.
		SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(mix.audioStream));
		SDL_LockAudioStream(mix.audioStream);

		// --- 3. a fade spans the full FADE_SAMPLE_COUNT ------------------
		beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
		pump(mix, kCallbacksPerFade - 1);
		check(mix.mode == SoundMixer::MODE_EARLY_CHANGE,
			"crossfade still running one callback short of the end");
		pump(mix, 1);
		check(mix.mode == SoundMixer::MODE_NORMAL,
			"crossfade lands after the expected callback count");
		checkTrack(mix.actTrack, MusicTrack::BuildingEvent,
			"crossfade hands over to the incoming track");

		// --- 4. a change asked for mid-fade is queued, not restarted -----
		beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
		pump(mix, 4);
		const unsigned fadePosBefore = mix.fadePos;
		mix.setNextTrack(MusicTrack::WarEvent, true);
		checkTrack(mix.pendingTrack, MusicTrack::WarEvent,
			"mid-fade request is queued");
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
		check(mix.mode == SoundMixer::MODE_EARLY_CHANGE,
			"queued change: a second fade starts rather than MODE_NORMAL");
		check(mix.fadePos == 0, "queued change: the second fade starts at zero");
		check(mix.pendingTrack == -1, "queued change: the queue is emptied");

		pump(mix, kCallbacksPerFade);
		checkTrack(mix.actTrack, MusicTrack::WarEvent,
			"queued change: the second fade lands on the queued track");
		check(mix.mode == SoundMixer::MODE_NORMAL,
			"queued change: back to normal playback afterwards");

		const auto currentPosition = op_pcm_tell(mix.tracks[mix.actTrack]);
		mix.setNextTrack(MusicTrack::WarEvent, true);
		check(mix.mode == SoundMixer::MODE_NORMAL, "repeated mood keeps normal playback");
		check(op_pcm_tell(mix.tracks[mix.actTrack]) == currentPosition,
			"repeated mood does not move the decoder");
		beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
		pump(mix, 4);
		const unsigned repeatedFadePosition = mix.fadePos;
		mix.setNextTrack(MusicTrack::WarEvent, true);
		mix.setNextTrack(MusicTrack::BuildingEvent, true);
		check(mix.pendingTrack == -1, "latest incoming mood cancels an older queued request");
		check(mix.fadePos == repeatedFadePosition, "repeated incoming mood preserves fade progress");
		pump(mix, kCallbacksPerFade - 4);
		check(mix.mode == SoundMixer::MODE_NORMAL, "repeated incoming mood does not start a self fade");

		// --- 6. stopping clears a queued change --------------------------
		beginCrossfade(mix, MusicTrack::InGameDefault, MusicTrack::BuildingEvent);
		pump(mix, 4);
		mix.setNextTrack(MusicTrack::WarEvent, true);
		mix.stopMusic();
		check(mix.pendingTrack == -1, "stopMusic clears a queued change");
		check(mix.mode == SoundMixer::MODE_STOP, "stopMusic starts the fade out");

		SDL_UnlockAudioStream(mix.audioStream);
	}

	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
}

TEST_SUITE("SoundMixerTrackSelection")
{
TEST_CASE("Opus trimmed timeline looping seeking and unavailable decoder")
{
	glob2test::ToolkitScope toolkit;
	GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
	SoundMixer mix(255, 255, true);
	mix.musicVolume = 255;
	REQUIRE(mix.loadTrack("test/fixtures/audio/trimmed.opus", 0) == 0);
	REQUIRE(mix.loadTrack("test/fixtures/audio/trimmed.opus", 1) == 1);
	CHECK(op_pcm_total(mix.tracks[0], -1) == 4813); // not a multiple of an Opus packet
	CHECK(op_pcm_seek(mix.tracks[0], 4800) == 0);
	mix.actTrack = mix.nextTrack = 0;
	mix.mode = SoundMixer::MODE_NORMAL;
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
	mix.mode = SoundMixer::MODE_EARLY_CHANGE;
	mix.fadePos = 0;
	mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
	CHECK(op_pcm_tell(mix.tracks[0]) == op_pcm_tell(mix.tracks[1]));
	CHECK(mix.loadTrack("test/fixtures/audio/unsupported-vorbis.ogg", 0) == -2);
	CHECK(op_pcm_total(mix.tracks[0], -1) == 4813); // rejection leaves the old decoder
    const auto profile = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0));
    std::ofstream(profile / "empty.opus", std::ios::binary);
    CHECK(mix.loadTrack("empty.opus", 0) == -2);
    {
        std::ifstream source(glob2test::sourceRoot() / "test/fixtures/audio/trimmed.opus", std::ios::binary);
        char header[48]; source.read(header, sizeof(header));
        std::ofstream(profile / "truncated.opus", std::ios::binary).write(header, sizeof(header));
    }
    CHECK(mix.loadTrack("truncated.opus", 0) == -2);
    REQUIRE(mix.loadTrack("test/fixtures/audio/damaged-packet.opus", 2) == 2);
    mix.actTrack = mix.nextTrack = 2;
    mix.mode = SoundMixer::MODE_NORMAL;
    for (int i = 0; i < 32; ++i)
        mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
    CHECK(op_pcm_tell(mix.tracks[2]) >= 0);
	// Missing/failed decoder must fill the whole callback and return promptly.
	op_free(mix.tracks[0]); mix.tracks[0] = nullptr;
	mix.actTrack = mix.nextTrack = 0;
	mix.mode = SoundMixer::MODE_NORMAL;
	output.fill(12345);
	mixaudio(&mix, reinterpret_cast<Uint8 *>(output.data()), sizeof(output));
	CHECK(std::all_of(output.begin(), output.end(), [](Sint16 n) { return n == 0; }));
}
}
