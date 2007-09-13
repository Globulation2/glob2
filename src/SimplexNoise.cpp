/* This is based on Perlin's improved (speed-wise, among other things)
 * ``Simplex'' algorithm for generating procedural noise.
 *
 * The algorithm is described in detail here:
 * http://staffwww.itn.liu.se/~stegu/simplexnoise/simplexnoise.pdf
 *
 * Author: Erik Søe Sørensen
 */

#define PERMUTATION_SIZE 256

namespace SimplexNoise {
	static const unsigned char perm[PERMUTATION_SIZE] = {
		99,147,142,89,96,166,18,157,66,229,25,161,139,10,236,101,
		195,232,32,128,165,255,8,126,239,163,112,180,234,85,191,189,
		214,7,122,48,111,202,199,196,47,219,9,84,127,21,50,17,
		186,97,225,123,194,170,223,107,38,28,230,154,177,45,27,51,
		151,164,133,61,222,173,149,39,156,42,200,49,141,224,117,108,
		159,118,1,150,71,60,208,35,78,52,210,217,197,0,231,94,
		31,148,30,119,82,175,201,6,138,36,20,220,22,242,233,16,
		247,250,168,131,244,241,14,162,121,87,221,57,206,120,238,92,
		41,93,77,26,73,100,169,145,80,63,205,3,227,179,209,103,
		67,37,56,167,104,55,116,198,98,171,15,146,34,207,213,129,
		245,155,23,132,228,113,44,4,19,115,72,90,102,187,134,105,
		181,64,124,91,212,65,160,243,183,204,211,59,193,53,24,136,
		83,158,29,33,74,62,252,144,185,86,79,248,226,216,237,95,
		140,215,12,184,188,249,40,240,172,43,253,246,153,75,5,46,
		13,251,70,109,190,114,106,192,58,182,135,54,152,69,178,203,
		130,81,76,174,254,235,137,143,218,2,176,68,125,11,110,88
	};
	/*
	static const int gradients3D[16][3] =
	{{1,1,0}, {1,-1,0}, {-1,1,0}, {-1,-1,0},
	 {1,0,1}, {1,0,-1}, {-1,0,1}, {-1,0,-1},
	 {0,1,1}, {0,1,-1}, {0,-1,1}, {0,-1,-1},
	 {1,1,0}, {-1,1,0}, {0,-1,1}, {0,-1,-1} // 4th line is an extra tetrahedron
	};
	*/

	static const int F3D = (int)(256 * 1.0/3.0); // 1/3 in 256ths
	static const int G3D = (int)(256 * 1.0/6.0); // 1/6 in 256ths

	typedef unsigned char byte;
#define FLOOR_MASK (~255)
#define FRAC_MASK (255)
#define INT_ROUND_DIV(x,divisor)    ( ((x)+((divisor)/2)    ) / (divisor) )
#define INT_ROUND_RSHIFT(x,places)  ( ((x)+(1<<((places)-1))) >> (places) )


	int contribution(int gx, int gy, int gz, int relX, int relY, int relZ);
	int dotprod(int hash, int x, int y, int z);
	int hashGridPoint(int gx, int gy, int gz);


