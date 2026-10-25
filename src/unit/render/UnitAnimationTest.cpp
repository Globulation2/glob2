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
					if (frame != previous)
						++changes;
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
			CHECK_EQ(1792 + delta / 8, unitAnimationFrame(1, 8, delta));
			for (int direction = 0; direction < 8; ++direction)
			{
				const int frame = unitMovementFrame(direction, delta);
				CHECK(frame >= 0);
				CHECK(frame < 256);
			}
		}
		// Appending presentation frames must not expand catalog validation.
		CHECK(validUnitAnimationBase(384));
		CHECK_FALSE(validUnitAnimationBase(385));
		CHECK_FALSE(validUnitAnimationBase(std::numeric_limits<std::uint32_t>::max()));
	}

	void testHistoricalOffsetsAtPhaseBoundaries()
	{
		// Every accepted offset remains valid, including bases that start inside
		// an action block. Every offset uses its containing block for turns.
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
					CHECK_EQ(1792 + (base / 64) * 32 + delta / 8, frame);
					CHECK(frame >= UNIT_ANIMATION_MOVEMENT_FRAME_COUNT);
					CHECK(frame < UNIT_ANIMATION_ATLAS_FRAME_COUNT);
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
	}
};

TEST_SUITE("UnitAnimation")
{
	TEST_CASE("HistoricalOutOfRangeTurns")
	{
		for (int base : {-1, std::numeric_limits<int>::min(), 385, 447, 448, 4096,
						 std::numeric_limits<int>::max()})
			for (int delta : {0, 7, 8, 248, 255})
			{
				const auto frame = unitAnimationFrame(base, 8, delta);
				const int clip = base < 0 ? 0 : 6;
				CHECK_EQ(1792 + clip * 32 + delta / 8, frame);
				CHECK(frame >= UNIT_ANIMATION_MOVEMENT_FRAME_COUNT);
				CHECK(frame < UNIT_ANIMATION_ATLAS_FRAME_COUNT);
			}
	}

	TEST_CASE("SkinSpriteTurns")
	{
		for (int delta = 0; delta < 256; ++delta)
			for (int direction = 0; direction <= 8; ++direction)
			{
				const auto meshFrame = unitSkinMeshFrame(direction, delta);
				const auto spriteFrame = unitSkinSpriteFrame(direction, delta);
				if (direction == 8)
				{
					CHECK_EQ(delta / 8, meshFrame);
					CHECK_EQ(256 + meshFrame, spriteFrame);
				}
				else
				{
					CHECK_EQ(unitMovementFrame(direction, delta), meshFrame);
					CHECK_EQ(meshFrame, spriteFrame);
				}
			}
	}

	TEST_CASE_FIXTURE(UnitAnimationTest, "DirectionsAndProgress")
	{
		testDirectionsAndProgress();
	}
	TEST_CASE_FIXTURE(UnitAnimationTest, "TurningKeepsItsDuration")
	{
		testTurningKeepsItsDuration();
	}
	TEST_CASE_FIXTURE(UnitAnimationTest, "LegacyCatalogAndSkinFrames")
	{
		testLegacyCatalogAndSkinFrames();
	}
	TEST_CASE_FIXTURE(UnitAnimationTest, "HistoricalOffsetsAtPhaseBoundaries")
	{
		testHistoricalOffsetsAtPhaseBoundaries();
	}
	TEST_CASE_FIXTURE(UnitAnimationTest, "TurnBlocksAndLoopBoundaries")
	{
		testTurnBlocksAndLoopBoundaries();
	}
}
