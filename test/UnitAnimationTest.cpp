// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "../src/render/UnitAnimation.h"

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

	void testTurningKeepsItsCadence()
	{
		for (int base = 0; base <= 384; base += 64)
			for (int delta = 0; delta < 256; ++delta)
			{
				const int frame = unitAnimationFrame(base, 8, delta);
				CHECK_EQ((base + 8 * (delta / 32)) * 4, frame);
				CHECK((frame >= 0 && frame < 1792));
			}
	}
};

TEST_SUITE("UnitAnimation")
{
	TEST_CASE_FIXTURE(UnitAnimationTest, "DirectionsAndProgress") { testDirectionsAndProgress(); }
	TEST_CASE_FIXTURE(UnitAnimationTest, "TurningKeepsItsCadence") { testTurningKeepsItsCadence(); }
}
