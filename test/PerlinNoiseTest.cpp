/*
 Copyright (C) 2010 Leo Wandersleb

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "PerlinNoiseTest.h"
#include "../src/PerlinNoise.h"
CPPUNIT_TEST_SUITE_REGISTRATION( PerlinNoiseTest );

const static int SEED = 12;
PerlinNoise * perlinNoise;
float * position;

//initialize perlinNoise with always the same seed and setUp a positon-vector.
void PerlinNoiseTest::setUp()
{
	perlinNoise = new PerlinNoise(SEED);
	position = new float[3];
	position[0] = .11111f;
	position[1] = .21111f;
	position[2] = .71111f;
}
void PerlinNoiseTest::tearDown()
{
	delete perlinNoise;
	delete[] position;
}
void PerlinNoiseTest::testConstructor()
{
	CPPUNIT_FAIL("not implemented yet!");
}
void PerlinNoiseTest::testNotZeroOne()
{
	float a = perlinNoise->Noise1d(position);
	CPPUNIT_ASSERT(a != 0.0f);
	CPPUNIT_ASSERT(a != 1.0f);
}
//via resetting the seed, noise should generate different values
void PerlinNoiseTest::testReseed()
{
	float a = perlinNoise->Noise1d(position);
	perlinNoise->reseed();
	float b = perlinNoise->Noise1d(position);
	CPPUNIT_ASSERT(a != b);
}
//via resetting the seed to different values, noise should generate different values
void PerlinNoiseTest::testReseedIntDifferent()
{
	float valueWithOriginalSeed = perlinNoise->Noise1d(position);
	perlinNoise->reseed(SEED + 3);
	float valueWithSomeOtherSeed = perlinNoise->Noise1d(position);
	CPPUNIT_ASSERT(valueWithOriginalSeed != valueWithSomeOtherSeed);
}
//via resetting the seed to what it was, noise should regenerate same values
void PerlinNoiseTest::testReseedIntSame()
{
	float valueWithOriginalSeed = perlinNoise->Noise1d(position);
	perlinNoise->reseed(SEED);
	float valueWithReseededOriginalSeed = perlinNoise->Noise1d(position);
	CPPUNIT_ASSERT_EQUAL(valueWithOriginalSeed, valueWithReseededOriginalSeed);
}
//the means to access 1d-noise should result in the same value
void PerlinNoiseTest::testnoise1d()
{
	float a = perlinNoise->Noise1d(position);
	float b = perlinNoise->Noise(position[0]);
	CPPUNIT_ASSERT_EQUAL(a, b);
}
//the means to access 2d-noise should result in the same value
void PerlinNoiseTest::testnoise2d()
{
	float a = perlinNoise->Noise2d(position);
	float b = perlinNoise->Noise(position[0], position[1]);
	CPPUNIT_ASSERT_EQUAL(a, b);
}
//the means to access 3d-noise should result in the same value
void PerlinNoiseTest::testnoise3d()
{
	float a = perlinNoise->Noise3d(position);
	float b = perlinNoise->Noise(position[0], position[1], position[2]);
	CPPUNIT_ASSERT(a == b);
}
