// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "MusicProducer.h"
#include <fstream>
#include <algorithm>
namespace
{
std::string track()
{
	return (glob2test::sourceRoot() / "test/fixtures/audio/trimmed.opus").string();
}
std::vector<unsigned char> compressed()
{
	std::ifstream file(track(), std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace
TEST_SUITE("MusicProducer")
{
	TEST_CASE("normal PCM matches direct Opus decoding across trimmed loops")
	{
		Music::Producer p;
		REQUIRE(p.load(track(), 0) == 0);
		p.select(0, false);
		p.mode = Music::Producer::MODE_NORMAL;
		auto *reference = op_open_file(track().c_str(), nullptr);
		REQUIRE(reference);
		std::array<std::int16_t, 2048> actual, expected;
		for (unsigned n = 0; n < 100; ++n)
		{
			auto frames = n % 2 ? 127u : 1024u;
			REQUIRE(Music::read(reference, expected.data(), frames));
			p.render(actual.data(), frames);
			CHECK(std::equal(actual.begin(), actual.begin() + frames * 2, expected.begin()));
		}
		op_free(reference);
	}
	TEST_CASE("file and memory producers have identical transitions and PCM")
	{
		Music::Producer file, memory;
		auto bytes = compressed();
		for (int i = 0; i < 5; ++i)
		{
			REQUIRE(file.load(track(), i) == i);
			REQUIRE(memory.loadMemory(bytes.data(), bytes.size(), i) == i);
		}
		file.select(2, false);
		memory.select(2, false);
		std::array<std::int16_t, 2048> a, b;
		for (unsigned i = 0; i < 150; ++i)
		{
			if (i == 30 || i == 33 || i == 34 || i == 70)
			{
				file.select(i % 3 + 2, true);
				memory.select(i % 3 + 2, true);
			}
			file.render(a.data(), 1024);
			memory.render(b.data(), 1024);
			CHECK(a == b);
			CHECK(file.snapshot().track == memory.snapshot().track);
		}
	}
	TEST_CASE("replacement is atomic and preserves latest pending mood")
	{
		Music::Producer p;
		const auto bytes = compressed();
		const std::array<std::vector<unsigned char>, 3> trio{bytes, bytes, bytes};
		REQUIRE(p.replaceMemory(trio));
		p.select(2, false);
		p.mode = Music::Producer::MODE_NORMAL;
		p.select(4, true);
		p.select(3, true);
		REQUIRE(p.pendingTrack == 3);
		REQUIRE(p.replaceMemory(trio));
		CHECK(p.actTrack == 3);
		CHECK(p.nextTrack == 3);
		CHECK(p.pendingTrack == -1);
		CHECK(p.fadePos == 0);
		auto *old = p.tracks[2];
		auto broken = trio;
		broken[2] = {1, 2, 3};
		CHECK_FALSE(p.replaceMemory(broken));
		CHECK(p.tracks[2] == old);
		CHECK(p.loadMemory(nullptr, 0, 2) == -2);
		CHECK(p.tracks[2] == old);
	}
	TEST_CASE("preview ownership pause seek and close preserve gameplay cursor")
	{
		Music::Producer p;
		REQUIRE(p.load(track(), 0) == 0);
		p.select(0, false);
		std::array<std::int16_t, 2048> pcm;
		p.render(pcm.data(), 1024);
		const auto position = op_pcm_tell(p.tracks[0]);
		const auto bytes = compressed();
		REQUIRE(p.openPreviewMemory({bytes, bytes, bytes}));
		CHECK(p.snapshot().preview);
		p.control(Music::Control::Play, 1);
		p.render(pcm.data(), 1024);
		CHECK(p.snapshot().position > 0);
		CHECK(op_pcm_tell(p.tracks[0]) == position);
		p.control(Music::Control::Play, 0);
		p.render(pcm.data(), 1024);
		CHECK(std::all_of(pcm.begin(), pcm.end(), [](auto n) { return !n; }));
		p.control(Music::Control::Seek, .05);
		CHECK(p.snapshot().position == doctest::Approx(.05));
		p.preview.reset();
		p.render(pcm.data(), 1024);
		CHECK(op_pcm_tell(p.tracks[0]) == position + 1024);
	}
}
