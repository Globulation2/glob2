/* @(#)s_asinh.c 5.1 93/09/24 */
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
//__FBSDID("$FreeBSD: src/lib/msun/src/s_asinh.c,v 1.9 2008/02/22 02:30:35 das Exp $");

/* glob2_math_asinh(x)
 * Method :
 *	Based on
 *		glob2_math_asinh(x) = sign(x) * glob2_math_log [ |x| + glob2_math_sqrt(x*x+1) ]
 *	we have
 *	glob2_math_asinh(x) := x  if  1+x*x=1,
 *		 := sign(x)*(glob2_math_log(x)+ln2)) for large |x|, else
 *		 := sign(x)*glob2_math_log(2|x|+1/(|x|+glob2_math_sqrt(x*x+1))) if|x|>2, else
 *		 := sign(x)*glob2_math_log1p(|x| + x^2/(1 + glob2_math_sqrt(1+x^2)))
 */

#include <float.h>
#include <openlibm_math.h>

#include "math_private.h"

static const double
one =  1.00000000000000000000e+00, /* 0x3FF00000, 0x00000000 */
ln2 =  6.93147180559945286227e-01, /* 0x3FE62E42, 0xFEFA39EF */
huge=  1.00000000000000000000e+300;

OLM_DLLEXPORT double
glob2_math_asinh(double x)
{
	double t,w;
	int32_t hx,ix;
	GET_HIGH_WORD(hx,x);
	ix = hx&0x7fffffff;
	if(ix>=0x7ff00000) return x+x;	/* x is inf or NaN */
	if(ix< 0x3e300000) {	/* |x|<2**-28 */
	    if(huge+x>one) return x;	/* return x inexact except 0 */
	}
	if(ix>0x41b00000) {	/* |x| > 2**28 */
	    w = glob2_math___ieee754_log(glob2_math_fabs(x))+ln2;
	} else if (ix>0x40000000) {	/* 2**28 > |x| > 2.0 */
	    t = glob2_math_fabs(x);
	    w = glob2_math___ieee754_log(2.0*t+one/(glob2_math___ieee754_sqrt(x*x+one)+t));
	} else {		/* 2.0 > |x| > 2**-28 */
	    t = x*x;
	    w =glob2_math_log1p(glob2_math_fabs(x)+t/(one+glob2_math___ieee754_sqrt(one+t)));
	}
	if(hx>0) return w; else return -w;
}

#if (LDBL_MANT_DIG == 53)
#endif
