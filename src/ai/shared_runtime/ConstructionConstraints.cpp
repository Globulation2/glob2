// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Building.h"
#include "Game.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Construction;


MinimumDistance::MinimumDistance(const Gradients::GradientInfo& gi, int distance) : gi(gi), gradient_cache(NULL), distance(distance)
{

}


int MinimumDistance::calculate_constraint(Runtime& runtime, int x, int y)
{
	return 0;
}


bool MinimumDistance::passes_constraint(Runtime& runtime, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&runtime.get_gradient_manager().get_gradient(gi);
	int height=gradient_cache->get_height(x, y);
	if(height==AI_SHARED_RUNTIME_GRADIENT_HEIGHT_UNREACHED)
		return false;
	if(height>=distance)
		return true;
	return false;
}


ConstraintType MinimumDistance::get_type()
{
	return CTMinimumDistance;
}



bool MinimumDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MinimumDistance");
	distance = stream->readSint32("distance");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MinimumDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MinimumDistance");
	stream->writeSint32(distance, "distance");
	gi.save(stream);
	stream->writeLeaveSection();
}



MaximumDistance::MaximumDistance(const Gradients::GradientInfo& gi, int distance) : gi(gi), gradient_cache(NULL), distance(distance)
{

}


int MaximumDistance::calculate_constraint(Runtime& runtime, int x, int y)
{
	return 0;
}


bool MaximumDistance::passes_constraint(Runtime& runtime, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&runtime.get_gradient_manager().get_gradient(gi);
	int height=gradient_cache->get_height(x, y);
	if(height==AI_SHARED_RUNTIME_GRADIENT_HEIGHT_UNREACHED)
		return false;
	if(height<=distance)
		return true;
	return false;
}


ConstraintType MaximumDistance::get_type()
{
	return CTMaximumDistance;
}



bool MaximumDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MaximumDistance");
	distance = stream->readSint32("distance");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MaximumDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MaximumDistance");
	stream->writeSint32(distance, "distance");
	gi.save(stream);
	stream->writeLeaveSection();
}



MinimizedDistance::MinimizedDistance(const Gradients::GradientInfo& gi, int weight) : gi(gi), gradient_cache(NULL), weight(weight)
{

}


int MinimizedDistance::calculate_constraint(Runtime& runtime, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&runtime.get_gradient_manager().get_gradient(gi);
	return -(gradient_cache->get_height(x, y) * weight);
}


bool MinimizedDistance::passes_constraint(Runtime& runtime, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&runtime.get_gradient_manager().get_gradient(gi);
	return gradient_cache->get_height(x, y)!=AI_SHARED_RUNTIME_GRADIENT_HEIGHT_UNREACHED;
}


ConstraintType MinimizedDistance::get_type()
{
	return CTMinimizedDistance;
}



bool MinimizedDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MinimizedDistance");
	weight = stream->readSint32("weight");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MinimizedDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MinimizedDistance");
	stream->writeSint32(weight, "weight");
	gi.save(stream);
	stream->writeLeaveSection();
}



MaximizedDistance::MaximizedDistance(const Gradients::GradientInfo& gi, int weight) : gi(gi), gradient_cache(NULL), weight(weight)
{

}


int MaximizedDistance::calculate_constraint(Runtime& runtime, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&runtime.get_gradient_manager().get_gradient(gi);
	return gradient_cache->get_height(x, y) * weight;
}


bool MaximizedDistance::passes_constraint(Runtime& runtime, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&runtime.get_gradient_manager().get_gradient(gi);
	return gradient_cache->get_height(x, y)!=AI_SHARED_RUNTIME_GRADIENT_HEIGHT_UNREACHED;
}


ConstraintType MaximizedDistance::get_type()
{
	return CTMaximizedDistance;
}



bool MaximizedDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MaximizedDistance");
	weight = stream->readSint32("weight");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MaximizedDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MaximizedDistance");
	stream->writeSint32(weight, "weight");
	gi.save(stream);
	stream->writeLeaveSection();
}



CenterOfBuilding::CenterOfBuilding(int gbid) : gbid(gbid)
{

}



int CenterOfBuilding::calculate_constraint(Runtime& runtime, int x, int y)
{
	return 0;
}



bool CenterOfBuilding::passes_constraint(Runtime& runtime, int x, int y)
{
	const AIEngine::BuildingView* b=runtime.observation().buildingSlots(Building::GIDtoTeam(gbid))[Building::GIDtoID(gbid)];
	if(b)
	{
		if(runtime.observation().normalizeX(b->posX+AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).width/2)==x &&
		   runtime.observation().normalizeY(b->posY+AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).height/2)==y)
		{
			return true;
		}
	}
	return false;
}


ConstraintType CenterOfBuilding::get_type()
{
	return CTCenterOfBuilding;
}



bool CenterOfBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("CenterOfBuilding");
	gbid = stream->readSint32("gbid");
	stream->readLeaveSection();
	return true;
}



void CenterOfBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("CenterOfBuilding");
	stream->writeSint32(gbid, "gbid");
	stream->writeLeaveSection();
}



SinglePosition::SinglePosition(int posx, int posy) : posx(posx), posy(posy)
{

}



int SinglePosition::calculate_constraint(Runtime& runtime, int x, int y)
{
	return 0;
}



bool SinglePosition::passes_constraint(Runtime& runtime, int x, int y)
{
	if(runtime.observation().normalizeX(posx)==x && runtime.observation().normalizeY(posy)==y)
		return true;
	return false;
}



ConstraintType SinglePosition::get_type()
{
	return CTSinglePosition;
}



bool SinglePosition::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("SinglePosition");
	posx = stream->readSint32("posx");
	posy = stream->readSint32("posy");
	stream->readLeaveSection();
	return true;
}



void SinglePosition::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("SinglePosition");
	stream->writeSint32(posx, "posx");
	stream->writeSint32(posy, "posy");
	stream->writeLeaveSection();
}