	/** Returns the noise value for a given point.
	 *  @param x,y,z  The coordinates - in 256ths.
	 *  @return A noise value in the interval [0;255] 
	 */
	int getNoise3D(int xin, int yin, int zin) {
		// 1. Find rough grid area:
		int skew = INT_ROUND_DIV(xin+yin+zin, 3);
		int gridX = (xin+skew) & FLOOR_MASK,
		    gridY = (yin+skew) & FLOOR_MASK,
		    gridZ = (zin+skew) & FLOOR_MASK;
		int igridX = gridX>>8;
		int igridY = gridY>>8;
		int igridZ = gridZ>>8;

		// 2. Find coordinates wrt. corner 0:
		int unskew = INT_ROUND_DIV(gridX+gridY+gridZ, 6);
		int x0 = gridX-unskew,
		    y0 = gridY-unskew,
		    z0 = gridZ-unskew;
		int relX = xin-x0, relY = yin-y0, relZ = zin-z0;
		
		// 3. Find exact grid area:
		int offsetX1, offsetY1;
		int offsetX2, offsetY2;
		/* Z offsets are calculated later, from the X and Y offsets, using
		 * the formulae offsetX1 + offsetY1 + offsetZ1 = 1
		 * and          offsetX2 + offsetY2 + offsetZ2 = 2.
		 * Z offsets are not declared as variables; this seems
		 * to relieve the register pressure and give a small speed-up.
		 */
		// Find order of coordinates within rough grid area:
		if (relX >= relY) {
			offsetX2 = 1; offsetY1 = 0; // X is not last and Y not first
			if (relY >= relZ) { // Ordering: X > Y > Z
				offsetX1 = 1; /*offsetY1 = 0;*/ //offsetZ1 = 0;
				/*offsetX2 = 1;*/ offsetY2 = 1; //offsetZ2 = 0;
			} else if (relX >= relZ) { // Ordering: X > Z > Y
				offsetX1 = 1; /*offsetY1 = 0;*/ //offsetZ1 = 0;
				/*offsetX2 = 1;*/ offsetY2 = 0; //offsetZ2 = 1;
			} else { // Ordering: Z > X > Y
				offsetX1 = 0; /*offsetY1 = 0;*/ //offsetZ1 = 1;
				/*offsetX2 = 1;*/ offsetY2 = 0; //offsetZ2 = 1;
			}
		} else { // Y > X
			offsetX1 = 0; offsetY2 = 1; // X is not first and Y not last
			if (relX >= relZ) { // Ordering: Y > X > Z
				/*offsetX1 = 0;*/ offsetY1 = 1; //offsetZ1 = 0;
				offsetX2 = 1; /*offsetY2 = 1;*/ //offsetZ2 = 0;
			} else if (relY >= relZ) { // Ordering: Y > Z > X
				/*offsetX1 = 0;*/ offsetY1 = 1; //offsetZ1 = 0;
				offsetX2 = 0; /*offsetY2 = 1;*/ //offsetZ2 = 1;
			} else { // Ordering: Z > Y > X
				/*offsetX1 = 0;*/ offsetY1 = 0; //offsetZ1 = 1;
				offsetX2 = 0; /*offsetY2 = 1;*/ //offsetZ2 = 1;
			}
		}
		
		// 4. Find coordinates wrt. the corners:
		int relX1 = relX - 256*offsetX1 + INT_ROUND_DIV(256,6);
		int relY1 = relY - 256*offsetY1 + INT_ROUND_DIV(256,6);
		int relZ1 = relZ - 256*(1-offsetX1-offsetY1) + INT_ROUND_DIV(256,6);
		int relX2 = relX - 256*offsetX2 + INT_ROUND_DIV(256,3);
		int relY2 = relY - 256*offsetY2 + INT_ROUND_DIV(256,3);
		int relZ2 = relZ - 256*(2-offsetX2-offsetY2) + INT_ROUND_DIV(256,3);
		int relX3 = relX - 256      + INT_ROUND_DIV(256,2);
		int relY3 = relY - 256      + INT_ROUND_DIV(256,2);
		int relZ3 = relZ - 256      + INT_ROUND_DIV(256,2);

		// 5. Compute noise value:
		int value = ( contribution(igridX, igridY, igridZ, relX, relY, relZ) +
			      contribution(igridX+offsetX1, igridY+offsetY1, igridZ+(1-offsetX1-offsetY1), relX1, relY1, relZ1) +
			      contribution(igridX+offsetX2, igridY+offsetY2, igridZ+(2-offsetX2-offsetY2), relX2, relY2, relZ2) +
			      contribution(igridX+1, igridY+1, igridZ+1, relX3, relY3, relZ3));

		return INT_ROUND_RSHIFT(value,12) + 128;
	}




	/** Returns the contribution from a grid point, as .24 fixed-point (ie. 2^16 x the .8 FiPo value). */
	int contribution(int gx, int gy, int gz, int relX, int relY, int relZ) {
		// relXYZ are .8 fixed-point.
		int hash = hashGridPoint(gx, gy, gz);
		int weight = INT_ROUND_DIV(256*256*3,5)- relX*relX - relY*relY - relZ*relZ;
		// weight is .16 fixed-point.
		if (weight<=0) return 0;
		int w2 = INT_ROUND_RSHIFT(weight*weight,16);
		// w2 is .16 fixed-point.
		int w4 = INT_ROUND_RSHIFT(w2*w2,16);
		// w4 is .16 fixed-point.
		int res = w4 * dotprod(hash, relX,relY,relZ);
		// res is .24 fixed-point.
		return res;
	}

	/** Dot product with gradients3D[hash&15] -
	 *  Because of the nature of gradients3D, no multiplications are needed - only additions and subtractions.
	 */
	int dotprod(int hash, int x, int y, int z) {
		/*
	static const int gradients3D[16][3] =
	{{1,1,0}, {1,-1,0}, {-1,1,0}, {-1,-1,0},
	 {1,0,1}, {1,0,-1}, {-1,0,1}, {-1,0,-1},
	 {0,1,1}, {0,1,-1}, {0,-1,1}, {0,-1,-1},
	 {1,1,0}, {-1,1,0}, {0,-1,1}, {0,-1,-1} // 4th line is an extra tetrahedron
	};
		*/
		switch (hash&15) {
		case 0: return  x + y;
		case 1: return  x - y;
		case 2: return  y-x;// + y;
		case 3: return -x - y;

		case 4: return  x + z;
		case 5: return  x - z;
		case 6: return z-x;// + z;
		case 7: return -x - z;

		case 8: return  y + z;
		case 9: return  y - z;
		case 10:return z-y;// + z;
		case 11:return -y - z;

		case 12: return  x + y;
		case 13: return y-x;// + y;
		case 14: return  z + y;
		case 15: return y-z;// - y;
		}//switch
	}

	int hashGridPoint(int gx, int gy, int gz) {
		return perm[(byte)(gz + perm[(byte)(gy + perm[(byte)gx])])];
	}
}
