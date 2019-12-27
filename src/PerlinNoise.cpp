// perlinNoise.cpp
// Class to implement coherent noise over 1, 2, or 3 dimensions.
// Implementation based on the Perlin noise function. Thanks to
// Ken Perlin of NYU for publishing his algorithm online.
// Copyright (c) 2001, Matt Zucker
// You may use this source code as you wish, but it is provided
// with no warranty. Please email me at mazucker@vassar.edu if
// you find it useful.

#include <stdio.h>
#include <time.h>
#include <math.h>
#include "PerlinNoise.h"

// TODO: change these preprocessor macros into inline functions

// S curve is (3x^2 - 2x^3) because it's quick to calculate
// though -cos(x * PI) * 0.5 + 0.5 would work too

#define easeCurve(t)( t * t * (3.0 - 2.0 * t) )
#define linearInterp(t, a, b)( a + t * (b - a) )
#define dot2(rx, ry, q)( rx * q[0] + ry * q[1] )
#define dot3(rx, ry, rz, q)     ( rx * q[0] + ry * q[1] + rz * q[2] )


#define setupValues(t, axis, g0, g1, d0, d1, pos) \
        t = pos[axis] + NOISE_LARGE_PWR2; \
        g0 = ((int)t) & NOISE_MOD_MASK; \
        g1 = (g0 + 1) & NOISE_MOD_MASK; \
        d0 = t - (int)t; \
        d1 = d0 - 1.0; \


// Konstruktor
PerlinNoise::PerlinNoise() {

}
// Destruktor
PerlinNoise::~PerlinNoise() {
}

// initialize static variables

unsigned PerlinNoise::initialized = 0;
unsigned PerlinNoise::permutationTable[ NOISE_WRAP_INDEX*2 + 2 ] = { 0 };
float PerlinNoise::gradientTable1d[ NOISE_WRAP_INDEX*2 + 2 ] = { 0 };
float PerlinNoise::gradientTable2d[ NOISE_WRAP_INDEX*2 + 2 ][ 2 ] = { { 0 } };
float PerlinNoise::gradientTable3d[ NOISE_WRAP_INDEX*2 + 2 ][ 3 ] = { { 0 } };

// return a random float in [-1,1]

inline float PerlinNoise::randNoiseFloat() {
  return ( float ) ( ( rand() % ( NOISE_WRAP_INDEX + NOISE_WRAP_INDEX ) ) -
                     NOISE_WRAP_INDEX ) / NOISE_WRAP_INDEX;
};

// convert a 2D vector into unit length

void PerlinNoise::normalize2d( float vector[ 2 ] ) {
  float length = sqrt( ( vector[ 0 ] * vector[ 0 ] ) + ( vector[ 1 ] * vector[ 1 ] ) );
  vector[ 0 ] /= length;
  vector[ 1 ] /= length;
}

// convert a 3D vector into unit length

void PerlinNoise::normalize3d( float vector[ 3 ] ) {
  float length = sqrt( ( vector[ 0 ] * vector[ 0 ] ) +
                       ( vector[ 1 ] * vector[ 1 ] ) +
                       ( vector[ 2 ] * vector[ 2 ] ) );
  vector[ 0 ] /= length;
  vector[ 1 ] /= length;
  vector[ 2 ] /= length;
}

//
// Mnemonics used in the following 3 functions:
//   L = left(-X direction)
//   R = right  (+X direction)
//   D = down   (-Y direction)
//   U = up     (+Y direction)
//   B = backwards(-Z direction)
//   F = forwards       (+Z direction)
//
// Not that it matters to the math, but a reader might want to know.
//
// noise1d - create 1-dimensional coherent noise
//   if you want to learn about how noise works, look at this
//   and then look at noise2d.

float PerlinNoise::Noise1d( float pos[ 1 ] ) {
  int gridPointL, gridPointR;
  float distFromL, distFromR, sX, t, u, v;

  if ( ! initialized ) { reseed(); }

  // find out neighboring grid points to pos and signed distances from pos to them.
  setupValues( t, 0, gridPointL, gridPointR, distFromL, distFromR, pos );

  sX = easeCurve( distFromL );

  // u, v, are the vectors from the grid pts. times the random gradients for the grid points
  // they are actually dot products, but this looks like scalar multiplication
  u = distFromL * gradientTable1d[ permutationTable[ gridPointL ] ];
  v = distFromR * gradientTable1d[ permutationTable[ gridPointR ] ];

  // return the linear interpretation between u and v (0 = u, 1 = v) at sX.
  return linearInterp( sX, u, v );
}

