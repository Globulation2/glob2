#ifndef __SIMPLEXNOISE_H__
#define __SIMPLEXNOISE_H__

namespace SimplexNoise {
	/** Returns the noise value for a given point.
	 *  @param x,y,z  The coordinates - in 256ths.
	 *  @return A noise value in the interval [0;255]
	 */
	int getNoise3D(int x, int y, int z);
}


#endif
