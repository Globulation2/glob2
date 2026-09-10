// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Leo Wandersleb
 
#include "GlobalContainer.h"
#include "HeightMapGenerator.h"
#include <math.h>
#include <vector>
#include <stdexcept>
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include "PerlinNoise.h"
#include "Utilities.h"

/// these faders are factors to be applicable to heightfields. they map (0,0)-(w,h) to [0..1]

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
	_pn.reseed(syncRand());
}

GAGCore::CooperativeTask HeightMap::makeStampTask(unsigned int radius)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	_r=radius;
    auto replacement = new float[(2*_r+1)*(2*_r+1)];
    delete [] _stamp;
    _stamp = replacement;
    lowerValid = false;
	for(unsigned int x=0; x<2*_r+1; x++)
	{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for(unsigned int y=0; y<2*_r+1;y++)
		{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			unsigned int dSquare=(x-_r)*(x-_r)+(y-_r)*(y-_r);
			if(dSquare<_r*_r)
				_stamp[x+y*(2*_r+1)]=(1.0-cos(sqrt(dSquare)*3.14159265/(float)_r))/2.0;
			else
				_stamp[x+y*(2*_r+1)]=.9999;
		}
	}
    co_return true;
}
GAGCore::CooperativeTask HeightMap::lowerTask(unsigned int coordX, unsigned int coordY)
{
    if (lowerValid && coordX == lowerX && coordY == lowerY) co_return true;
    lowerValid = true; lowerX = coordX; lowerY = coordY;
    unsigned operations = 0;
	{
		assert(_stamp);
		for(unsigned int x=0; x<2*_r+1;x++)
		{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			/// this loop can be replaced by a somehow complicated memcpy
			for(unsigned int y=0; y<2*_r+1;y++)
			{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				unsigned int coord1d=(unsigned int)(_w+x-_r+coordX)%_w+((unsigned int)(_h+y-_r+coordY)%_h)*_w;
				if(_map[coord1d]>_stamp[x+y*(2*_r+1)])
					_map[coord1d]=_stamp[x+y*(2*_r+1)];
			}
		}
	}
    co_return true;
}

GAGCore::CooperativeTask HeightMap::differenceStampTask(unsigned int coordX, unsigned int coordY)
{
    unsigned operations = 0;
	{
		assert(_stamp);
		for(unsigned int x=0; x<2*_r+1;x++)
		{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			for(unsigned int y=0; y<2*_r+1;y++)
			{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				unsigned int coord1d=(unsigned int)(_w+x-_r+coordX)%_w+((unsigned int)(_h+y-_r+coordY)%_h)*_w;
				_map[coord1d]=fabs((1.0-_stamp[x+y*(2*_r+1)])-_map[coord1d]);
			}
		}
	}
    co_return true;
}

GAGCore::CooperativeTask HeightMap::addNoiseTask(float weight, float smoothingFactor)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	assert((weight>0) && (weight<=1.0));
	for (int x=0; (unsigned int)x<_w; x++)
	{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for (int y=0; (unsigned int)y<_h; y++)
		{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			_map[x+_w*y] = _map[x+_w*y]*(1.0-weight)+
				(faderCenter(x,y,_w,_h)   *_pn.Noise((float)(        x)/smoothingFactor,(float)(        y)/smoothingFactor)+
				faderLeftRight(x,y,_w,_h)*_pn.Noise((float)((x+_w/2)%_w+_w)/smoothingFactor,(float)(        y+_h)/smoothingFactor)+
				faderTopBottom(x,y,_w,_h)*_pn.Noise((float)(        x+2*_w)/smoothingFactor,(float)((y+_h/2)%_h+2*_h)/smoothingFactor)+
				faderCorner(x,y,_w,_h)*_pn.Noise((float)((x+_w/2)%_w+3*_w)/smoothingFactor,(float)((y+_h/2)%_h+3*4)/smoothingFactor)+
				+4.0)/8.0*weight;
		}
	}
    co_return true;
}

void HeightMap::makeIslands(unsigned int count, float smoothingFactor)
{
    makeIslandsTask(count, smoothingFactor).run();
}

GAGCore::CooperativeTask HeightMap::makeIslandsTask(unsigned int count, float smoothingFactor)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	assert (count);
	_pn.reseed(syncRand());
	std::vector<int> centerX(count);
	std::vector<int> centerY(count);
	float mindist=sqrt(_w*_h/count)/2.0;
	assert(mindist>0);
	co_await makeStampTask((unsigned int)(mindist*2));
	centerX[0]=syncRand()%_w;centerY[0]=syncRand()%_h;
	/// find spots with distance>min. distance
	for (unsigned int i=1; i<count; i++)
	{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		bool foundSpot=false;
		unsigned int tries = 0;
		int newPosX, newPosY;
		do
		{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			newPosX=syncRand()%_w;
			newPosY=syncRand()%_h;
			tries++;
			foundSpot=true;
			for (unsigned int j=0; j<i; j++) {
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				int distX=std::min(abs(newPosX-centerX[j]),(int)_w-abs(newPosX-centerX[j]));
				int distY=std::min(abs(newPosY-centerY[j]),(int)_h-abs(newPosY-centerY[j]));
				if(distX<mindist && distY<mindist)
					foundSpot=false;
			}
		} while (!foundSpot && tries<count*count*_w*_h);
		if(!foundSpot)
		{
			std::cout <<count << " " << mindist << " " << tries << " " << newPosX << "/" << newPosY << " " << i << "\n";
			throw std::runtime_error("Could not place height-map islands");
		}
		centerX[i]=newPosX;centerY[i]=newPosY;
	}
	///level the terrain
	co_await fillTask(0.0);
	for (unsigned int i=0; i<count; i++)
	{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		co_await differenceStampTask(centerX[i],centerY[i]);
	}
	co_await addNoiseTask(.7,smoothingFactor);
	co_await normalizeTask();
    co_return true;
}

