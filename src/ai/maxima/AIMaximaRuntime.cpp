#include "PowerOfTwo.h"
#include "Material.h"
#include "AIStateSerialization.h"
#include "field/UniformTraversal.h"
#include "FileFormatVersions.h"
#include <PerformanceTelemetry.h>
#include "AITelemetryFields.h"
#include "AIMaximaRuntime.h"
#include "AIMaximaContinuation.h"
#include "AIMaximaStrategy.h"

#include "Brush.h"
#include "ai/observation/ObservationAreaOrders.h"
#include "Building.h"
#include "Game.h"
#include "AIRuleOrders.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h" // Pre-catalog save import only
#include "AIMaximaBuildings.h"
#include "Unit.h"
#include <Stream.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <iostream>
#include <sstream>
#include <typeinfo>

using std::shared_ptr;

namespace AIMaximaRuntime
{

bool telemetry_enabled()
{
	return AIMaxima::StrategyResolver::telemetryEnabled();
}
namespace
{

    const AIEngine::BuildingView* building_from_gid(const AIEngine::AIWorldView& world,int gid)
    {return world.buildingAtSlot(gid);}
    bool visible_to(const AIEngine::AIWorldView& world,unsigned observerTeam,const AIEngine::BuildingView* building)
    {return building && (building->team==int(observerTeam) || (building->seenByMask&world.teams[observerTeam].mask));}

