#ifndef __PERLINNOISE_H__
#define __PERLINNOISE_H__

#include <stdlib.h>
//#include "Vector.h"

// It must be true that (x % NOISE_WRAP_INDEX) == (x & NOISE_MOD_MASK)
// so NOISE_WRAP_INDEX must be a power of two, and NOISE_MOD_MASK must be
// that power of 2 - 1.  as indices are implemented, as unsigned chars,
// NOISE_WRAP_INDEX shoud be less than or equal to 256.
// There's no good reason to change it from 256, really.

#define NOISE_WRAP_INDEX  256   
#define NOISE_MOD_MASK  255     


#define NOISE_LARGE_PWR2 4096

class PerlinNoise {
private:
  static unsigned initialized;  

  static unsigned permutationTable[ NOISE_WRAP_INDEX*2 + 2 ]; 
  static float gradientTable1d[ NOISE_WRAP_INDEX*2 + 2 ]; 
  static float gradientTable2d[ NOISE_WRAP_INDEX*2 + 2 ][ 2 ]; 
  static float gradientTable3d[ NOISE_WRAP_INDEX*2 + 2 ][ 3 ]; 

  static float randNoiseFloat(); 
  static void normalize2d( float vector[ 2 ] ); 
  static void normalize3d( float vector[ 3 ] ); 
  static void generateLookupTables(); 

public:

  PerlinNoise();
  PerlinNoise( unsigned int rSeed ) { reseed(rSeed); }
  ~PerlinNoise();

  static void reseed(); 
  static void reseed( unsigned int rSeed ); 

  float Noise1d( float pos[ 1 ] ); 
  float Noise2d( float pos[ 2 ] ); 
  float Noise3d( float pos[ 3 ] ); 

  float Noise( float );             
  float Noise( float, float );        
  float Noise( float, float, float );   

//  float Turbulence( Vec3f &p, int oct, bool hard = false );

};

#endif  // __PERLINNOISE_H__
