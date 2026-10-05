// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include <PerformanceTelemetry.h>
#include "shared_runtime/Runtime.h"
#include "Building.h"
#include <queue>
#include <algorithm>
#include "Game.h"
#include "Order.h"
#include "Utilities.h"
#include "Brush.h"
#include "FileFormatVersions.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;
using std::shared_ptr;


void GradientInfo::add_source(Entities::Entity* source)
{
	sources.push_back(std::shared_ptr<Entities::Entity>(source));
}


void GradientInfo::add_obstacle(Entities::Entity* obstacle)
{
	obstacles.push_back(std::shared_ptr<Entities::Entity>(obstacle));
}

GradientInfo GradientInfo::clone() const
{
	GradientInfo copy;
	copy.needs_updated=needs_updated;
    copy.terrainTravel=terrainTravel;
	for(const auto& source : sources)
		copy.sources.push_back(source->clone());
	for(const auto& obstacle : obstacles)
		copy.obstacles.push_back(obstacle->clone());
	return copy;
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
	if(terrainTravel!=rhs.terrainTravel || sources.size()!=rhs.sources.size() || obstacles.size() != rhs.obstacles.size())
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
    const unsigned travel=versionMinor>=FILE_FORMAT_VERSION_TERRAIN_PROPERTIES?stream->readUint8("terrainTravel"):0;
    if(!field::validTerrainTravel(travel)) return false;
    terrainTravel=static_cast<field::TerrainTravel>(travel);

	stream->readEnterSection("sources");
	int size=stream->readCount("size");
	sources.resize(size);
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		sources[n]=std::shared_ptr<Entities::Entity>(Entities::Entity::load_entity(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("obstacles");
	size=stream->readCount("size");
	obstacles.resize(size);
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		obstacles[n]=std::shared_ptr<Entities::Entity>(Entities::Entity::load_entity(stream, player, versionMinor));
		// Old Water obstacles meant non-walkable terrain, not irrigation.
		if (versionMinor<FILE_FORMAT_VERSION_TERRAIN_PROPERTIES && obstacles[n]->get_type()==Entities::EWater)
			obstacles[n]=std::make_shared<Entities::Unwalkable>();
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}



void GradientInfo::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("GradientInfo");
    stream->writeUint8(static_cast<unsigned>(terrainTravel),"terrainTravel");

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



void Gradient::recalculate(Map* map, field::Frontier& frontier)
{
	PERF_SCOPE_TIME(AIGradient);
	width=map->getW();
    terrainGeneration=map->terrainGeneration();
	gradient.resize(map->getW()*map->getH());
	std::fill(gradient.begin(), gradient.end(),0);

	frontier.clear();
	for(int x=0; x<map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(gradient_info.match_source(map, x, y))
			{
				gradient[get_pos(x, y)]=AI_SHARED_RUNTIME_GRADIENT_SOURCE_SEED;
				frontier.push_back(get_pos(x,y));
			}
			else if(gradient_info.match_obstacle(map, x, y) || !field::terrainTravelAllowed(map->terrainPropertiesAt(x,y),gradient_info.terrainTravel))
				gradient[get_pos(x, y)]=AI_SHARED_RUNTIME_GRADIENT_OBSTACLE_MARKER;
		}
	}
    if(gradient_info.terrainTravel!=field::TerrainTravel::Geometric &&
        (gradient_info.terrainTravel==field::TerrainTravel::Fly?map->hasAirTerrainConstraints():map->hasTerrainMovementModifiers()))
    {
		field::expandTerrainTravel(
			gradient, width, map->getH(), gradient_info.terrainTravel,
			[&](std::size_t i) { return map->terrainTypeAt(i); }, map->terrainRegistry());
		frontier.clear();
    }
    else expand_bfs(frontier);
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
	return gradient[get_pos(posx, posy)]-AI_SHARED_RUNTIME_GRADIENT_SOURCE_SEED;
}


bool Gradient::within_dist(int posx, int posy, int max_dist) const
{
	int h = get_height(posx, posy);
	return h >= 0 && h < max_dist;
}



GradientManager::GradientManager(Map* map) : map(map), cur_update(0), timer(0)
{
}

std::unique_ptr<GradientManager> GradientManager::clone() const
{
	auto copy=std::make_unique<GradientManager>(map);
	copy->cur_update=cur_update;
	copy->timer=timer;
	copy->ticks_since_update=ticks_since_update;
	copy->queuedGradients=queuedGradients;
	copy->gradients.reserve(gradients.size());
	for(const auto& gradient : gradients)
	{
		auto field=std::make_shared<Gradient>(gradient->gradient_info.clone());
		field->width=gradient->width;
        field->terrainGeneration=gradient->terrainGeneration;
		field->gradient=gradient->gradient;
		copy->gradients.push_back(std::move(field));
	}
	return copy;
}


Gradient& GradientManager::get_gradient(const GradientInfo& gi)
{
	for(std::vector<std::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if((*i)->terrainGeneration!=map->terrainGeneration() || ticks_since_update[i-gradients.begin()]>AI_SHARED_RUNTIME_GRADIENT_STALE_TICKS)
			{
				ticks_since_update[i-gradients.begin()]=0;
				(*i)->recalculate(map,frontier);
			}
			return **i;
		}
	}

	//Did not find a matching gradient
	gradients.push_back(std::shared_ptr<Gradient>(new Gradient(gi)));
	(*(gradients.end()-1))->recalculate(map,frontier);
	ticks_since_update.push_back(0);
	return **(gradients.end()-1);
}