	Conditions::Result wait_for_building(Context& context, int id)
	{
		if(context.get_building_register().is_building_found(id))
			return Conditions::Ready;
		if(context.get_building_register().is_building_pending(id))
			return Conditions::Waiting;
		return Conditions::Impossible;
	}
}

namespace Gradients
{
namespace Entities
{
Entity* Entity::load(GAGCore::InputStream* stream)
{
	const Uint8 kind=stream->readUint8("type");
	switch(kind)
	{
		case EBuilding:
		{
			const int buildingType=stream->readSint32("building_type");
			const int team=stream->readSint32("team");
			const bool includeConstruction=stream->readUint8("include_construction");
			return new Building(buildingType, team, includeConstruction);
		}
		case EAnyTeamBuilding:
		{
			const int team=stream->readSint32("team");
			const bool includeConstruction=stream->readUint8("include_construction");
			return new AnyTeamBuilding(team, includeConstruction);
		}
		case EMaterialSource: return new MaterialSource(stream->readSint32("resource_type"));
		case EAnyResource: return new AnyResource;
        case EResourceGroundObstacle: return new ResourceGroundObstacle;
		case EWater: return new Water;
		case EUnwalkable: return new Unwalkable;
		case EPosition:
		{
			const int x=stream->readSint32("x");
			const int y=stream->readSint32("y");
			return new Position(x, y);
		}
		case ESand: return new Sand;
	}
	throw std::runtime_error("Unknown saved gradient entity");
}

Building::Building(int buildingType, int team, bool includeConstruction)
	: buildingType(buildingType), team(team), includeConstruction(includeConstruction) {}

bool Building::matches(const AIEngine::AIWorldView& world,unsigned observerTeam, int x, int y) const
{
	const int gid=world.occupancyAt(world.tileIndex(x,y)).building;
	const AIEngine::BuildingView* building=building_from_gid(world,gid);
	if(!building || ::Building::GIDtoTeam(gid)!=team || !visible_to(world,observerTeam,building)
		|| (!includeConstruction && building->constructionResultState!=::Building::NO_CONSTRUCTION)) return false;
	using I=AIPlanning::BuildingIntent;
	constexpr auto bit=[](I intent){return std::uint64_t(1)<<static_cast<unsigned>(intent);};
	static constexpr std::uint64_t demands[]={
		bit(I::ProduceWorker)|bit(I::ProduceExplorer)|bit(I::ProduceWarrior),
		bit(I::Feed),bit(I::Heal),bit(I::TrainWalk),bit(I::TrainSwim),
		bit(I::TrainAttackSpeed)|bit(I::TrainAttackStrength),bit(I::TrainConstruction),
		bit(I::ProjectileDefense),bit(I::AttractExplorers),bit(I::AttractWarriors),
		bit(I::AttractWorkers),0,bit(I::ExchangeResources)};
	if(buildingType<0 || size_t(buildingType)>=std::size(demands))return false;
	const int completed=AIEngine::ObservationQueries::buildingType(world,*building).isBuildingSite ? AIEngine::ObservationQueries::buildingType(world,*building).nextLevel : building->typeNum;
	return (world.capabilities().intentMask(completed)&demands[buildingType])!=0;
}

bool Building::equals(const Entity& other) const
{
	const Building* rhs=dynamic_cast<const Building*>(&other);
	return rhs && rhs->buildingType==buildingType && rhs->team==team
		&& rhs->includeConstruction==includeConstruction;
}
void Building::save(GAGCore::OutputStream* stream) const
{stream->writeUint8(type(),"type");stream->writeSint32(buildingType,"building_type");stream->writeSint32(team,"team");stream->writeUint8(includeConstruction,"include_construction");}

AnyTeamBuilding::AnyTeamBuilding(int team, bool includeConstruction)
	: team(team), includeConstruction(includeConstruction) {}

bool AnyTeamBuilding::matches(const AIEngine::AIWorldView& world,unsigned observerTeam, int x, int y) const
{
	const int gid=world.occupancyAt(world.tileIndex(x,y)).building;
	const AIEngine::BuildingView* building=building_from_gid(world,gid);
	return building && ::Building::GIDtoTeam(gid)==team
		&& visible_to(world,observerTeam,building)
		&& (includeConstruction
			|| building->constructionResultState==::Building::NO_CONSTRUCTION);
}

bool AnyTeamBuilding::equals(const Entity& other) const
{
	const AnyTeamBuilding* rhs=dynamic_cast<const AnyTeamBuilding*>(&other);
	return rhs && rhs->team==team
		&& rhs->includeConstruction==includeConstruction;
}
void AnyTeamBuilding::save(GAGCore::OutputStream* stream) const
{stream->writeUint8(type(),"type");stream->writeSint32(team,"team");stream->writeUint8(includeConstruction,"include_construction");}

MaterialSource::MaterialSource(int material) : material(material) {}
bool MaterialSource::matches(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{ return MapState::hasMaterialSlot(world.state(),world.tileIndex(x,y),material); }
bool MaterialSource::equals(const Entity& other) const
{
	const MaterialSource* rhs=dynamic_cast<const MaterialSource*>(&other);
	return rhs && rhs->material==material;
}
bool MaterialSource::can_change() const
{ return true; }
void MaterialSource::save(GAGCore::OutputStream* stream) const
{stream->writeUint8(type(),"type");stream->writeSint32(material,"resource_type");}

bool ResourceGroundObstacle::matches(const AIEngine::AIWorldView& world,unsigned,int x,int y) const
{ return MapState::resourceBlocksGround(world.state(),world.tileIndex(x,y)); }
bool ResourceGroundObstacle::equals(const Entity& other) const
{ return dynamic_cast<const ResourceGroundObstacle*>(&other)!=nullptr; }
void ResourceGroundObstacle::save(GAGCore::OutputStream* stream) const
{ stream->writeUint8(type(),"type"); }

bool AnyResource::matches(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{ return (world.resourceAt(world.tileIndex(x,y)).resource.type!=NO_RES_TYPE); }
bool AnyResource::equals(const Entity& other) const
{ return dynamic_cast<const AnyResource*>(&other)!=NULL; }
void AnyResource::save(GAGCore::OutputStream* stream) const{stream->writeUint8(type(),"type");}

bool Water::matches(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{ return terrainProvidesFertility(AIEngine::ObservationQueries::terrain(world,x,y)); }
bool Water::equals(const Entity& other) const
{ return other.type()==EWater; }
void Water::save(GAGCore::OutputStream* stream) const{stream->writeUint8(type(),"type");}

bool Unwalkable::matches(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{ return !AIEngine::ObservationQueries::terrain(world,x,y).walkable; }
bool Unwalkable::equals(const Entity& other) const
{ return dynamic_cast<const Unwalkable*>(&other)!=nullptr; }
void Unwalkable::save(GAGCore::OutputStream* stream) const
{ stream->writeUint8(type(),"type"); }

Position::Position(int x,int y) : x(x),y(y) {}
bool Position::matches(const AIEngine::AIWorldView&,unsigned,int px,int py) const { return px==x && py==y; }
bool Position::equals(const Entity& other) const
{
	const Position* rhs=dynamic_cast<const Position*>(&other);
	return rhs && rhs->x==x && rhs->y==y;
}
void Position::save(GAGCore::OutputStream* stream) const
{stream->writeUint8(type(),"type");stream->writeSint32(x,"x");stream->writeSint32(y,"y");}

bool Sand::matches(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{ return AIEngine::ObservationQueries::terrain(world,x,y).inhibitionQ8 != 0; }
bool Sand::equals(const Entity& other) const
{ return dynamic_cast<const Sand*>(&other)!=NULL; }
void Sand::save(GAGCore::OutputStream* stream) const{stream->writeUint8(type(),"type");}
}

void GradientInfo::add_source(Entities::Entity* source)
{ sources.push_back(shared_ptr<Entities::Entity>(source)); }
void GradientInfo::add_obstacle(Entities::Entity* obstacle)
{ obstacles.push_back(shared_ptr<Entities::Entity>(obstacle)); }
bool GradientInfo::matches_source(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{
	for(size_t i=0;i<sources.size();++i)
		if(sources[i]->matches(world,observerTeam,x,y)) return true;
	return false;
}
bool GradientInfo::matches_obstacle(const AIEngine::AIWorldView& world,unsigned observerTeam,int x,int y) const
{
	for(size_t i=0;i<obstacles.size();++i)
		if(obstacles[i]->matches(world,observerTeam,x,y)) return true;
	return false;
}
bool GradientInfo::needs_updating() const
{
	for(size_t i=0;i<sources.size();++i) if(sources[i]->can_change()) return true;
	for(size_t i=0;i<obstacles.size();++i) if(obstacles[i]->can_change()) return true;
	return false;
}
bool GradientInfo::operator==(const GradientInfo& rhs) const
{
	if(terrainTravel!=rhs.terrainTravel || sources.size()!=rhs.sources.size() || obstacles.size()!=rhs.obstacles.size()) return false;
	for(size_t i=0;i<sources.size();++i) if(!sources[i]->equals(*rhs.sources[i])) return false;
	for(size_t i=0;i<obstacles.size();++i) if(!obstacles[i]->equals(*rhs.obstacles[i])) return false;
	return true;
}
void GradientInfo::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GradientInfo");
    stream->writeUint8(static_cast<unsigned>(terrainTravel),"terrainTravel");
	stream->writeUint32(sources.size(),"source_count");
	for(size_t i=0;i<sources.size();++i){stream->writeEnterSection(i);sources[i]->save(stream);stream->writeLeaveSection();}
	stream->writeUint32(obstacles.size(),"obstacle_count");
	for(size_t i=0;i<obstacles.size();++i){stream->writeEnterSection(i+sources.size());obstacles[i]->save(stream);stream->writeLeaveSection();}
	stream->writeLeaveSection();
}
bool GradientInfo::load(GAGCore::InputStream* stream,Sint32 versionMinor)
{
	sources.clear();obstacles.clear();stream->readEnterSection("GradientInfo");
    const unsigned travel=versionMinor>=FILE_FORMAT_VERSION_TERRAIN_PROPERTIES?stream->readUint8("terrainTravel"):0;
    if(!field::validTerrainTravel(travel)) throw std::runtime_error("Invalid AI terrain travel mode");
    terrainTravel=static_cast<field::TerrainTravel>(travel);
	Uint32 count=stream->readCount("source_count");
	for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);sources.push_back(shared_ptr<Entities::Entity>(Entities::Entity::load(stream)));stream->readLeaveSection();}
	const Uint32 sourceCount=count;count=stream->readCount("obstacle_count");
	for(Uint32 i=0;i<count;++i){stream->readEnterSection(i+sourceCount);obstacles.push_back(shared_ptr<Entities::Entity>(Entities::Entity::load(stream)));stream->readLeaveSection();}
	for(auto& obstacle:obstacles) if(versionMinor<FILE_FORMAT_VERSION_TERRAIN_PROPERTIES && obstacle->type()==Entities::EWater)
		obstacle=std::make_shared<Entities::Unwalkable>();
	stream->readLeaveSection();return true;
}

Gradient::Gradient(const GradientInfo& info) : info(info),width(0),sourceCount(0) {}

void Gradient::recalculate(const AIEngine::AIWorldView& world,unsigned observerTeam, field::Frontier& frontier)
{
	const AIEngine::AIWorldView* map=&world;
	width=world.width;
    terrainGeneration=world.terrainRevision;
	const int height=world.height;
	values.assign(width*height,UnreachableCell);
	sourceCount=0;
	auto& queue=frontier;
	queue.clear();
    // Compile the profiled resource-obstruction predicate once per field.
    // All other entities retain their original matcher; source and obstacle
    // precedence, scan order, invalidation and serialization remain unchanged.
    struct PreparedMatches
    {
        const std::vector<std::shared_ptr<Entities::Entity>>& original;
        bool groundResources=false;
        std::vector<const Entities::Entity*> other;
        explicit PreparedMatches(const std::vector<std::shared_ptr<Entities::Entity>>& entities):original(entities)
        {
            for(const auto& entity:entities)
                groundResources|=entity->type()==Entities::EResourceGroundObstacle;
            if(!groundResources) return;
            other.reserve(entities.size());
            for(const auto& entity:entities)
                if(entity->type()!=Entities::EResourceGroundObstacle) other.push_back(entity.get());
        }
        bool matches(const AIEngine::AIWorldView& world,unsigned team,int x,int y,bool blocksGround) const
        {
            if(!groundResources)
            {
                for(const auto& entity:original) if(entity->matches(world,team,x,y)) return true;
                return false;
            }
            if(blocksGround) return true;
            for(const auto* entity:other) if(entity->matches(world,team,x,y)) return true;
            return false;
        }
    };
    const PreparedMatches sources(info.sources),obstacles(info.obstacles);
    const bool needsGround=sources.groundResources || obstacles.groundResources;
	for(int x=0;x<width;++x)
		for(int y=0;y<height;++y)
		{
			const int at=y*width+x;
            const bool blocked=needsGround && MapState::resourceBlocksGround(world.state(),at);
			if(sources.matches(world,observerTeam,x,y,blocked)) { values[at]=SourceCell; queue.push_back(at);++sourceCount; }
			else if(obstacles.matches(world,observerTeam,x,y,blocked) || !field::terrainTravelAllowed(AIEngine::ObservationQueries::terrain(world,x,y),info.terrainTravel)) values[at]=ObstacleCell;
		}
	if(info.terrainTravel!=field::TerrainTravel::Geometric &&
        (info.terrainTravel==field::TerrainTravel::Fly?world.airTerrainConstraints:world.terrainMovementModifiers))
    {
		field::expandTerrainTravel(
			values, width, height, info.terrainTravel,
			[&](std::size_t i) { return map->cellRuleAt(i); }, *map->cellRules);
		queue.clear();
    }
    else field::expandDistances(values,queue,{width,height},field::Surrounding,UnreachableCell);
}

int Gradient::get_height(int x,int y) const
{
	if(width<=0 || values.empty()) return -2;
	if(x<0 || x>=width) x=powerOfTwoRemainder(powerOfTwoRemainder(x, width)+width, width);
	if(y<0 || size_t(y)*width>=values.size())
	{
		const int height=values.size()/width;
		y=powerOfTwoRemainder(powerOfTwoRemainder(y, height)+height, height);
	}
	return values[y*width+x]-SourceCell;
}

GradientManager::GradientManager(Player* player)
	: binding(player),lastWorldStep(static_cast<Uint32>(-1)) {}
int GradientManager::find(const GradientInfo& info) const
{
	for(size_t i=0;i<gradients.size();++i) if(gradients[i]->info==info) return int(i);
	return -1;
}
Gradient& GradientManager::get_gradient(const GradientInfo& info)
{
	int index=find(info);
	if(index<0)
	{
		gradients.push_back(shared_ptr<Gradient>(new Gradient(info)));
		ages.push_back(0); index=int(gradients.size())-1;
		gradients[index]->recalculate(observation(),teamNumber(),frontier);
	}
	else if(gradients[index]->terrainGeneration!=observation().terrainRevision)
    { gradients[index]->recalculate(observation(),teamNumber(),frontier); ages[index]=0; }
    else if(ages[index]>150 && info.needs_updating())
	{
		if(queuedIndexes.insert(index).second)
			queued.push(index);
	}
	return *gradients[index];
}
void GradientManager::queue_gradient(const GradientInfo& info)
{
	int index=find(info);
	if(index<0)
	{
		gradients.push_back(shared_ptr<Gradient>(new Gradient(info)));
		ages.push_back(200); index=int(gradients.size())-1;
	}
	if((gradients[index]->terrainGeneration!=observation().terrainRevision || info.needs_updating() || ages[index]>150)
	   && queuedIndexes.insert(index).second)
		queued.push(index);
}
bool GradientManager::is_updated(const GradientInfo& info) const
{
	const int index=find(info);
	return index<0 || (gradients[index]->terrainGeneration==observation().terrainRevision && (!info.needs_updating() || ages[index]<=150));
}
void GradientManager::update(Uint32 step)
{
    lastTerrainRevision=observation().terrainRevision;
	PERF_SCOPE_TIME(AIGradient);
	if(lastWorldStep==step) return;
	lastWorldStep=step;
	for(size_t i=0;i<ages.size();++i) ++ages[i];
	if(!queued.empty())
	{
		const int index=queued.front(); queued.pop();queuedIndexes.erase(index);
		if(index>=0 && index<int(gradients.size()) && (gradients[index]->terrainGeneration!=observation().terrainRevision || ages[index]>50))
		{ gradients[index]->recalculate(observation(),teamNumber(),frontier); ages[index]=0; }
	}
}
void GradientManager::saveExecutionState(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("GradientExecution95");
    AIMaximaContinuation::Writer archive(stream,true);
    archive("lastWorldStep",lastWorldStep);
    archive("ages",ages);
    stream->writeUint32(gradients.size(),"size");
    for(size_t i=0;i<gradients.size();++i)
    {
        stream->writeEnterSection(i);
        const Gradient& gradient=*gradients[i];
        gradient.info.save(stream);
        stream->writeUint8(gradient.terrainGeneration==binding.getOwner()->map->terrainGeneration(),"terrainCurrent");
        archive("width",gradient.width);
        archive("sourceCount",gradient.sourceCount);
        archive("values",gradient.values);
        stream->writeLeaveSection();
    }
    std::vector<int> pending;
    std::queue<int> copy=queued;
    while(!copy.empty()) { pending.push_back(copy.front()); copy.pop(); }
    archive("queued",pending);
    stream->writeLeaveSection();
}
void GradientManager::loadExecutionState(GAGCore::InputStream* stream,Sint32 versionMinor)
{
    invalidate();
    stream->readEnterSection("GradientExecution95");
    AIMaximaContinuation::Reader archive(stream,versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE,versionMinor);
    archive("lastWorldStep",lastWorldStep);
    archive("ages",ages);
    const Uint32 size=stream->readCount("size");
    if(size!=ages.size()) throw std::runtime_error("Invalid gradient continuation count");
    for(Uint32 i=0;i<size;++i)
    {
        stream->readEnterSection(i);
        GradientInfo info;
        if(!info.load(stream,versionMinor)) throw std::runtime_error("Invalid gradient continuation source");
        shared_ptr<Gradient> gradient(new Gradient(info));
        const bool terrainCurrent=versionMinor<FILE_FORMAT_VERSION_TERRAIN_PROPERTIES || stream->readUint8("terrainCurrent");
        gradient->terrainGeneration=terrainCurrent?observation().terrainRevision:0;
        archive("width",gradient->width);
        archive("sourceCount",gradient->sourceCount);
        archive("values",gradient->values);
        if(gradient->width<0 || (!gradient->values.empty() &&
           (gradient->width==0 || gradient->values.size()%gradient->width!=0)))
            throw std::runtime_error("Invalid gradient continuation dimensions");
        gradients.push_back(gradient);
        stream->readLeaveSection();
    }
    std::vector<int> pending;
    archive("queued",pending);
    for(int index:pending)
    {
        if(index<0 || size_t(index)>=gradients.size() || !queuedIndexes.insert(index).second)
            throw std::runtime_error("Invalid gradient continuation queue");
        queued.push(index);
    }
    stream->readLeaveSection();
}
void GradientManager::invalidate()
{
	gradients.clear(); ages.clear(); while(!queued.empty()) queued.pop();queuedIndexes.clear();
}
}

namespace Construction
{
BuildingRecord::BuildingRecord()
	: x(-1),y(-1),type(-1),gid(NOGBID),age(-1),runtimeIdentity(0),issued(false),upgrading(false),upgradeSeen(false),awaitingUpgradeExecution(false) {}

BuildingRegister::BuildingRegister(Player* player)
    : binding(player),nextId(0) {}
Uint64 BuildingRegister::identity_of(const AIEngine::BuildingView* building) const
{
    return building && ::Building::GIDtoTeam(building->gid)==teamNumber()
        ? building->scriptIdentity : 0;
}
void BuildingRegister::initiate()
{
	pendingBuildings.clear(); foundBuildings.clear(); nextId=0;
	for(int i=0;i<Building::MAX_COUNT;++i)
	{
		const AIEngine::BuildingView* b=observation().buildingSlots(teamNumber())[i]; if(!b) continue;
		BuildingRecord r; r.x=b->posX; r.y=b->posY; r.type=b->typeNum; r.gid=b->gid;
		r.runtimeIdentity=identity_of(b);
		foundBuildings[nextId++]=r;
	}
}
}

namespace Conditions
{
Condition* Condition::load(GAGCore::InputStream* stream)
{
	GAGCore::InputStream::NestedRead nesting(*stream);
	stream->readEnterSection("Condition");
	const int kind=stream->readSint32("type");Condition* result=NULL;
	if(kind==0){const int id=stream->readSint32("id");result=new ParticularBuilding(BuildingCondition::load(stream),id);}
	else if(kind==1)result=new BuildingDestroyed(stream->readSint32("id"));
	else if(kind==2)result=new EnemyBuildingDestroyed(stream->readSint32("gid"));
    else if(kind==4) {const int id=stream->readSint32("id");const unsigned mask=stream->readUint8("unitMask");
        if(id<0 || !mask || (mask&~((1u<<NB_UNIT_TYPE)-1))) throw std::runtime_error("Invalid attraction retirement condition");
        result=new AttractionRetiredOrDestroyed(id,mask);}
	else if(kind==3){std::unique_ptr<Condition> first(Condition::load(stream));std::unique_ptr<Condition> second(Condition::load(stream));result=new EitherCondition(first.release(),second.release());}
	stream->readLeaveSection();if(!result)throw std::runtime_error("Unknown saved AI type");return result;
}
BuildingCondition* BuildingCondition::load(GAGCore::InputStream* stream)
{
	stream->readEnterSection("BuildingCondition");
	const int kind=stream->readSint32("type");BuildingCondition* result=NULL;
	switch(kind){case 0:result=new NotUnderConstruction;break;case 1:result=new UnderConstruction;break;case 2:result=new BeingUpgraded;break;case 3:result=new BeingUpgradedTo(stream->readSint32("value"));break;case 4:result=new SpecificBuildingType(stream->readSint32("value"));break;case 5:result=new BuildingLevel(stream->readSint32("value"));break;case 6:result=new Upgradable;break;case 7:result=new StaffableConstructionSite;break;default:break;}
	stream->readLeaveSection();if(!result)throw std::runtime_error("Unknown saved AI type");return result;
}
ParticularBuilding::ParticularBuilding(BuildingCondition* condition,int id)
	: condition(condition),id(id) {}
Result ParticularBuilding::passes(Context& context) const
{
	if(context.get_building_register().is_building_found(id))
		return condition->passes(context,id)?Ready:Waiting;
	return context.get_building_register().is_building_pending(id)?Waiting:Impossible;
}
void ParticularBuilding::save(GAGCore::OutputStream* stream) const
{stream->writeEnterSection("Condition");stream->writeSint32(type(),"type");stream->writeSint32(id,"id");condition->save(stream);stream->writeLeaveSection();}
Result BuildingDestroyed::passes(Context& context) const
{
	if(context.get_building_register().is_building_found(id)
	   || context.get_building_register().is_building_pending(id)) return Waiting;
	return Ready;
}
void BuildingDestroyed::save(GAGCore::OutputStream* stream) const
{stream->writeEnterSection("Condition");stream->writeSint32(type(),"type");stream->writeSint32(id,"id");stream->writeLeaveSection();}
Result AttractionRetiredOrDestroyed::passes(Context& context) const
{return context.attraction_retired_or_destroyed(id,unitMask)?Ready:Waiting;}
void AttractionRetiredOrDestroyed::save(GAGCore::OutputStream* stream) const
{stream->writeEnterSection("Condition");stream->writeSint32(type(),"type");stream->writeSint32(id,"id");stream->writeUint8(unitMask,"unitMask");stream->writeLeaveSection();}
Result EnemyBuildingDestroyed::passes(Context& context) const
{ return building_from_gid(context.observation(),gid)?Waiting:Ready; }
void EnemyBuildingDestroyed::save(GAGCore::OutputStream* stream) const
{stream->writeEnterSection("Condition");stream->writeSint32(type(),"type");stream->writeSint32(gid,"gid");stream->writeLeaveSection();}
EitherCondition::EitherCondition(Condition* a,Condition* b):first(a),second(b){}
Result EitherCondition::passes(Context& context) const
{
	const Result a=first->passes(context), b=second->passes(context);
	if(a==Ready||b==Ready)return Ready;
	if(a==Impossible&&b==Impossible)return Impossible;
	return Waiting;
}
void EitherCondition::save(GAGCore::OutputStream* stream) const
{stream->writeEnterSection("Condition");stream->writeSint32(type(),"type");first->save(stream);second->save(stream);stream->writeLeaveSection();}
bool NotUnderConstruction::passes(Context& c,int id) const
{ const AIEngine::BuildingView* b=c.get_building_register().get_building(id);return b&&b->constructionResultState==::Building::NO_CONSTRUCTION&&!c.get_building_register().is_building_upgrading(id); }
bool UnderConstruction::passes(Context& c,int id) const
{ const AIEngine::BuildingView* b=c.get_building_register().get_building(id);return b&&b->constructionResultState!=::Building::NO_CONSTRUCTION; }
bool StaffableConstructionSite::passes(Context& c,int id) const
{ const AIEngine::BuildingView* b=c.get_building_register().get_building(id);
  return b&&b->constructionResultState!=::Building::NO_CONSTRUCTION
      &&b->buildingState==::Building::ALIVE; }
bool BeingUpgraded::passes(Context& c,int id) const {return c.get_building_register().is_building_upgrading(id);}
bool BeingUpgradedTo::passes(Context& c,int id) const {return c.get_building_register().is_building_upgrading(id)&&c.get_building_register().get_level(id)==level-1;}
bool SpecificBuildingType::passes(Context& c,int id) const {return c.get_building_register().has_role(id,buildingType);}
bool BuildingLevel::passes(Context& c,int id) const {return c.get_building_register().get_level(id)==level;}
bool Upgradable::passes(Context& c,int id) const
{ const AIEngine::BuildingView* b=c.get_building_register().get_building(id);return b&&!c.observation().configuration->isUnitUpgradesDisabled()&&c.observation().isUpgradeAvailable(*b); }
void NotUnderConstruction::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void UnderConstruction::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void BeingUpgraded::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void BeingUpgradedTo::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeSint32(level,"value");s->writeLeaveSection();}
void SpecificBuildingType::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeSint32(buildingType,"value");s->writeLeaveSection();}
void BuildingLevel::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeSint32(level,"value");s->writeLeaveSection();}
void Upgradable::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void StaffableConstructionSite::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
}

namespace Management
{
using Conditions::Condition;
using Conditions::Result;
using Conditions::Ready;
MaterialTracker::MaterialTracker(Context& context,int id,int length,int material)
	: context(context),record(length,0),position(0),timer(0),buildingId(id),material(material) {}
void MaterialTracker::tick()
{
	++timer;if(timer%10)return;
	const auto* building=context.get_building_register().get_building(buildingId);
	if(!building||record.empty())return;
	int stock=0;
	if(material==RecurringInputStock)
	{
		const auto& semantics=AIEngine::ObservationQueries::buildingType(context.observation(),*building).semantics;
		for(int r=0;r<MaterialSlotCount;++r)
		{
			bool used=(semantics.feeding.enabled&&semantics.feeding.cost[r]>0)
				||(semantics.healing.enabled&&semantics.healing.cost[r]>0)
				||(AIEngine::ObservationQueries::buildingType(context.observation(),*building).shootingRange>0&&semantics.ammunitionMaterial==r&&semantics.ammunitionCost>0);
			for(const auto& recipe:semantics.production.recipes) used|=recipe.enabled&&recipe.cost[r]>0;
			for(const auto& training:semantics.training) used|=training.enabled&&training.cost[r]>0;
			if(used)stock+=context.observation().buildingResources(*building)[r];
		}
	}
	else stock=context.observation().buildingResources(*building)[material];
	record[position]=stock;position=(position+1)%record.size();
}
int MaterialTracker::get_total_level() const
{int total=0;for(size_t i=0;i<record.size();++i)total+=record[i];return total;}
void MaterialTracker::save(GAGCore::OutputStream* stream) const
{
	stream->writeUint32(buildingId,"building_id");stream->writeSint32(material,"resource");stream->writeUint32(position,"position");stream->writeSint32(timer,"timer");stream->writeUint32(record.size(),"size");for(size_t i=0;i<record.size();++i){stream->writeEnterSection(i);stream->writeSint32(record[i],"value");stream->writeLeaveSection();}
}
MaterialTracker* MaterialTracker::load(Context& context,GAGCore::InputStream* stream)
{
	const int id=stream->readUint32("building_id");const int material=stream->readSint32("resource");const Uint32 position=stream->readUint32("position");const int timer=stream->readSint32("timer");const Uint32 size=stream->readCount("size");
	if(material<0 || material>MaterialTracker::RecurringInputStock || !size || position>=size)throw std::runtime_error("Invalid saved resource tracker");
	std::unique_ptr<MaterialTracker> tracker(new MaterialTracker(context,id,size,material));tracker->position=size?position%size:0;tracker->timer=timer;
	for(Uint32 i=0;i<size;++i){stream->readEnterSection(i);tracker->record[i]=stream->readSint32("value");stream->readLeaveSection();}return tracker.release();
}

void ManagementOrder::add_condition(Condition* condition){conditions.push_back(shared_ptr<Condition>(condition));}
Result ManagementOrder::ready(Context& context) const
{
	for(size_t i=0;i<conditions.size();++i){Result r=conditions[i]->passes(context);if(r!=Ready)return r;}return wait(context);
}
void ManagementOrder::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("ManagementOrder");stream->writeSint32(type(),"type");save_payload(stream);stream->writeUint32(conditions.size(),"condition_count");for(size_t i=0;i<conditions.size();++i){stream->writeEnterSection(i);conditions[i]->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
}
ManagementOrder* ManagementOrder::load(GAGCore::InputStream* stream,Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");const int kind=stream->readSint32("type");std::unique_ptr<ManagementOrder> order;
	switch(kind)
	{
		case 0:{const int workers=stream->readSint32("workers");const int id=stream->readSint32("id");order.reset(new AssignWorkers(workers,id));break;}
		case 1:{const int worker=stream->readSint32("worker");const int explorer=stream->readSint32("explorer");const int warrior=stream->readSint32("warrior");order.reset(new ChangeSwarm(worker,explorer,warrior,stream->readSint32("id")));break;}
		case 2:order.reset(new DestroyBuilding(stream->readSint32("id")));break;
		case 3:{const int length=stream->readSint32("length");const int material=stream->readSint32("resource");if(length<=0 || length>1048576 || material<0 || material>MaterialTracker::RecurringInputStock)throw std::runtime_error("Invalid resource tracker order");order.reset(new AddMaterialTracker(length,material,stream->readSint32("id")));break;}
		case 4:{const int size=stream->readSint32("value");order.reset(new ChangeFlagSize(size,stream->readSint32("id")));break;}
		case 5:{const int level=stream->readSint32("value");const int id=stream->readSint32("id");const int targetRole=versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG ? stream->readSint32("target_role") : -1;if(targetRole < -1 || targetRole > 1)throw std::runtime_error("Invalid saved attraction requirement");order.reset(new ChangeFlagMinimumLevel(level,id,targetRole));break;}
		case 6:{const int x=stream->readSint32("x");const int y=stream->readSint32("y");order.reset(new ChangeFlagPosition(x,y,stream->readSint32("id")));break;}
		case 7:case 8:{const int savedArea=stream->readSint32("area_type");if(savedArea<0 || savedArea>FarmArea)throw std::runtime_error("Invalid saved area type");const AreaType area=static_cast<AreaType>(savedArea);const Uint32 count=stream->readCount("location_count");if(kind==7){std::unique_ptr<AddArea> areaOrder(new AddArea(area));for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);const int x=stream->readSint32("x");const int y=stream->readSint32("y");areaOrder->add_location(x,y);stream->readLeaveSection();}order.reset(areaOrder.release());}else{std::unique_ptr<RemoveArea> areaOrder(new RemoveArea(area));for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);const int x=stream->readSint32("x");const int y=stream->readSint32("y");areaOrder->add_location(x,y);stream->readLeaveSection();}order.reset(areaOrder.release());}break;}
		case 9:{const int team=stream->readSint32("team");const OptionalBool allied=static_cast<OptionalBool>(stream->readSint32("allied"));const OptionalBool enemy=static_cast<OptionalBool>(stream->readSint32("enemy"));const OptionalBool market=static_cast<OptionalBool>(stream->readSint32("market"));const OptionalBool inn=static_cast<OptionalBool>(stream->readSint32("inn"));const OptionalBool other=static_cast<OptionalBool>(stream->readSint32("other"));order.reset(new ChangeAlliances(team,allied,enemy,market,inn,other));break;}
		case 10:order.reset(new UpgradeRepair(stream->readSint32("id")));break;
		case 11:{const RuntimeEvent::Type eventType=static_cast<RuntimeEvent::Type>(stream->readSint32("event_type"));const int first=stream->readSint32("first");const int second=stream->readSint32("second");order.reset(new Notify(RuntimeEvent(eventType,first,second)));break;}
		case 12:{const int priority=stream->readSint32("value");order.reset(new ChangePriority(priority,stream->readSint32("id")));break;}
        case 13:{const int id=stream->readSint32("id");const unsigned mask=stream->readUint8("unitMask");
            if(id<0 || !mask || (mask&~((1u<<NB_UNIT_TYPE)-1))) throw std::runtime_error("Invalid attraction retirement order");
            order.reset(new RetireAttraction(id,mask));break;}
		default:break;
	}
	const Uint32 count=stream->readCount("condition_count");for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);Conditions::Condition* condition=Conditions::Condition::load(stream);if(order&&condition)order->add_condition(condition);else delete condition;stream->readLeaveSection();}
	stream->readLeaveSection();if(!order)throw std::runtime_error("Unknown saved AI order");return order.release();
}

