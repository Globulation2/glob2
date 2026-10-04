// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

#include "PerlinNoiseTest.h"
#include "../src/map/generator/shared/Noise.h"
TEST_SUITE("PerlinNoise")
{
	TEST_CASE_FIXTURE(PerlinNoiseTest, "Constructor") { testConstructor(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "NotZeroOne") { testNotZeroOne(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "Reseed") { testReseed(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "ReseedIntDifferent") { testReseedIntDifferent(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "ReseedIntSame") { testReseedIntSame(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "noise1d") { testnoise1d(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "noise2d") { testnoise2d(); }
	TEST_CASE_FIXTURE(PerlinNoiseTest, "noise3d") { testnoise3d(); }
}

const static int SEED = 12;
GenerationNoise * perlinNoise;
float * position;

//initialize perlinNoise with always the same seed and setUp a positon-vector.
PerlinNoiseTest::PerlinNoiseTest()
{
	perlinNoise = new GenerationNoise(SEED);
	position = new float[3];
	position[0] = .11111f;
	position[1] = .21111f;
	position[2] = .71111f;
}
PerlinNoiseTest::~PerlinNoiseTest()
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
	CHECK(a != 0.0f);
	CHECK(a != 1.0f);
}
//via resetting the seed, noise should generate different values
void PerlinNoiseTest::testReseed()
{
	float a = perlinNoise->Noise1d(position);
	perlinNoise->reseed(SEED + 1);
	float b = perlinNoise->Noise1d(position);
	CHECK(a != b);
}
//via resetting the seed to different values, noise should generate different values
void PerlinNoiseTest::testReseedIntDifferent()
{
	float valueWithOriginalSeed = perlinNoise->Noise1d(position);
	perlinNoise->reseed(SEED + 3);
	float valueWithSomeOtherSeed = perlinNoise->Noise1d(position);
	CHECK(valueWithOriginalSeed != valueWithSomeOtherSeed);
}
//via resetting the seed to what it was, noise should regenerate same values
void PerlinNoiseTest::testReseedIntSame()
{
	float valueWithOriginalSeed = perlinNoise->Noise1d(position);
	perlinNoise->reseed(SEED);
	float valueWithReseededOriginalSeed = perlinNoise->Noise1d(position);
	CHECK_EQ(valueWithOriginalSeed, valueWithReseededOriginalSeed);
}
//the means to access 1d-noise should result in the same value
void PerlinNoiseTest::testnoise1d()
{
	float a = perlinNoise->Noise1d(position);
	float b = perlinNoise->Noise(position[0]);
	CHECK_EQ(a, b);
}
//the means to access 2d-noise should result in the same value
void PerlinNoiseTest::testnoise2d()
{
	float a = perlinNoise->Noise2d(position);
	float b = perlinNoise->Noise(position[0], position[1]);
	CHECK_EQ(a, b);
}
//the means to access 3d-noise should result in the same value
void PerlinNoiseTest::testnoise3d()
{
	float a = perlinNoise->Noise3d(position);
	float b = perlinNoise->Noise(position[0], position[1], position[2]);
	CHECK(a == b);
}
