// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Private header: bodies of the master gradient templates.  Cross-TU template
// calls (Building/forbidden/guard/clear -> updateGlobalGradient) require these
// bodies to be visible at every call site.
#pragma once
#include "Map.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "MapInternal.h"
/*! Note that you can't provide any listedAddr[], or the gradient may technically end up
	wrong. Given the results of the tests, this will never happen. The easiest way to provide
	a listedAddr[] which guarantee a correct result, is to put only references to gradient
	heights that are all the same. Currently this is the case of all gradient computation but
	the AI ones (GT_UNDEFINED). For further undestanding you have to dig into the code and
	try #define check_disorderable_gradient_error_probability */
template<typename Tint> void Map::updateGlobalGradientVersionSimple(
	Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType)
{
	size_t listCountRead = 0;
	#ifdef check_disorderable_gradient_error_probability
	size_t listCountSizeMax = 0;
	#endif
	while (listCountRead < listCountWrite)
	{
		Tint deltaAddrG = listedAddr[(listCountRead++)&(size-1)];
		
		size_t y = deltaAddrG >> wDec;      // Calculate the coordinates of
		size_t x = deltaAddrG & wMask;      // the current field and of the
		
		size_t yu = ((y - 1) & hMask);      // fields next to it.
		size_t yd = ((y + 1) & hMask);      // We live on a torus! If we are on
		size_t xl = ((x - 1) & wMask);      // the "last line" of the map, the
		size_t xr = ((x + 1) & wMask);      // next line is the line 0 again.
		
		Uint8 g = gradient[(y << wDec) | x] - 1;
		if (g <= 1)        // All free non-source-fields start with gradient=1
			continue;  // There is no need to propagate gradient when g==1
		
		size_t deltaAddrC[8];
		Uint8 *addr;
		Uint8 side;
		
		deltaAddrC[0] = (yu << wDec) | xl;  // Calculate the positions of the
		deltaAddrC[1] = (yu << wDec) | x ;  // 8 fields next to us from their
		deltaAddrC[2] = (yu << wDec) | xr;  // coordinates.
		deltaAddrC[3] = (y  << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x ;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y  << wDec) | xl;
		for (int ci=0; ci<8; ci++)          // Check for each of this fields if we
		{                                   // can improve its gradient value
			addr = &gradient[deltaAddrC[ci]];
			side = *addr;
			if (side > 0 && side < g)   // side==0 means: you cannot walk on
			{                           // this field.
				                    // If we can improve this field
				*addr = g;          // we must add it as a new source
				#ifdef check_disorderable_gradient_error_probability
				size_t listCountSize = 1 + listCountWrite - listCountRead;
				if (listCountSizeMax < listCountSize)
					listCountSizeMax = listCountSize;
				#endif
				// Here we check if the queue is large enough to
				// contain this field as a new gradient source.
				if (listCountWrite + 1 + size!= listCountRead)
					listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
				else
					fprintf(stderr, "Map::updateGlobalGradientVersionSimple(): listedAddr[] overflow error");
			}
		}
	}
	#ifdef check_disorderable_gradient_error_probability
	if (listCountSizeMax < size)
		listCountSizeStats[gradientType][listCountSizeMax]++;
	else
		listCountSizeStatsOver[gradientType]++;
	#endif
	//assert(listCountWrite<=size);
}