AssignWorkers::AssignWorkers(int workers,int id)
	:workers(workers>MAXIMA_MAX_UNIT_WORKING?MAXIMA_MAX_UNIT_WORKING:workers),id(id){}
Result AssignWorkers::wait(Context& c) const{return wait_for_building(c,id);}
void AssignWorkers::modify(Context& c){const AIEngine::BuildingView* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderModifyBuilding(b->gid,std::clamp(workers,0,AIEngine::ObservationQueries::buildingType(c.observation(),*b).semantics.assignmentLimit))));}
void AssignWorkers::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(workers,"workers");s->writeSint32(id,"id");}
ChangeSwarm::ChangeSwarm(int worker,int explorer,int warrior,int id):worker(worker),explorer(explorer),warrior(warrior),id(id){}
Result ChangeSwarm::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeSwarm::modify(Context& c){const AIEngine::BuildingView* b=c.get_building_register().get_building(id);if(b){Sint32 ratios[NB_UNIT_TYPE]={worker,explorer,warrior};for(int u=0;u<NB_UNIT_TYPE;++u)if(!AIEngine::ObservationQueries::buildingType(c.observation(),*b).semantics.production.recipes[u].enabled)ratios[u]=0;c.push_order(shared_ptr<Order>(new OrderModifySwarm(b->gid,ratios)));}}
void ChangeSwarm::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(worker,"worker");s->writeSint32(explorer,"explorer");s->writeSint32(warrior,"warrior");s->writeSint32(id,"id");}
DestroyBuilding::DestroyBuilding(int id):id(id){}
Result DestroyBuilding::wait(Context& c) const{return wait_for_building(c,id);}
void DestroyBuilding::modify(Context& c){const AIEngine::BuildingView* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderDelete(b->gid)));}
void DestroyBuilding::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(id,"id");}
Result RetireAttraction::wait(Context& c) const {return wait_for_building(c,id);}
void RetireAttraction::modify(Context& c)
{
	const auto* building=c.get_building_register().get_building(id);
	if(!building)return;
    const unsigned completedMask=c.complete_attraction_retirement(id,unitMask);
    if(AIPlanning::hasIndependentAttractionUse(AIEngine::ObservationQueries::buildingType(c.observation(),*building),completedMask)) return;
    const auto& semantics=AIEngine::ObservationQueries::buildingType(c.observation(),*building).semantics;
	if(semantics.instantPlacement&&!semantics.occupiesGround)
		c.push_order(std::make_shared<OrderDelete>(building->gid));
	else c.push_order(std::make_shared<OrderModifyBuilding>(building->gid,0));
}
void RetireAttraction::save_payload(GAGCore::OutputStream* s) const {s->writeSint32(id,"id");s->writeUint8(unitMask,"unitMask");}
AddMaterialTracker::AddMaterialTracker(int length,int material,int id):length(length),material(material),id(id){}
Result AddMaterialTracker::wait(Context& c) const{return wait_for_building(c,id);}
void AddMaterialTracker::modify(Context& c){c.add_material_tracker(new MaterialTracker(c,id,length,material),id);}
void AddMaterialTracker::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(length,"length");s->writeSint32(material,"resource");s->writeSint32(id,"id");}
ChangeFlagSize::ChangeFlagSize(int size,int id):size(size),id(id){}
Result ChangeFlagSize::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeFlagSize::modify(Context& c){const AIEngine::BuildingView* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderModifyFlag(b->gid,std::clamp(size,0,AIEngine::ObservationQueries::buildingType(c.observation(),*b).maxUnitStayRange))));}
void ChangeFlagSize::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(size,"value");s->writeSint32(id,"id");}
ChangeFlagMinimumLevel::ChangeFlagMinimumLevel(int level,int id,int targetRole):level(level),id(id),targetRole(targetRole){}
Result ChangeFlagMinimumLevel::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeFlagMinimumLevel::modify(Context& c)
{
	const auto* b=c.get_building_register().get_building(id);if(!b)return;
	const bool explorers=targetRole==1 || (targetRole<0 && AIEngine::ObservationQueries::buildingType(c.observation(),*b).zonable[EXPLORER]
		&& !AIEngine::ObservationQueries::buildingType(c.observation(),*b).zonable[WORKER] && !AIEngine::ObservationQueries::buildingType(c.observation(),*b).zonable[WARRIOR]);
	const int requirement=explorers ? (targetRole<0 ? level>1 : level!=0) : level-1;
	c.push_order(std::make_shared<OrderModifyMinLevelToFlag>(b->gid,requirement,explorers ? 1 : 0));
}
void ChangeFlagMinimumLevel::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(level,"value");s->writeSint32(id,"id");s->writeSint32(targetRole,"target_role");}
ChangeFlagPosition::ChangeFlagPosition(int x,int y,int id):x(x),y(y),id(id){}
Result ChangeFlagPosition::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeFlagPosition::modify(Context& c){const AIEngine::BuildingView* b=c.get_building_register().get_building(id);if(b&&AIEngine::ObservationQueries::buildingType(c.observation(),*b).semantics.relocatable)c.push_order(shared_ptr<Order>(new OrderMoveFlag(b->gid,x,y,true)));}
void ChangeFlagPosition::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(x,"x");s->writeSint32(y,"y");s->writeSint32(id,"id");}

