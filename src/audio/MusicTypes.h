// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

// Value-only playback contracts. Gameplay and UI do not include decoder or queue internals.
namespace Music
{
constexpr int Rate = 48000;
constexpr unsigned Chunk = 1024;
constexpr unsigned GameFadeFrames = (16384 * Rate + 22050) / 44100;
// Position is in seconds on the trimmed 48 kHz timeline. A producer snapshot
// follows decoding; a facade snapshot follows consumption of its prepared PCM.
struct Snapshot
{
	int track = -1, next = -1, pending = -1, mode = 0;
	unsigned fade = 0;
	bool preview = false, playing = false, audition = false, failed = false;
	double position = 0, duration = 0;
	std::array<double, 3> weights{1, 0, 0};
};
enum class Control
{
	Play,
	Mood,
	Seek,
	Fade,
	Blend,
	Audition,
	Reset
};
// Monotonic counters are sampled outside the audio callback. Native counters
// wrap at unsigned precision; compare short observation intervals by subtraction.
struct Diagnostics
{
	unsigned queuedFrames = 0, starvationFrames = 0, underruns = 0, consumedFrames = 0,
			 maxRenderUs = 0, maxCallbackUs = 0, maxCommandLatencyUs = 0;
};
} // namespace Music
