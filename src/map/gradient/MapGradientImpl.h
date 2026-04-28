/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
// Private header: bodies of the master gradient templates.  Cross-TU template
// calls (Building/forbidden/guard/clear -> updateGlobalGradient) require these
// bodies to be visible at every call site.
#pragma once
#include "Map.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "MapInternal.h"
#if defined( LOG_GRADIENT_LINE_GRADIENT )
#include <map>
#endif
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

template<typename Tint> void Map::updateGlobalGradientVersionKai(Uint8 *gradient, Tint *listedAddr, size_t listCountWrite)
{
	// This version tries to go through the memory in consecutive order
	// in the hope that the cache usage will be improved.
	// Instead of picking one individual field and test its neighbours,
	// we test if the field to its right is the next field we must process.
	// If it is, we test the field to right of this field and so on.
	// Otherwise we stop.  We also stop if the gradient value of the field to
	// the right differs from that of the current field or if we have reached
	// the end of the line.  (We don't have to, but we do.)
	// After that we have a horizontal line segment.
	// Now we check if we can improve the line segment above it, and below it.
	// And the fields on the left and right.

	size_t sizeMask = size-1;  // Mask needed to use listedAddr as queue.
	size_t listCountRead = 0;  // Index of first untreated field in listedAddr.

#if defined(LOG_GRADIENT_LINE_GRADIENT)
	std::map<size_t,int> dcount;
#endif
#if defined(LOG_SIMON_GRADIENT)
	size_t spared=0;
	size_t listCountWriteStart=listCountWrite;
#endif
	
	while (listCountRead < listCountWrite)  // While listedAddr not empty.
	{
		Tint deltaAddrG = listedAddr[listCountRead&sizeMask];
		
		size_t y = deltaAddrG >> wDec;
		size_t x = deltaAddrG & wMask;
		
		size_t yu = ((y - 1) & hMask);
		size_t yd = ((y + 1) & hMask);

		
		Uint8 myg = gradient[deltaAddrG]; // Get the gradient of the current field
		Uint8 g = myg-1;   // g will be the gradient of the children.
		if (g <= 1)        // All free non-source-fields start with gradient=1
		{
			listCountRead++;
			continue;  // There is no need to propagate gradient when g==1
		}
		

		Uint8 *addr;       // Pointer to a field.
		Uint8 side;        // Gradient value of a field.
		size_t pos;        // pos stores the combined (x,y) coordinate.		


                // Get the length of the segment.
		size_t d;                     // Length of the line segment.
		size_t ylineDec = y << wDec;  // Line the field is in.
		// remember: && and || only compute second argument if they have to.
		for (d=1; (++listCountRead < listCountWrite); d++) // While not empty.
		{
			pos = listedAddr[listCountRead&sizeMask]; // Next untreated field.
			// We can tollerate gaps of length 1.
			// Break if this field has not the same g as I, or is not the one
			// to my right or the one behind this.

			if (gradient[pos] != myg)   // Need same g for all fields in line.
				break;
			if (pos == (ylineDec | ( (d + x) & wMask ) ) )
				continue;    // If the next field is beside to the right.
#define ALLOW_SMALL_GAPS
#if defined( ALLOW_SMALL_GAPS )
			if (pos == (ylineDec | ( (d + 1 + x) & wMask ) ) )
			{       // If it is behind it.  We overleap one field.
				addr = &gradient[(ylineDec | ( (x+d++) & wMask ) )];
				side = *addr;
				if ( side>0 && side<g )     // Check if we can improve,
					*addr = g;          // the field we overleap.
				continue;     // Line grew 2 fields longer this time.
			}
#endif
				break;
		}
		// (x+d-1)&wMask is the last element of the line segment.
		// d is the size of the segment. listCountRead is in correct position.

#if defined( LOG_GRADIENT_LINE_GRADIENT )
		++dcount[d];
#endif



		bool leftflag=false;   // True if we might need to put the field left
		bool rightflag=false;  // resp. right of the segment to listedAddr.

                // Handle the upper line first then the lower line.
		ylineDec = yu << wDec;   
		for (int upperOrLower=0;upperOrLower<=1;upperOrLower++)
		{
			// The left of the first field is special,
			// since we have to test its left.
			pos  = ylineDec | ( (x-1) & wMask );
			addr = &gradient[pos];
			side = *addr;
			if ( side>0 && side<g )     // Check if we can improve.
			{
				*addr = g;
				listedAddr[(listCountWrite++)&sizeMask] = pos;				
			} else if (side == 0)       // See Simons version.
				leftflag=true;

			// Handle the whole segment:
			for (size_t i=0; i<d; i++)
			{
				pos  = ylineDec | ((x+i) & wMask);
				addr = &gradient[pos];
				side = *addr;
				if ( side>0 && side<g )
				{
					*addr = g;
					listedAddr[(listCountWrite++)&sizeMask] = pos;
				}
			}

			// The right of the last field is special,
			// since we have to test its right.
			pos = ylineDec | ( (x+d) & wMask );
			addr = &gradient[pos];
			side = *addr;
			if ( side>0 && side<g )
			{
				*addr = g;
				listedAddr[(listCountWrite++)&sizeMask] = pos;				
			} else if (side == 0)
				rightflag=true;

			ylineDec = yd << wDec;  // Change attention to the lower line.
		}
                // The segment is processed.		
		// Now handle leftmost and rightmost field.
		pos = (y << wDec) | ( (x-1) & wMask );
		addr = &gradient[pos];
		side = *addr;
		if ( side>0 && side<g )
		{
			*addr = g;
			if (leftflag)   // See Simons version.
				listedAddr[(listCountWrite++)&sizeMask] = pos;
#if defined(LOG_SIMON_GRADIENT)
			else
				spared++;
#endif

		}
		pos = (y << wDec) | ( (x+d) & wMask );
		addr = &gradient[pos];
		side = *addr;
		if ( side>0 && side<g )
		{
			*addr = g;
			if (rightflag)
				listedAddr[(listCountWrite++)&sizeMask] = pos;
#if defined(LOG_SIMON_GRADIENT)
			else
				spared++;
#endif

		}
	}
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon,"listed: %4d inserted: %4d spared: %3d\n",listCountWrite, listCountWrite-listCountWriteStart,spared);
#endif

