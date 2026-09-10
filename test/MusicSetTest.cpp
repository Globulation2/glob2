// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "SoundMixer.h"
#include <FileManager.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

GlobalContainer *globalContainer = nullptr;

int main(int argc, char **argv)
{
	assert(argc == 2 && std::string(argv[1]).find("glob2-music-test-") == 0);
	globalContainer = new GlobalContainer(argv[1]);
	auto& settings = globalContainer->settings;
	assert(settings.musicSet.empty());
	settings.screenWidth = 640;
	settings.screenHeight = 480;
	settings.screenFlags = GAGCore::GraphicContext::USEGPU;
	settings.mute = true;
	globalContainer->load();
	auto& mixer = *globalContainer->mix;
	const auto sets = SoundMixer::getMusicSets();
	assert(std::find(sets.begin(), sets.end(), "original") != sets.end());
	assert(std::find(sets.begin(), sets.end(), "seedling") != sets.end());
	assert(std::is_sorted(sets.begin(), sets.end()));
	assert(std::adjacent_find(sets.begin(), sets.end()) == sets.end());
	assert(SoundMixer::musicSetLabel("bramble-dance") == "Bramble Dance");
	assert(mixer.selectMusicSet("seedling"));
	assert(mixer.getMusicSet() == "seedling");
	SDL_PauseAudio(1);
	mixer.actTrack = 4;
	mixer.nextTrack = 2;
	mixer.mode = SoundMixer::MODE_EARLY_CHANGE;
	assert(mixer.selectMusicSet("original"));
	assert(mixer.actTrack == 2 && mixer.nextTrack == 2 && mixer.mode == SoundMixer::MODE_START);
	assert(ov_pcm_tell(mixer.tracks[2]) == 0);
	auto *current = mixer.tracks[2];
	assert(mixer.selectMusicSet("original") && mixer.tracks[2] == current);
	assert(!mixer.selectMusicSet("../original") && mixer.tracks[2] == current);

	const auto profile = std::filesystem::path(globalContainer->fileManager->getDir(0));
	const auto incomplete = profile / "data/zik/test-incomplete";
	std::filesystem::create_directories(incomplete);
	std::ofstream(incomplete / "a1.ogg") << "invalid";
	const auto discovered = SoundMixer::getMusicSets();
	assert(std::find(discovered.begin(), discovered.end(), "test-incomplete") == discovered.end());
	const auto broken = profile / "data/zik/test-broken";
	std::filesystem::create_directories(broken);
	for (int i = 1; i <= 2; ++i)
		std::filesystem::copy_file("data/zik/seedling/a1.ogg", broken / ("a" + std::to_string(i) + ".ogg"));
	std::ofstream(broken / "a3.ogg") << "invalid";
	assert(!mixer.selectMusicSet("test-broken"));
	assert(mixer.getMusicSet() == "original" && mixer.tracks[2] == current);
	std::filesystem::copy_file("data/zik/original/a1.ogg", broken / "a3.ogg", std::filesystem::copy_options::overwrite_existing);
	assert(!mixer.selectMusicSet("test-broken"));
	assert(mixer.getMusicSet() == "original" && mixer.tracks[2] == current);
	std::filesystem::remove_all(broken);
	std::filesystem::remove_all(incomplete);
	std::cout << "PASS: discovery, atomic failure, different lengths, queued mood, same-set no-op\n";
	SDL_PauseAudio(0);
	for (const auto& set : sets)
	{
		assert(mixer.selectMusicSet(set));
		mixer.setNextTrack(MusicTrack::WarEvent, true);
		SDL_Delay(100);
	}
	SDL_PauseAudio(1);
	std::cout << "PASS: all installed sets switch with the audio callback running\n";

	{
		GameGUI gui;
		InGameOptionScreen screen(&gui);
		assert(screen.musicSet->getCount() == sets.size() + 1);
		const auto it = std::find(screen.musicSets.begin(), screen.musicSets.end(), "seedling");
		screen.musicSet->setIndex(static_cast<int>(it - screen.musicSets.begin()));
		const auto endValue = screen.endValue;
		screen.onAction(screen.musicSet, GAGGUI::BUTTON_STATE_CHANGED, InGameOptionScreen::MUSIC_SET, 0);
		screen.onAction(screen.musicSet, GAGGUI::BUTTON_RELEASED, InGameOptionScreen::MUSIC_SET, 0);
		assert(screen.endValue == endValue);
		assert(settings.mute && settings.musicSet == "seedling" && mixer.getMusicSet() == "seedling");
		assert(screen.musicSetStatus->getText() == "Playing: Seedling");
	}
	Settings loaded;
	loaded.load();
	assert(loaded.musicSet == "seedling");
	{
		GameGUI gui;
		InGameOptionScreen screen(&gui);
		assert(screen.musicSets[screen.musicSet->getIndex()] == "seedling");
		screen.musicSet->setIndex(0);
		screen.onAction(screen.musicSet, GAGGUI::BUTTON_STATE_CHANGED, InGameOptionScreen::MUSIC_SET, 0);
		assert(settings.musicSet.empty());
		assert(std::find(sets.begin(), sets.end(), mixer.getMusicSet()) != sets.end());
	}
	loaded.load();
	assert(loaded.musicSet.empty());
	std::cout << "PASS: in-game selector, muted changes, persistence and Random reset\n";
	delete globalContainer;
	globalContainer = nullptr;
}
