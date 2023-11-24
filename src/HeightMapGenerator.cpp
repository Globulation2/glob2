/***************************************************************************
 *            HeightMapGenerator.cpp
 *
 *  Sat Jan 14 11:45:30 2006
 *  Copyright  2006  Leo Wandersleb
 *  Email: Leo.Wandersleb@gmx.de
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#include "GlobalContainer.h"
#include "HeightMapGenerator.h"
#include <math.h>
#include "PerlinNoise.h"

/// these faders are factors to be applicable to height fields. they map (0,0)-(w,h) to [0..1]

inline float faderCenter   (int x, int y, int w, int h) /// to have zero at the borders and 1 in the center
{
	return (1.0-cos(2.0*3.14159265*(float)x/(float)w))*(1.0-cos(2.0*3.14159265*(float)y/(float)h))/4.0;
}
inline float faderLeftRight(int x, int y, int w, int h) /// to have 0 at top and bottom border and 1 at the middle of right and left border
{
	return (faderCenter((x+w/2)%w,y,w,h));
}
inline float faderTopBottom(int x, int y, int w, int h) /// to have 1 at the middle of top and bottom border and 0 at right and left border
{
	return (faderCenter(x,(y+h/2)%h,w,h));
}
inline float faderCorner(int x, int y, int w, int h) /// to have 1 in the corners and 0 on a cross going through the center
{
	return (faderCenter((x+w/2)%w,(y+h/2)%h,w,h));
}

HeightMap::HeightMap(unsigned int width, unsigned int height)
{
	init(width,height);
}
	
HeightMap::~HeightMap()
{
	delete [] _map;
	if(_stamp)
		delete [] _stamp;
}

void HeightMap::init(unsigned int width, unsigned int height)
{
	_w=width; _h=height;
	_map=new float[_w*_h];
	_stamp=NULL;
	_pn.reseed();
}

void HeightMap::makeStamp(unsigned int radius)
{
	_r=radius;
	if(_stamp)
		delete [] _stamp;
	_stamp=new float[(2*_r+1)*(2*_r+1)];
	for(unsigned int x=0; x<2*_r+1; x++)
	{
		for(unsigned int y=0; y<2*_r+1;y++)
		{
			unsigned int dSquare=(x-_r)*(x-_r)+(y-_r)*(y-_r);
			if(dSquare<_r*_r)
				_stamp[x+y*(2*_r+1)]=(1.0-cos(sqrt(dSquare)*3.14159265/(float)_r))/2.0;
			else
				_stamp[x+y*(2*_r+1)]=.9999;
		}
	}
}
inline void HeightMap::lower(unsigned int coordX, unsigned int coordY)
{
	static unsigned int oldX=(unsigned int)-1;
	static unsigned int oldY=(unsigned int)-1;
	if((coordX!=oldX) || (coordY!=oldY)) //don't stamp the same spot again. if stamp is moved like in river maps this saves a lot of time
	{
		assert(_stamp);
		for(unsigned int x=0; x<2*_r+1;x++)
		{
			/// this loop can be replaced by a somehow complicated memcpy
			for(unsigned int y=0; y<2*_r+1;y++)
			{
				unsigned int coord1d=(unsigned int)(_w+x-_r+coordX)%_w+((unsigned int)(_h+y-_r+coordY)%_h)*_w;
				if(_map[coord1d]>_stamp[x+y*(2*_r+1)])
					_map[coord1d]=_stamp[x+y*(2*_r+1)];
			}
		}
		oldX=coordX;
		oldY=coordY;
	}
}

inline void HeightMap::maxRise(unsigned int coordX, unsigned int coordY)
{
	static unsigned int oldX=(unsigned int)-1;
	static unsigned int oldY=(unsigned int)-1;
	if((coordX!=oldX) || (coordY!=oldY)) //don't stamp the same spot again. if stamp is moved like in river maps this saves a lot of time
	{
		assert(_stamp);
		for(unsigned int x=0; x<2*_r+1;x++)
		{
			for(unsigned int y=0; y<2*_r+1;y++)
			{
				unsigned int coord1d=(unsigned int)(_w+x-_r+coordX)%_w+((unsigned int)(_h+y-_r+coordY)%_h)*_w;
				if(_map[coord1d]<1-_stamp[x+y*(2*_r+1)])
					_map[coord1d]=1-_stamp[x+y*(2*_r+1)];
			}
		}
		oldX=coordX;
		oldY=coordY;
	}
}

inline void HeightMap::differenceStamp(unsigned int coordX, unsigned int coordY)
{
	static unsigned int oldX=(unsigned int)-1;
	static unsigned int oldY=(unsigned int)-1;
	if((coordX!=oldX) || (coordY!=oldY)) //don't stamp the same spot again. if stamp is moved like in river maps this saves a lot of time
	{
		assert(_stamp);
		for(unsigned int x=0; x<2*_r+1;x++)
		{
			for(unsigned int y=0; y<2*_r+1;y++)
			{
				unsigned int coord1d=(unsigned int)(_w+x-_r+coordX)%_w+((unsigned int)(_h+y-_r+coordY)%_h)*_w;
				_map[coord1d]=fabs((1.0-_stamp[x+y*(2*_r+1)])-_map[coord1d]);
			}
		}
		oldX=coordX;
		oldY=coordY;
	}
}

inline void HeightMap::addNoise(float weight, float smoothingFactor)
{
	assert((weight>0) && (weight<=1.0));
	for (int x=0; (unsigned int)x<_w; x++)
	{
		for (int y=0; (unsigned int)y<_h; y++)
		{
			_map[x+_w*y] = _map[x+_w*y]*(1.0-weight)+
				(faderCenter(x,y,_w,_h)   *_pn.Noise((float)(        x)/smoothingFactor,(float)(        y)/smoothingFactor)+
				faderLeftRight(x,y,_w,_h)*_pn.Noise((float)((x+_w/2)%_w+_w)/smoothingFactor,(float)(        y+_h)/smoothingFactor)+
				faderTopBottom(x,y,_w,_h)*_pn.Noise((float)(        x+2*_w)/smoothingFactor,(float)((y+_h/2)%_h+2*_h)/smoothingFactor)+
				faderCorner(x,y,_w,_h)*_pn.Noise((float)((x+_w/2)%_w+3*_w)/smoothingFactor,(float)((y+_h/2)%_h+3*4)/smoothingFactor)+
				+4.0)/8.0*weight;
		}
	}
}

void HeightMap::makeIslands(unsigned int count, float smoothingFactor)
{
	assert (count);
	PerlinNoise pn;
	pn.reseed();
	int * centerX = new int[count];
	int * centerY = new int[count];
	float minDist=sqrt(_w*_h/count)/2.0;
	assert(minDist>0);
	makeStamp((unsigned int)(minDist*2));
	centerX[0]=rand()%_w;centerY[0]=rand()%_h;
	/// find spots with distance>min. distance
	for (unsigned int i=1; i<count; i++)
	{
		bool foundSpot=false;
		unsigned int tries = 0;
		int newPosX, newPosY;
		do
		{
			newPosX=rand()%_w;
			newPosY=rand()%_h;
			tries++;
			foundSpot=true;
			for (unsigned int j=0; j<i; j++) {
				int distX=std::min(abs(newPosX-centerX[j]),(int)_w-abs(newPosX-centerX[j]));
				int distY=std::min(abs(newPosY-centerY[j]),(int)_h-abs(newPosY-centerY[j]));
				if(distX<minDist && distY<minDist)
					foundSpot=false;
			}
		} while (!foundSpot && tries<count*count*_w*_h);
		if(!foundSpot)
		{
			std::cout <<count << " " << minDist << " " << tries << " " << newPosX << "/" << newPosY << " " << i << "\n";
			assert (false);
		}
		centerX[i]=newPosX;centerY[i]=newPosY;
	}
	///level the terrain
	operator=(0.0);
	for (unsigned int i=0; i<count; i++)
	{
		differenceStamp(centerX[i],centerY[i]);
	}
	addNoise(.7,smoothingFactor);
	normalize();
	delete[] centerX;
	delete[] centerY;
}

void HeightMap::makeRiver(unsigned int maxDiameter, float smoothingFactor)
{
	/// riverRadius refers to the distance between center of the river and the maximum distance that gets lowered.
	makeStamp(maxDiameter/2);
	/// level the map
	operator=(1.0);
	
	/// find start for a random walk
	float startingPointX=rand()%_w;
	float startingPointY=rand()%_h;
	
	/// the target=start+(w,h) is set now. tmpRand(0,1,2)==position(+h,+w,+w+h)
	float targetPointX;
	float targetPointY;
	if(_w==_h)
	{
		unsigned int tmpRand=rand()%3;
		targetPointX=startingPointX+(tmpRand>0?_w:0);
		targetPointY=startingPointY+_h-(tmpRand%2)*_h;
	}
	else if (_w>_h)
	{
		targetPointX=startingPointX+_w;
		targetPointY=startingPointY+(rand()%(_w/_h))*_h;
	}
	else
	{
		targetPointX=startingPointX+(rand()%(_h/_w))*_w;
		targetPointY=startingPointY+_h;
	}
	float targetDirection=asin((targetPointY-startingPointY)/sqrt(pow(targetPointX-startingPointX,2)+pow(targetPointY-startingPointY,2)));
	float targetDirectionX=cos(targetDirection);
	float targetDirectionY=sin(targetDirection);
	/// length of direct line
	float straightRiverLength=sqrt(pow(targetPointX-startingPointX,2)+pow(targetPointY-startingPointY,2));
	for(float t=0; t<straightRiverLength;t+=straightRiverLength/10.0/(_w+_h))
	{
		float offset=(1.0-cos(t/straightRiverLength*2*3.14159265))*(_pn.Noise(t/153.3)*300.0+_pn.Noise(t/13.3)*50.0-175.0);
		if(t<straightRiverLength/2.0)
			offset+=(1+cos(t/straightRiverLength*2*3.14159265))*(_pn.Noise(t/153.3)*300.0+_pn.Noise(t/13.3)*50.0-175.0);
		else
			offset+=(1+cos(t/straightRiverLength*2*3.14159265))*(_pn.Noise((straightRiverLength-t)/153.3)*300.0+_pn.Noise((straightRiverLength-t)/13.3)*50.0-175.0);
		float reachedPointX=targetDirectionX*t-targetDirectionY*offset/4;
		float reachedPointY=targetDirectionY*t+targetDirectionX*offset/4;
		lower((unsigned int)reachedPointX,(unsigned int)reachedPointY);
	}
	addNoise(.1,smoothingFactor);
	normalize();
}

void HeightMap::stampOutput(char * filename)
{
	char * hm2=new char[(2*_r+1)*(2*_r+1)];
	StreamBackend * stream = Toolkit::getFileManager()->openOutputStreamBackend(filename);

	for(unsigned int i=0; i<(2*_r+1)*(2*_r+1); i++)
		hm2[i]=(char)(_stamp[i]*256);

	stream->write(hm2, (2*_r+1)*(2*_r+1)*sizeof(char));
	delete stream;
	delete [] hm2;
}

void HeightMap::mapOutput(char * filename)
{
	char * hm2=new char[_w*_h];
	StreamBackend * stream = Toolkit::getFileManager()->openOutputStreamBackend(filename);

	for(unsigned int i=0; i<_w*_h; i++)
		hm2[i]=(char)(_map[i]*256);

	stream->write(hm2, _w*_h*sizeof(char));
	delete stream;
	delete [] hm2;
}

void HeightMap::makeCraters(unsigned int craterCount, unsigned int craterRadius, float smoothingFactor)
{
	makeStamp(craterRadius);
	operator=(1.0);
	for(unsigned int t=0; t<craterCount; t++)
		lower(rand()%_w,rand()%_h);
	addNoise(.8,smoothingFactor);
	normalize();
}



void HeightMap::makePlain(float smoothingFactor)
{
	operator=(1.0);
	addNoise(.99,smoothingFactor);
	normalize();
}



void HeightMap::makeSwamp(float smoothingFactor)
{
	operator=(1.0);
	addNoise(.99,smoothingFactor);
	normalize();
}

void HeightMap::normalize()
{
	float min=100000.0;
	float max=-100000.0;
	for(unsigned int i=0; i<_w*_h; i++)
	{
		min=_map[i]<min?_map[i]:min;
		max=_map[i]>max?_map[i]:max;
	}
	min-=.01;
	max+=.01;
	//std::cout << "min, max=" << min << ", " << max << "\n";
	float range=max-min;
	for(unsigned int i=0; i<_w*_h; i++)
		_map[i]=(_map[i]-min)/range;
}
