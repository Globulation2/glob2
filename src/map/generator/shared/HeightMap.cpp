// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Leo Wandersleb

#include "HeightMap.h"
#include "GenerationResult.h"
#include "GlobalContainer.h"
#include "Noise.h"
#include <math.h>
#include <vector>

/// these faders are factors to be applicable to heightfields. they map (0,0)-(w,h) to [0..1]

inline float faderCenter(int x, int y, int w,
						 int h) /// to have zero at the borders and 1 in the center
{
	return (1.0 - cos(2.0 * 3.14159265 * (float)x / (float)w)) *
		   (1.0 - cos(2.0 * 3.14159265 * (float)y / (float)h)) / 4.0;
}
inline float faderLeftRight(
	int x, int y, int w,
	int h) /// to have 0 at top and bottom border and 1 at the middle of right and left border
{
	return (faderCenter((x + w / 2) % w, y, w, h));
}
inline float faderTopBottom(
	int x, int y, int w,
	int h) /// to have 1 at the middle of top and bottom border and 0 at right and left border
{
	return (faderCenter(x, (y + h / 2) % h, w, h));
}
inline float
faderCorner(int x, int y, int w,
			int h) /// to have 1 in the corners and 0 on a cross going through the center
{
	return (faderCenter((x + w / 2) % w, (y + h / 2) % h, w, h));
}

HeightMap::HeightMap(unsigned int width, unsigned int height, std::mt19937 &rng)
	: _pn(rng()), random(rng)
{
	init(width, height);
}

void HeightMap::init(unsigned int width, unsigned int height)
{
	_w = width;
	_h = height;
	_map.assign(size_t(_w) * _h, 0.f);
	_stamp.clear();
}

// The stamp: a round bowl, 0 at its centre rising as (1 - cos(pi * d / r)) / 2 to 1 at radius r,
// and .9999 outside. lower() presses it into the field (keeping the lower value) to dig a crater or
// a river bed; differenceStamp() uses its complement, a hill of height 1, to raise an island.
void HeightMap::makeStamp(unsigned int radius)
{
	_r = std::max(1u, radius);
	oldLowerX = oldLowerY = oldDifferenceX = oldDifferenceY = ~0u;
	_stamp.assign(size_t(2 * _r + 1) * (2 * _r + 1), 0.f);
	for (unsigned int x = 0; x < 2 * _r + 1; x++)
	{
		for (unsigned int y = 0; y < 2 * _r + 1; y++)
		{
			unsigned int dSquare = (x - _r) * (x - _r) + (y - _r) * (y - _r);
			if (dSquare < _r * _r)
				_stamp[x + y * (2 * _r + 1)] =
					(1.0 - cos(sqrt(dSquare) * 3.14159265 / (float)_r)) / 2.0;
			else
				_stamp[x + y * (2 * _r + 1)] = .9999;
		}
	}
}
inline void HeightMap::lower(unsigned int coordX, unsigned int coordY)
{
	if ((coordX != oldLowerX) ||
		(coordY != oldLowerY)) // don't stamp the same spot again. if stamp is moved like in
							   // rivermaps this saves a lot of time
	{
		assert(!_stamp.empty());
		for (unsigned int x = 0; x < 2 * _r + 1; x++)
		{
			/// this loop can be replaced by a somehow complicated memcpy
			for (unsigned int y = 0; y < 2 * _r + 1; y++)
			{
				unsigned int coord1d = (unsigned int)(_w + x - _r + coordX) % _w +
									   ((unsigned int)(_h + y - _r + coordY) % _h) * _w;
				if (_map[coord1d] > _stamp[x + y * (2 * _r + 1)])
					_map[coord1d] = _stamp[x + y * (2 * _r + 1)];
			}
		}
		oldLowerX = coordX;
		oldLowerY = coordY;
	}
}

inline void HeightMap::differenceStamp(unsigned int coordX, unsigned int coordY)
{
	if ((coordX != oldDifferenceX) ||
		(coordY != oldDifferenceY)) // don't stamp the same spot again. if stamp is moved like in
									// rivermaps this saves a lot of time
	{
		assert(!_stamp.empty());
		for (unsigned int x = 0; x < 2 * _r + 1; x++)
		{
			for (unsigned int y = 0; y < 2 * _r + 1; y++)
			{
				unsigned int coord1d = (unsigned int)(_w + x - _r + coordX) % _w +
									   ((unsigned int)(_h + y - _r + coordY) % _h) * _w;
				_map[coord1d] = fabs((1.0 - _stamp[x + y * (2 * _r + 1)]) - _map[coord1d]);
			}
		}
		oldDifferenceX = coordX;
		oldDifferenceY = coordY;
	}
}

