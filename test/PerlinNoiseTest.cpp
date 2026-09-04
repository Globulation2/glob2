// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

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
	//The constructor is tested implicitly by being used in setUp
	//Actually it has no state that could be tested.
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
