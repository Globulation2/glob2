// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MusicTypes.h"
#include <atomic>
#include <algorithm>
#include <bit>
#include <cmath>
namespace Music
{
// Exactly one producer and one consumer. Neither side can reset the other's cursor.
template <class T, unsigned Capacity> class Ring
{
	static constexpr unsigned Storage = std::bit_ceil(Capacity);
	std::array<T, Storage> items{};
	alignas(64) std::atomic<unsigned> write{0};
	alignas(64) std::atomic<unsigned> read{0};

  public:
	static_assert(std::atomic<unsigned>::is_always_lock_free);
	unsigned size() const
	{
		auto r = read.load(std::memory_order_acquire);
		return std::min(Capacity, write.load(std::memory_order_acquire) - r);
	}
	T *writable()
	{
		auto w = write.load(std::memory_order_relaxed);
		return w - read.load(std::memory_order_acquire) < Capacity ? &items[w % Storage] : nullptr;
	}
	void commit() { write.fetch_add(1, std::memory_order_release); }
	const T *front() const
	{
		auto r = read.load(std::memory_order_relaxed);
		return r != write.load(std::memory_order_acquire) ? &items[r % Storage] : nullptr;
	}
	void pop() { read.fetch_add(1, std::memory_order_release); }
};
constexpr unsigned TargetBlocks = 36, LowBlocks = 24, CapacityBlocks = 48;
struct Block
{
	std::array<std::int16_t, Chunk * 2> pcm{};
	Snapshot state;
	unsigned generation = 0, commandId = 0, commandAt = 0;
};
// Atomic fields make seqlock retries well-defined in C++, including on 32-bit targets.
// The callback is the only writer; readers never make it wait.
class SnapshotMailbox
{
	std::atomic<unsigned> sequence{0}, generation{0};
	std::array<std::atomic<int>, 9> integers{};
	std::array<std::atomic<float>, 5> numbers{};