// Blends the field with noise: field * (1 - weight) + noise * weight. The noise is four copies of
// Perlin noise read at different offsets and cross-faded with the fader functions above, so it
// wraps seamlessly at every edge: the map is a torus and a coast must not break at the border.
// The weight is each shape's balance between design and chaos: .1 for the river (the bed must
// survive), .7 islands, .8 craters, .99 swamp and plain (all noise).
inline void HeightMap::addNoise(float weight, float smoothingFactor)
{
	assert((weight > 0) && (weight <= 1.0));
	for (int x = 0; (unsigned int)x < _w; x++)
	{
		for (int y = 0; (unsigned int)y < _h; y++)
		{
			_map[x + _w * y] =
				_map[x + _w * y] * (1.0 - weight) +
				(faderCenter(x, y, _w, _h) *
					 _pn.Noise((float)(x) / smoothingFactor, (float)(y) / smoothingFactor) +
				 faderLeftRight(x, y, _w, _h) *
					 _pn.Noise((float)((x + _w / 2) % _w + _w) / smoothingFactor,
							   (float)(y + _h) / smoothingFactor) +
				 faderTopBottom(x, y, _w, _h) *
					 _pn.Noise((float)(x + 2 * _w) / smoothingFactor,
							   (float)((y + _h / 2) % _h + 2 * _h) / smoothingFactor) +
				 faderCorner(x, y, _w, _h) *
					 _pn.Noise((float)((x + _w / 2) % _w + 3 * _w) / smoothingFactor,
							   (float)((y + _h / 2) % _h + 3 * 4) / smoothingFactor) +
				 +4.0) /
					8.0 * weight;
		}
	}
}

// Islands: `count` hills, then noise. Hill centres are drawn at random and rejected while any
// earlier centre lies within `mindist` on both axes, where mindist is half the side of each
// island's share of the map. Each hill has radius 2 * mindist, the whole side of that share, so
// neighbouring hills overlap heavily. differenceStamp takes |hill - field|, so where two hills
// overlap they cancel into a valley rather than add up: the overlaps become the channels between
// islands. Noise at weight 0.7 then roughens the coasts; the histogram decides how much is above
// water. Islands asks for (colonies + extra islands) / 2^repeat hills, one or more per colony per
// patch.
void HeightMap::makeIslands(unsigned int count, float smoothingFactor)
{
	assert(count);
	GenerationNoise pn(random());
	std::vector<int> centerX(count);
	std::vector<int> centerY(count);
	float mindist = sqrt(_w * _h / count) / 2.0;
	assert(mindist > 0);
	makeStamp((unsigned int)(mindist * 2));
	centerX[0] = static_cast<int>(random() & 0x7fffffffu) % _w;
	centerY[0] = static_cast<int>(random() & 0x7fffffffu) % _h;
	/// find spots with distance>min. distance
	for (unsigned int i = 1; i < count; i++)
	{
		bool foundSpot = false;
		unsigned int tries = 0;
		int newPosX, newPosY;
		do
		{
			newPosX = static_cast<int>(random() & 0x7fffffffu) % _w;
			newPosY = static_cast<int>(random() & 0x7fffffffu) % _h;
			tries++;
			foundSpot = true;
			for (unsigned int j = 0; j < i; j++)
			{
				int distX =
					std::min(abs(newPosX - centerX[j]), (int)_w - abs(newPosX - centerX[j]));
				int distY =
					std::min(abs(newPosY - centerY[j]), (int)_h - abs(newPosY - centerY[j]));
				if (distX < mindist && distY < mindist)
					foundSpot = false;
			}
		} while (!foundSpot && tries < std::min(1000000u, count * count * _w * _h));
		if (!foundSpot)
		{
			throw GenerationFailure("Cannot space the requested islands");
		}
		centerX[i] = newPosX;
		centerY[i] = newPosY;
	}
	/// level the terrain
	operator=(0.0);
	for (unsigned int i = 0; i < count; i++)
	{
		differenceStamp(centerX[i], centerY[i]);
	}
	addNoise(.7, smoothingFactor);
	normalize();
}

