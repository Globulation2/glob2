// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include <limits>
#include "BuildingType.h"
#include "shared_runtime/BuildingDemands.h"
#include "FileFormatVersions.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Conditions;


BuildingOrder::BuildingOrder(int building_type, int number_of_workers) : building_type(building_type), number_of_workers(number_of_workers)
{

}



BuildingOrder::BuildingOrder(Runtime& runtime,int demand,int workers) : BuildingOrder(demand,workers)
{
 bind(runtime);
}
unsigned BuildingOrder::input_resource_mask(Runtime& runtime) const
{
 if(concrete_type<0) return 0;
 const auto* placement=runtime.player->game->buildingsTypes.get(concrete_type);
 const auto* completed=placement->isBuildingSite ? runtime.player->game->buildingsTypes.get(placement->nextLevel) : placement;
 const auto& spec=completed->semantics;
 unsigned recurring=0,construction=0;
 for(int resource=0;resource<MAX_NB_RESOURCES;++resource) {
  bool consumes=(spec.feeding.enabled && spec.feeding.cost[resource]>0) || (spec.healing.enabled && spec.healing.cost[resource]>0);
  for(const auto& recipe:spec.production.recipes) consumes|=recipe.enabled && recipe.cost[resource]>0;
  for(const auto& training:spec.training) consumes|=training.enabled && training.cost[resource]>0;
  consumes|=completed->shootingRange>0 && spec.ammunitionMaterial==resource && spec.ammunitionCost>0;
  if(consumes) recurring|=1u<<resource;
  if(placement->semantics.constructionCost[resource]>0) construction|=1u<<resource;
 }
 return recurring ? recurring : construction;
}

void BuildingOrder::add_input_distance_constraints(Runtime& runtime,int defaultWeight,
    std::initializer_list<std::pair<int,int>> resourceWeights,int maximumDistance)
{
    const unsigned inputs=input_resource_mask(runtime);
    for(int resource=0;resource<MAX_NB_RESOURCES;++resource) if(inputs&(1u<<resource)) {
        int weight=defaultWeight;
        for(const auto& [selected,preference]:resourceWeights) if(selected==resource) weight=preference;
        GradientInfo gradient;gradient.add_source(new Entities::MaterialSource(resource));
        add_constraint(new MinimizedDistance(gradient,weight));
        if(maximumDistance>=0) add_constraint(new MaximumDistance(gradient,maximumDistance));
    }
}

