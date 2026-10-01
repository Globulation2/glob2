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

#include "Glob2Test.h"

#include "ParticleCrossfade.h"

class ParticleCrossfadeTest
{
public:

protected:
	// The two frames crossfade at constant total opacity.
	void testAlphaSplitIsConstantOpacity(void)
	{
		for (int age = 0; age <= 50; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 8, age, 50);
			CHECK_EQ(PARTICLE_ALPHA_OPAQUE, (int)c.alphaA + (int)c.alphaB);
		}
	}

	void testFrameBIsAlwaysNextFrame(void)
	{
		for (int age = 0; age <= 30; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(2, 7, age, 30);
			CHECK_EQ(c.frameA + 1, c.frameB);
		}
	}

	// At age 0 the particle shows startImg fully opaque.
	void testStartsAtStartImg(void)
	{
		ParticleCrossfade c = computeParticleCrossfade(3, 9, 0, 40);
		CHECK_EQ(3, c.frameA);
		CHECK_EQ((int)PARTICLE_ALPHA_OPAQUE, (int)c.alphaA);
		CHECK_EQ(0, (int)c.alphaB);
	}

	// endImg is one past the last drawable frame: once frameA reaches
	// endImg - 1 there is no next frame to blend in.
	void testFrameBSuppressedAtLastFrame(void)
	{
		bool sawSuppressed = false;
		for (int age = 0; age <= 20; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 4, age, 20);
			CHECK(c.frameA < 4);
			CHECK_EQ(c.frameB < 4, c.hasFrameB);
			if (!c.hasFrameB)
				sawSuppressed = true;
		}
		CHECK(sawSuppressed);
	}

	void testFrameProgressionIsMonotonic(void)
	{
		int prev = 0;
		for (int age = 0; age <= 60; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 6, age, 60);
			CHECK(c.frameA >= prev);
			prev = c.frameA;
		}
		// the interpolation must actually advance past the first frame
		CHECK(prev > 0);
	}

	// The in-game emitters (smoke, turret flash) use startImg=0, endImg=2:
	// frame 0 fades into frame 1, and frame 1 finishes without a blend target.
	void testSmokeParticleRange(void)
	{
		for (int age = 0; age <= 50; age++)
		{
			ParticleCrossfade c = computeParticleCrossfade(0, 2, age, 50);
			CHECK((c.frameA == 0 || c.frameA == 1));
			if (c.hasFrameB)
				CHECK_EQ(1, c.frameB);
		}
	}
};

TEST_SUITE("ParticleCrossfade")
{
	TEST_CASE_FIXTURE(ParticleCrossfadeTest, "AlphaSplitIsConstantOpacity") { testAlphaSplitIsConstantOpacity(); }
	TEST_CASE_FIXTURE(ParticleCrossfadeTest, "FrameBIsAlwaysNextFrame") { testFrameBIsAlwaysNextFrame(); }
	TEST_CASE_FIXTURE(ParticleCrossfadeTest, "StartsAtStartImg") { testStartsAtStartImg(); }
	TEST_CASE_FIXTURE(ParticleCrossfadeTest, "FrameBSuppressedAtLastFrame") { testFrameBSuppressedAtLastFrame(); }
	TEST_CASE_FIXTURE(ParticleCrossfadeTest, "FrameProgressionIsMonotonic") { testFrameProgressionIsMonotonic(); }
	TEST_CASE_FIXTURE(ParticleCrossfadeTest, "SmokeParticleRange") { testSmokeParticleRange(); }
}
