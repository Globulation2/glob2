// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "SoundMixer.h"
#include "GameGUIDialog.h"
#include <FileManager.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

TEST_SUITE("MusicSet")
{
TEST_CASE("discovery atomic replacement queued moods and muted preferences [display][writes-preferences]")
{
	glob2test::ScopedEnvironment audio("SDL_AUDIODRIVER", "dummy");
	glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true});
	auto& settings = globalContainer->settings;
	REQUIRE(settings.musicSet.empty());

	auto& mixer = *globalContainer->mix;
	const auto sets = SoundMixer::getMusicSets();
	REQUIRE(std::find(sets.begin(), sets.end(), "original") != sets.end());
	REQUIRE(std::find(sets.begin(), sets.end(), "seedling") != sets.end());
	REQUIRE(std::is_sorted(sets.begin(), sets.end()));
	REQUIRE(std::adjacent_find(sets.begin(), sets.end()) == sets.end());
	REQUIRE(SoundMixer::musicSetLabel("bramble-dance") == "Bramble Dance");
	REQUIRE(mixer.selectMusicSet("seedling"));
	REQUIRE(mixer.getMusicSet() == "seedling");
	if (mixer.audioStream) SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(mixer.audioStream));
	mixer.actTrack = 4;
	mixer.nextTrack = 2;
	mixer.mode = SoundMixer::MODE_EARLY_CHANGE;
	REQUIRE(mixer.selectMusicSet("original"));
	REQUIRE((mixer.actTrack == 2 && mixer.nextTrack == 2 && mixer.mode == SoundMixer::MODE_START));
	REQUIRE(ov_pcm_tell(mixer.tracks[2]) == 0);
	mixer.mode = SoundMixer::MODE_EARLY_CHANGE;
	mixer.nextTrack = 4;
	mixer.pendingTrack = 3;
	mixer.fadePos = 2048;
	REQUIRE(mixer.selectMusicSet("seedling"));
	CHECK(mixer.actTrack == 3);
	CHECK(mixer.nextTrack == 3);
	CHECK(mixer.pendingTrack == -1);
	CHECK(mixer.fadePos == 0);
	REQUIRE(mixer.selectMusicSet("original"));
	auto *current = mixer.tracks[2];
	REQUIRE((mixer.selectMusicSet("original") && mixer.tracks[2] == current));
	REQUIRE((!mixer.selectMusicSet("../original") && mixer.tracks[2] == current));

	const auto profile = std::filesystem::path(globalContainer->fileManager->getDir(0));
	const auto incomplete = profile / "data/zik/test-incomplete";
	std::filesystem::create_directories(incomplete);
	std::ofstream(incomplete / "a1.ogg") << "invalid";
	const auto discovered = SoundMixer::getMusicSets();
	REQUIRE(std::find(discovered.begin(), discovered.end(), "test-incomplete") == discovered.end());
	const auto broken = profile / "data/zik/test-broken";
	std::filesystem::create_directories(broken);
	for (int i = 1; i <= 2; ++i)
		std::filesystem::copy_file(glob2test::sourceRoot() / "data/zik/seedling/a1.ogg", broken / ("a" + std::to_string(i) + ".ogg"));
	std::ofstream(broken / "a3.ogg") << "invalid";
	REQUIRE(!mixer.selectMusicSet("test-broken"));
	REQUIRE((mixer.getMusicSet() == "original" && mixer.tracks[2] == current));
	std::filesystem::copy_file(glob2test::sourceRoot() / "data/zik/original/a1.ogg", broken / "a3.ogg", std::filesystem::copy_options::overwrite_existing);
	REQUIRE(!mixer.selectMusicSet("test-broken"));
	REQUIRE((mixer.getMusicSet() == "original" && mixer.tracks[2] == current));
	std::filesystem::remove_all(broken);
	std::filesystem::remove_all(incomplete);
	std::cout << "PASS: discovery, atomic failure, different lengths, queued mood, same-set no-op\n";
	mixer.setVolume(255, 255, false);
	for (const auto& set : sets)
	{
		REQUIRE(mixer.selectMusicSet(set));
		mixer.setNextTrack(MusicTrack::WarEvent, true);
		SDL_Delay(100);
	}
	if (mixer.audioStream) SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(mixer.audioStream));
	std::cout << "PASS: all installed sets switch with the audio callback running\n";


	mixer.setVolume(255,255,true);
	{
		GameGUI gui;
		InGameOptionScreen screen(&gui);
		REQUIRE(screen.setMusicSet("seedling"));
		CHECK(settings.mute);
		CHECK(settings.musicSet == "seedling");
		CHECK(mixer.getMusicSet() == "seedling");
		CHECK_FALSE(screen.setMusicSet("../original"));
		CHECK(settings.musicSet == "seedling");
	}
	Settings loaded;
	loaded.load();
	CHECK(loaded.musicSet == "seedling");
	{
		GameGUI gui;
		InGameOptionScreen screen(&gui);
		REQUIRE(screen.setMusicSet(""));
		CHECK(settings.musicSet.empty());
	}
	loaded.load();
	CHECK(loaded.musicSet.empty());
}
}
