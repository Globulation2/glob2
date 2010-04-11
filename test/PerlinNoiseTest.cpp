#include "PerlinNoiseTest.h"
#include "../src/PerlinNoise.h"
CPPUNIT_TEST_SUITE_REGISTRATION( PerlinNoiseTest );

const static int SEED = 12;
PerlinNoise * perlinNoise;
float * position;

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
void PerlinNoiseTest::testReseed()
{
	float a = perlinNoise->Noise1d(position);
	perlinNoise->reseed();
	float b = perlinNoise->Noise1d(position);
	CPPUNIT_ASSERT(a != b);
}
void PerlinNoiseTest::testReseedInt()
{
	float a = perlinNoise->Noise1d(position);
	perlinNoise->reseed(SEED);
	float b = perlinNoise->Noise1d(position);
	CPPUNIT_ASSERT_EQUAL(a, b);
}
void PerlinNoiseTest::testnoise1d()
{
	float a = perlinNoise->Noise1d(position);
	float b = perlinNoise->Noise(position[0]);
	CPPUNIT_ASSERT_EQUAL(a, b);
}
void PerlinNoiseTest::testnoise2d()
{
	float a = perlinNoise->Noise2d(position);
	float b = perlinNoise->Noise(position[0], position[1]);
	CPPUNIT_ASSERT_EQUAL(a, b);
}
void PerlinNoiseTest::testnoise3d()
{
	float a = perlinNoise->Noise3d(position);
	float b = perlinNoise->Noise(position[0], position[1], position[2]);
	CPPUNIT_ASSERT(a == b);
}
