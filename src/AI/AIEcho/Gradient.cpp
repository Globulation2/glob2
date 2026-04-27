/*
  Copyright (C) 2006 Bradley Arsenault

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

#include "AIEcho.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingsTypes.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include <tuple>
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using std::shared_ptr;


GradientInfo::GradientInfo()
{
	needs_updated=indeterminate;
}


GradientInfo::~GradientInfo()
{

}


void GradientInfo::add_source(Entities::Entity* source)
{
	sources.push_back(std::shared_ptr<Entities::Entity>(source));
}


void GradientInfo::add_obstacle(Entities::Entity* obstacle)
{
	obstacles.push_back(std::shared_ptr<Entities::Entity>(obstacle));
}


bool GradientInfo::match_source(Map* map, int posx, int posy)
{
	for(unsigned int x=0; x<sources.size(); ++x)
		if(sources[x]->is_entity(map, posx, posy))
			return true;
	return false;
}


bool GradientInfo::match_obstacle(Map* map, int posx, int posy)
{
	for(unsigned int x=0; x<obstacles.size(); ++x)
		if(obstacles[x]->is_entity(map, posx, posy))
			return true;
	return false;
}


bool GradientInfo::operator==(const GradientInfo& rhs) const
{
	if(sources.size()!=rhs.sources.size() || obstacles.size() != rhs.obstacles.size())
		return false;
	for(unsigned int i=0; i<sources.size(); ++i)
	{
		if(!((*sources[i])==(*rhs.sources[i])))
			return false;
	}

	for(unsigned int i=0; i<obstacles.size(); ++i)
	{
		if(!((*obstacles[i])==(*rhs.obstacles[i])))
			return false;
	}
	return true;
}



bool GradientInfo::needs_updating() const
{
	if(needs_updated)
		return true;
	else if(!needs_updated)
		return false;
	else
	{
		needs_updated=false;
		for(unsigned int i=0; i<sources.size(); ++i)
		{
			if(sources[i]->can_change())
			{
				needs_updated=true;
				return true;
			}
		}

		for(unsigned int i=0; i<obstacles.size(); ++i)
		{
			if(obstacles[i]->can_change())
			{
				needs_updated=true;
				return true;
			}
		}
	}
	return false;
}



bool GradientInfo::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("GradientInfo");

	stream->readEnterSection("sources");
	int size=stream->readUint32("size");
	sources.resize(size);
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		sources[n]=std::shared_ptr<Entities::Entity>(Entities::Entity::load_entity(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("obstacles");
	size=stream->readUint32("size");
	obstacles.resize(size);
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		obstacles[n]=std::shared_ptr<Entities::Entity>(Entities::Entity::load_entity(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}



void GradientInfo::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("GradientInfo");

	stream->writeEnterSection("sources");
	stream->writeUint32(sources.size(), "size");
	for(unsigned n=0; n<sources.size(); ++n)
	{
		stream->writeEnterSection(n);
		Entities::Entity::save_entity(sources[n].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("obstacles");
	stream->writeUint32(obstacles.size(), "size");
	for(unsigned n=0; n<obstacles.size(); ++n)
	{
		stream->writeEnterSection(n);
		Entities::Entity::save_entity(obstacles[n].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}



GradientInfo make_gradient_info(Entities::Entity* source)
{
	GradientInfo gi;
	gi.add_source(source);
	return gi;
}



GradientInfo make_gradient_info_obstacle(Entities::Entity* source, Entities::Entity* obstacle)
{
	GradientInfo gi;
	gi.add_source(source);
	gi.add_obstacle(obstacle);
	return gi;
}



GradientInfo make_gradient_info(Entities::Entity* source1, Entities::Entity* source2)
{
	GradientInfo gi;
	gi.add_source(source1);
	gi.add_source(source2);
	return gi;
}



GradientInfo make_gradient_info_obstacle(Entities::Entity* source1, Entities::Entity* source2, Entities::Entity* obstacle)
{
	GradientInfo gi;
	gi.add_source(source1);
	gi.add_source(source2);
	gi.add_obstacle(obstacle);
	return gi;
}



Gradient::Gradient(const GradientInfo& gi) 
{
	gradient_info=gi;
	width=0;
}


void Gradient::recalculate(Map* map)
{
	width=map->getW();
//	if(gradient==NULL)
//		gradient=new Sint16[map->getW()*map->getH()];
//	std::fill(gradient, gradient+(map->getW()*map->getH()),0); 

	gradient.resize(map->getW()*map->getH());
	std::fill(gradient.begin(), gradient.end(),0); 

	std::queue<position> positions;
	for(int x=0; x<map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(gradient_info.match_source(map, x, y))
			{
				gradient[get_pos(x, y)]=2;
				positions.push(position(x, y));
			}
			else if(gradient_info.match_obstacle(map, x, y))
				gradient[get_pos(x, y)]=1;
		}
	}
	while(!positions.empty())
	{
		position p=positions.front();
		positions.pop();

		int left=p.x-1;
		if(left<0)
			left+=map->getW();
		int right=p.x+1;
		if(right>=map->getW())
			right-=map->getW();
		int up=p.y-1;
		if(up<0)
			up+=map->getH();
		int down=p.y+1;
		if(down>=map->getH())
			down-=map->getH();
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


int Gradient::get_height(int posx, int posy) const
{
	return gradient[get_pos(posx, posy)]-2;
}



GradientManager::GradientManager(Map* map) : map(map), cur_update(0), timer(0)
{
}


Gradient& GradientManager::get_gradient(const GradientInfo& gi)
{
	for(std::vector<std::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if(ticks_since_update[i-gradients.begin()]>150)
			{
				ticks_since_update[i-gradients.begin()]=0;
				(*i)->recalculate(map);
			}
			return **i;
		}
	}

	//Did not find a matching gradient
	gradients.push_back(std::shared_ptr<Gradient>(new Gradient(gi)));
	(*(gradients.end()-1))->recalculate(map);
	ticks_since_update.push_back(0);
	return **(gradients.end()-1);
}


void GradientManager::queue_gradient(const GradientInfo& gi)
{
	for(unsigned i=0; i<gradients.size(); ++i)
	{
		if(gradients[i]->get_gradient_info() == gi)
		{
			if(gi.needs_updating())
			{
				queuedGradients.push(i);
			}
			return;
		}
	}
	//Did not find a matching gradient
	gradients.push_back(std::shared_ptr<Gradient>(new Gradient(gi)));
	ticks_since_update.push_back(200);
	queuedGradients.push(gradients.size()-1);
}


bool GradientManager::is_updated(const GradientInfo& gi)
{
	for(std::vector<std::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if(ticks_since_update[i-gradients.begin()]>150 && (*i)->get_gradient_info().needs_updating())
			{
				return false;
			}
			return true;
		}
	}
	//If the gradient hasn't been queued to be updated, consider it updated,
	//and it will be calculated on request
	return true;
}


void GradientManager::update()
{
	timer++;
	std::transform(ticks_since_update.begin(), ticks_since_update.end(), ticks_since_update.begin(), increment);

	if((timer%1)==0 && !queuedGradients.empty())
	{
		int g=queuedGradients.front();
		if(ticks_since_update[g]>50)
		{
			gradients[g]->recalculate(map);
			ticks_since_update[g]=0;
		}
		queuedGradients.pop();
		return;
	}
}


