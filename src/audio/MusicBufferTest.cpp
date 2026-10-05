// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "MusicBuffer.h"
#include <thread>
using namespace Music;
namespace
{
void fill(Buffer &b, unsigned count = TargetBlocks, int value = 12000)
{
	while (b.queue.size() < count)
	{
		auto *block = b.queue.writable();
		REQUIRE(block);
		block->generation = b.generation.load();
		block->pcm.fill(value);
		b.queue.commit();
	}
}
} // namespace
TEST_SUITE("MusicBuffer")
{
	TEST_CASE("ring wraps under concurrent producer and consumer without loss")
	{
		Ring<unsigned, 48> ring;
		std::thread producer(
			[&]
			{
				for (unsigned i = 0; i < 100000; ++i)
				{
					unsigned *p;
					while (!(p = ring.writable()))
						std::this_thread::yield();
					*p = i;
					ring.commit();
				}
			});
		bool ordered = true;
		for (unsigned i = 0; i < 100000; ++i)
		{
			const unsigned *p;
			while (!(p = ring.front()))
				std::this_thread::yield();
			if (*p != i)
				ordered = false;
			ring.pop();
		}
		producer.join();
		CHECK(ordered);
		CHECK(ring.size() == 0);
	}
	TEST_CASE("prefill capacity variable requests and bounded producer stalls")
	{
		for (unsigned stall : {4800u, 12000u, 19200u})
		{
			Buffer b;
			fill(b, CapacityBlocks);
			CHECK_FALSE(b.queue.writable());
			std::array<std::int16_t, Chunk * 2> pcm;
			// Start, drain to the refill threshold (427 ms), then stop producing.
			b.consume(pcm.data(), Chunk);
			while (b.queue.size() > LowBlocks)
				b.consume(pcm.data(), Chunk);
			for (unsigned remaining = stall; remaining;)
			{
				unsigned frames = std::min(remaining, 127u);
				b.consume(pcm.data(), frames);
				remaining -= frames;
				CHECK(std::all_of(pcm.begin(), pcm.begin() + frames * 2,
								  [](auto n) { return n == 12000; }));
			}
			CHECK(b.starvationFrames.load() == 0);
			CHECK(b.underruns.load() == 0);
		}
	}
	TEST_CASE("exhaustion fades once and waits for full recovery")
	{
		Buffer b;
		fill(b);
		std::array<std::int16_t, Chunk * 2> pcm;
		for (unsigned i = 0; i < TargetBlocks + 2; ++i)
			b.consume(pcm.data(), Chunk);
		CHECK(b.underruns.load() == 1);
		CHECK(b.starvationFrames.load() == 2 * Chunk);
		CHECK(std::all_of(pcm.begin(), pcm.end(), [](auto n) { return n == 0; }));
		fill(b, LowBlocks);
		b.consume(pcm.data(), Chunk);
		CHECK(b.buffering);
		fill(b);
		b.consume(pcm.data(), Chunk);
		CHECK_FALSE(b.buffering);
		CHECK(pcm.front() == 0);
		CHECK(pcm.back() == 12000);
		CHECK(b.underruns.load() == 1);
	}
	TEST_CASE("preview pause retains partial PCM and the consumed clock")
	{
		Buffer b;
		for (unsigned n = 0; n < TargetBlocks; ++n)
		{
			auto *block = b.queue.writable();
			REQUIRE(block);
			block->state.preview = block->state.playing = true;
			block->state.duration = 10;
			block->state.position = double((n + 1) * Chunk) / Rate;
			for (unsigned f = 0; f < Chunk; ++f)
				block->pcm[f * 2] = block->pcm[f * 2 + 1] = std::int16_t(f + 1000);
			b.queue.commit();
		}
		std::array<std::int16_t, Chunk * 2> pcm;
		b.consume(pcm.data(), 400);
		b.paused = true;
		for (unsigned i = 0; i < 8; ++i)
			b.consume(pcm.data(), Chunk);
		CHECK(b.offset == 400);
		CHECK(b.queue.size() == TargetBlocks);
		CHECK(b.consumedFrames.load() == 400);
		CHECK(b.starvationFrames.load() == 0);
		CHECK(std::all_of(pcm.begin(), pcm.end(), [](auto sample) { return sample == 0; }));
		Snapshot state;
		REQUIRE(b.snapshot.read(state, 0));
		CHECK_FALSE(state.playing);
		CHECK(state.position == doctest::Approx(400.0 / Rate));
		b.paused = false;
		b.consume(pcm.data(), 300);
		CHECK(pcm[299 * 2] == 1699);
		CHECK(b.offset == 700);
		CHECK(b.consumedFrames.load() == 700);
		b.paused = true;
		b.consume(pcm.data(), Chunk);
		++b.generation;
		b.consume(pcm.data(), Chunk);
		CHECK(std::all_of(pcm.begin(), pcm.end(), [](auto sample) { return sample == 0; }));
	}
	TEST_CASE("seek while preview is paused replaces stale PCM without advancing the clock")
	{
		Buffer b;
		fill(b);
		std::array<std::int16_t, Chunk * 2> pcm;
		b.consume(pcm.data(), 400);
		b.paused = true;
		++b.generation;
		b.consume(pcm.data(), Chunk);
		CHECK(b.queue.size() == 0);
		for (unsigned n = 0; n < TargetBlocks; ++n)
		{
			auto *block = b.queue.writable();
			REQUIRE(block);
			block->generation = b.generation.load();
			block->pcm.fill(-12000);
			block->state.preview = block->state.playing = true;
			block->state.duration = 10;
			block->state.position = 3 + double((n + 1) * Chunk) / Rate;
			b.queue.commit();
		}
		b.consume(pcm.data(), Chunk);
		Snapshot state;
		REQUIRE(b.snapshot.read(state, b.generation.load()));
		CHECK(state.position == doctest::Approx(3));
		CHECK_FALSE(state.playing);
		CHECK(b.queue.size() == TargetBlocks);
		b.paused = false;
		b.consume(pcm.data(), Chunk);
		CHECK(pcm.back() == -12000);
		CHECK(b.underruns.load() == 0);
	}
	TEST_CASE("generation replacement rejects old PCM without resetting shared cursors")
	{
		Buffer b;
		fill(b, TargetBlocks, 12000);
		std::array<std::int16_t, Chunk * 2> pcm;
		b.consume(pcm.data(), 127);
		++b.generation;
		b.consume(pcm.data(), Chunk);
		CHECK(b.queue.size() == 0);
		CHECK(std::all_of(pcm.begin() + Buffer::RampFrames * 2, pcm.end(),
						  [](auto n) { return n == 0; }));
		fill(b, TargetBlocks, -12000);
		b.consume(pcm.data(), Chunk);
		CHECK(pcm.back() == -12000);
		CHECK(b.underruns.load() == 0);
	}
}