template<typename Tint> void Map::updateGlobalGradientVersionSimon(Uint8 *gradient, Tint *listedAddr, size_t listCountWrite)
{
/* This algorithm uses the fact that all fields which are adjacent to the field
   directly below the current one, are also adjacent to either the field to its left
   or right.  Thus this field only needs to become a source if its left or right
   is not accessable. The same with the other 3 directions.
      |       |
      |       |
------+-------+------
      |current|
      |field  |
------+-------+------
   L  | below |   R
      |       |
------+-------+------
 next |next to| next
 to L | both  | to R
*/

#if defined(LOG_SIMON_GRADIENT)
	size_t spared=0;
	size_t listCountWriteStart=listCountWrite;
#endif


	size_t listCountRead = 0;
	while (listCountRead < listCountWrite)
	{
		Tint deltaAddrG = listedAddr[(listCountRead++)&(size-1)];
		
		size_t y = deltaAddrG >> wDec;
		size_t x = deltaAddrG & wMask;
		
		size_t yu = ((y - 1) & hMask);
		size_t yd = ((y + 1) & hMask);
		size_t xl = ((x - 1) & wMask);
		size_t xr = ((x + 1) & wMask);
		
		Uint8 g = gradient[(y << wDec) | x] - 1;
		if (g <= 1)
			continue;
		
		Uint32 flag = 0;
		Uint8 *addr;
		Uint8 side;
		{ // In this scope we care only about the diagonal neighbours.
                /* We will use flags to mark if at least one of the 2 fields
                   next to a adjacent nondiagonal field is not accessable.
                   Binary representation:
                    9 = 1001
                    3 = 0011
                    6 = 0110
                   12 = 1100
                           1 is the upper right
                          1  is the lower right
                         1   is the lower left
                        1    is the upper left
	        */
			const Uint32 diagFlags[4] = {9, 3, 6, 12};
			size_t deltaAddrC[4];
			
			deltaAddrC[0] = (yu << wDec) | xl; // Calculate the position
			deltaAddrC[1] = (yu << wDec) | xr; // of the 4 diagonal fields
			deltaAddrC[2] = (yd << wDec) | xr;
			deltaAddrC[3] = (yd << wDec) | xl;
                        //  0|_|1
                        //  _|*|_     * represents the current field
                        //  3| |2
			for (size_t ci = 0; ci < 4; ci++)  // Check them
			{
				addr = &gradient[deltaAddrC[ci]];
				side = *addr;
				if (side > 0 && side < g)
				{
					*addr = g;
					listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
				}
				else if (side == 0)            // If field is inaccessable,
					flag |= diagFlags[ci]; // mark the corresponding bit
			}
		}
		{ // Now we take a look at our nondiagonal neighbours
			size_t deltaAddrC[4];
			
			deltaAddrC[0] = (yu << wDec) | x ;   // _|0|_
			deltaAddrC[1] = (y  << wDec) | xr;   // 3|*|1
			deltaAddrC[2] = (yd << wDec) | x ;   //  |2| 
			deltaAddrC[3] = (y  << wDec) | xl;


			for (size_t ci = 0; ci < 4; ci++)
			{
				addr = &gradient[deltaAddrC[ci]];
				side = *addr;
				if (side > 0 && side < g)
				{
					*addr = g;
                                // Only mark this as a new source, 
                                // if its left or right was inaccessable.
					if (flag & 1) // Information is in the first bit
						listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
#if defined(LOG_SIMON_GRADIENT)
					else
						spared++;
#endif
				}
				flag >>= 1;  // Shift the next bit into position
			}
		}
	}
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon,"listed: %4d inserted: %4d spared: %3d\n",listCountWrite, listCountWrite-listCountWriteStart,spared);
#endif
	//assert(listCountWrite<=size);
}

template<typename Tint> void Map::updateGlobalGradient(
	Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType, bool canSwim)
{
	#define USE_DYNAMICAL_GRADIENT_VERSION_SR

#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon, "gradientType: %d\n", gradientType);
	fprintf(logSimon, "canSwim: %d\n", canSwim);
#endif

	#if defined(USE_GRADIENT_VERSION_SIMON)
		updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);

	#elif defined(USE_GRADIENT_VERSION_SIMPLE)
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);

	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION_SR)
		if (gradientType == GT_RESOURCE)
			updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
		else
			updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);

	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION)
		// use the fastest gradient computation for each GradientType:
		switch (gradientType)
		{
			case GT_UNDEFINED:
				updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
				// speed 105.09% compare to simple on test
			break;

			case GT_RESOURCE:
				updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
				//speed 104.76% compare to simple on test
			break;

			case GT_BUILDING:
				updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
			break;

			case GT_FORBIDDEN:
				updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
			break;

			case GT_GUARD_AREA:
				updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
				// fastest one here
			break;

			case GT_CLEAR_AREA:
				updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
				// fastest one here
			break;

			default:
				assert(false);
				abort();
			break;
		}

	#else
		#error Please select a gradient version
	#endif
}