AddArea::AddArea(AreaType type):areaType(type){}
void AddArea::add_location(int x,int y){locations.push_back(position(x,y));}
Result AddArea::wait(Context&) const{return Ready;}
void AddArea::modify(Context& c)
{
	BrushAccumulator acc;for(size_t i=0;i<locations.size();++i)acc.applyBrush(BrushApplication(c.observation().normalizeX(locations[i].x),c.observation().normalizeY(locations[i].y),0),c.observation().width,c.observation().height);if(!acc.getApplicationCount())return;
	if(areaType==ClearingArea)c.push_order(AIEngine::observationAreaOrder<OrderAlterClearArea>(c.teamNumber(),BrushTool::MODE_ADD,acc));
	else if(areaType==ForbiddenArea)c.push_order(AIEngine::observationAreaOrder<OrderAlterForbidden>(c.teamNumber(),BrushTool::MODE_ADD,acc));
	else if(areaType==FarmArea)c.push_order(AIEngine::observationAreaOrder<OrderAlterFarmArea>(c.teamNumber(),BrushTool::MODE_ADD,acc));
	else c.push_order(AIEngine::observationAreaOrder<OrderAlterGuardArea>(c.teamNumber(),BrushTool::MODE_ADD,acc));
}
void AddArea::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(areaType,"area_type");s->writeUint32(locations.size(),"location_count");for(size_t i=0;i<locations.size();++i){s->writeEnterSection(i);s->writeSint32(locations[i].x,"x");s->writeSint32(locations[i].y,"y");s->writeLeaveSection();}}
RemoveArea::RemoveArea(AreaType type):areaType(type){}
void RemoveArea::add_location(int x,int y){locations.push_back(position(x,y));}
Result RemoveArea::wait(Context&) const{return Ready;}
void RemoveArea::modify(Context& c)
{
	BrushAccumulator acc;for(size_t i=0;i<locations.size();++i)acc.applyBrush(BrushApplication(c.observation().normalizeX(locations[i].x),c.observation().normalizeY(locations[i].y),0),c.observation().width,c.observation().height);if(!acc.getApplicationCount())return;
	if(areaType==ClearingArea)c.push_order(AIEngine::observationAreaOrder<OrderAlterClearArea>(c.teamNumber(),BrushTool::MODE_DEL,acc));
	else if(areaType==ForbiddenArea)c.push_order(AIEngine::observationAreaOrder<OrderAlterForbidden>(c.teamNumber(),BrushTool::MODE_DEL,acc));
	else if(areaType==FarmArea)c.push_order(AIEngine::observationAreaOrder<OrderAlterFarmArea>(c.teamNumber(),BrushTool::MODE_DEL,acc));
	else c.push_order(AIEngine::observationAreaOrder<OrderAlterGuardArea>(c.teamNumber(),BrushTool::MODE_DEL,acc));
}
void RemoveArea::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(areaType,"area_type");s->writeUint32(locations.size(),"location_count");for(size_t i=0;i<locations.size();++i){s->writeEnterSection(i);s->writeSint32(locations[i].x,"x");s->writeSint32(locations[i].y,"y");s->writeLeaveSection();}}
ChangeAlliances::ChangeAlliances(int team,OptionalBool allied,OptionalBool enemy,OptionalBool market,OptionalBool inn,OptionalBool other):team(team),allied(allied),enemy(enemy),market(market),inn(inn),other(other){}
Result ChangeAlliances::wait(Context&) const{return Ready;}
void ChangeAlliances::modify(Context& c)
{
	if(team<0 || std::size_t(team)>=c.observation().teams.size())return;
    const auto* t=&c.observation().teams[team];
	struct Bit { static void apply(Uint32& mask,Uint32 bit,OptionalBool value){if(value==KeepValue)return;if(value==SetValue)mask|=bit;else mask&=~bit;} };
	Bit::apply(c.allies,t->mask,allied);Bit::apply(c.enemies,t->mask,enemy);Bit::apply(c.market_view,t->mask,market);Bit::apply(c.inn_view,t->mask,inn);Bit::apply(c.other_view,t->mask,other);
	c.push_order(shared_ptr<Order>(new SetAllianceOrder(c.teamNumber(),c.allies,c.enemies,c.market_view,c.inn_view,c.other_view)));
}
void ChangeAlliances::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(team,"team");s->writeSint32(allied,"allied");s->writeSint32(enemy,"enemy");s->writeSint32(market,"market");s->writeSint32(inn,"inn");s->writeSint32(other,"other");}
ChangePriority::ChangePriority(int priority,int id):priority(priority),id(id){}
Result ChangePriority::wait(Context& c) const{return wait_for_building(c,id);}
void ChangePriority::modify(Context& c){const AIEngine::BuildingView* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderChangePriority(b->gid,priority)));}
void ChangePriority::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(priority,"value");s->writeSint32(id,"id");}
UpgradeRepair::UpgradeRepair(int id):id(id){}
Result UpgradeRepair::wait(Context& c) const{return wait_for_building(c,id);}
void UpgradeRepair::modify(Context& c)
{
	const AIEngine::BuildingView* b=c.get_building_register().get_building(id);
	if(!b || c.get_building_register().is_building_upgrading(id)) return;
	// A restored management request must not register an impossible upgrade
	// after the planner has removed training investments. Damaged repairs remain.
	if(b->hp<b->maxHp) { if(!AIEngine::ObservationQueries::buildingType(c.observation(),*b).semantics.repairable) return; }
	else if(c.observation().configuration->isUnitUpgradesDisabled() || !c.observation().isUpgradeAvailable(*b)) return;
	c.push_order(AIEngine::ObservationQueries::constructionOrder(c.observation(),*b,1,1));
	c.get_building_register().set_upgrading(id,true);
}
void UpgradeRepair::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(id,"id");}
Notify::Notify(const RuntimeEvent& event):event(event){}
Result Notify::wait(Context&) const{return Ready;}
void Notify::modify(Context& c){c.dispatch_event(event);}
void Notify::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(event.type,"event_type");s->writeSint32(event.first,"first");s->writeSint32(event.second,"second");}
}

