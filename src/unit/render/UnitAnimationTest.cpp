// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "UnitAnimation.h"
#include <limits>

class UnitAnimationTest
{
public:

	void testDirectionsAndProgress()
	{
		for (int base = 0; base <= 384; base += 64)
			for (int direction = 0; direction < 8; ++direction)
			{
				int changes = 0;
				int previous = -1;
				for (int delta = 0; delta < 256; ++delta)
				{
					const int frame = unitAnimationFrame(base, direction, delta);
					CHECK((frame >= 0 && frame < 1792));
					CHECK(frame >= base * 4 + direction * 32);
					CHECK(frame < base * 4 + (direction + 1) * 32);
					if (delta % 32 == 0)
						CHECK_EQ((base + direction * 8 + delta / 32) * 4, frame);
					if (frame != previous) ++changes;
					previous = frame;
				}
				CHECK_EQ(32, changes);
			}
	}

	void testTurningKeepsItsDuration()
	{
		for (int base = 0; base <= 384; base += 64)
			for (int delta = 0; delta < 256; ++delta)
			{
				const int frame = unitAnimationFrame(base, 8, delta);
				CHECK_EQ(1792 + (base / 64) * 32 + delta / 8, frame);
				CHECK((frame >= 1792 && frame < UNIT_ANIMATION_ATLAS_FRAME_COUNT));
			}
	}

	void testLegacyCatalogAndSkinFrames()
	{
		for (int delta = 0; delta < 256; ++delta)
		{
			// Historical catalogs can start partway through a stock action.
			CHECK_EQ(4 + (delta / 32) * 32, unitAnimationFrame(1, 8, delta));
			for (int direction = 0; direction <= 8; ++direction)
			{
				const int frame = unitSkinAnimationFrame(direction, delta);
				CHECK(frame >= 0);
				CHECK(frame < 256);
			}
			CHECK_EQ((delta / 32) * 32, unitSkinAnimationFrame(8, delta));
		}
		// Appending presentation frames must not expand catalog validation.
		CHECK(validUnitAnimationBase(384));
		CHECK_FALSE(validUnitAnimationBase(385));
		CHECK_FALSE(validUnitAnimationBase(std::numeric_limits<std::uint32_t>::max()));
	}

	void testHistoricalOffsetsAtPhaseBoundaries()
	{
		// Every accepted offset remains valid, including bases that start inside
		// an action block. Only aligned stock bases may use the appended turns.
		for (int base = 0; base <= 384; ++base)
		{
			CHECK(validUnitAnimationBase(base));
			for (int delta : {0, 7, 8, 31, 32, 247, 248, 255})
			{
				for (int direction = 0; direction < 8; ++direction)
					CHECK_EQ(base * 4 + direction * 32 + delta / 8,
						unitAnimationFrame(base, direction, delta));
				if (base % 64 != 0)
				{
					const int frame = unitAnimationFrame(base, 8, delta);
					CHECK_EQ(base * 4 + (delta / 32) * 32, frame);
					CHECK(frame < UNIT_ANIMATION_MOVEMENT_FRAME_COUNT);
				}
			}
		}
	}

	void testTurnBlocksAndLoopBoundaries()
	{
		for (int clip = 0; clip < 7; ++clip)
		{
			const int base = clip * 64;
			const int first = unitAnimationFrame(base, 8, 0);
			const int last = unitAnimationFrame(base, 8, 255);
			CHECK_EQ(first + 31, last);
			CHECK_EQ(first, unitAnimationFrame(base, 8, 7));
			CHECK_EQ(first + 1, unitAnimationFrame(base, 8, 8));
			CHECK_EQ(last, unitAnimationFrame(base, 8, 248));
			// Each loop occupies exactly one existing HD-completeness block.
			CHECK_EQ(0, first % 32);
			if (clip < 6)
				CHECK_EQ(last + 1, unitAnimationFrame(base + 64, 8, 0));
			else
				CHECK_EQ(UNIT_ANIMATION_ATLAS_FRAME_COUNT - 1, last);
		}
		// Skin turns keep their separate eight-frame cadence at the same edges.
		CHECK_EQ(0, unitSkinAnimationFrame(8, 31));
		CHECK_EQ(32, unitSkinAnimationFrame(8, 32));
		CHECK_EQ(224, unitSkinAnimationFrame(8, 255));
	}
};

TEST_SUITE("UnitAnimation")
{
	TEST_CASE_FIXTURE(UnitAnimationTest, "DirectionsAndProgress") { testDirectionsAndProgress(); }
	TEST_CASE_FIXTURE(UnitAnimationTest, "TurningKeepsItsDuration") { testTurningKeepsItsDuration(); }
	TEST_CASE_FIXTURE(UnitAnimationTest, "LegacyCatalogAndSkinFrames") { testLegacyCatalogAndSkinFrames(); }
	TEST_CASE_FIXTURE(UnitAnimationTest, "HistoricalOffsetsAtPhaseBoundaries") { testHistoricalOffsetsAtPhaseBoundaries(); }
	TEST_CASE_FIXTURE(UnitAnimationTest, "TurnBlocksAndLoopBoundaries") { testTurnBlocksAndLoopBoundaries(); }
}
