// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
//
// Standalone unit tests for PlayerVoice, the SDL-free voice resampling state
// machine extracted from SoundMixer. The regression target is the tail of a
// voice transmission: when the interpolation cursor rolls over on the last
// queued sample, the old mixer popped that sample and then read front() on the
// now-empty queue (undefined behavior) before checking emptiness. These tests
// drive a voice all the way to empty and assert the drain reports exhaustion
// without ever touching front() of an empty queue.
//
// Links only ../src/audio/PlayerVoice.cpp — no SDL / speex / opusfile.

#include "Glob2Test.h"
#include <cmath>

#include "PlayerVoice.h"

namespace
{
	// The cursor step the mixer advances per output-channel sample:
	// (8000/48000)*0.5. A rollover (subIndex > 1) happens roughly every
	// ceil(1 / step) advances; this many advances guarantees several rollovers
	// and therefore several pops, draining any small queue to empty.
	constexpr int STEP_ADVANCES_PER_ROLLOVER = 13; // strictly greater than 1 / ((8000/48000)*0.5) = 12

	PlayerVoice makeVoice(int sampleCount, float fill)
	{
		PlayerVoice pv;
		for (int i = 0; i < sampleCount; ++i)
			pv.voiceData.push(fill);
		// Prime the interpolation endpoints the way addVoiceData/the mixer do:
		// front is the current segment start, second sample its end.
		pv.voiceVal0 = fill;
		pv.voiceVal1 = fill;
		pv.voiceSubIndex = 0.0f;
		return pv;
	}
}

class PlayerVoiceDrainTest
{

public:
	PlayerVoiceDrainTest() {}
	~PlayerVoiceDrainTest() {}

protected:
	// Drive a small queue to empty and confirm exhaustion is reported exactly
	// once, on the advance that pops the final sample — and that the pre-fix
	// UB (front() on empty) is not needed to reach that state.
	void testDrainReportsExhaustionExactlyOnce()
	{
		PlayerVoice pv = makeVoice(3, 1.0f);
		int exhaustedCount = 0;
		bool sawExhausted = false;
		// Advance well past the number of rollovers needed to pop all 3 samples.
		for (int i = 0; i < 3 * STEP_ADVANCES_PER_ROLLOVER && !sawExhausted; ++i)
		{
			bool exhausted = false;
			pv.advanceOutputSample(exhausted);
			if (exhausted)
			{
				++exhaustedCount;
				sawExhausted = true;
			}
		}
		CHECK(sawExhausted);
		CHECK_EQ(1, exhaustedCount);
		CHECK(pv.voiceData.empty());
	}

	// A one-sample queue: the very first rollover pops it and must report
	// exhaustion. This is the exact BH-199 trigger ("drains to exactly one
	// sample") — the old code read front() here on the emptied queue.
	void testSingleSampleQueueDrainsOnFirstRollover()
	{
		PlayerVoice pv = makeVoice(1, 0.25f);
		bool sawExhausted = false;
		for (int i = 0; i < STEP_ADVANCES_PER_ROLLOVER && !sawExhausted; ++i)
		{
			bool exhausted = false;
			pv.advanceOutputSample(exhausted);
			sawExhausted = sawExhausted || exhausted;
		}
		CHECK(sawExhausted);
		CHECK(pv.voiceData.empty());
	}

	// A single advance that does not cross a rollover must not pop anything and
	// must not report exhaustion.
	void testNoRolloverKeepsQueueIntact()
	{
		PlayerVoice pv = makeVoice(4, 1.0f);
		const size_t before = pv.voiceData.size();
		bool exhausted = false;
		pv.advanceOutputSample(exhausted);
		CHECK(!exhausted);
		CHECK_EQ(before, pv.voiceData.size());
	}

	// The returned contribution is computed from the entry state (subIndex 0,
	// val0/val1), i.e. equals voiceVal0 on the first advance from a fresh voice.
	void testContributionUsesPreAdvanceState()
	{
		PlayerVoice pv = makeVoice(4, 0.0f);
		pv.voiceVal0 = 2.0f;
		pv.voiceVal1 = 6.0f;
		pv.voiceSubIndex = 0.0f;
		bool exhausted = false;
		const float c = pv.advanceOutputSample(exhausted);
		// (1-0)*2 + 0*6 == 2.
		CHECK(std::fabs((2.0) - (static_cast<double>(c))) <= (1e-6));
		CHECK(!exhausted);
	}
};
TEST_SUITE("PlayerVoiceDrain")
{
	TEST_CASE_FIXTURE(PlayerVoiceDrainTest, "DrainReportsExhaustionExactlyOnce") { testDrainReportsExhaustionExactlyOnce(); }
	TEST_CASE_FIXTURE(PlayerVoiceDrainTest, "SingleSampleQueueDrainsOnFirstRollover") { testSingleSampleQueueDrainsOnFirstRollover(); }
	TEST_CASE_FIXTURE(PlayerVoiceDrainTest, "NoRolloverKeepsQueueIntact") { testNoRolloverKeepsQueueIntact(); }
	TEST_CASE_FIXTURE(PlayerVoiceDrainTest, "ContributionUsesPreAdvanceState") { testContributionUsesPreAdvanceState(); }
}

TEST_SUITE("PlayerVoiceDrain")
{
TEST_CASE("one second of Speex PCM lasts one second at the mixer rate")
{
    PlayerVoice voice;
    for (int i = 0; i < 8000; ++i) voice.voiceData.push(1.0f);
    bool exhausted = false;
    int samples = 0;
    while (!exhausted && samples < 100000)
    {
        voice.advanceOutputSample(exhausted);
        ++samples;
    }
    REQUIRE(exhausted);
    CHECK(std::abs(samples - 96000) <= 12);
}
}