namespace Construction
{
unsigned BuildingRegister::register_building()
{ pendingBuildings[nextId]=BuildingRecord(); return nextId++; }
void BuildingRegister::issue_order(int id,int x,int y,int type)
{
	BuildingRecord& r=pendingBuildings[id]; r.x=x;r.y=y;r.type=type;r.age=0;r.issued=true;
}
void BuildingRegister::remove_building(int id) { pendingBuildings.erase(id); }
void BuildingRegister::set_upgrading(int id, bool awaitingExecution)
{
    auto found=foundBuildings.find(id);
    if(found==foundBuildings.end()) return;
    found->second.upgrading=true;
    found->second.upgradeSeen=false;
    found->second.awaitingUpgradeExecution=awaitingExecution;
}
void BuildingRegister::order_execution_completed(int gid, bool accepted, std::optional<Uint32> generation)
{
    for(auto& [id,record]:foundBuildings)
        if(record.gid==gid && (!generation || record.runtimeIdentity==*generation) && record.awaitingUpgradeExecution) {
            record.awaitingUpgradeExecution=false;
            if(!accepted) { record.upgrading=false; record.upgradeSeen=false; }
            break;
        }
}

void BuildingRegister::tick()
{
	for(std::map<int,BuildingRecord>::iterator i=pendingBuildings.begin();i!=pendingBuildings.end();)
	{
		BuildingRecord& r=i->second;
		if(r.issued)
		{
			if(++r.age>300) { pendingBuildings.erase(i++); continue; }
			int gid=NOGBID;
			if(const auto* requested=&observation().catalog->at(r.type).resolvedType; requested && !requested->semantics.occupiesGround)
			{
				for(int b=0;b<Building::MAX_COUNT;++b)
				{
					const AIEngine::BuildingView* candidate=observation().buildingSlots(teamNumber())[b];
					if(candidate && candidate->posX==r.x && candidate->posY==r.y
					   && AIMaximaBuildings::lineageRoot(observation(),candidate->typeNum)==AIMaximaBuildings::lineageRoot(observation(),r.type)) { gid=candidate->gid; break; }
				}
			}
			else gid=observation().occupancyAt(observation().tileIndex(r.x,r.y)).building;
			const AIEngine::BuildingView* found=building_from_gid(observation(),gid);
			if(found && Building::GIDtoTeam(gid)==teamNumber()
			   && AIMaximaBuildings::lineageRoot(observation(),found->typeNum)==AIMaximaBuildings::lineageRoot(observation(),r.type))
			{
				r.gid=gid; r.runtimeIdentity=identity_of(found);
				foundBuildings[i->first]=r; pendingBuildings.erase(i++); continue;
			}
		}
		++i;
	}
	for(std::map<int,BuildingRecord>::iterator i=foundBuildings.begin();i!=foundBuildings.end();)
	{
		BuildingRecord& r=i->second;
		const AIEngine::BuildingView* b=get_building(i->first);
		if(!b) { foundBuildings.erase(i++); continue; }
		r.x=b->posX; r.y=b->posY; r.type=b->typeNum;
		if(r.upgrading)
		{
			if(b->constructionResultState!=::Building::NO_CONSTRUCTION) { r.upgradeSeen=true; r.awaitingUpgradeExecution=false; }
			// A delayed command may not have executed yet. The receipt releases
			// this barrier even when an instant repair leaves the type unchanged.
			else if(!r.awaitingUpgradeExecution) { r.upgrading=false; r.upgradeSeen=false; }
		}
		++i;
	}
}
bool BuildingRegister::is_building_pending(unsigned id) const { return pendingBuildings.count(id)!=0; }
bool BuildingRegister::is_building_found(unsigned id) const { return get_building(id)!=NULL; }
bool BuildingRegister::is_building_upgrading(unsigned id) const
{ std::map<int,BuildingRecord>::const_iterator i=foundBuildings.find(id); return i!=foundBuildings.end() && i->second.upgrading; }
const AIEngine::BuildingView* BuildingRegister::get_building(unsigned id) const
{
	std::map<int,BuildingRecord>::const_iterator i=foundBuildings.find(id);
	if(i==foundBuildings.end()) return NULL;
	const AIEngine::BuildingView* building=building_from_gid(observation(),i->second.gid);
	return building && building->buildingState!=::Building::DEAD
		&& identity_of(building)==i->second.runtimeIdentity ? building : NULL;
}
const ::BuildingType* BuildingRegister::get_building_type(unsigned id) const
{ const AIEngine::BuildingView* b=get_building(id); return b?&AIEngine::ObservationQueries::buildingType(observation(),*b):NULL; }
bool BuildingRegister::has_role(unsigned id,int role) const
{ const auto* b=get_building_type(id);return b&&AIMaximaBuildings::serves(observation(),*b,role); }
int BuildingRegister::get_type(unsigned id) const
{ std::map<int,BuildingRecord>::const_iterator i=foundBuildings.find(id); return i==foundBuildings.end()?-1:i->second.type; }
int BuildingRegister::get_level(unsigned id) const { const AIEngine::BuildingView* b=get_building(id); return b?AIMaximaBuildings::lineagePosition(observation(),b->typeNum):0; }
int BuildingRegister::get_assigned(unsigned id) const { const AIEngine::BuildingView* b=get_building(id); return b?b->maxUnitWorking:0; }
int BuildingRegister::get_enrolled(unsigned id) const { const AIEngine::BuildingView* b=get_building(id); return b?static_cast<int>(b->working.count):0; }
int BuildingRegister::get_on_site(unsigned id) const
{
	const AIEngine::BuildingView* b=get_building(id);
	if(!b)return 0;
	const int range=b->unitStayRange+1;
	int result=0;
    for(const auto& identity:observation().workers(*b))
        if(const auto* unit=observation().unit(identity);unit && observation().distanceSquared(b->posX,b->posY,unit->posX,unit->posY)<range*range) ++result;
	return result;
}

void BuildingRegister::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("V3BuildingRegister");
	stream->writeUint32(nextId,"next_id");
	stream->writeUint32(pendingBuildings.size(),"pending_size"); unsigned n=0;
	for(std::map<int,BuildingRecord>::const_iterator i=pendingBuildings.begin();i!=pendingBuildings.end();++i,++n)
	{
		stream->writeEnterSection(n); stream->writeSint32(i->first,"id");
		const BuildingRecord& r=i->second; stream->writeSint32(r.x,"x");stream->writeSint32(r.y,"y");stream->writeSint32(r.type,"type");stream->writeSint32(r.gid,"gid");stream->writeSint32(r.age,"age");stream->writeUint8(r.issued,"issued");stream->writeLeaveSection();
	}
	unsigned liveCount=0;
	for(const auto& record:foundBuildings) if(get_building(record.first)) ++liveCount;
	stream->writeUint32(liveCount,"found_size"); n=0;
	for(std::map<int,BuildingRecord>::const_iterator i=foundBuildings.begin();i!=foundBuildings.end();++i)
	{
		if(!get_building(i->first)) continue;
		stream->writeEnterSection(n++); stream->writeSint32(i->first,"id"); const BuildingRecord& r=i->second;
		stream->writeSint32(r.x,"x");stream->writeSint32(r.y,"y");stream->writeSint32(r.type,"type");stream->writeSint32(r.gid,"gid");stream->writeUint8(r.upgrading,"upgrading");stream->writeUint8(r.upgradeSeen,"upgrade_seen");stream->writeUint8(r.awaitingUpgradeExecution,"awaiting_upgrade_execution");stream->writeUint32(r.runtimeIdentity,"targetGeneration");stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
bool BuildingRegister::load(GAGCore::InputStream* stream,Sint32 versionMinor)
{
	pendingBuildings.clear(); foundBuildings.clear(); stream->readEnterSection("V3BuildingRegister");
	nextId=stream->readUint32("next_id"); Uint32 size=stream->readCount("pending_size");
	for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);int id=stream->readSint32("id");BuildingRecord r;r.x=stream->readSint32("x");r.y=stream->readSint32("y");r.type=stream->readSint32("type");r.gid=stream->readSint32("gid");r.age=stream->readSint32("age");r.issued=stream->readUint8("issued");pendingBuildings[id]=r;stream->readLeaveSection();}
	size=stream->readCount("found_size");
	for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);int id=stream->readSint32("id");BuildingRecord r;r.x=stream->readSint32("x");r.y=stream->readSint32("y");r.type=stream->readSint32("type");r.gid=stream->readSint32("gid");r.upgrading=stream->readUint8("upgrading");r.upgradeSeen=stream->readUint8("upgrade_seen");if(versionMinor>=FILE_FORMAT_VERSION_AI_PIPELINE){r.awaitingUpgradeExecution=stream->readUint8("awaiting_upgrade_execution");r.runtimeIdentity=stream->readUint32("targetGeneration");}foundBuildings[id]=r;stream->readLeaveSection();}
	for(auto& record:foundBuildings)
	{
		if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG &&
		   (record.second.type<0 || size_t(record.second.type)>=observation().catalog->size()))
			throw std::runtime_error("Invalid saved Maxima building variant");
		const AIEngine::BuildingView* building=building_from_gid(observation(),record.second.gid);
		if(building) {
            if(versionMinor<FILE_FORMAT_VERSION_AI_PIPELINE) record.second.runtimeIdentity=identity_of(building);
            if(record.second.runtimeIdentity==identity_of(building)) record.second.type=building->typeNum;
        }
	}
	if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG)
		for(auto& [id,r]:pendingBuildings) if(r.issued) {
			if(r.type<0||r.type>12)throw std::runtime_error("Invalid legacy Maxima building role");
			r.type=binding.getOwner()->game->buildingsTypes.getPlaceableTypeNum(IntBuildingType::typeFromShortNumber(r.type));
		}
	for(const auto& [id,r]:pendingBuildings) if(r.issued &&
		(r.type<0 || size_t(r.type)>=observation().catalog->size()
		 || !observation().catalog->at(r.type).semantics.placeable
		 || r.x<0 || r.y<0 || r.x>=observation().width || r.y>=observation().height))
		throw std::runtime_error("Invalid saved Maxima pending building");
	stream->readLeaveSection(); return true;
}

Constraint* Constraint::load(GAGCore::InputStream* stream,Sint32 versionMinor)
{
	stream->readEnterSection("Constraint");
	const int kind=stream->readSint32("type");
	Constraint* result=NULL;
	if(kind>=0&&kind<=3)
	{
		Gradients::GradientInfo info;info.load(stream,versionMinor);
		const int value=stream->readSint32("value");
		if(kind==0)result=new MinimumDistance(info,value);
		else if(kind==1)result=new MaximumDistance(info,value);
		else if(kind==2)result=new MinimizedDistance(info,value);
		else result=new MaximizedDistance(info,value);
	}
	else if(kind==4)result=new CenterOfBuilding(stream->readSint32("gid"));
	else if(kind==5){const int x=stream->readSint32("x");const int y=stream->readSint32("y");result=new SinglePosition(x,y);}
	stream->readLeaveSection();
	return result;
}

MinimumDistance::MinimumDistance(const Gradients::GradientInfo& i,int d):info(i),distance(d),cached(NULL){}
int MinimumDistance::score(Context&,int,int){return 0;}
bool MinimumDistance::passes(Context& c,int x,int y){if(!cached)cached=&c.get_gradient_manager().get_gradient(info);int h=cached->get_height(x,y);return h!=-2&&h>=distance;}
void MinimumDistance::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");info.save(s);s->writeSint32(distance,"value");s->writeLeaveSection();}
MaximumDistance::MaximumDistance(const Gradients::GradientInfo& i,int d):info(i),distance(d),cached(NULL){}
int MaximumDistance::score(Context&,int,int){return 0;}
bool MaximumDistance::passes(Context& c,int x,int y){if(!cached)cached=&c.get_gradient_manager().get_gradient(info);int h=cached->get_height(x,y);return h!=-2&&h<=distance;}
void MaximumDistance::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");info.save(s);s->writeSint32(distance,"value");s->writeLeaveSection();}
MinimizedDistance::MinimizedDistance(const Gradients::GradientInfo& i,int w):info(i),weight(w),cached(NULL){}
int MinimizedDistance::score(Context& c,int x,int y){if(!cached)cached=&c.get_gradient_manager().get_gradient(info);return -cached->get_height(x,y)*weight;}
bool MinimizedDistance::passes(Context& c,int x,int y){if(!cached)cached=&c.get_gradient_manager().get_gradient(info);return cached->get_height(x,y)!=-2;}
void MinimizedDistance::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");info.save(s);s->writeSint32(weight,"value");s->writeLeaveSection();}
MaximizedDistance::MaximizedDistance(const Gradients::GradientInfo& i,int w):info(i),weight(w),cached(NULL){}
int MaximizedDistance::score(Context& c,int x,int y){if(!cached)cached=&c.get_gradient_manager().get_gradient(info);return cached->has_sources()?cached->get_height(x,y)*weight:0;}
bool MaximizedDistance::passes(Context& c,int x,int y){if(!cached)cached=&c.get_gradient_manager().get_gradient(info);return !cached->has_sources()||cached->get_height(x,y)!=-2;}
void MaximizedDistance::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");info.save(s);s->writeSint32(weight,"value");s->writeLeaveSection();}
int CenterOfBuilding::score(Context&,int,int){return 0;}
bool CenterOfBuilding::passes(Context& c,int x,int y)
{ const AIEngine::BuildingView* b=building_from_gid(c.observation(),gid); return b && c.observation().normalizeX(b->posX+AIEngine::ObservationQueries::buildingType(c.observation(),*b).width/2)==x && c.observation().normalizeY(b->posY+AIEngine::ObservationQueries::buildingType(c.observation(),*b).height/2)==y; }
bool CenterOfBuilding::exact_position(Context& c,int& x,int& y)
{ const AIEngine::BuildingView* b=building_from_gid(c.observation(),gid);if(!b)return false;x=c.observation().normalizeX(b->posX+AIEngine::ObservationQueries::buildingType(c.observation(),*b).width/2);y=c.observation().normalizeY(b->posY+AIEngine::ObservationQueries::buildingType(c.observation(),*b).height/2);return true; }
void CenterOfBuilding::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");s->writeSint32(gid,"gid");s->writeLeaveSection();}
int SinglePosition::score(Context&,int,int){return 0;}
bool SinglePosition::passes(Context& c,int px,int py)
{return px==c.observation().normalizeX(x)&&py==c.observation().normalizeY(y);}
bool SinglePosition::exact_position(Context&,int& px,int& py){px=x;py=y;return true;}
void SinglePosition::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");s->writeSint32(x,"x");s->writeSint32(y,"y");s->writeLeaveSection();}