#if defined( LOG_GRADIENT_LINE_GRADIENT )
	FILE *dlog = globalContainer->logFileManager->getFile("GradientLineLength.log");
	for (std::map<size_t,int>::iterator it=dcount.begin();it!=dcount.end();it++)
		fprintf(dlog,"line length: %3d count: %4d\n",it->first,it->second);
#endif
}

template<typename Tint> void Map::updateGlobalGradient(
	Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType, bool canSwim)
{
	#define USE_DYNAMICAL_GRADIENT_VERSION_SR

#if defined(LOG_GRADIENT_LINE_GRADIENT)
	FILE *dlog = globalContainer->logFileManager->getFile("GradientLineLength.log");
	fprintf(dlog, "gradientType: %d\n", gradientType);
	fprintf(dlog, "canSwim: %d\n", canSwim);
#endif
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon, "gradientType: %d\n", gradientType);
	fprintf(logSimon, "canSwim: %d\n", canSwim);
#endif
	
	#if defined( USE_GRADIENT_VERSION_TEST_KAI)
	if (gradientType == GT_UNDEFINED)
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
	else
	{
		Tint *testListedAddr = new Tint[size];
		Uint8 *testGradient = new Uint8[size];
		memcpy (testListedAddr, listedAddr, size);
		memcpy (testGradient, gradient, size);
		updateGlobalGradientVersionKai<Tint>(testGradient, testListedAddr, listCountWrite);
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		assert (memcmp (testGradient, gradient, size) == 0);
	}
	
	#elif defined(USE_GRADIENT_VERSION_KAI)
		updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
		
	#elif defined(USE_GRADIENT_VERSION_SIMON)
		updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
		
	#elif defined(USE_GRADIENT_VERSION_SIMPLE)
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		
	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION_SR)
		if (gradientType == GT_RESOURCE)
			updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
		else
			updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		
	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION_KR)
		if (gradientType == GT_RESOURCE)
			updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
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
				updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
				// speed 100.29% compare to simple on test
			break;
			
			case GT_FORBIDDEN:
				updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
				// speed 100.18% compare to simple on test
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
