// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicStream.h"
#include "MusicLibrary.h"
#include "Glob2Test.h"
#include <cmath>
#include <filesystem>
#include <fstream>
TEST_SUITE("CommunityMusic")
{
	TEST_CASE("computed fades preserve every entry of the original lookup table")
	{
		const unsigned samples = Music::GameFadeFrames * 2;
		double l = static_cast<double>(samples - 1), m = 65535, a = -2 / (l * l * l),
			   b = 3 / (l * l);
		for (unsigned i = 0; i < samples; ++i)
		{
			double x = i;
			CHECK(Music::fadeGain(i, samples) ==
				  static_cast<int>(m * (a * (x * x * x) + b * (x * x))));
		}
		CHECK(Music::fadeGain(samples + 1024, samples) == Music::fadeGain(samples - 1, samples));
	}
	TEST_CASE("preview storage is bounded independently of track length")
	{
		CHECK(sizeof(Music::Preview) < 16 * 1024);
		Music::Preview preview;
		std::array<std::int16_t, 2048> buffer{};
		preview.render(buffer.data(), 1024);
		for (auto sample : buffer)
			CHECK(sample == 0);
		CHECK_FALSE(preview.openMemory(3, nullptr, 0));
		CHECK_FALSE(preview.seekTo(5));
		preview.setFade(NAN);
		preview.setBlend(NAN);
		CHECK(preview.weights()[0] == 1);
	}
	TEST_CASE("archive parser rejects malformed and oversized archives")
	{
		auto root = std::filesystem::temp_directory_path() / "glob2-music-import-test";
		std::filesystem::remove_all(root);
		Music::Library library(root);
		CHECK_THROWS(library.importZip({1, 2, 3}));
		CHECK_THROWS(library.paths("../../outside"));
		CHECK_THROWS(library.importTracks({}));
		CHECK(library.list().empty());
		std::filesystem::remove_all(root);
	}
}
