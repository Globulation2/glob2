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
	// Only Original ships; a second valid set is a copy of it in the disposable profile.
	const auto profile = std::filesystem::path(globalContainer->fileManager->getDir(0));
	const auto fixture = profile / "data/zik/test-set";
	std::filesystem::create_directories(fixture);
	for (int i = 1; i <= 3; ++i)
	{
		const auto file = "a" + std::to_string(i) + ".opus";
		std::filesystem::copy_file(glob2test::sourceRoot() / "data/zik/original" / file, fixture / file,
			std::filesystem::copy_options::overwrite_existing);
	}
	const auto sets = SoundMixer::getMusicSets();
	REQUIRE(std::find(sets.begin(), sets.end(), "original") != sets.end());
	REQUIRE(std::find(sets.begin(), sets.end(), "test-set") != sets.end());
	REQUIRE(std::is_sorted(sets.begin(), sets.end()));
	REQUIRE(std::adjacent_find(sets.begin(), sets.end()) == sets.end());
	REQUIRE(SoundMixer::musicSetLabel("test-set") == "Test Set");
	REQUIRE(mixer.selectMusicSet("test-set"));
	REQUIRE(mixer.getMusicSet() == "test-set");
	if (mixer.audioStream) SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(mixer.audioStream));
	mixer.actTrack = 4;
	mixer.nextTrack = 2;
	mixer.mode = SoundMixer::MODE_EARLY_CHANGE;
	REQUIRE(mixer.selectMusicSet("original"));
	REQUIRE((mixer.actTrack == 2 && mixer.nextTrack == 2 && mixer.mode == SoundMixer::MODE_START));
	REQUIRE(op_pcm_tell(mixer.tracks[2]) == 0);
	mixer.mode = SoundMixer::MODE_EARLY_CHANGE;
	mixer.nextTrack = 4;
	mixer.pendingTrack = 3;
	mixer.fadePos = 2048;
	REQUIRE(mixer.selectMusicSet("test-set"));
	CHECK(mixer.actTrack == 3);
	CHECK(mixer.nextTrack == 3);
	CHECK(mixer.pendingTrack == -1);
	CHECK(mixer.fadePos == 0);
	REQUIRE(mixer.selectMusicSet("original"));
	auto *current = mixer.tracks[2];
	REQUIRE((mixer.selectMusicSet("original") && mixer.tracks[2] == current));
	REQUIRE((!mixer.selectMusicSet("../original") && mixer.tracks[2] == current));

	const auto incomplete = profile / "data/zik/test-incomplete";
	std::filesystem::create_directories(incomplete);
	std::ofstream(incomplete / "a1.opus") << "invalid";
	const auto discovered = SoundMixer::getMusicSets();
	REQUIRE(std::find(discovered.begin(), discovered.end(), "test-incomplete") == discovered.end());
	const auto broken = profile / "data/zik/test-broken";
	std::filesystem::create_directories(broken);
	for (int i = 1; i <= 2; ++i)
		std::filesystem::copy_file(glob2test::sourceRoot() / "data/zik/original/a1.opus", broken / ("a" + std::to_string(i) + ".opus"));
	std::ofstream(broken / "a3.opus") << "invalid";
	REQUIRE(!mixer.selectMusicSet("test-broken"));
	REQUIRE((mixer.getMusicSet() == "original" && mixer.tracks[2] == current));
	// A valid 48 kHz stereo stream whose length differs from the other two.
	std::filesystem::copy_file(glob2test::sourceRoot() / "data/zik/menu.opus", broken / "a3.opus", std::filesystem::copy_options::overwrite_existing);
	REQUIRE(!mixer.selectMusicSet("test-broken"));
	REQUIRE((mixer.getMusicSet() == "original" && mixer.tracks[2] == current));
	{
		// Discovery lists the broken set; a random pick must skip it, not fail.
		const auto withBroken = SoundMixer::getMusicSets();
		REQUIRE(std::find(withBroken.begin(), withBroken.end(), "test-broken") != withBroken.end());
		srand(214);
		std::vector<std::string> picked;
		for (int i = 0; i < 200; ++i)
		{
			REQUIRE(mixer.selectMusicSet(""));
			REQUIRE(mixer.getMusicSet() != "test-broken");
			REQUIRE(mixer.tracks[2] != nullptr);
			picked.push_back(mixer.getMusicSet());
		}
		std::sort(picked.begin(), picked.end());
		picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
		CHECK(picked.size() == withBroken.size() - 1);
		REQUIRE(mixer.selectMusicSet("original"));
		current = mixer.tracks[2];
	}
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
		REQUIRE(screen.setMusicSet("test-set"));
		CHECK(settings.mute);
		CHECK(settings.musicSet == "test-set");
		CHECK(mixer.getMusicSet() == "test-set");
		CHECK_FALSE(screen.setMusicSet("../original"));
		CHECK(settings.musicSet == "test-set");
	}
	Settings loaded;
	loaded.load();
	CHECK(loaded.musicSet == "test-set");
	{
		GameGUI gui;
		InGameOptionScreen screen(&gui);
		REQUIRE(screen.setMusicSet(""));
		CHECK(settings.musicSet.empty());
	}
	loaded.load();
	CHECK(loaded.musicSet.empty());
	REQUIRE(mixer.selectMusicSet("original"));
	std::filesystem::remove_all(fixture);
}
}