void GradientManager::queue_gradient(const GradientInfo& gi)
{
	for(unsigned i=0; i<gradients.size(); ++i)
	{
		if(gradients[i]->get_gradient_info() == gi)
		{
			if(gradients[i]->terrainGeneration!=map->terrainGeneration() || gi.needs_updating())
			{
				queuedGradients.push(i);
			}
			return;
		}
	}
	//Did not find a matching gradient
	gradients.push_back(std::shared_ptr<Gradient>(new Gradient(gi)));
	ticks_since_update.push_back(AI_SHARED_RUNTIME_GRADIENT_INITIAL_AGE_TICKS);
	queuedGradients.push(gradients.size()-1);
}


bool GradientManager::is_updated(const GradientInfo& gi)
{
	for(std::vector<std::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if((*i)->terrainGeneration!=map->terrainGeneration() || (ticks_since_update[i-gradients.begin()]>AI_SHARED_RUNTIME_GRADIENT_STALE_TICKS && (*i)->get_gradient_info().needs_updating()))
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
		if(gradients[g]->terrainGeneration!=map->terrainGeneration() || ticks_since_update[g]>AI_SHARED_RUNTIME_GRADIENT_QUEUE_MIN_AGE_TICKS)
		{
			gradients[g]->recalculate(map,frontier);
			ticks_since_update[g]=0;
		}
		queuedGradients.pop();
		return;
	}
}



// These fields are observable scheduling state: recomputing them on load can
// change both placement scores and the tick a pending building becomes ready.
void GradientManager::save(GAGCore::OutputStream* stream)
{
	stream->writeEnterSection("GradientManager");
	stream->writeSint32(timer,"timer");
	stream->writeUint32(cur_update,"curUpdate");
	stream->writeUint32(gradients.size(),"count");
	stream->writeEnterSection("gradients");
	for(size_t i=0;i<gradients.size();++i)
	{
		stream->writeEnterSection(i);
		Gradient& g=*gradients[i];
		g.gradient_info.save(stream);
        stream->writeUint8(g.terrainGeneration==map->terrainGeneration(),"terrainCurrent");
		stream->writeSint32(ticks_since_update[i],"age");
		stream->writeSint32(g.width,"width");
		stream->writeUint32(g.gradient.size(),"size");
		stream->writeEnterSection("values");
		for(size_t j=0;j<g.gradient.size();++j)
			stream->writeSint16(g.gradient[j],std::to_string(j));
		stream->writeLeaveSection();
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	auto pending=queuedGradients;
	stream->writeUint32(pending.size(),"queuedCount");
	stream->writeEnterSection("queued");
	for(size_t i=0;!pending.empty();++i)
	{
		stream->writeUint32(pending.front(),std::to_string(i));
		pending.pop();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}

bool GradientManager::load(GAGCore::InputStream* stream,Player* player,Sint32 versionMinor)
{
	stream->readEnterSection("GradientManager");
	timer=stream->readSint32("timer");
	cur_update=stream->readUint32("curUpdate");
	const Uint32 count=stream->readCount("count");
	stream->readEnterSection("gradients");
	for(Uint32 i=0;i<count;++i)
	{
		if(stream->isEndOfStream()) return false;
		stream->readEnterSection(i);
		GradientInfo info;
		if(!info.load(stream,player,versionMinor)) return false;
		auto g=std::make_shared<Gradient>(info);
        const bool terrainCurrent=versionMinor<FILE_FORMAT_VERSION_TERRAIN_PROPERTIES || stream->readUint8("terrainCurrent");
        g->terrainGeneration=terrainCurrent?map->terrainGeneration():0;
		ticks_since_update.push_back(stream->readSint32("age"));
		g->width=stream->readSint32("width");
		const Uint32 size=stream->readCount("size");
		// A queued gradient can be uncomputed; materialized fields must match
		// the loaded map. Values use explicit endian-safe signed 16-bit IO.
		if(size ? (size!=Uint32(map->getW()*map->getH()) || g->width!=map->getW()) : g->width!=0)
			return false;
		g->gradient.resize(size);
		stream->readEnterSection("values");
		for(Uint32 j=0;j<size;++j)g->gradient[j]=stream->readSint16(std::to_string(j));
		stream->readLeaveSection();
		gradients.push_back(g);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	const Uint32 queued=stream->readCount("queuedCount");
	stream->readEnterSection("queued");
	for(Uint32 i=0;i<queued;++i)
	{
		if(stream->isEndOfStream()) return false;
		const Uint32 index=stream->readUint32(std::to_string(i));
		if(index>=count) return false;
		queuedGradients.push(index);
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}