BuildingOrder::BuildingOrder(int type,int workers):type(type),workers(workers),id(-1),concreteType(-1),searchCursor(0),searchWidth(0),searchHeight(0),searchBestScore(INT_MIN),searchBest(-1,-1),searchActive(false){}
void BuildingOrder::save(GAGCore::OutputStream* s) const
{
	s->writeEnterSection("BuildingOrder");s->writeSint32(type,"building_type");s->writeSint32(workers,"workers");s->writeSint32(id,"id");s->writeSint32(concreteType,"concrete_type");
	s->writeUint32(constraints.size(),"constraint_count");for(size_t i=0;i<constraints.size();++i){s->writeEnterSection(i);constraints[i]->save(s);s->writeLeaveSection();}
	s->writeUint32(conditions.size(),"condition_count");for(size_t i=0;i<conditions.size();++i){s->writeEnterSection(i+constraints.size());conditions[i]->save(s);s->writeLeaveSection();}
	s->writeLeaveSection();
}
BuildingOrder* BuildingOrder::load(GAGCore::InputStream* s,Sint32 versionMinor)
{
	s->readEnterSection("BuildingOrder");const int buildingType=s->readSint32("building_type");const int workerCount=s->readSint32("workers");std::unique_ptr<BuildingOrder> order(new BuildingOrder(buildingType,workerCount));order->id=s->readSint32("id");
	if(buildingType<0 || buildingType>AIMaximaBuildings::ResourceExchange || workerCount<0)
		throw std::runtime_error("Invalid saved Maxima building demand");
	if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) {
		order->concreteType=s->readSint32("concrete_type");
		if(order->concreteType < -1) throw std::runtime_error("Invalid saved Maxima building variant");
	}
	Uint32 count=s->readCount("constraint_count");for(Uint32 i=0;i<count;++i){s->readEnterSection(i);order->add_constraint(Constraint::load(s,versionMinor));s->readLeaveSection();}
	const Uint32 offset=count;count=s->readCount("condition_count");for(Uint32 i=0;i<count;++i){s->readEnterSection(i+offset);order->add_condition(Conditions::Condition::load(s));s->readLeaveSection();}
	s->readLeaveSection();return order.release();
}
void BuildingOrder::add_constraint(Constraint* c){constraints.push_back(shared_ptr<Constraint>(c));}
void BuildingOrder::add_condition(Conditions::Condition* c){conditions.push_back(shared_ptr<Conditions::Condition>(c));}
Conditions::Result BuildingOrder::conditions_pass(Context& context) const
{
	for(size_t i=0;i<conditions.size();++i)
	{
		const Conditions::Result result=conditions[i]->passes(context);
		if(result!=Conditions::Ready)return result;
	}
	bool waiting=false;
	Gradients::GradientManager& manager=context.get_gradient_manager();
	for(size_t i=0;i<constraints.size();++i)
	{
		const Gradients::GradientInfo* info=constraints[i]->gradient_info();
		if(info && !manager.is_updated(*info))
		{
			// Bounded searches can outlive their initial gradient refresh.
			manager.queue_gradient(*info);
			waiting=true;
		}
	}
	return waiting ? Conditions::Waiting : Conditions::Ready;
}
void BuildingOrder::queue_gradients(Gradients::GradientManager& gm){for(size_t i=0;i<constraints.size();++i)if(constraints[i]->gradient_info())gm.queue_gradient(*constraints[i]->gradient_info());}
void BuildingOrder::reset_search()
{searchCursor=0;searchWidth=searchHeight=0;searchBestScore=INT_MIN;searchBest=position(-1,-1);searchActive=false;}
bool BuildingOrder::score_location(Context& context,const BuildingType* bt,bool flag,
	int x,int y,int& total)
{
	const AIEngine::AIWorldView* map=&context.observation();
	if(!AIEngine::ObservationQueries::roomForBuilding(context.observation(),x,y,*bt,context.teamNumber())) return false;
	if(!flag&&!AIEngine::ObservationQueries::hardBuildingSpace((*map),x,y,bt->width,bt->height))return false;
	if(flag)
	{
		for(int b=0;b<Building::MAX_COUNT;++b){const AIEngine::BuildingView* f=context.observation().buildingSlots(context.teamNumber())[b];if(f&&!AIEngine::ObservationQueries::buildingType(context.observation(),*f).semantics.occupiesGround&&f->posX==x&&f->posY==y)return false;}
	}
	bool ok=true;total=0;
	for(size_t ci=0;ci<constraints.size()&&ok;++ci)
	{
        int exactX=0,exactY=0;
        // Exact coordinates constrain the placement origin. Applying them to
        // every corner rejects every footprint larger than a single tile.
        if(constraints[ci]->exact_position(context,exactX,exactY))
            ok=constraints[ci]->passes(context,(*map).normalizeX(x),(*map).normalizeY(y));
        else
            for(int dx=0;dx<bt->width&&ok;++dx)for(int dy=0;dy<bt->height&&ok;++dy)if(dx==0||dy==0||dx==bt->width-1||dy==bt->height-1)ok=constraints[ci]->passes(context,(*map).normalizeX(x+dx),(*map).normalizeY(y+dy));
		if(!flag&&(!AIEngine::ObservationQueries::discovered((*map),x,y,context.observedTeam().allies)||!AIEngine::ObservationQueries::discovered((*map),x+bt->width-1,y+bt->height-1,context.observedTeam().allies)))ok=false;
		if(ok){total+=constraints[ci]->score(context,(*map).normalizeX(x),(*map).normalizeY(y));total+=constraints[ci]->score(context,(*map).normalizeX(x+bt->width-1),(*map).normalizeY(y));total+=constraints[ci]->score(context,(*map).normalizeX(x),(*map).normalizeY(y+bt->height-1));total+=constraints[ci]->score(context,(*map).normalizeX(x+bt->width-1),(*map).normalizeY(y+bt->height-1));}
	}
	return ok;
}
PlacementResult BuildingOrder::find_location(Context& context,int cellBudget,
	bool& complete)
{
	const AIEngine::AIWorldView* map=&context.observation();
	if(concreteType<0) concreteType=AIMaximaBuildings::choose(context.observation(),context.observedTeam(),type).placementType;
	if(concreteType<0 || static_cast<size_t>(concreteType)>=context.observation().catalog->size())
	{complete=true;reset_search();return PlacementResult();}
	const BuildingType* bt=&context.observation().catalog->at(concreteType).resolvedType;
	if(!bt || !context.observation().catalog->at(concreteType).available){complete=true;reset_search();return PlacementResult();}
	const bool flag=!bt->semantics.occupiesGround;

	// Most tactical flags already carry an exact coordinate.  Resolving it here
	// turns the former O(map area) scan into one validation without changing the
	// chosen location or the constraint scoring order.
	bool hasExact=false;position exact;
	for(size_t ci=0;ci<constraints.size();++ci)
	{
		int x=0,y=0;
		if(!constraints[ci]->exact_position(context,x,y))continue;
		x=(*map).normalizeX(x);y=(*map).normalizeY(y);
		if(hasExact&&(exact.x!=x||exact.y!=y))
		{complete=true;reset_search();return PlacementResult();}
		exact=position(x,y);hasExact=true;
	}
	if(hasExact)
	{
		int score=0;complete=true;reset_search();
		return score_location(context,bt,flag,exact.x,exact.y,score)
			?PlacementResult(exact):PlacementResult();
	}

	const int width=(*map).width,height=(*map).height;
	if(!searchActive||searchWidth!=width||searchHeight!=height)
	{reset_search();searchActive=true;searchWidth=width;searchHeight=height;}
	const int end=std::min(width*height,searchCursor+std::max(1,cellBudget));
	for(;searchCursor<end;++searchCursor)
	{
		// Preserve the legacy x-major/y-minor traversal exactly so ties select the
		// same coordinate when the world is unchanged during the bounded search.
		const int x=searchCursor/height,y=powerOfTwoRemainder(searchCursor, height);int score=0;
		if(score_location(context,bt,flag,x,y,score)&&score>searchBestScore)
		{searchBest=position(x,y);searchBestScore=score;}
	}
	complete=searchCursor>=width*height;
	if(!complete)return PlacementResult();
	const PlacementResult result=searchBest.x<0
		?PlacementResult():PlacementResult(searchBest);
	reset_search();return result;
}
}

namespace SearchTools
{
BuildingSearch::BuildingSearch(Context& context):context(context){}
void BuildingSearch::add_condition(Conditions::BuildingCondition* condition)
{conditions.push_back(shared_ptr<Conditions::BuildingCondition>(condition));}
bool BuildingSearch::matches(int id) const
{for(size_t i=0;i<conditions.size();++i)if(!conditions[i]->passes(context,id))return false;return true;}
int BuildingSearch::count_buildings(){int count=0;for(building_search_iterator i=begin();i!=end();++i)++count;return count;}
building_search_iterator BuildingSearch::begin(){return building_search_iterator(this,false);}
building_search_iterator BuildingSearch::end(){return building_search_iterator(this,true);}

building_search_iterator::building_search_iterator():search(NULL),ended(true){}
building_search_iterator::building_search_iterator(BuildingSearch* search,bool end):search(search),ended(end)
{
	iterator=search->context.get_building_register().found().begin();
	if(!ended)advance();
}
void building_search_iterator::advance()
{
	const std::map<int,Construction::BuildingRecord>& found=search->context.get_building_register().found();
	while(iterator!=found.end()&&!search->matches(iterator->first))++iterator;
	ended=iterator==found.end();
}
unsigned building_search_iterator::operator*() const{return iterator->first;}
building_search_iterator& building_search_iterator::operator++(){++iterator;advance();return *this;}
bool building_search_iterator::operator!=(const building_search_iterator& rhs) const
{if(ended||rhs.ended)return ended!=rhs.ended;return search!=rhs.search||iterator!=rhs.iterator;}

enemy_team_iterator::enemy_team_iterator(Context& context):context(&context),team(-1),ended(false){advance();}
enemy_team_iterator::enemy_team_iterator():context(NULL),team(-1),ended(true){}
void enemy_team_iterator::advance()
{
	if(ended)return;
	for(++team;team<Team::MAX_COUNT;++team)
	{
		const auto* candidate=std::size_t(team)<context->observation().teams.size() ? &context->observation().teams[team] : nullptr;
		if(candidate&&(context->observedTeam().enemies&candidate->mask))return;
	}
	ended=true;
}
unsigned enemy_team_iterator::operator*() const{return team;}
enemy_team_iterator& enemy_team_iterator::operator++(){advance();return *this;}
bool enemy_team_iterator::operator!=(const enemy_team_iterator& rhs) const
{if(ended||rhs.ended)return ended!=rhs.ended;return context!=rhs.context||team!=rhs.team;}

enemy_building_iterator::enemy_building_iterator():context(NULL),team(-1),type(-1),level(-1),index(-1),gid(-1),construction(AnyConstruction),ended(true){}
enemy_building_iterator::enemy_building_iterator(Context& context,int team,int type,int level,ConstructionFilter construction)
	:context(&context),team(team),type(type),level(level),index(-1),gid(-1),construction(construction),ended(false){advance();}
void enemy_building_iterator::advance()
{
	if(ended)return; const auto* owner=(team>=0&&std::size_t(team)<context->observation().teams.size()) ? &context->observation().teams[team] : nullptr;
	if(!owner){ended=true;return;}
	for(++index;index<Building::MAX_COUNT;++index)
	{
		const AIEngine::BuildingView* b=context->observation().buildingSlots(team)[index];if(!b||!(b->seenByMask&context->observedTeam().mask))continue;
		if(type!=-1&&!AIMaximaBuildings::serves(context->observation(),AIEngine::ObservationQueries::buildingType(context->observation(),*b),type))continue;
		if(level!=-1&&AIMaximaBuildings::lineagePosition(context->observation(),b->typeNum)!=level)continue;
		if(construction!=AnyConstruction&&int(construction)!=int(bool(AIEngine::ObservationQueries::buildingType(context->observation(),*b).isBuildingSite)))continue;
		gid=b->gid;return;
	}
	ended=true;
}
unsigned enemy_building_iterator::operator*() const{return gid;}
enemy_building_iterator& enemy_building_iterator::operator++(){advance();return *this;}
bool enemy_building_iterator::operator!=(const enemy_building_iterator& rhs) const
{if(ended||rhs.ended)return ended!=rhs.ended;return context!=rhs.context||team!=rhs.team||index!=rhs.index;}

MapInfo::MapInfo(Context& context):context(context){}
int MapInfo::get_width()const{return context.observation().width;}
int MapInfo::get_height()const{return context.observation().height;}
bool MapInfo::is_forbidden_area(int x,int y)const{return (context.observation().areasAt(context.observation().tileIndex(x,y)).forbidden&context.observedTeam().mask)!=0;}
bool MapInfo::is_guard_area(int x,int y)const{return (context.observation().areasAt(context.observation().tileIndex(x,y)).guard&context.observedTeam().mask)!=0;}
bool MapInfo::is_clearing_area(int x,int y)const{return (context.observation().areasAt(context.observation().tileIndex(x,y)).clear&context.observedTeam().mask)!=0;}
bool MapInfo::is_discovered(int x,int y)const{return AIEngine::ObservationQueries::discovered(context.observation(),x,y,context.observedTeam().mask);}
bool MapInfo::is_resource(int x,int y,int type)const{return MapState::hasMaterialSlot(context.observation().state(),context.observation().tileIndex(x,y),type);}
bool MapInfo::is_resource(int x,int y)const{return (context.observation().resourceAt(context.observation().tileIndex(x,y)).resource.type!=NO_RES_TYPE);}
bool MapInfo::is_walkable(int x,int y)const{return AIEngine::ObservationQueries::terrain(context.observation(),x,y).walkable;}
bool MapInfo::is_crop_habitat(int x,int y)const{return MapState::terrainSupportsMaterial(context.observation().state(),context.observation().tileIndex(x,y),MaterialId::Food);}
bool MapInfo::is_water(int x,int y)const{return AIEngine::ObservationQueries::terrain(context.observation(),x,y).swimmable;}
bool MapInfo::is_sand(int x,int y)const{return AIEngine::ObservationQueries::terrain(context.observation(),x,y).inhibitionQ8 != 0;}
bool MapInfo::is_grass(int x,int y)const{return AIEngine::ObservationQueries::terrain(context.observation(),x,y).buildable;}
bool MapInfo::backs_onto_sand(int x,int y)const
{for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if((dx||dy)&&AIEngine::ObservationQueries::terrain(context.observation(),x+dx,y+dy).shoreline)return true;return false;}
int MapInfo::get_ammount_resource(int x,int y)const{return context.observation().resourceAt(context.observation().tileIndex(x,y)).resource.amount;}
}

