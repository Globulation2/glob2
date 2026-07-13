// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingType.h"
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



void Gradient::recalculate(Map* map)
{
	width=map->getW();
	gradient.resize(map->getW()*map->getH());
	std::fill(gradient.begin(), gradient.end(),0);

	std::queue<position> positions;
	for(int x=0; x<map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(gradient_info.match_source(map, x, y))
			{
				gradient[get_pos(x, y)]=AI_ECHO_GRADIENT_SOURCE_SEED;
				positions.push(position(x, y));
			}
			else if(gradient_info.match_obstacle(map, x, y))
				gradient[get_pos(x, y)]=AI_ECHO_GRADIENT_OBSTACLE_MARKER;
		}
	}
	expand_bfs(positions);
}


int Gradient::get_height(int posx, int posy) const
{
	// Torus-wrap: callers (e.g. Nicowar farming) query x±1/y±1 neighbours that
	// step off the map edge; unwrapped, y=-1 indexed before the buffer and read
	// uninitialized heap, making farming decisions non-deterministic run-to-run.
	const int height = static_cast<int>(gradient.size()) / width;
	posx = (posx + width) % width;
	posy = (posy + height) % height;
	// Reverses the +SOURCE_SEED offset applied at recalculate(): source tiles
	// (internal value 2) → height 0; obstacles (1) → -1; unreached (0) → -2.
	return gradient[get_pos(posx, posy)]-AI_ECHO_GRADIENT_SOURCE_SEED;
}


bool Gradient::within_dist(int posx, int posy, int max_dist) const
{
	int h = get_height(posx, posy);
	return h >= 0 && h < max_dist;
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
			if(ticks_since_update[i-gradients.begin()]>AI_ECHO_GRADIENT_STALE_TICKS)
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
	ticks_since_update.push_back(AI_ECHO_GRADIENT_INITIAL_AGE_TICKS);
	queuedGradients.push(gradients.size()-1);
}


bool GradientManager::is_updated(const GradientInfo& gi)
{
	for(std::vector<std::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if(ticks_since_update[i-gradients.begin()]>AI_ECHO_GRADIENT_STALE_TICKS && (*i)->get_gradient_info().needs_updating())
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

	// (timer%1)==0 is a tautology — preserved verbatim per audit note L8
	// (bugs_surfaced_during_magic_number_audit.md). Looks like a disabled
	// throttle; do NOT name as a constant or restore an intended period.
	if((timer%1)==0 && !queuedGradients.empty())
	{
		int g=queuedGradients.front();
		if(ticks_since_update[g]>AI_ECHO_GRADIENT_QUEUE_MIN_AGE_TICKS)
		{
			gradients[g]->recalculate(map);
			ticks_since_update[g]=0;
		}
		queuedGradients.pop();
		return;
	}
}


