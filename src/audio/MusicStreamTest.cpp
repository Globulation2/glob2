// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicStream.h"
#include "MusicLibrary.h"
#include "Glob2Test.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <zlib.h>
namespace
{
void zip32(std::vector<unsigned char> &bytes, size_t at, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		bytes[at + i] = value >> (i * 8);
}
// One structurally valid stored entry; validation then rejects the incomplete set.
std::vector<unsigned char> singleEntryZip()
{
	const std::string name = "a1.opus";
	std::vector<unsigned char> bytes(113);
	zip32(bytes, 0, 0x04034b50);
	zip32(bytes, 14, crc32(0, reinterpret_cast<const Bytef *>("x"), 1));
	zip32(bytes, 18, 1);
	zip32(bytes, 22, 1);
	bytes[26] = name.size();
	std::copy(name.begin(), name.end(), bytes.begin() + 30);
	bytes[37] = 'x';
	zip32(bytes, 38, 0x02014b50);
	zip32(bytes, 38 + 16, crc32(0, reinterpret_cast<const Bytef *>("x"), 1));
	zip32(bytes, 38 + 20, 1);
	zip32(bytes, 38 + 24, 1);
	bytes[38 + 28] = name.size();
	std::copy(name.begin(), name.end(), bytes.begin() + 38 + 46);
	zip32(bytes, 91, 0x06054b50);
	bytes[91 + 8] = bytes[91 + 10] = 1;
	zip32(bytes, 91 + 12, 53);
	zip32(bytes, 91 + 16, 38);
	return bytes;
}
} // namespace
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
	TEST_CASE("archive offsets and sizes cannot wrap on 32-bit targets")
	{
		auto root = std::filesystem::temp_directory_path() / "glob2-music-zip-bounds-test";
		std::filesystem::remove_all(root);
		Music::Library library(root);
		for (size_t field : {size_t(38 + 42), size_t(38 + 20), size_t(91 + 12), size_t(91 + 16)})
			for (std::uint32_t value : {0xffffffffu, 0xfffffffeu, 0xfffffff0u})
			{
				auto archive = singleEntryZip();
				zip32(archive, field, value);
				CHECK_THROWS(library.importZip(archive));
			}
		CHECK(library.list().empty());
		std::filesystem::remove_all(root);
	}
}