void Context::bindObservation(const AIEngine::DecisionContext& decision)
{
    observationBound=true;
    binding.bindOwned(decision.observation ? decision.observation : std::make_shared<AIEngine::AIWorldView>(decision.world.components()),decision.player,decision.team);
    buildings.bind(observation(),playerNumber(),teamNumber());gradients.bind(observation(),playerNumber(),teamNumber());
}
void Context::releaseObservation()
{
    buildings.unbind();gradients.unbind();binding.clear();observationBound=false;
}
Context::OwnerObservationScope::OwnerObservationScope(Context& owner)
{
    if(owner.observationBound)return;
    context=&owner;
    try {
        auto observed=AIEngine::AIWorldView::capture(*owner.player->game,AIEngine::AIWorldView::captureCatalog(*owner.player->game));
        const std::vector<AIEngine::ExecutionReceipt> receipts;
        owner.bindObservation({*observed,unsigned(owner.player->number),unsigned(owner.player->team->teamNumber),receipts,observed});
    } catch(...) {owner.releaseObservation();throw;}
}
Context::OwnerObservationScope::~OwnerObservationScope(){if(context)context->releaseObservation();}
Context::Context(Player* player)
	:player(player),binding(player),allies(0),enemies(0),inn_view(0),market_view(0),other_view(0),activeAI(NULL),buildings(player),gradients(player),nullOrder(new NullOrder()),timer(0),previousBuildingId(-1),initialized(false),fruitOnMap(false),profileAiMicros(0),profileHousekeepingMicros(0),profileBuildingSearchMicros(0),profileBuildingSearchMaxMicros(0),profileBuildingSearchCalls(0){}

void Context::initialize()
{
    auto ownerObservation=scopeOwnerObservation();
	buildings.initiate();detect_fruit();allies=observedTeam().allies;enemies=observedTeam().enemies;
	market_view=observedTeam().exchangeVision;inn_view=observedTeam().foodVision;other_view=observedTeam().otherVision;initialized=true;
}
void Context::detect_fruit()
{
	fruitOnMap=false;for(int x=0;x<observation().width&&!fruitOnMap;++x)for(int y=0;y<observation().height;++y)if(MapState::hasMaterial(observation().state(),observation().tileIndex(x,y),MaterialId::Cherries)||MapState::hasMaterial(observation().state(),observation().tileIndex(x,y),MaterialId::Oranges)||MapState::hasMaterial(observation().state(),observation().tileIndex(x,y),MaterialId::Prunes)){fruitOnMap=true;break;}
}
unsigned Context::add_building_order(Construction::BuildingOrder* order)
{
	telemetry.count(AITrace::AI7::runtime_building_queued);
	buildingOrders.push_back(shared_ptr<Construction::BuildingOrder>(order));order->queue_gradients(gradients);order->id=buildings.register_building();
    if(order->type==AIMaximaBuildings::WarriorAttraction) begin_attraction(order->id,1u<<WARRIOR);
    if(order->type==AIMaximaBuildings::ExploreAttraction) begin_attraction(order->id,1u<<EXPLORER);
    if(order->type==AIMaximaBuildings::WorkerAttraction) begin_attraction(order->id,1u<<WORKER);
    return order->id;
}

bool Context::get_building_position(int id, int& x, int& y)
{
	if(const AIEngine::BuildingView* building=buildings.get_building(id))
	{ x=building->posX; y=building->posY; return true; }
	const auto pending=buildings.pending().find(id);
	if(pending==buildings.pending().end()) return false;
	if(pending->second.issued)
	{ x=pending->second.x; y=pending->second.y; return true; }
	for(const auto& order:buildingOrders)
		if(order->id==id)
			for(const auto& constraint:order->constraints)
				if(constraint->exact_position(*this,x,y))
				{ x=observation().normalizeX(x); y=observation().normalizeY(y); return true; }
	return false;
}

bool Context::begin_attraction(int id,unsigned unitMask)
{
    auto found=retiredAttractions.find(id);
    if(found==retiredAttractions.end() || !(found->second&unitMask)) return false;
    found->second&=~unitMask;
    if(!found->second) retiredAttractions.erase(found);
    return true;
}

unsigned Context::complete_attraction_retirement(int id,unsigned unitMask)
{return retiredAttractions[id]|=unitMask;}
bool Context::attraction_retired_or_destroyed(int id,unsigned unitMask) const
{
    const auto found=retiredAttractions.find(id);
    return (found!=retiredAttractions.end() && (found->second&unitMask)==unitMask)
        || (!buildings.is_building_found(id) && !buildings.is_building_pending(id));
}

void Context::cancel_or_destroy_building(int id,unsigned retiringUnitMask)
{
	for(size_t i=0; i<buildingOrders.size(); ++i)
		if(buildingOrders[i]->id==id)
		{
			buildingOrders.erase(buildingOrders.begin()+i);
			buildings.remove_building(id);
			return;
		}
	if(buildings.is_building_found(id) || buildings.is_building_pending(id))
		add_management_order(new Management::RetireAttraction(id,retiringUnitMask));
}

std::vector<int> Context::material_source_flags(int material) const
{
	std::set<int> ids;
	Gradients::GradientInfo source;
	source.add_source(new Gradients::Entities::MaterialSource(material));
	for(size_t i=0; i<buildingOrders.size(); ++i)
	{
		const Construction::BuildingOrder& order=*buildingOrders[i];
		if(order.type!=AIMaximaBuildings::ExploreAttraction) continue;
		for(size_t j=0; j<order.constraints.size(); ++j)
		{
			Construction::Constraint& constraint=*order.constraints[j];
			if(dynamic_cast<Construction::MaximumDistance*>(&constraint)
			   && constraint.gradient_info()
			   && *constraint.gradient_info()==source)
				ids.insert(order.id);
		}
	}
	// These records are already serialized by the runtime. Reconstructing from
	// them also adopts fruit flags from saves made before lifecycle tracking.
	const std::map<int, Construction::BuildingRecord>* records[]={
		&buildings.pending(), &buildings.found()};
	for(size_t group=0; group<2; ++group)
		for(auto i=records[group]->begin(); i!=records[group]->end(); ++i)
		{
			const Construction::BuildingRecord& record=i->second;
			if(record.type<0 || static_cast<size_t>(record.type)>=observation().catalog->size()) continue;
			const auto* descriptor=&observation().catalog->at(record.type).resolvedType;
			if(!descriptor||!AIMaximaBuildings::serves(observation(),*descriptor,AIMaximaBuildings::ExploreAttraction)) continue;
			const AIEngine::BuildingView* flag=buildings.get_building(i->first);
			if(group==1 && !flag) continue;
			const int x=flag ? flag->posX : record.x;
			const int y=flag ? flag->posY : record.y;
			if(x>=0 && y>=0 && MapState::hasMaterialSlot(observation().state(),observation().tileIndex(x,y),material))
				ids.insert(i->first);
		}
	return std::vector<int>(ids.begin(), ids.end());
}
int Context::issue_building_at(int engineType,int workers,int x,int y)
{
	if(engineType<0 || static_cast<size_t>(engineType)>=observation().catalog->size())return -1;
	const BuildingType* site=&observation().catalog->at(engineType).resolvedType;
	if(!site || !site->semantics.placeable || !observation().catalog->at(engineType).available
	   || (site->semantics.occupiesGround && !AIEngine::ObservationQueries::hardBuildingSpace(observation(),x,y,site->width,site->height))
	   || !AIEngine::ObservationQueries::discovered(observation(),x,y,observedTeam().allies)
	   || !AIEngine::ObservationQueries::discovered(observation(),x+site->width-1,y+site->height-1,observedTeam().allies))return -1;
	const int id=buildings.register_building();buildings.issue_order(id,x,y,engineType);
	workers=std::clamp(workers,0,site->semantics.assignmentLimit);
	Management::AssignWorkers* assignment=new Management::AssignWorkers(workers,id);
	if(site->isBuildingSite)assignment->add_condition(new Conditions::ParticularBuilding(new Conditions::UnderConstruction,id));
	add_management_order(assignment);
	push_order(AIEngine::ObservationQueries::createOrder(observation(), teamNumber(),x,y,engineType,workers,workers));
	previousBuildingId=id;return id;
}

bool Context::issue_upgrade_repair(int id,bool repair)
{
	const AIEngine::BuildingView* building=buildings.get_building(id);
	if(!building || buildings.is_building_upgrading(id) || AIEngine::ObservationQueries::buildingType(observation(),*building).isBuildingSite
	   || building->constructionResultState!=::Building::NO_CONSTRUCTION)
		return false;
	if(repair)
	{
		if(!AIEngine::ObservationQueries::buildingType(observation(),*building).semantics.repairable || building->hp>=building->maxHp
		   || !observation().isHardSpaceForBuildingSite(*building, false))return false;
	}
	else
	{
		if(observation().configuration->isUnitUpgradesDisabled()
		   || building->hp<building->maxHp
		   || !observation().isUpgradeAvailable(*building)
		   || !observation().isHardSpaceForBuildingSite(*building, true))return false;
	}
	push_order(AIEngine::ObservationQueries::constructionOrder(observation(), *building,1,1));
	buildings.set_upgrading(id,true);
	return true;
}
void Context::add_management_order(Management::ManagementOrder *order)
{
	telemetry.count(AITrace::AI7::runtime_management_queued);
	managementOrders.push_back(shared_ptr<Management::ManagementOrder>(order));
}
void Context::add_material_tracker(Management::MaterialTracker* tracker,int id){trackers[id]=shared_ptr<Management::MaterialTracker>(tracker);}
shared_ptr<Management::MaterialTracker> Context::get_material_tracker(int id)
{std::map<int,shared_ptr<Management::MaterialTracker> >::iterator i=trackers.find(id);return i==trackers.end()?shared_ptr<Management::MaterialTracker>():i->second;}
const TeamStat& Context::get_team_stats(){return *&observedTeam().statistics;}
void Context::dispatch_event(const RuntimeEvent& event){if(activeAI)activeAI->handle_event(*this,event);}

void Context::update_trackers()
{
    auto ownerObservation=scopeOwnerObservation();
	for(std::map<int,shared_ptr<Management::MaterialTracker> >::iterator i=trackers.begin();i!=trackers.end();)
	{if(!buildings.is_building_found(i->first)&&!buildings.is_building_pending(i->first)){trackers.erase(i++);continue;}if(buildings.is_building_found(i->first))i->second->tick();++i;}
}
void Context::update_management_orders()
{
    auto ownerObservation=scopeOwnerObservation();
	for(size_t i=0;i<managementOrders.size();)
	{
		Conditions::Result result=managementOrders[i]->ready(*this);
		if (result == Conditions::Ready)
		{
			shared_ptr<Management::ManagementOrder> current = managementOrders[i];
			managementOrders.erase(managementOrders.begin() + i);
			current->modify(*this);
			telemetry.count(AITrace::AI7::runtime_management_applied);
		}
		else if (result == Conditions::Impossible)
		{
			telemetry.count(AITrace::AI7::runtime_management_impossible);
			managementOrders.erase(managementOrders.begin() + i);
		}
		else ++i;
	}
}
void Context::update_building_orders()
{
    auto ownerObservation=scopeOwnerObservation();
	PERF_SCOPE_TIME(AIPlan);
	for(size_t i=0;i<buildingOrders.size();)
	{
		Conditions::Result result=buildingOrders[i]->conditions_pass(*this);
		if(result==Conditions::Waiting){++i;continue;}
		if(result==Conditions::Impossible){buildings.remove_building(buildingOrders[i]->id);buildingOrders.erase(buildingOrders.begin()+i);continue;}
		if(previousBuildingId!=-1&&buildings.is_building_pending(previousBuildingId)&&!buildings.is_building_found(previousBuildingId))break;
		bool complete=false;
		// Cap generic gradient-driven searches. Exact-position tactical orders take
		// the fast path and finish immediately; broad searches resume four ticks
		// later in the same deterministic traversal order.
		const PlacementResult placement=buildingOrders[i]->find_location(*this,
			2048,complete);
		if (!complete)
		{
			telemetry.count(AITrace::AI7::runtime_placement_deferred);
			break;
		}
		if (!placement.found)
		{
			telemetry.count(AITrace::AI7::runtime_placement_failed);
			buildings.remove_building(buildingOrders[i]->id);
			buildingOrders.erase(buildingOrders.begin() + i);
			continue;
		}
		const position p=placement.value;
		const int engineType=buildingOrders[i]->concreteType;
		const auto* descriptor=&observation().catalog->at(engineType).resolvedType;
		const int id=buildingOrders[i]->id;buildings.issue_order(id,p.x,p.y,engineType);
		const int workers=std::clamp(buildingOrders[i]->workers,0,descriptor->semantics.assignmentLimit);
		Management::AssignWorkers* assignment=new Management::AssignWorkers(workers,id);
		if(descriptor->isBuildingSite)assignment->add_condition(new Conditions::ParticularBuilding(new Conditions::UnderConstruction,id));
		add_management_order(assignment);
		push_order(AIEngine::ObservationQueries::createOrder(observation(),teamNumber(),p.x,p.y,engineType,workers,workers));
		previousBuildingId=id;buildingOrders.erase(buildingOrders.begin()+i);break;
	}
}

