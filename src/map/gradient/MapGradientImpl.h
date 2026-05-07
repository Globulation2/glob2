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

// LogFileManager.h does `#define fprintf if(false)fprintf` to silence stale
// log code. We need real fprintf for the dual-run divergence dump, so undo
// the macro here. (Local restore lasts until any subsequent re-include of
// LogFileManager.h, which doesn't happen in this header.)
#ifdef fprintf
#undef fprintf
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
					gradientOverflowCount[gradientType]++;
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
					// Instrumentation only: Simon has no overflow guard, so a
					// full queue silently overwrites unread entries. Count the
					// occurrence but preserve the existing write so Simon's
					// behavior is unchanged for Phase 0 measurement.
					if (listCountWrite + 1 + size == listCountRead)
						simonGradientOverflowCount++;
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
					{
						if (listCountWrite + 1 + size == listCountRead)
							simonGradientOverflowCount++;
						listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
					}
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

// Chamfer distance transform with orthogonal=1, diagonal=1 weights — produces
// the same byte output as the BFS version on convergence. Two sweep directions
// per pass (forward then backward), repeated until a full pass writes nothing.
//
// Saturation rules mirror BFS exactly:
//   - cells with value 0 are obstacles and never written
//   - sources keep their seed value (a propagation candidate cand = vn - 1
//     is always lower than vn, never raises a higher cell)
//   - propagation floor is 2: a neighbor whose value is < 3 cannot lift this
//     cell (cand = vn - 1 must be >= 2 to exceed any free cell's seed of 1).
//     This matches BFS's `if (g - 1 <= 1) continue` early-out.
//
// `gradientType` is currently unused but kept for symmetry with the BFS path.
template<typename Tint> void Map::updateGlobalGradientVersionChamfer(Uint8 *gradient, GradientType /*gradientType*/)
{
	int passes = 0;
	bool changed;
	do
	{
		changed = false;

		// Forward sweep: in-set neighbors are NW, N, NE, W (already visited).
		for (size_t y = 0; y < (size_t)h; y++)
		{
			size_t yu = ((y - 1) & hMask);
			for (size_t x = 0; x < (size_t)w; x++)
			{
				Uint8 g = gradient[(y << wDec) | x];
				if (g == 0)
					continue;
				size_t xl = ((x - 1) & wMask);
				size_t xr = ((x + 1) & wMask);
				Uint8 best = g;
				Uint8 vNW = gradient[(yu << wDec) | xl];
				Uint8 vN  = gradient[(yu << wDec) | x ];
				Uint8 vNE = gradient[(yu << wDec) | xr];
				Uint8 vW  = gradient[(y  << wDec) | xl];
				if (vNW >= 3 && (Uint8)(vNW - 1) > best) best = vNW - 1;
				if (vN  >= 3 && (Uint8)(vN  - 1) > best) best = vN  - 1;
				if (vNE >= 3 && (Uint8)(vNE - 1) > best) best = vNE - 1;
				if (vW  >= 3 && (Uint8)(vW  - 1) > best) best = vW  - 1;
				if (best != g)
				{
					gradient[(y << wDec) | x] = best;
					changed = true;
				}
			}
		}

		// Backward sweep: in-set neighbors are SE, S, SW, E (already visited).
		for (size_t y = (size_t)h; y-- > 0; )
		{
			size_t yd = ((y + 1) & hMask);
			for (size_t x = (size_t)w; x-- > 0; )
			{
				Uint8 g = gradient[(y << wDec) | x];
				if (g == 0)
					continue;
				size_t xl = ((x - 1) & wMask);
				size_t xr = ((x + 1) & wMask);
				Uint8 best = g;
				Uint8 vSE = gradient[(yd << wDec) | xr];
				Uint8 vS  = gradient[(yd << wDec) | x ];
				Uint8 vSW = gradient[(yd << wDec) | xl];
				Uint8 vE  = gradient[(y  << wDec) | xr];
				if (vSE >= 3 && (Uint8)(vSE - 1) > best) best = vSE - 1;
				if (vS  >= 3 && (Uint8)(vS  - 1) > best) best = vS  - 1;
				if (vSW >= 3 && (Uint8)(vSW - 1) > best) best = vSW - 1;
				if (vE  >= 3 && (Uint8)(vE  - 1) > best) best = vE  - 1;
				if (best != g)
				{
					gradient[(y << wDec) | x] = best;
					changed = true;
				}
			}
		}

		passes++;
		if (passes >= 32)
		{
			FILE* dump = fopen("/tmp/glob2-gradient-divergence.txt", "w");
			if (dump)
			{
				fprintf(dump, "[chamfer] passes >= 32 - algorithm not converging. w=%d h=%d size=%zu\n",
					(int)w, (int)h, size);
				fclose(dump);
			}
			abort();
		}
	} while (changed);
}