void HeightMap::makeRiver(unsigned int maxDiameter, float smoothingFactor)
{
    makeRiverTask(maxDiameter, smoothingFactor).run();
}

GAGCore::CooperativeTask HeightMap::makeRiverTask(unsigned int maxDiameter, float smoothingFactor)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	/// riverRadius refers to the distance between center of the river and the maximum distance that gets lowered.
	co_await makeStampTask(maxDiameter/2);
	/// level the map
	co_await fillTask(1.0);
	
	/// find start for a random walk
	float startingPointX=syncRand()%_w;
	float startingPointY=syncRand()%_h;
	
	/// the target=start+(w,h) is set now. tmprand(0,1,2)==position(+h,+w,+w+h)
	float targetPointX;
	float targetPointY;
	if(_w==_h)
	{
		unsigned int tmprand=syncRand()%3;
		targetPointX=startingPointX+(tmprand>0?_w:0);
		targetPointY=startingPointY+_h-(tmprand%2)*_h;
	}
	else if (_w>_h)
	{
		targetPointX=startingPointX+_w;
		targetPointY=startingPointY+(syncRand()%(_w/_h))*_h;
	}
	else
	{
		targetPointX=startingPointX+(syncRand()%(_h/_w))*_w;
		targetPointY=startingPointY+_h;
	}
	float targetDirection=asin((targetPointY-startingPointY)/sqrt(pow(targetPointX-startingPointX,2)+pow(targetPointY-startingPointY,2)));
	float targetDirectionX=cos(targetDirection);
	float targetDirectionY=sin(targetDirection);
	/// length of direct line
	float straightRiverLength=sqrt(pow(targetPointX-startingPointX,2)+pow(targetPointY-startingPointY,2));
	for(float t=0; t<straightRiverLength;t+=straightRiverLength/10.0/(_w+_h))
	{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		float offset=(1.0-cos(t/straightRiverLength*2*3.14159265))*(_pn.Noise(t/153.3)*300.0+_pn.Noise(t/13.3)*50.0-175.0);
		if(t<straightRiverLength/2.0)
			offset+=(1+cos(t/straightRiverLength*2*3.14159265))*(_pn.Noise(t/153.3)*300.0+_pn.Noise(t/13.3)*50.0-175.0);
		else
			offset+=(1+cos(t/straightRiverLength*2*3.14159265))*(_pn.Noise((straightRiverLength-t)/153.3)*300.0+_pn.Noise((straightRiverLength-t)/13.3)*50.0-175.0);
		float reachedPointX=targetDirectionX*t-targetDirectionY*offset/4;
		float reachedPointY=targetDirectionY*t+targetDirectionX*offset/4;
		co_await lowerTask((unsigned int)reachedPointX,(unsigned int)reachedPointY);
	}
	co_await addNoiseTask(.1,smoothingFactor);
	co_await normalizeTask();
    co_return true;
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
    makeCratersTask(craterCount, craterRadius, smoothingFactor).run();
}

GAGCore::CooperativeTask HeightMap::makeCratersTask(unsigned int craterCount, unsigned int craterRadius, float smoothingFactor)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	co_await makeStampTask(craterRadius);
	co_await fillTask(1.0);
	for(unsigned int t=0; t<craterCount; t++) {
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
        const auto x = syncRand()%_w;
        const auto y = syncRand()%_h;
        co_await lowerTask(x,y);
    }
	co_await addNoiseTask(.8,smoothingFactor);
	co_await normalizeTask();
    co_return true;
}



void HeightMap::makePlain(float smoothingFactor)
{
    makePlainTask(smoothingFactor).run();
}

GAGCore::CooperativeTask HeightMap::makePlainTask(float smoothingFactor)
{
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	co_await fillTask(1.0);
	co_await addNoiseTask(.99,smoothingFactor);
	co_await normalizeTask();
    co_return true;
}



void HeightMap::makeSwamp(float smoothingFactor)
{
    makeSwampTask(smoothingFactor).run();
}

GAGCore::CooperativeTask HeightMap::makeSwampTask(float smoothingFactor)
{
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	co_await fillTask(1.0);
	co_await addNoiseTask(.99,smoothingFactor);
	co_await normalizeTask();
    co_return true;
}

GAGCore::CooperativeTask HeightMap::normalizeTask()
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	float min=100000.0;
	float max=-100000.0;
	for(unsigned int i=0; i<_w*_h; i++)
	{
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		min=_map[i]<min?_map[i]:min;
		max=_map[i]>max?_map[i]:max;
	}
	min-=.01;
	max+=.01;
	float range=max-min;
	for(unsigned int i=0; i<_w*_h; i++) {
        if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
        _map[i]=(_map[i]-min)/range;
    }
    co_return true;
}

GAGCore::CooperativeTask HeightMap::fillTask(float value)
{
    lowerValid = false;
    for (unsigned i = 0; i < _w * _h; ++i) {
        _map[i] = value;
        if (i % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
    }
    co_return true;
}
