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
// Links only ../src/PlayerVoice.cpp — no SDL / speex / vorbis.

#include <cppunit/extensions/HelperMacros.h>

#include "../src/PlayerVoice.h"

namespace
{
	// The cursor step the mixer advances per output-channel sample:
	// (8000/44100)*0.5. A rollover (subIndex > 1) happens roughly every
	// ceil(1 / step) advances; this many advances guarantees several rollovers
	// and therefore several pops, draining any small queue to empty.
	constexpr int STEP_ADVANCES_PER_ROLLOVER = 12; // 1 / ((8000/44100)*0.5) ~= 11.03

	PlayerVoice makeVoice(int sampleCount, float fill)
	{
		PlayerVoice pv;
		for (int i = 0; i < sampleCount; ++i)
			pv.voiceDatas.push(fill);
		// Prime the interpolation endpoints the way addVoiceData/the mixer do:
		// front is the current segment start, second sample its end.
		pv.voiceVal0 = fill;
		pv.voiceVal1 = fill;
		pv.voiceSubIndex = 0.0f;
		return pv;
	}
}

class PlayerVoiceDrainTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(PlayerVoiceDrainTest);
		CPPUNIT_TEST(testDrainReportsExhaustionExactlyOnce);
		CPPUNIT_TEST(testSingleSampleQueueDrainsOnFirstRollover);
		CPPUNIT_TEST(testNoRolloverKeepsQueueIntact);
		CPPUNIT_TEST(testContributionUsesPreAdvanceState);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp(void) override {}
	void tearDown(void) override {}

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
		CPPUNIT_ASSERT(sawExhausted);
		CPPUNIT_ASSERT_EQUAL(1, exhaustedCount);
		CPPUNIT_ASSERT(pv.voiceDatas.empty());
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
		CPPUNIT_ASSERT(sawExhausted);
		CPPUNIT_ASSERT(pv.voiceDatas.empty());
	}

	// A single advance that does not cross a rollover must not pop anything and
	// must not report exhaustion.
	void testNoRolloverKeepsQueueIntact()
	{
		PlayerVoice pv = makeVoice(4, 1.0f);
		const size_t before = pv.voiceDatas.size();
		bool exhausted = false;
		pv.advanceOutputSample(exhausted);
		CPPUNIT_ASSERT(!exhausted);
		CPPUNIT_ASSERT_EQUAL(before, pv.voiceDatas.size());
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
		CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, static_cast<double>(c), 1e-6);
		CPPUNIT_ASSERT(!exhausted);
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(PlayerVoiceDrainTest);
