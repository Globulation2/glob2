#include "MiniCppUnit.h"
#include "../src/PerlinNoise.h"

class PerlinNoiseTest : public TestFixture<PerlinNoiseTest>
{
public:
    TEST_FIXTURE( PerlinNoiseTest )
    {
        TEST_CASE( testNotZeroOne );
        TEST_CASE( testReseed );
        TEST_CASE( testReseedInt );
        TEST_CASE( testnoise1d );
        TEST_CASE( testnoise2d );
        TEST_CASE( testnoise3d );
    }
    const static int SEED=12;
    PerlinNoise * pn;
    float * position;
    void setUp()
    {
        pn = new PerlinNoise(SEED);
        position=new float[3];
        position[0]=.11111f;
        position[1]=.21111f;
        position[2]=.71111f;
    }
    void testNotZeroOne()
    {
        float a=pn->Noise1d(position);
        ASSERT(a!=0.0f);
        ASSERT(a!=1.0f);
    }
    void testReseed()
    {
        float a=pn->Noise1d(position);
        pn->reseed();
        float b=pn->Noise1d(position);
        ASSERT(a!=b);
    }
    void testReseedInt()
    {
        float a=pn->Noise1d(position);
        pn->reseed(SEED);
        float b=pn->Noise1d(position);
        ASSERT(a==b);
    }
    void testnoise1d()
    {
        float a=pn->Noise1d(position);
        float b=pn->Noise(position[0]);
        ASSERT(a==b);
    }
    void testnoise2d()
    {
        float a=pn->Noise2d(position);
        float b=pn->Noise(position[0],position[1]);
        ASSERT(a==b);
    }
    void testnoise3d()
    {
        float a=pn->Noise3d(position);
        float b=pn->Noise(position[0],position[1],position[2]);
        ASSERT(a==b);
    }
};

REGISTER_FIXTURE( PerlinNoiseTest );