template<typename Tint> void Map::updateGlobalGradient(
	Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType, bool canSwim)
{
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon, "gradientType: %d\n", gradientType);
	fprintf(logSimon, "canSwim: %d\n", canSwim);
#endif

	// Phase 4: chamfer is production. Dual-run snapshots the seeded gradient
	// before chamfer runs, then runs the SR-style BFS on the snapshot as a
	// conformance reference and compares. Any divergence aborts with the
	// offending gradientType plus the first 64 differing cells.
	static const bool dualRun = getenv("GLOB2_GRADIENT_DUAL_RUN") != nullptr;
	Uint8 *bfsCopy = nullptr;
	Uint8 *seedSnap = nullptr;
	if (dualRun)
	{
		bfsCopy = new Uint8[size];
		seedSnap = new Uint8[size];
		memcpy(bfsCopy, gradient, size);
		memcpy(seedSnap, gradient, size);
	}

	updateGlobalGradientVersionChamfer<Tint>(gradient, gradientType);

	if (dualRun)
	{
		// SR-style BFS reference: Simon for GT_RESOURCE, Simple otherwise.
		if (gradientType == GT_RESOURCE)
			updateGlobalGradientVersionSimon<Tint>(bfsCopy, listedAddr, listCountWrite);
		else
			updateGlobalGradientVersionSimple<Tint>(bfsCopy, listedAddr, listCountWrite, gradientType);

		if (memcmp(gradient, bfsCopy, size) != 0)
		{
			static const char* gtNames[GT_SIZE] = {
				"GT_UNDEFINED", "GT_RESOURCE", "GT_BUILDING",
				"GT_FORBIDDEN", "GT_GUARD_AREA", "GT_CLEAR_AREA"
			};
			// Dump to a file with explicit fclose: stdio buffers attached to
			// stderr/stdout are not reliably flushed by abort() on macOS when
			// the streams are redirected, so direct file I/O is safer here.
			FILE* dump = fopen("/tmp/glob2-gradient-divergence.txt", "w");
			if (dump)
			{
				fprintf(dump, "[gradient-dual-run] DIVERGENCE gradientType=%s canSwim=%d size=%zu w=%d h=%d\n",
					gtNames[gradientType], (int)canSwim, size, (int)w, (int)h);
				int shown = 0;
				for (size_t i = 0; i < size && shown < 64; i++)
				{
					if (gradient[i] != bfsCopy[i])
					{
						size_t y = i >> wDec;
						size_t x = i & wMask;
						fprintf(dump, "  (x=%zu y=%zu) chamfer=%u bfs=%u\n",
							x, y, (unsigned)gradient[i], (unsigned)bfsCopy[i]);
						shown++;
					}
				}
				size_t totalDiff = 0;
				for (size_t i = 0; i < size; i++)
					if (gradient[i] != bfsCopy[i]) totalDiff++;
				fprintf(dump, "  ... total differing cells = %zu\n", totalDiff);
				fclose(dump);
			}
			// Also dump the seed state, BFS result, and chamfer result as
			// raw bytes so an offline reproducer can replay the exact same
			// inputs.  Three contiguous size-byte blobs in dim w x h.
			FILE* binDump = fopen("/tmp/glob2-gradient-divergence.bin", "wb");
			if (binDump)
			{
				Uint32 hdr[7] = { (Uint32)w, (Uint32)h, (Uint32)size,
				                  (Uint32)gradientType, (Uint32)canSwim,
				                  (Uint32)listCountWrite, (Uint32)sizeof(Tint) };
				fwrite(hdr, sizeof(hdr), 1, binDump);
				fwrite(seedSnap, 1, size, binDump);
				fwrite(bfsCopy,  1, size, binDump);   // BFS result
				fwrite(gradient, 1, size, binDump);   // chamfer result
				fwrite(listedAddr, sizeof(Tint), listCountWrite, binDump);
				fclose(binDump);
			}
			delete[] bfsCopy;
			delete[] seedSnap;
			abort();
		}
		delete[] bfsCopy;
		delete[] seedSnap;
	}
}