// create 2d coherent noise

float PerlinNoise::Noise2d( float pos[ 2 ] ) {
  int gridPointL, gridPointR, gridPointD, gridPointU;
  int indexLD, indexRD, indexLU, indexRU;
  float distFromL, distFromR, distFromD, distFromU;
  float *q, sX, sY, a, b, t, u, v;
  register int indexL, indexR;

  if ( ! initialized ) { reseed(); }

  // find out neighboring grid points to pos and signed distances from pos to them.
  setupValues( t, 0, gridPointL, gridPointR, distFromL, distFromR, pos );
  setupValues( t, 1, gridPointD, gridPointU, distFromD, distFromU, pos );

  // Generate some temporary indexes associated with the left and right grid values
  indexL = permutationTable[ gridPointL ];
  indexR = permutationTable[ gridPointR ];

  // Generate indexes in the permutation table for all 4 corners
  indexLD = permutationTable[ indexL + gridPointD ];
  indexRD = permutationTable[ indexR + gridPointD ];
  indexLU = permutationTable[ indexL + gridPointU ];
  indexRU = permutationTable[ indexR + gridPointU ];

  // Get the s curves at the proper values
  sX = easeCurve( distFromL );
  sY = easeCurve( distFromD );

  // Do the dot products for the lower left corner and lower right corners.
  // Interpolate between those dot products value sX to get a.
  q = gradientTable2d[ indexLD ]; u = dot2( distFromL, distFromD, q );
  q = gradientTable2d[ indexRD ]; v = dot2( distFromR, distFromD, q );
  a = linearInterp( sX, u, v );

  // Do the dot products for the upper left corner and upper right corners.
  // Interpolate between those dot products at value sX to get b.
  q = gradientTable2d[ indexLU ]; u = dot2( distFromL, distFromU, q );
  q = gradientTable2d[ indexRU ]; v = dot2( distFromR, distFromU, q );
  b = linearInterp( sX, u, v );

  // Interpolate between a and b at value sY to get the noise return value.
  return linearInterp( sY, a, b );
}

// you guessed it -- 3D coherent noise

float PerlinNoise::Noise3d( float pos[ 3 ] ) {
  int gridPointL, gridPointR, gridPointD, gridPointU, gridPointB, gridPointF;
  int indexLD, indexLU, indexRD, indexRU;
  float distFromL, distFromR, distFromD, distFromU, distFromB, distFromF;
  float *q, sX, sY, sZ, a, b, c, d, t, u, v;
  register int indexL, indexR;

  if ( ! initialized ) { reseed(); }

  // find out neighboring grid points to pos and signed distances from pos to them.
  setupValues( t, 0, gridPointL, gridPointR, distFromL, distFromR, pos );
  setupValues( t, 1, gridPointD, gridPointU, distFromD, distFromU, pos );
  setupValues( t, 2, gridPointB, gridPointF, distFromB, distFromF, pos );

  indexL = permutationTable[ gridPointL ];
  indexR = permutationTable[ gridPointR ];

  indexLD = permutationTable[ indexL + gridPointD ];
  indexRD = permutationTable[ indexR + gridPointD ];
  indexLU = permutationTable[ indexL + gridPointU ];
  indexRU = permutationTable[ indexR + gridPointU ];

  sX = easeCurve( distFromL );
  sY = easeCurve( distFromD );
  sZ = easeCurve( distFromB );

  q = gradientTable3d[ indexLD + gridPointB ]; u = dot3( distFromL, distFromD, distFromB, q );
  q = gradientTable3d[ indexRD + gridPointB ]; v = dot3( distFromR, distFromD, distFromB, q );
  a = linearInterp( sX, u, v );

  q = gradientTable3d[ indexLU + gridPointB ]; u = dot3( distFromL, distFromU, distFromB, q );
  q = gradientTable3d[ indexRU + gridPointB ]; v = dot3( distFromR, distFromU, distFromB, q );
  b = linearInterp( sX, u, v );

  c = linearInterp( sY, a, b );

  q = gradientTable3d[ indexLD + gridPointF ]; u = dot3( distFromL, distFromD, distFromF, q );
  q = gradientTable3d[ indexRD + gridPointF ]; v = dot3( distFromR, distFromD, distFromF, q );
  a = linearInterp( sX, u, v );

  q = gradientTable3d[ indexLU + gridPointF ]; u = dot3( distFromL, distFromU, distFromF, q );
  q = gradientTable3d[ indexRU + gridPointF ]; v = dot3( distFromR, distFromU, distFromF, q );
  b = linearInterp( sX, u, v );

  d = linearInterp( sY, a, b );

  return linearInterp( sZ, c, d );
}

