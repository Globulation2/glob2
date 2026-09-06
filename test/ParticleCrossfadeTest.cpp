// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

/*********************************************************
 *
 * Regression test for computeParticleCrossfade — the pure frame/alpha
 * interpolation extracted from GameGUI::drawParticles (bug BH-391: the
 * blend-in frame was drawn centered with the fade-out frame's dimensions).
 * The extraction moves the frame selection into a testable unit; the
 * centering itself now fetches dimensions per frame in
 * drawCenteredParticleSprite, so the copy-paste bug cannot recur.
 *
 * Header-only include — no game objects, no SDL, nothing to link.
 *
 ********************************************************/

#include <cppunit/extensions/HelperMacros.h>

#include "ParticleCrossfade.h"

class ParticleCrossfadeTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(ParticleCrossfadeTest);
		CPPUNIT_TEST(testAlphaSplitIsConstantOpacity);
		CPPUNIT_TEST(testFrameBIsAlwaysNextFrame);
		CPPUNIT_TEST(testStartsAtStartImg);
		CPPUNIT_TEST(testFrameBSuppressedAtLastFrame);
		CPPUNIT_TEST(testFrameProgressionIsMonotonic);
		CPPUNIT_TEST(testSmokeParticleRange);
	CPPUNIT_TEST_SUITE_END();

protected:
	// The two frames crossfade at constant total opacity.
	void testAlphaSplitIsConstantOpacity(void)
	{
		for (int age = 0; age <= 50; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 8, age, 50);
			CPPUNIT_ASSERT_EQUAL(PARTICLE_ALPHA_OPAQUE, (int)c.alphaA + (int)c.alphaB);
		}
	}

	void testFrameBIsAlwaysNextFrame(void)
	{
		for (int age = 0; age <= 30; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(2, 7, age, 30);
			CPPUNIT_ASSERT_EQUAL(c.frameA + 1, c.frameB);
		}
	}

	// At age 0 the particle shows startImg fully opaque.
	void testStartsAtStartImg(void)
	{
		ParticleCrossfade c = computeParticleCrossfade(3, 9, 0, 40);
		CPPUNIT_ASSERT_EQUAL(3, c.frameA);
		CPPUNIT_ASSERT_EQUAL((int)PARTICLE_ALPHA_OPAQUE, (int)c.alphaA);
		CPPUNIT_ASSERT_EQUAL(0, (int)c.alphaB);
	}

	// endImg is one past the last drawable frame: once frameA reaches
	// endImg - 1 there is no next frame to blend in.
	void testFrameBSuppressedAtLastFrame(void)
	{
		bool sawSuppressed = false;
		for (int age = 0; age <= 20; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 4, age, 20);
			CPPUNIT_ASSERT(c.frameA < 4);
			CPPUNIT_ASSERT_EQUAL(c.frameB < 4, c.hasFrameB);
			if (!c.hasFrameB)
				sawSuppressed = true;
		}
		CPPUNIT_ASSERT(sawSuppressed);
	}

	void testFrameProgressionIsMonotonic(void)
	{
		int prev = 0;
		for (int age = 0; age <= 60; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 6, age, 60);
			CPPUNIT_ASSERT(c.frameA >= prev);
			prev = c.frameA;
		}
		// the interpolation must actually advance past the first frame
		CPPUNIT_ASSERT(prev > 0);
	}

	// The in-game emitters (smoke, turret flash) use startImg=0, endImg=2:
	// frame 0 fades into frame 1, and frame 1 finishes without a blend target.
	void testSmokeParticleRange(void)
	{
		for (int age = 0; age <= 50; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 2, age, 50);
			CPPUNIT_ASSERT(c.frameA == 0 || c.frameA == 1);
			if (c.hasFrameB)
				CPPUNIT_ASSERT_EQUAL(1, c.frameB);
		}
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(ParticleCrossfadeTest);
