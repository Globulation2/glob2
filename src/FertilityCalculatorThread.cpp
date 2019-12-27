/*
  Copyright (C) 2007-2008 Bradley Arsenault

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

#include "FertilityCalculatorThread.h"
#include "Map.h"

#include "FertilityCalculatorThreadMessage.h"

FertilityCalculatorThread::FertilityCalculatorThread(Map& map, std::queue<boost::shared_ptr<FertilityCalculatorThreadMessage> >& outgoing, boost::recursive_mutex& outgoingMutex)
	: outgoing(outgoing), outgoingMutex(outgoingMutex), map(map)
{
}



void FertilityCalculatorThread::operator()()
{
	///This function goes so quick relative to the following, it isn't even considered for percent complete
	computeRessourcesGradient();
	fertilitymax = 0;
	fertility.resize(map.getW() * map.getH());
	std::fill(fertility.begin(), fertility.end(), 0);
	for(int x=0; x<map.getW(); ++x)
	{
		updatePercentComplete(float(x) / float(map.getW()));
		for(int y=0; y<map.getH(); ++y)
		{
			if(map.isGrass(x, y))
			{
				Uint8 dist = gradient[get_pos(x,y)];
				//if dist = 0 or 1, then no path can be found.
				if(dist > 1)
				{
					Uint16 total=0;
					for(int nx = -15; nx <= 15; ++nx)
					{
						for(int ny = -15; ny <= 15; ++ny)
						{
							int value = (15 - std::abs(nx)) * (15 - std::abs(ny));
							//Square root fall-off, to make things more even
							if(map.isWater(x+nx, y+ny))
								total += int(4.2f * std::sqrt((float)value));
						}
					}
					fertilitymax = std::max(fertilitymax, total);
					fertility[x * map.getH() + y] = total;
				}
			}
		}
	}

	for(int x=0; x<map.getW(); ++x)
	{
		for(int y=0; y<map.getH(); ++y)
		{
			map.getCase(x, y).fertility = fertility[x * map.getH() + y];
			map.fertilityMaximum = fertilitymax;
		}
	}


	boost::shared_ptr<FCTFertilityCompleted> message(new FCTFertilityCompleted);
	sendToMainThread(message);
}



void FertilityCalculatorThread::sendMessage(boost::shared_ptr<FertilityCalculatorThreadMessage> message)
{
	boost::recursive_mutex::scoped_lock lock(incomingMutex);
	incoming.push(message);
}



bool FertilityCalculatorThread::hasThreadExited()
{
	return hasExited;
}



void FertilityCalculatorThread::sendToMainThread(boost::shared_ptr<FertilityCalculatorThreadMessage> message)
{
	boost::recursive_mutex::scoped_lock lock(outgoingMutex);
	outgoing.push(message);
}




void FertilityCalculatorThread::computeRessourcesGradient()
{
	gradient.resize(map.getW()*map.getH());
	std::fill(gradient.begin(), gradient.end(),0);

	std::queue<position> positions;
	for(int x=0; x<map.getW(); ++x)
	{
		for(int y=0; y<map.getH(); ++y)
		{
			if(map.isRessourceTakeable(x, y, CORN) || map.isRessourceTakeable(x, y, WOOD))
			{
				gradient[get_pos(x, y)]=2;
				positions.push(position(x, y));
			}
			else if(!map.isGrass(x, y))
				gradient[get_pos(x, y)]=1;
		}
	}
	while(!positions.empty())
	{
		position p=positions.front();
		positions.pop();

		int left=map.normalizeX(p.x-1);
		int right=map.normalizeX(p.x+1);
		int up=map.normalizeY(p.y-1);
		int down=map.normalizeY(p.y+1);
		int center_h=p.x;
		int center_y=p.y;
		int n=gradient[get_pos(center_h, center_y)];

		if(gradient[get_pos(left, up)]==0)
		{
			gradient[get_pos(left, up)]=n+1;
			positions.push(position(left, up));
		}

		if(gradient[get_pos(center_h, up)]==0)
		{
			gradient[get_pos(center_h, up)]=n+1;
			positions.push(position(center_h, up));
		}

		if(gradient[get_pos(right, up)]==0)
		{
			gradient[get_pos(right, up)]=n+1;
			positions.push(position(right, up));
		}

		if(gradient[get_pos(left, center_y)]==0)
		{
			gradient[get_pos(left, center_y)]=n+1;
			positions.push(position(left, center_y));
		}

		if(gradient[get_pos(right, center_y)]==0)
		{
			gradient[get_pos(right, center_y)]=n+1;
			positions.push(position(right, center_y));
		}

		if(gradient[get_pos(left, down)]==0)
		{
			gradient[get_pos(left, down)]=n+1;
			positions.push(position(left, down));
		}

		if(gradient[get_pos(center_h, down)]==0)
		{
			gradient[get_pos(center_h, down)]=n+1;
			positions.push(position(center_h, down));
		}

		if(gradient[get_pos(right, down)]==0)
		{
			gradient[get_pos(right, down)]=n+1;
			positions.push(position(right, down));
		}
	}
}


void FertilityCalculatorThread::updatePercentComplete(float percent)
{
	boost::shared_ptr<FCTUpdateCompletionPercent> message(new FCTUpdateCompletionPercent(percent));
	sendToMainThread(message);
}



int FertilityCalculatorThread::get_pos(int x, int y)
{
	return x*map.getH()+y;
}