void Context::record_profile(long long totalMicros,long long aiMicros,
	long long housekeepingMicros,long long buildingSearchMicros)
{
	profileTickMicros.push_back(totalMicros);profileAiMicros+=aiMicros;
	profileHousekeepingMicros+=housekeepingMicros;
	if(buildingSearchMicros>=0)
	{
		profileBuildingSearchMicros+=buildingSearchMicros;
		profileBuildingSearchMaxMicros=std::max(profileBuildingSearchMaxMicros,
			buildingSearchMicros);
		++profileBuildingSearchCalls;
	}
	if(profileTickMicros.size()<1000)return;
	std::vector<long long> sorted=profileTickMicros;
	std::sort(sorted.begin(),sorted.end());
	const size_t n=sorted.size();
    std::ostringstream text;
	text<<"MAXIMA_TELEMETRY\t"<<timer<<"\t"
		<<teamNumber()<<"\truntime_performance"
		<<"\tsamples="<<n
		<<"\ttick_p50_us="<<sorted[(n*50)/100]
		<<"\ttick_p95_us="<<sorted[(n*95)/100]
		<<"\ttick_p99_us="<<sorted[(n*99)/100]
		<<"\ttick_max_us="<<sorted[n-1]
		<<"\tai_total_us="<<profileAiMicros
		<<"\thousekeeping_total_us="<<profileHousekeepingMicros
		<<"\tbuilding_search_total_us="<<profileBuildingSearchMicros
		<<"\tbuilding_search_max_us="<<profileBuildingSearchMaxMicros
		<<"\tbuilding_search_calls="<<profileBuildingSearchCalls
        <<"\tgame_tick="<<observation().tick<<'\n';
    bufferedDiagnostics.push_back({{},{},text.str(),true});
	profileTickMicros.clear();profileAiMicros=profileHousekeepingMicros=0;
	profileBuildingSearchMicros=profileBuildingSearchMaxMicros=0;
	profileBuildingSearchCalls=0;
}

shared_ptr<Order> Context::getOrder(RuntimeAI& ai)
{
	const bool profiling=telemetry_enabled();
	const std::chrono::steady_clock::time_point totalStarted=profiling
		?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
	activeAI=&ai;if(!initialized)initialize();gradients.update(observation().tick);
	while (!orders.empty() && (!AIEngine::selectedTargetExists(*orders.front(),observation())
        || !AIEngine::ObservationQueries::permittedQueuedOrder(observation(),*orders.front()))) {
        if(const auto* construction=dynamic_cast<const OrderConstruction*>(orders.front().get()))
            buildings.order_execution_completed(construction->gid,false,
                orders.front()->aiSelectedTarget ? std::optional<Uint32>(orders.front()->aiSelectedTarget->generation) : std::nullopt);
        orders.pop_front();
    }
	if(!orders.empty())
	{
		shared_ptr<Order> order=orders.front();orders.pop_front();
		if(profiling)record_profile(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now()-totalStarted).count(),0,0,-1);
		return order;
	}
	// Building discovery and declarative management conditions change much more
	// slowly than unit simulation.  Batch that housekeeping while leaving the AI
	// tick and already-issued engine orders responsive; newly queued management
	// work waits at most three logical ticks.
	// A stable team phase prevents several Maxima instances from doing their
	// housekeeping on the same simulation update. Team zero retains the legacy
	// phase; the remaining teams occupy the other three slots.
	const bool housekeepingDue=((timer+teamNumber())&3)==0;
	long long housekeepingMicros=0,buildingSearchMicros=-1;
	std::chrono::steady_clock::time_point phaseStarted;
	if(profiling)phaseStarted=std::chrono::steady_clock::now();
	if(housekeepingDue)
		buildings.tick();
        std::erase_if(retiredAttractions,[&](const auto& entry){return !buildings.is_building_found(entry.first) && !buildings.is_building_pending(entry.first);});
	// Tracker ages and their ten-tick sample period use the AI's logical clock,
	// independently of the slower building-discovery/management cadence.
	update_trackers();
	if(housekeepingDue)
		update_management_orders();
	if(profiling)housekeepingMicros+=std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-phaseStarted).count();
	if(profiling)phaseStarted=std::chrono::steady_clock::now();
	ai.tick(*this);
	const long long aiMicros=profiling
		?std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now()-phaseStarted).count():0;
	if(housekeepingDue)
	{
		if(profiling)phaseStarted=std::chrono::steady_clock::now();
		update_management_orders();
		if(profiling)
			housekeepingMicros+=std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now()-phaseStarted).count();
		if(profiling)phaseStarted=std::chrono::steady_clock::now();
		update_building_orders();
		if(profiling)buildingSearchMicros=
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now()-phaseStarted).count();
	}
	++timer;
	if(profiling)record_profile(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-totalStarted).count(),aiMicros,
		housekeepingMicros,buildingSearchMicros);
	return nullOrder;
}

void Context::save(GAGCore::OutputStream* stream) const
{
    auto ownerObservation=const_cast<Context*>(this)->scopeOwnerObservation();
	stream->writeEnterSection("V3Runtime");stream->writeSint32(timer,"timer");stream->writeSint32(previousBuildingId,"previous_building_id");stream->writeUint8(initialized,"initialized");stream->writeUint8(fruitOnMap,"fruit_on_map");stream->writeUint32(allies,"allies");stream->writeUint32(enemies,"enemies");stream->writeUint32(inn_view,"inn_view");stream->writeUint32(market_view,"market_view");stream->writeUint32(other_view,"other_view");
	stream->writeEnterSection("orders");stream->writeUint32(orders.size(),"size");unsigned n=0;for(std::list<shared_ptr<Order> >::const_iterator i=orders.begin();i!=orders.end();++i,++n){stream->writeEnterSection(n);stream->writeUint32((*i)->getDataLength(),"size");stream->writeUint8((*i)->getOrderType(),"type");stream->write((*i)->getData(),(*i)->getDataLength(),"data");AIEngine::saveSelectedTarget(*stream,**i);stream->writeLeaveSection();}stream->writeLeaveSection();
	buildings.save(stream);
	stream->writeEnterSection("building_orders");stream->writeUint32(buildingOrders.size(),"size");for(size_t i=0;i<buildingOrders.size();++i){stream->writeEnterSection(i);buildingOrders[i]->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("management_orders");stream->writeUint32(managementOrders.size(),"size");for(size_t i=0;i<managementOrders.size();++i){stream->writeEnterSection(i);managementOrders[i]->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("trackers");stream->writeUint32(trackers.size(),"size");n=0;for(std::map<int,shared_ptr<Management::MaterialTracker> >::const_iterator i=trackers.begin();i!=trackers.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first,"id");i->second->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
    stream->writeEnterSection("retiredAttractions");stream->writeUint32(retiredAttractions.size(),"size");
    Uint32 retiredIndex=0;
    for(const auto& [id,mask]:retiredAttractions) {stream->writeEnterSection(retiredIndex++);stream->writeSint32(id,"id");stream->writeUint8(mask,"unitMask");stream->writeLeaveSection();}
    stream->writeLeaveSection();
	stream->writeLeaveSection();
}
void Context::saveExecutionState(GAGCore::OutputStream* stream) const
{
    auto ownerObservation=scopeOwnerObservation();
    stream->writeEnterSection("RuntimeExecution95");
    AIMaximaContinuation::Writer archive(stream,true);
    stream->writeUint32(buildingOrders.size(),"size");
    for(size_t i=0;i<buildingOrders.size();++i)
    {
        stream->writeEnterSection(i);
        const Construction::BuildingOrder& order=*buildingOrders[i];
        archive("searchCursor",order.searchCursor);
        archive("searchWidth",order.searchWidth);
        archive("searchHeight",order.searchHeight);
        archive("searchBestScore",order.searchBestScore);
        archive("searchBestX",order.searchBest.x);
        archive("searchBestY",order.searchBest.y);
        archive("searchActive",order.searchActive);
        stream->writeLeaveSection();
    }
    gradients.saveExecutionState(stream);
    stream->writeEnterSection("FoundBuildingExecution96");
    for(const auto& entry:buildings.foundBuildings)
    {
        if(!buildings.get_building(entry.first)) continue;
        stream->writeSint32(entry.second.age,"age");
        stream->writeUint8(entry.second.issued,"issued");
    }
    stream->writeLeaveSection();
    stream->writeLeaveSection();
}
void Context::loadExecutionState(GAGCore::InputStream* stream, Sint32 versionMinor)
{
    auto ownerObservation=scopeOwnerObservation();
    stream->readEnterSection("RuntimeExecution95");
    AIMaximaContinuation::Reader archive(stream,versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE,versionMinor);
    const Uint32 size=stream->readCount("size");
    if(size!=buildingOrders.size()) throw std::runtime_error("Invalid building search continuation count");
    for(Uint32 i=0;i<size;++i)
    {
        stream->readEnterSection(i);
        Construction::BuildingOrder& order=*buildingOrders[i];
        archive("searchCursor",order.searchCursor);
        archive("searchWidth",order.searchWidth);
        archive("searchHeight",order.searchHeight);
        archive("searchBestScore",order.searchBestScore);
        archive("searchBestX",order.searchBest.x);
        archive("searchBestY",order.searchBest.y);
        archive("searchActive",order.searchActive);
        stream->readLeaveSection();
    }
    gradients.loadExecutionState(stream,versionMinor);
    {
        stream->readEnterSection("FoundBuildingExecution96");
        for(auto& entry:buildings.foundBuildings)
        {
            entry.second.age=stream->readSint32("age");
            entry.second.issued=stream->readUint8("issued");
        }
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
}
bool Context::load(GAGCore::InputStream* stream,Sint32 versionMinor)
{
    auto ownerObservation=scopeOwnerObservation();
	stream->readEnterSection("V3Runtime");timer=stream->readSint32("timer");previousBuildingId=stream->readSint32("previous_building_id");initialized=stream->readUint8("initialized");fruitOnMap=stream->readUint8("fruit_on_map");allies=stream->readUint32("allies");enemies=stream->readUint32("enemies");inn_view=stream->readUint32("inn_view");market_view=stream->readUint32("market_view");other_view=stream->readUint32("other_view");
	orders.clear();stream->readEnterSection("orders");Uint32 size=stream->readCount("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);Uint32 length=stream->readCount("size");std::vector<Uint8> data(length+1);data[0]=stream->readUint8("type");stream->read(data.data()+1,length,"data");auto order=Order::getOrder(data.data(),data.size(),versionMinor);if(!order)throw std::runtime_error("Invalid saved AI order");AIStateSerialization::normalizeLegacyOrderStaffing(*player->game,*order,versionMinor);AIEngine::loadSelectedTarget(*stream,*order,versionMinor);orders.push_back(order);stream->readLeaveSection();}stream->readLeaveSection();
	buildings.load(stream,versionMinor);gradients.invalidate();
	buildingOrders.clear();stream->readEnterSection("building_orders");size=stream->readCount("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);shared_ptr<Construction::BuildingOrder> order(Construction::BuildingOrder::load(stream,versionMinor));if(order){
		if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG)
			order->concreteType=binding.getOwner()->game->buildingsTypes.getPlaceableTypeNum(IntBuildingType::typeFromShortNumber(order->type));
		if(order->concreteType>=0 && (size_t(order->concreteType)>=observation().catalog->size()
			|| !observation().catalog->at(order->concreteType).semantics.placeable))
			throw std::runtime_error("Invalid saved Maxima placement variant");
		order->queue_gradients(gradients);buildingOrders.push_back(order);}stream->readLeaveSection();}stream->readLeaveSection();
	managementOrders.clear();stream->readEnterSection("management_orders");size=stream->readCount("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);shared_ptr<Management::ManagementOrder> order(Management::ManagementOrder::load(stream,versionMinor));if(order)managementOrders.push_back(order);stream->readLeaveSection();}stream->readLeaveSection();
	trackers.clear();stream->readEnterSection("trackers");size=stream->readCount("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int id=stream->readSint32("id");trackers[id]=shared_ptr<Management::MaterialTracker>(Management::MaterialTracker::load(*this,stream));stream->readLeaveSection();}stream->readLeaveSection();
    retiredAttractions.clear();
    if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) {
        stream->readEnterSection("retiredAttractions");const auto count=stream->readCount("size");
        for(Uint32 i=0;i<count;++i) {
            stream->readEnterSection(i);const int id=stream->readSint32("id");const unsigned mask=stream->readUint8("unitMask");
            if(id<0 || !mask || (mask&~((1u<<NB_UNIT_TYPE)-1)) || !retiredAttractions.emplace(id,mask).second) return false;
            stream->readLeaveSection();
        }
        stream->readLeaveSection();
    }
	stream->readLeaveSection();return true;
}

}

Uint64 AIMaximaRuntime::Gradients::GradientManager::retainedVectorBytes() const noexcept
{
    Uint64 bytes = gradients.capacity() * sizeof(gradients[0])
        + ages.capacity() * sizeof(int) + frontier.retainedBytes();
    for (const auto& item : gradients) if (item)
        bytes += item->values.capacity() * sizeof(Sint16)
            + item->info.sources.capacity() * sizeof(item->info.sources[0])
            + item->info.obstacles.capacity() * sizeof(item->info.obstacles[0]);
    return bytes;
}
