
/* @(#)e_cosh.c 1.3 95/01/18 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

#include "cdefs-compat.h"
//__FBSDID("$FreeBSD: src/lib/msun/src/e_cosh.c,v 1.10 2011/10/21 06:28:47 das Exp $");

/* glob2_math___ieee754_cosh(x)
 * Method : 
 * mathematically glob2_math_cosh(x) if defined to be (glob2_math_exp(x)+glob2_math_exp(-x))/2
 *	1. Replace x by |x| (glob2_math_cosh(x) = glob2_math_cosh(-x)). 
 *	2. 
 *		                                        [ glob2_math_exp(x) - 1 ]^2 
 *	    0        <= x <= ln2/2  :  glob2_math_cosh(x) := 1 + -------------------
 *			       			           2*glob2_math_exp(x)
 *
 *		                                  glob2_math_exp(x) +  1/glob2_math_exp(x)
 *	    ln2/2    <= x <= 22     :  glob2_math_cosh(x) := -------------------
 *			       			          2
 *	    22       <= x <= lnovft :  glob2_math_cosh(x) := glob2_math_exp(x)/2 
 *	    lnovft   <= x <= ln2ovft:  glob2_math_cosh(x) := glob2_math_exp(x/2)/2 * glob2_math_exp(x/2)
 *	    ln2ovft  <  x	    :  glob2_math_cosh(x) := huge*huge (overflow)
 *
 * Special cases:
 *	glob2_math_cosh(x) is |x| if x is +INF, -INF, or NaN.
 *	only glob2_math_cosh(0)=1 is exact for finite x.
 */

#include <float.h>
#include <openlibm_math.h>

#include "math_private.h"

static const double one = 1.0, half=0.5, huge = 1.0e300;

OLM_DLLEXPORT double
glob2_math___ieee754_cosh(double x)
{
	double t,w;
	int32_t ix;

    /* High word of |x|. */
	GET_HIGH_WORD(ix,x);
	ix &= 0x7fffffff;

    /* x is INF or NaN */
	if(ix>=0x7ff00000) return x*x;	

    /* |x| in [0,0.5*ln2], return 1+glob2_math_expm1(|x|)^2/(2*glob2_math_exp(|x|)) */
	if(ix<0x3fd62e43) {
	    t = glob2_math_expm1(glob2_math_fabs(x));
	    w = one+t;
	    if (ix<0x3c800000) return w;	/* glob2_math_cosh(tiny) = 1 */
	    return one+(t*t)/(w+w);
	}

    /* |x| in [0.5*ln2,22], return (glob2_math_exp(|x|)+1/glob2_math_exp(|x|)/2; */
	if (ix < 0x40360000) {
		t = glob2_math___ieee754_exp(glob2_math_fabs(x));
		return half*t+half/t;
	}

    /* |x| in [22, glob2_math_log(maxdouble)] return half*glob2_math_exp(|x|) */
	if (ix < 0x40862E42)  return half*glob2_math___ieee754_exp(glob2_math_fabs(x));

    /* |x| in [glob2_math_log(maxdouble), overflowthresold] */
	if (ix<=0x408633CE)
	    return glob2_math___ldexp_exp(glob2_math_fabs(x), -1);

    /* |x| > overflowthresold, glob2_math_cosh(x) overflow */
	return huge*huge;
}

#if (LDBL_MANT_DIG == 53)
#endif
