/* @(#)s_tan.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include "cdefs-compat.h"
//__FBSDID("$FreeBSD: src/lib/msun/src/s_tan.c,v 1.13 2011/02/10 07:37:50 das Exp $");

/* glob2_math_tan(x)
 * Return tangent function of x.
 *
 * kernel function:
 *	glob2_math___kernel_tan		... tangent function on [-pi/4,pi/4]
 *	glob2_math___ieee754_rem_pio2	... argument reduction routine
 *
 * Method.
 *      Let S,C and T denote the glob2_math_sin, glob2_math_cos and glob2_math_tan respectively on
 *	[-PI/4, +PI/4]. Reduce the argument x to y1+y2 = x-k*pi/2
 *	in [-pi/4 , +pi/4], and let n = k mod 4.
 *	We have
 *
 *          n        glob2_math_sin(x)      glob2_math_cos(x)        glob2_math_tan(x)
 *     ----------------------------------------------------------
 *	    0	       S	   C		 T
 *	    1	       C	  -S		-1/T
 *	    2	      -S	  -C		 T
 *	    3	      -C	   S		-1/T
 *     ----------------------------------------------------------
 *
 * Special cases:
 *      Let trig be any of glob2_math_sin, glob2_math_cos, or glob2_math_tan.
 *      trig(+-INF)  is NaN, with signals;
 *      trig(NaN)    is that NaN;
 *
 * Accuracy:
 *	TRIG(x) returns trig(x) nearly rounded
 */

#include <float.h>
#include <openlibm_math.h>

//#define INLINE_REM_PIO2
#include "math_private.h"
//#include "e_rem_pio2.c"

OLM_DLLEXPORT double
glob2_math_tan(double x)
{
	double y[2],z=0.0;
	int32_t n, ix;

    /* High word of x. */
	GET_HIGH_WORD(ix,x);

    /* |x| ~< pi/4 */
	ix &= 0x7fffffff;
	if(ix <= 0x3fe921fb) {
	    if(ix<0x3e400000)			/* x < 2**-27 */
		if((int)x==0) return x;		/* generate inexact */
	    return glob2_math___kernel_tan(x,z,1);
	}

    /* glob2_math_tan(Inf or NaN) is NaN */
	else if (ix>=0x7ff00000) return x-x;		/* NaN */

    /* argument reduction needed */
	else {
	    n = glob2_math___ieee754_rem_pio2(x,y);
	    return glob2_math___kernel_tan(y[0],y[1],1-((n&1)<<1)); /*   1 -- n even
							-1 -- n odd */
	}
}

#if (LDBL_MANT_DIG == 53)
#endif