// you can call noise component-wise, too.

float PerlinNoise::Noise( float x ) {
  return Noise1d( &x );
}

float PerlinNoise::Noise( float x, float y ) {
  float p[ 2 ] = { x, y };
  return Noise2d( p );
}

float PerlinNoise::Noise( float x, float y, float z ) {
  float p[ 3 ] = { x, y, z };
  return Noise3d( p );
}

// reinitialize with new, random values.

void PerlinNoise::reseed() {
  srand( ( unsigned int ) ( time( NULL ) + rand() ) );
  generateLookupTables();
}

// reinitialize using a user-specified random seed.

void PerlinNoise::reseed( unsigned int rSeed ) {
  srand( rSeed );
  generateLookupTables();
}

// initialize everything during constructor or reseed -- note
// that space was already allocated for the gradientTable
// during the constructor

void PerlinNoise::generateLookupTables() {
  unsigned i, j, temp;

  for ( i = 0; i < NOISE_WRAP_INDEX; i++ ) {
    // put index into permutationTable[index], we will shuffle later
    permutationTable[ i ] = i;

    gradientTable1d[ i ] = randNoiseFloat();

    for ( j = 0; j < 2; j++ ) { gradientTable2d[ i ][ j ] = randNoiseFloat(); }
    normalize2d( gradientTable2d[ i ] );

    for ( j = 0; j < 3; j++ ) { gradientTable3d[ i ][ j ] = randNoiseFloat(); }
    normalize3d( gradientTable3d[ i ] );
  }

  // Shuffle permutation table up to NOISE_WRAP_INDEX
  for ( i = 0; i < NOISE_WRAP_INDEX; i++ ) {
    j = rand() & NOISE_MOD_MASK;
    temp = permutationTable[ i ];
    permutationTable[ i ] = permutationTable[ j ];
    permutationTable[ j ] = temp;
  }

  // Add the rest of the table entries in, duplicating
  // indices and entries so that they can effectively be indexed
  // by unsigned chars.  I think.  Ask Perlin what this is really doing.
  //
  // This is the only part of the algorithm that I don't understand 100%.

  for ( i = 0; i < NOISE_WRAP_INDEX + 2; i++ ) {
    permutationTable[ NOISE_WRAP_INDEX + i ] = permutationTable[ i ];

    gradientTable1d[ NOISE_WRAP_INDEX + i ] = gradientTable1d[ i ];

    for ( j = 0; j < 2; j++ ) {
      gradientTable2d[ NOISE_WRAP_INDEX + i ][ j ] = gradientTable2d[ i ][ j ];
    }

    for ( j = 0; j < 3; j++ ) {
      gradientTable3d[ NOISE_WRAP_INDEX + i ][ j ] = gradientTable3d[ i ][ j ];
    }
  }

  // And we're done. Set initialized to true
  initialized = 1;
}
// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------
/*float
PerlinNoise::Turbulence( Vec3f &p, int oct, bool hard ) {

  Vec3f tp = p;
  float f_tp[ 3 ] = { tp.x, tp.y, tp.z };

  float val, amp = 1.0, sum = 0.0;
  for ( int i = 0;i < oct;i++, amp *= 0.5, tp *= 2.0 ) {
    val = Noise3d( f_tp );
    if ( hard ) val = fabs( val );
    sum += amp * val;
  }
  return 0.5 + 0.5*( sum * ( ( float ) ( 1 << oct ) / ( float ) ( ( 1 << ( oct + 1 ) ) - 1 ) ) );
}*/