bool BuildingOrder::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingOrder");

	building_type=stream->readUint32("building_type");
 if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) {
  concrete_type=player ? importLegacyBuildingId(*player->game,building_type,0,true) : -1;
  building_type=importLegacyBuildingDemand(building_type);
 } else concrete_type=stream->readSint32("concrete_type");
 if(building_type<0 || building_type>=BuildingDemand::Count || concrete_type < -1 || (concrete_type>=0 && player && size_t(concrete_type)>=player->game->buildingsTypes.size())) return false;
	number_of_workers=stream->readUint32("number_of_workers");
	// Saves older than 96 do not carry it; Runtime::load registers a fresh one.
	if (versionMinor>=96)
		id=static_cast<int>(stream->readUint32("id"));

	stream->readEnterSection("constraints");
	Uint32 size = stream->readCount("size");
	constraints.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		constraints[x] = std::shared_ptr<Constraint>(Constraint::load_constraint(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	stream->readEnterSection("conditions");
	size = stream->readCount("size");
	conditions.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		conditions[x] = std::shared_ptr<Condition>(Condition::load_condition(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void BuildingOrder::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingOrder");

	stream->writeUint32(building_type, "building_type");
 stream->writeSint32(concrete_type,"concrete_type");
	stream->writeUint32(number_of_workers, "number_of_workers");
	stream->writeUint32(static_cast<Uint32>(id), "id");

	stream->writeEnterSection("constraints");
	stream->writeUint32(constraints.size(), "size");
	for(unsigned x=0; x<constraints.size(); ++x)
	{
		stream->writeEnterSection(x);
		Constraint::save_constraint(constraints[x].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("conditions");
	stream->writeUint32(conditions.size(), "size");
	for(unsigned x=0; x<conditions.size(); ++x)
	{
		stream->writeEnterSection(x);
		Condition::save_condition(conditions[x].get(), stream);
		stream->writeLeaveSection();
	}

	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void BuildingOrder::add_constraint(Constraint* constraint)
{
	constraints.push_back(std::shared_ptr<Constraint>(constraint));
}


void BuildingOrder::add_condition(Condition* condition)
{
	conditions.push_back(std::shared_ptr<Condition>(condition));
}



bool BuildingOrder::bind(Runtime& runtime)
{
 auto& game=*runtime.player->game;
 const auto& index=game.buildingCapabilities();
 const auto intent=buildingIntent(building_type);
 if(!AIPlanning::BuildingCapabilityIndex::allowed(intent,game.gameHeader)) return false;
 if(concrete_type>=0) {
  const auto* type=game.buildingsTypes.get(concrete_type);
  return index.available({concrete_type,type->isBuildingSite ? type->nextLevel : concrete_type},intent,game.gameHeader);
 }
 int count=0;
 for(const auto& candidate:index.placements(intent))
  if(index.available(candidate,intent,game.gameHeader) && runtime.random()%++count==0) concrete_type=candidate.placementType;
 return concrete_type>=0;
}

position BuildingOrder::find_location(Runtime& runtime, Map* map, GradientManager& manager)
{
	position best(-1,-1);
	Player* player=runtime.player;
	int best_score=std::numeric_limits<int>::min();
 if(!bind(runtime)) return position(-1,-1);
 const auto* type=runtime.player->game->buildingsTypes.get(concrete_type);
 const bool check_flag=!type->semantics.occupiesGround;

	for(int x=0; x<map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(!runtime.player->game->checkRoomForBuilding(x,y,type,runtime.player->team->teamNumber))
				continue;

			if(check_flag && runtime.get_flag_map().get_flag(x, y)!=NOGBID)
				continue;
			int score=0;
			bool passes=true;
			for(std::vector<std::shared_ptr<Constraint> >::iterator i=constraints.begin(); i!=constraints.end(); ++i)
			{
				if ((*i)->applies_to_origin())
					passes=(*i)->passes_constraint(runtime,x,y);
				else
				for(int x2=0; x2<type->width && passes; ++x2)
					for(int y2=0; y2<type->height && passes; ++y2)
						if((x2==0 || y2==0 || x2==type->width-1 || y2==type->height-1))
						{
							if(!(*i)->passes_constraint(runtime, map->normalizeX(x+x2), map->normalizeY(y+y2)))
							{
									passes=false;
							}
						}
				if(!passes)
				{
					break;
				}

				if(!check_flag && (!map->isMapDiscovered(x, y, player->team->allies) ||
				   !map->isMapDiscovered(x+type->width-1, y+type->height-1, player->team->allies))
				    )
				{
					passes=false;
					break;
				}
				score+=(*i)->calculate_constraint(runtime, map->normalizeX(x), map->normalizeY(y));
				score+=(*i)->calculate_constraint(runtime, map->normalizeX(x+type->width-1), map->normalizeY(y+type->height-1));
				score+=(*i)->calculate_constraint(runtime, map->normalizeX(x), map->normalizeY(y+type->height-1));
				score+=(*i)->calculate_constraint(runtime, map->normalizeX(x+type->width-1), map->normalizeY(y));
			}
			if(!passes)
				continue;
			if(score>best_score)
			{
				best=position(x, y);
				best_score=score;
			}
		}
	}

	return best;
}



tribool BuildingOrder::passes_conditions(Runtime& runtime)
{
	for(unsigned int i=0; i<conditions.size(); ++i)
	{
		tribool passes=conditions[i]->passes(runtime);
		if(passes)
			continue;
		else if(!passes)
			return false;
		else
			return indeterminate;

	}

	for(unsigned n=0; n<constraints.size(); ++n)
	{
		if(constraints[n]->get_gradient_info())
		{
			bool is_updated=runtime.get_gradient_manager().is_updated(*constraints[n]->get_gradient_info());
			if(!is_updated)
				return false;
		}
	}

	return true;
}



void BuildingOrder::queue_gradients(Gradients::GradientManager& manager)
{
	for(unsigned n=0; n<constraints.size(); ++n)
	{
		if(constraints[n]->get_gradient_info())
		{
			manager.queue_gradient(*constraints[n]->get_gradient_info());
		}
	}
}