// River: a river bed pressed along a straight line from a random point to that point moved a whole
// map width, height or both, so on the torus the line ends where it began and the river is one
// closed loop across the map, with no source and no mouth. On a rectangle it runs along the long
// side. River width is a percentage of the mean side (the stamp's diameter).
//
// The meander constants (winding river): the bed is moved sideways by two octaves of 1D noise,
// feature lengths about 153 and 13 steps with weights 300 and 50. This noise stays within about
// +-0.5, so the two swing by up to +-175, and the -175 turns that into a one-sided offset of 0 to
// -350: the river only ever bends to one side of its line, which on a torus is just a shift. The
// meander is computed twice, forwards from the start and backwards from the end, and cross-faded
// with (1 -+ cos) weights that sum to 2, so the offset is the same at both ends and the loop closes
// without a jump. Divided by 4, the river could in theory wander up to 175 tiles off its line; in
// practice the noise stays well inside its range and the river is a gentle wave. Only 0.1 of noise
// is added afterwards, so the bed dominates: the water is the river, not scattered ponds.
void HeightMap::makeRiver(unsigned int maxDiameter, float smoothingFactor, bool winding)
{
	/// riverRadius refers to the distance between center of the river and the maximum distance that
	/// gets lowered.
	makeStamp(maxDiameter / 2);
	/// level the map
	operator=(1.0);

	/// find start for a random walk
	float startingPointX = static_cast<int>(random() & 0x7fffffffu) % _w;
	float startingPointY = static_cast<int>(random() & 0x7fffffffu) % _h;

	/// the target=start+(w,h) is set now. tmprand(0,1,2)==position(+h,+w,+w+h)
	float targetPointX;
	float targetPointY;
	if (_w == _h)
	{
		unsigned int tmprand = static_cast<int>(random() & 0x7fffffffu) % 3;
		targetPointX = startingPointX + (tmprand > 0 ? _w : 0);
		targetPointY = startingPointY + _h - (tmprand % 2) * _h;
	}
	else if (_w > _h)
	{
		targetPointX = startingPointX + _w;
		targetPointY = startingPointY + (static_cast<int>(random() & 0x7fffffffu) % (_w / _h)) * _h;
	}
	else
	{
		targetPointX = startingPointX + (static_cast<int>(random() & 0x7fffffffu) % (_h / _w)) * _w;
		targetPointY = startingPointY + _h;
	}
	float targetDirection =
		asin((targetPointY - startingPointY) /
			 sqrt(pow(targetPointX - startingPointX, 2) + pow(targetPointY - startingPointY, 2)));
	float targetDirectionX = cos(targetDirection);
	float targetDirectionY = sin(targetDirection);
	/// length of direct line
	float straightRiverLength =
		sqrt(pow(targetPointX - startingPointX, 2) + pow(targetPointY - startingPointY, 2));
	for (float t = 0; t < straightRiverLength; t += straightRiverLength / 10.0 / (_w + _h))
	{
		// The meander is a pure function of t, so leaving it out changes nothing else.
		float offset = 0;
		if (winding)
		{
			offset = (1.0 - cos(t / straightRiverLength * 2 * 3.14159265)) *
					 (_pn.Noise(t / 153.3) * 300.0 + _pn.Noise(t / 13.3) * 50.0 - 175.0);
			if (t < straightRiverLength / 2.0)
				offset += (1 + cos(t / straightRiverLength * 2 * 3.14159265)) *
						  (_pn.Noise(t / 153.3) * 300.0 + _pn.Noise(t / 13.3) * 50.0 - 175.0);
			else
				offset += (1 + cos(t / straightRiverLength * 2 * 3.14159265)) *
						  (_pn.Noise((straightRiverLength - t) / 153.3) * 300.0 +
						   _pn.Noise((straightRiverLength - t) / 13.3) * 50.0 - 175.0);
		}
		float reachedPointX = targetDirectionX * t - targetDirectionY * offset / 4;
		float reachedPointY = targetDirectionY * t + targetDirectionX * offset / 4;
		lower((unsigned int)reachedPointX, (unsigned int)reachedPointY);
	}
	addNoise(.1, smoothingFactor);
	normalize();
}

// Crater lakes: `craterCount` bowls of radius `craterRadius` pressed at random into high ground,
// then noise at weight 0.8. lower() keeps the minimum, so overlapping bowls merge into one lake.
// The generator asks for width * height * density / 30000 craters, about 54 on a 256x256 map at the
// default density of 25, which is one crater per 1200 tiles: with radius 25 each bowl covers about
// 2000, so the bowls would cover the map more than once over; but only their deepest parts fall
// under the water share, so the lakes come out as round ponds much smaller than the bowls.
void HeightMap::makeCraters(unsigned int craterCount, unsigned int craterRadius,
							float smoothingFactor)
{
	makeStamp(craterRadius);
	operator=(1.0);
	for (unsigned int t = 0; t < craterCount; t++)
		lower(static_cast<int>(random() & 0x7fffffffu) % _w,
			  static_cast<int>(random() & 0x7fffffffu) % _h);
	addNoise(.8, smoothingFactor);
	normalize();
}

void HeightMap::makePlain(float smoothingFactor)
{
	operator=(1.0);
	addNoise(.99, smoothingFactor);
	normalize();
}

void HeightMap::makeSwamp(float smoothingFactor)
{
	operator=(1.0);
	addNoise(.99, smoothingFactor);
	normalize();
}

void HeightMap::normalize()
{
	float min = 100000.0;
	float max = -100000.0;
	for (unsigned int i = 0; i < _w * _h; i++)
	{
		min = _map[i] < min ? _map[i] : min;
		max = _map[i] > max ? _map[i] : max;
	}
	min -= .01;
	max += .01;
	float range = max - min;
	for (unsigned int i = 0; i < _w * _h; i++)
		_map[i] = (_map[i] - min) / range;
}