  public:
	static_assert(std::atomic<float>::is_always_lock_free);
	void publish(const Snapshot &s, unsigned gen)
	{
		sequence.fetch_add(1, std::memory_order_acq_rel);
		const int data[] = {s.track,   s.next,    s.pending,  s.mode,  int(s.fade),
							s.preview, s.playing, s.audition, s.failed};
		const float values[] = {float(s.position), float(s.duration), float(s.weights[0]),
								float(s.weights[1]), float(s.weights[2])};
		for (unsigned i = 0; i < 9; ++i)
			integers[i].store(data[i], std::memory_order_relaxed);
		for (unsigned i = 0; i < 5; ++i)
			numbers[i].store(values[i], std::memory_order_relaxed);
		generation.store(gen, std::memory_order_relaxed);
		sequence.fetch_add(1, std::memory_order_release);
	}
	bool read(Snapshot &s, unsigned gen) const
	{
		for (unsigned attempt = 0; attempt < 4; ++attempt)
		{
			auto before = sequence.load(std::memory_order_acquire);
			if (!before || before % 2)
				continue;
			int v[9];
			float n[5];
			for (unsigned i = 0; i < 9; ++i)
				v[i] = integers[i].load(std::memory_order_relaxed);
			for (unsigned i = 0; i < 5; ++i)
				n[i] = numbers[i].load(std::memory_order_relaxed);
			auto g = generation.load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (before != sequence.load(std::memory_order_relaxed) || g != gen)
				continue;
			s = {v[0],       v[1],       v[2],       v[3], unsigned(v[4]), bool(v[5]),
				 bool(v[6]), bool(v[7]), bool(v[8]), n[0], n[1],           {n[2], n[3], n[4]}};
			return true;
		}
		return false;
	}
};
class Buffer
{
  public:
	Ring<Block, CapacityBlocks> queue;
	std::atomic<bool> needsPrefill{true}, paused{false};
	std::atomic<unsigned> generation{0}, starvationFrames{0}, underruns{0}, consumedFrames{0},
		maxCommandLatencyUs{0};
	SnapshotMailbox snapshot;
	// Consumer-owned recovery state. Startup and resets are not underruns.
	unsigned offset = 0, seenGeneration = 0, fade = 0, consumedCommand = 0;
	bool buffering = true, started = false, wasPaused = false;
	std::array<std::int16_t, 2> last{};
	static constexpr unsigned RampFrames = 240; // five milliseconds
	void consume(std::int16_t *output, unsigned frames, unsigned nowMs = 0)
	{
		unsigned gen = generation.load(std::memory_order_acquire);
		if (gen != seenGeneration)
		{
			seenGeneration = gen;
			offset = 0;
			buffering = true;
			started = false;
			fade = RampFrames;
		}
		while (auto *block = queue.front())
		{
			if (block->generation == gen)
				break;
			queue.pop();
			offset = 0;
		}
		const bool pause = paused.load(std::memory_order_acquire);
		if (pause)
		{
			// Pause the transport, not the decoder timeline: retain every prepared
			// sample and the partial-block offset so resume cannot skip music.
			if (!wasPaused)
				fade = RampFrames;
			wasPaused = true;
			for (unsigned f = 0; f < frames; ++f)
			{
				for (unsigned c = 0; c < 2; ++c)
					output[f * 2 + c] = std::int16_t(int(last[c]) * int(fade) / int(RampFrames));
				if (fade)
					--fade;
			}
			if (!fade)
				last = {}; // A later seek while paused must not restart an old tail.
			publishPosition(gen, true);
			return;
		}
		if (wasPaused)
		{
			wasPaused = false;
			fade = buffering ? 0 : RampFrames;
		}
		if (buffering && !fade && queue.size() >= TargetBlocks)
		{
			buffering = false;
			started = true;
			fade = RampFrames;
		}
		needsPrefill.store(buffering, std::memory_order_release);
		for (unsigned f = 0; f < frames; ++f)
		{
			const auto *block = buffering ? nullptr : queue.front();
			if (!block)
			{
				if (!buffering)
				{
					buffering = true;
					needsPrefill.store(true, std::memory_order_release);
					fade = RampFrames;
					underruns.fetch_add(1, std::memory_order_relaxed);
				}
				for (unsigned c = 0; c < 2; ++c)
					output[f * 2 + c] = std::int16_t(int(last[c]) * int(fade) / int(RampFrames));
				if (fade)
					--fade;
				if (started)
					starvationFrames.fetch_add(1, std::memory_order_relaxed);
				continue;
			}
			if (block->commandId && block->commandId != consumedCommand)
			{
				consumedCommand = block->commandId;
				auto delay = (nowMs - block->commandAt) * 1000;
				maxCommandLatencyUs.store(std::max(delay, maxCommandLatencyUs.load()),
										  std::memory_order_relaxed);
			}
			for (unsigned c = 0; c < 2; ++c)
			{
				int sample = block->pcm[offset * 2 + c];
				if (fade)
					sample = sample * int(RampFrames - fade) / int(RampFrames);
				output[f * 2 + c] = last[c] = std::int16_t(sample);
			}
			if (fade)
				--fade;
			if (++offset == Chunk)
			{
				snapshot.publish(block->state, gen);
				queue.pop();
				offset = 0;
			}
			consumedFrames.fetch_add(1, std::memory_order_relaxed);
		}
		publishPosition(gen, false);
	}

  private:
	void publishPosition(unsigned gen, bool pause)
	{
		const auto *block = queue.front();
		if (!block || block->generation != gen)
			return;
		auto state = block->state;
		// Blocks hold the producer's end position. The display follows the
		// consumed portion, including a partial block parked by pause or seek.
		if (state.preview && state.playing && state.duration > 0)
		{
			state.position =
				std::fmod(state.position - double(Chunk - offset) / Rate, state.duration);
			if (state.position < 0)
				state.position += state.duration;
		}
		if (pause && state.preview)
			state.playing = false;
		snapshot.publish(state, gen);
	}
};
} // namespace Music
