// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "MusicLibraryScreen.h"
#include "MusicImportScreen.h"
#include "MusicSetScreen.h"
#include "MusicUI.h"
#include "SoundMixer.h"
#include <fstream>
#include <cstdlib>
#include "GlobalContainer.h"
#include <ScreenStack.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <filesystem>
namespace
{
void capture(Glob2UI::Screen &screen, const std::string &name)
{
	screen.beginExecution(globalContainer->gfx);
	screen.paintFrame(SDL_GetTicks());
	globalContainer->gfx->printScreen(name);
	screen.paintFrame(SDL_GetTicks());
	globalContainer->gfx->nextFrame();
	const auto file = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / name;
	REQUIRE(std::filesystem::exists(file));
	std::filesystem::copy_file(file, glob2test::artifactDir() / name,
							   std::filesystem::copy_options::overwrite_existing);
}
void views(int width, int height)
{
	glob2test::ScopedEnvironment audio("SDL_AUDIODRIVER", "dummy");
	glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
		.display = true, .loadStrings = true, .width = width, .height = height});
	REQUIRE(musicText("Calm") == "Calm");
	REQUIRE(musicText("Play") == "Play");
	REQUIRE(musicText("Untranslated music label") == "Untranslated music label");
	GAGGUI::ScreenStack stack(*globalContainer->gfx);
	MusicLibraryScreen library(stack);
	library.requested = true;
	library.items =
		nlohmann::json::array({{{"id", "4f59b80a-a421-4237-8a96-c05e99a9a554"},
								{"metadata",
								 {{"title", "Moss lantern"},
								  {"artist", "Community composer"},
								  {"description", "Three arrangements for a forest colony."}}},
								{"frames", 4800000},
								{"likes", 12},
								{"liked", false}}});
	capture(library, "music-library.bmp");
	MusicImportScreen importScreen;
	capture(importScreen, "music-import.bmp");
	Music::Metadata meta;
	meta.title = "Moss lantern";
	meta.artist = "Community composer";
	meta.license = "CC0-1.0";
	meta.frames = 4800000;
	meta.description = "Three arrangements for a forest colony.";
	auto root = glob2test::sourceRoot() / "data/zik/original";
	std::array<std::string, 3> paths{(root / "a1.opus").string(), (root / "a2.opus").string(),
									 (root / "a3.opus").string()};
	MusicSetScreen detail(meta, paths, musicText("Install"));
	for (unsigned mood = 0; mood < 3; ++mood)
		detail.info.waveforms[mood].assign(512, .25f + .2f * mood);
	capture(detail, "music-detail.bmp");
	CHECK(detail.preview.ready());
}
} // namespace
TEST_SUITE("CommunityMusicUI")
{
	TEST_CASE("desktop screens [display][artifacts]")
	{
		views(1280, 800);
	}
	TEST_CASE("touch screens [display][artifacts]")
	{
		views(480, 800);
	}
	TEST_CASE("web release imports and becomes selectable [display]" *
			  doctest::skip(std::getenv("GLOB2_MUSIC_TEST_ARCHIVE") == nullptr))
	{
		const char *archivePath = std::getenv("GLOB2_MUSIC_TEST_ARCHIVE");
		if (!archivePath)
		{
			MESSAGE("Set GLOB2_MUSIC_TEST_ARCHIVE to the real web E2E download to run this "
					"integration.");
			return;
		}
		glob2test::ScopedEnvironment audio("SDL_AUDIODRIVER", "dummy");
		glob2test::HeadlessGlobals globals(
			glob2test::GlobalsOptions{.display = true, .loadStrings = true});
		std::ifstream file(archivePath, std::ios::binary);
		std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
		Music::Library library(GAGCore::Toolkit::getFileManager()->getDir(0));
		auto sets = library.importZip(bytes);
		REQUIRE(sets.size() == 1);
		CHECK(sets.front().info.title == "Moss lantern");
		CHECK(SoundMixer::musicSetLabel(sets.front().directory) == "Moss lantern");
		REQUIRE(globalContainer->mix->selectMusicSet(sets.front().directory));
		CHECK(globalContainer->mix->getMusicSet() == sets.front().directory);
	}
}
