#include "AIMaximaRuntime.h"
#include "AIMaximaContinuation.h"

#include "Brush.h"
#include "Building.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
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
namespace
{
	bool is_flag_type(int type)
	{
		return type>IntBuildingType::DEFENSE_BUILDING
			&& type<IntBuildingType::STONE_WALL;
	}

	Building* building_from_gid(Player* player, int gid)
	{
		if(gid==NOGBID)
			return NULL;
		const int team=Building::GIDtoTeam(gid);
		const int id=Building::GIDtoID(gid);
		if(team<0 || team>=Team::MAX_COUNT || !player->game->teams[team]
		   || id<0 || id>=Building::MAX_COUNT)
			return NULL;
		return player->game->teams[team]->myBuildings[id];
	}

	bool visible_to(Player* player, const Building* building)
	{
		return building && (Building::GIDtoTeam(building->gid)
			==player->team->teamNumber || (building->seenByMask&player->team->me));
	}

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
	const EntityType kind=static_cast<EntityType>(stream->readUint8("type"));
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
		case EResource: return new Resource(stream->readSint32("resource_type"));
		case EAnyResource: return new AnyResource;
		case EWater: return new Water;
		case EPosition:
		{
			const int x=stream->readSint32("x");
			const int y=stream->readSint32("y");
			return new Position(x, y);
		}
		case ESand: return new Sand;
	}
	return NULL;
}

Building::Building(int buildingType, int team, bool includeConstruction)
	: buildingType(buildingType), team(team), includeConstruction(includeConstruction) {}

bool Building::matches(Player* player, int x, int y) const
{
	const int gid=player->map->getBuilding(x,y);
	::Building* building=building_from_gid(player,gid);
	return building && ::Building::GIDtoTeam(gid)==team
		&& visible_to(player,building)
		&& building->type->shortTypeNum==buildingType
		&& (includeConstruction
			|| building->constructionResultState==::Building::NO_CONSTRUCTION);
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

bool AnyTeamBuilding::matches(Player* player, int x, int y) const
{
	const int gid=player->map->getBuilding(x,y);
	::Building* building=building_from_gid(player,gid);
	return building && ::Building::GIDtoTeam(gid)==team
		&& visible_to(player,building)
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

Resource::Resource(int resourceType) : resourceType(resourceType) {}
bool Resource::matches(Player* player,int x,int y) const
{ return player->map->isResourceTakeable(x,y,resourceType); }
bool Resource::equals(const Entity& other) const
{
	const Resource* rhs=dynamic_cast<const Resource*>(&other);
	return rhs && rhs->resourceType==resourceType;
}
bool Resource::can_change() const
{ return resourceType==WOOD || resourceType==CORN || resourceType==ALGA; }
void Resource::save(GAGCore::OutputStream* stream) const
{stream->writeUint8(type(),"type");stream->writeSint32(resourceType,"resource_type");}

bool AnyResource::matches(Player* player,int x,int y) const
{ return player->map->isResource(x,y); }
bool AnyResource::equals(const Entity& other) const
{ return dynamic_cast<const AnyResource*>(&other)!=NULL; }
void AnyResource::save(GAGCore::OutputStream* stream) const{stream->writeUint8(type(),"type");}

bool Water::matches(Player* player,int x,int y) const
{ return player->map->isWater(x,y); }
bool Water::equals(const Entity& other) const
{ return dynamic_cast<const Water*>(&other)!=NULL; }
void Water::save(GAGCore::OutputStream* stream) const{stream->writeUint8(type(),"type");}

Position::Position(int x,int y) : x(x),y(y) {}
bool Position::matches(Player*,int px,int py) const { return px==x && py==y; }
bool Position::equals(const Entity& other) const
{
	const Position* rhs=dynamic_cast<const Position*>(&other);
	return rhs && rhs->x==x && rhs->y==y;
}
void Position::save(GAGCore::OutputStream* stream) const
{stream->writeUint8(type(),"type");stream->writeSint32(x,"x");stream->writeSint32(y,"y");}

bool Sand::matches(Player* player,int x,int y) const
{ return player->map->isSand(x,y); }
bool Sand::equals(const Entity& other) const
{ return dynamic_cast<const Sand*>(&other)!=NULL; }
void Sand::save(GAGCore::OutputStream* stream) const{stream->writeUint8(type(),"type");}
}

void GradientInfo::add_source(Entities::Entity* source)
{ sources.push_back(shared_ptr<Entities::Entity>(source)); }
void GradientInfo::add_obstacle(Entities::Entity* obstacle)
{ obstacles.push_back(shared_ptr<Entities::Entity>(obstacle)); }
bool GradientInfo::matches_source(Player* p,int x,int y) const
{
	for(size_t i=0;i<sources.size();++i)
		if(sources[i]->matches(p,x,y)) return true;
	return false;
}
bool GradientInfo::matches_obstacle(Player* p,int x,int y) const
{
	for(size_t i=0;i<obstacles.size();++i)
		if(obstacles[i]->matches(p,x,y)) return true;
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
	if(sources.size()!=rhs.sources.size() || obstacles.size()!=rhs.obstacles.size()) return false;
	for(size_t i=0;i<sources.size();++i) if(!sources[i]->equals(*rhs.sources[i])) return false;
	for(size_t i=0;i<obstacles.size();++i) if(!obstacles[i]->equals(*rhs.obstacles[i])) return false;
	return true;
}
void GradientInfo::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GradientInfo");
	stream->writeUint32(sources.size(),"source_count");
	for(size_t i=0;i<sources.size();++i){stream->writeEnterSection(i);sources[i]->save(stream);stream->writeLeaveSection();}
	stream->writeUint32(obstacles.size(),"obstacle_count");
	for(size_t i=0;i<obstacles.size();++i){stream->writeEnterSection(i+sources.size());obstacles[i]->save(stream);stream->writeLeaveSection();}
	stream->writeLeaveSection();
}
bool GradientInfo::load(GAGCore::InputStream* stream)
{
	sources.clear();obstacles.clear();stream->readEnterSection("GradientInfo");
	Uint32 count=stream->readUint32("source_count");
	for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);sources.push_back(shared_ptr<Entities::Entity>(Entities::Entity::load(stream)));stream->readLeaveSection();}
	const Uint32 sourceCount=count;count=stream->readUint32("obstacle_count");
	for(Uint32 i=0;i<count;++i){stream->readEnterSection(i+sourceCount);obstacles.push_back(shared_ptr<Entities::Entity>(Entities::Entity::load(stream)));stream->readLeaveSection();}
	stream->readLeaveSection();return true;
}

Gradient::Gradient(const GradientInfo& info) : info(info),width(0),sourceCount(0) {}

void Gradient::recalculate(Player* player)
{
	Map* map=player->map;
	width=map->getW();
	const int height=map->getH();
	values.assign(width*height,UnreachableCell);
	sourceCount=0;
	std::vector<int> queue;
	queue.reserve(width*height);
	for(int x=0;x<width;++x)
		for(int y=0;y<height;++y)
		{
			const int at=y*width+x;
			if(info.matches_source(player,x,y)) { values[at]=SourceCell; queue.push_back(at);++sourceCount; }
			else if(info.matches_obstacle(player,x,y)) values[at]=ObstacleCell;
		}
	for(size_t head=0;head<queue.size();++head)
	{
		const int at=queue[head];
		const int px=at%width;
		const int py=at/width;
		const Sint16 next=values[at]+1;
		const int xs[3]={px==0?width-1:px-1,px,px+1==width?0:px+1};
		const int ys[3]={py==0?height-1:py-1,py,py+1==height?0:py+1};
		for(int dy=-1;dy<=1;++dy)
			for(int dx=-1;dx<=1;++dx)
			{
				if(dx==0 && dy==0) continue;
				const int nx=xs[dx+1];
				const int ny=ys[dy+1];
				Sint16& value=values[ny*width+nx];
				if(value==UnreachableCell) { value=next; queue.push_back(ny*width+nx); }
			}
	}
}

int Gradient::get_height(int x,int y) const
{
	if(width<=0 || values.empty()) return -2;
	if(x<0 || x>=width) x=(x%width+width)%width;
	if(y<0 || size_t(y)*width>=values.size())
	{
		const int height=values.size()/width;
		y=(y%height+height)%height;
	}
	return values[y*width+x]-SourceCell;
}

GradientManager::GradientManager(Player* player)
	: player(player),lastWorldStep(static_cast<Uint32>(-1)) {}
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
		gradients[index]->recalculate(player);
	}
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
	if((info.needs_updating() || ages[index]>150)
	   && queuedIndexes.insert(index).second)
		queued.push(index);
}
bool GradientManager::is_updated(const GradientInfo& info) const
{
	const int index=find(info);
	return index<0 || !info.needs_updating() || ages[index]<=150;
}
void GradientManager::update(Uint32 step)
{
	if(lastWorldStep==step) return;
	lastWorldStep=step;
	for(size_t i=0;i<ages.size();++i) ++ages[i];
	if(!queued.empty())
	{
		const int index=queued.front(); queued.pop();queuedIndexes.erase(index);
		if(index>=0 && index<int(gradients.size()) && ages[index]>50)
		{ gradients[index]->recalculate(player); ages[index]=0; }
	}
}
void GradientManager::saveExecutionState(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("GradientExecution95");
    AIMaximaContinuation::Writer archive(stream);
    archive("lastWorldStep",lastWorldStep);
    archive("ages",ages);
    stream->writeUint32(gradients.size(),"size");
    for(size_t i=0;i<gradients.size();++i)
    {
        stream->writeEnterSection(i);
        const Gradient& gradient=*gradients[i];
        gradient.info.save(stream);
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
void GradientManager::loadExecutionState(GAGCore::InputStream* stream)
{
    invalidate();
    stream->readEnterSection("GradientExecution95");
    AIMaximaContinuation::Reader archive(stream);
    archive("lastWorldStep",lastWorldStep);
    archive("ages",ages);
    const Uint32 size=stream->readUint32("size");
    if(size!=ages.size()) throw std::runtime_error("Invalid gradient continuation count");
    for(Uint32 i=0;i<size;++i)
    {
        stream->readEnterSection(i);
        GradientInfo info;
        if(!info.load(stream)) throw std::runtime_error("Invalid gradient continuation source");
        shared_ptr<Gradient> gradient(new Gradient(info));
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
	: x(-1),y(-1),type(-1),gid(NOGBID),age(-1),runtimeIdentity(0),issued(false),upgrading(false),upgradeSeen(false) {}

BuildingRegister::BuildingRegister(Player* player) : player(player),nextId(0) {}
void BuildingRegister::initiate()
{
	pendingBuildings.clear(); foundBuildings.clear(); nextId=0;
	for(int i=0;i<Building::MAX_COUNT;++i)
	{
		::Building* b=player->team->myBuildings[i]; if(!b) continue;
		BuildingRecord r; r.x=b->posX; r.y=b->posY; r.type=b->type->shortTypeNum; r.gid=b->gid;
		r.runtimeIdentity=b->getRuntimeIdentity();
		foundBuildings[nextId++]=r;
	}
}
}

namespace Conditions
{
Condition* Condition::load(GAGCore::InputStream* stream)
{
	stream->readEnterSection("Condition");
	const int kind=stream->readSint32("type");Condition* result=NULL;
	if(kind==0){const int id=stream->readSint32("id");result=new ParticularBuilding(BuildingCondition::load(stream),id);}
	else if(kind==1)result=new BuildingDestroyed(stream->readSint32("id"));
	else if(kind==2)result=new EnemyBuildingDestroyed(stream->readSint32("gid"));
	else if(kind==3){Condition* first=Condition::load(stream);Condition* second=Condition::load(stream);result=new EitherCondition(first,second);}
	stream->readLeaveSection();return result;
}
BuildingCondition* BuildingCondition::load(GAGCore::InputStream* stream)
{
	stream->readEnterSection("BuildingCondition");
	const int kind=stream->readSint32("type");BuildingCondition* result=NULL;
	switch(kind){case 0:result=new NotUnderConstruction;break;case 1:result=new UnderConstruction;break;case 2:result=new BeingUpgraded;break;case 3:result=new BeingUpgradedTo(stream->readSint32("value"));break;case 4:result=new SpecificBuildingType(stream->readSint32("value"));break;case 5:result=new BuildingLevel(stream->readSint32("value"));break;case 6:result=new Upgradable;break;default:break;}
	stream->readLeaveSection();return result;
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
Result EnemyBuildingDestroyed::passes(Context& context) const
{ return building_from_gid(context.player,gid)?Waiting:Ready; }
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
{ ::Building* b=c.get_building_register().get_building(id);return b&&b->constructionResultState==::Building::NO_CONSTRUCTION&&!c.get_building_register().is_building_upgrading(id); }
bool UnderConstruction::passes(Context& c,int id) const
{ ::Building* b=c.get_building_register().get_building(id);return b&&b->constructionResultState!=::Building::NO_CONSTRUCTION; }
bool BeingUpgraded::passes(Context& c,int id) const {return c.get_building_register().is_building_upgrading(id);}
bool BeingUpgradedTo::passes(Context& c,int id) const {return c.get_building_register().is_building_upgrading(id)&&c.get_building_register().get_level(id)==level-1;}
bool SpecificBuildingType::passes(Context& c,int id) const {return c.get_building_register().get_type(id)==buildingType;}
bool BuildingLevel::passes(Context& c,int id) const {return c.get_building_register().get_level(id)==level;}
bool Upgradable::passes(Context& c,int id) const
{ ::Building* b=c.get_building_register().get_building(id);return b&&b->type->nextLevel!=-1; }
void NotUnderConstruction::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void UnderConstruction::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void BeingUpgraded::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
void BeingUpgradedTo::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeSint32(level,"value");s->writeLeaveSection();}
void SpecificBuildingType::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeSint32(buildingType,"value");s->writeLeaveSection();}
void BuildingLevel::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeSint32(level,"value");s->writeLeaveSection();}
void Upgradable::save(GAGCore::OutputStream* s)const{s->writeEnterSection("BuildingCondition");s->writeSint32(type(),"type");s->writeLeaveSection();}
}

namespace Management
{
using Conditions::Condition;
using Conditions::Result;
using Conditions::Ready;
ResourceTracker::ResourceTracker(Context& context,int id,int length,int resource)
	: context(context),record(length,0),position(0),timer(0),buildingId(id),resource(resource) {}
void ResourceTracker::tick()
{
	++timer;if(timer%10)return;::Building* b=context.get_building_register().get_building(buildingId);if(!b||record.empty())return;record[position]=b->resources[resource];position=(position+1)%record.size();
}
int ResourceTracker::get_total_level() const
{int total=0;for(size_t i=0;i<record.size();++i)total+=record[i];return total;}
void ResourceTracker::save(GAGCore::OutputStream* stream) const
{
	stream->writeUint32(buildingId,"building_id");stream->writeSint32(resource,"resource");stream->writeUint32(position,"position");stream->writeSint32(timer,"timer");stream->writeUint32(record.size(),"size");for(size_t i=0;i<record.size();++i){stream->writeEnterSection(i);stream->writeSint32(record[i],"value");stream->writeLeaveSection();}
}
ResourceTracker* ResourceTracker::load(Context& context,GAGCore::InputStream* stream)
{
	const int id=stream->readUint32("building_id");const int resource=stream->readSint32("resource");const Uint32 position=stream->readUint32("position");const int timer=stream->readSint32("timer");const Uint32 size=stream->readUint32("size");
	ResourceTracker* tracker=new ResourceTracker(context,id,size,resource);tracker->position=size?position%size:0;tracker->timer=timer;
	for(Uint32 i=0;i<size;++i){stream->readEnterSection(i);tracker->record[i]=stream->readSint32("value");stream->readLeaveSection();}return tracker;
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
ManagementOrder* ManagementOrder::load(GAGCore::InputStream* stream)
{
	stream->readEnterSection("ManagementOrder");const int kind=stream->readSint32("type");ManagementOrder* order=NULL;
	switch(kind)
	{
		case 0:{const int workers=stream->readSint32("workers");const int id=stream->readSint32("id");order=new AssignWorkers(workers,id);break;}
		case 1:{const int worker=stream->readSint32("worker");const int explorer=stream->readSint32("explorer");const int warrior=stream->readSint32("warrior");order=new ChangeSwarm(worker,explorer,warrior,stream->readSint32("id"));break;}
		case 2:order=new DestroyBuilding(stream->readSint32("id"));break;
		case 3:{const int length=stream->readSint32("length");const int resource=stream->readSint32("resource");order=new AddResourceTracker(length,resource,stream->readSint32("id"));break;}
		case 4:{const int size=stream->readSint32("value");order=new ChangeFlagSize(size,stream->readSint32("id"));break;}
		case 5:{const int level=stream->readSint32("value");order=new ChangeFlagMinimumLevel(level,stream->readSint32("id"));break;}
		case 6:{const int x=stream->readSint32("x");const int y=stream->readSint32("y");order=new ChangeFlagPosition(x,y,stream->readSint32("id"));break;}
		case 7:case 8:{const AreaType area=static_cast<AreaType>(stream->readSint32("area_type"));const Uint32 count=stream->readUint32("location_count");if(kind==7){AddArea* areaOrder=new AddArea(area);for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);const int x=stream->readSint32("x");const int y=stream->readSint32("y");areaOrder->add_location(x,y);stream->readLeaveSection();}order=areaOrder;}else{RemoveArea* areaOrder=new RemoveArea(area);for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);const int x=stream->readSint32("x");const int y=stream->readSint32("y");areaOrder->add_location(x,y);stream->readLeaveSection();}order=areaOrder;}break;}
		case 9:{const int team=stream->readSint32("team");const OptionalBool allied=static_cast<OptionalBool>(stream->readSint32("allied"));const OptionalBool enemy=static_cast<OptionalBool>(stream->readSint32("enemy"));const OptionalBool market=static_cast<OptionalBool>(stream->readSint32("market"));const OptionalBool inn=static_cast<OptionalBool>(stream->readSint32("inn"));const OptionalBool other=static_cast<OptionalBool>(stream->readSint32("other"));order=new ChangeAlliances(team,allied,enemy,market,inn,other);break;}
		case 10:order=new UpgradeRepair(stream->readSint32("id"));break;
		case 11:{const RuntimeEvent::Type eventType=static_cast<RuntimeEvent::Type>(stream->readSint32("event_type"));const int first=stream->readSint32("first");const int second=stream->readSint32("second");order=new Notify(RuntimeEvent(eventType,first,second));break;}
		default:break;
	}
	const Uint32 count=stream->readUint32("condition_count");for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);Conditions::Condition* condition=Conditions::Condition::load(stream);if(order&&condition)order->add_condition(condition);else delete condition;stream->readLeaveSection();}
	stream->readLeaveSection();return order;
}

AssignWorkers::AssignWorkers(int workers,int id)
	:workers(workers>Building::MAX_UNIT_WORKING?Building::MAX_UNIT_WORKING:workers),id(id){}
Result AssignWorkers::wait(Context& c) const{return wait_for_building(c,id);}
void AssignWorkers::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderModifyBuilding(b->gid,workers)));}
void AssignWorkers::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(workers,"workers");s->writeSint32(id,"id");}
ChangeSwarm::ChangeSwarm(int worker,int explorer,int warrior,int id):worker(worker),explorer(explorer),warrior(warrior),id(id){}
Result ChangeSwarm::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeSwarm::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b){Sint32 ratios[NB_UNIT_TYPE]={worker,explorer,warrior};c.push_order(shared_ptr<Order>(new OrderModifySwarm(b->gid,ratios)));}}
void ChangeSwarm::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(worker,"worker");s->writeSint32(explorer,"explorer");s->writeSint32(warrior,"warrior");s->writeSint32(id,"id");}
DestroyBuilding::DestroyBuilding(int id):id(id){}
Result DestroyBuilding::wait(Context& c) const{return wait_for_building(c,id);}
void DestroyBuilding::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderDelete(b->gid)));}
void DestroyBuilding::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(id,"id");}
AddResourceTracker::AddResourceTracker(int length,int resource,int id):length(length),resource(resource),id(id){}
Result AddResourceTracker::wait(Context& c) const{return wait_for_building(c,id);}
void AddResourceTracker::modify(Context& c){c.add_resource_tracker(new ResourceTracker(c,id,length,resource),id);}
void AddResourceTracker::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(length,"length");s->writeSint32(resource,"resource");s->writeSint32(id,"id");}
ChangeFlagSize::ChangeFlagSize(int size,int id):size(size),id(id){}
Result ChangeFlagSize::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeFlagSize::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderModifyFlag(b->gid,size)));}
void ChangeFlagSize::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(size,"value");s->writeSint32(id,"id");}
ChangeFlagMinimumLevel::ChangeFlagMinimumLevel(int level,int id):level(level),id(id){}
Result ChangeFlagMinimumLevel::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeFlagMinimumLevel::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderModifyMinLevelToFlag(b->gid,level-1)));}
void ChangeFlagMinimumLevel::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(level,"value");s->writeSint32(id,"id");}
ChangeFlagPosition::ChangeFlagPosition(int x,int y,int id):x(x),y(y),id(id){}
Result ChangeFlagPosition::wait(Context& c) const{return wait_for_building(c,id);}
void ChangeFlagPosition::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b)c.push_order(shared_ptr<Order>(new OrderMoveFlag(b->gid,x,y,true)));}
void ChangeFlagPosition::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(x,"x");s->writeSint32(y,"y");s->writeSint32(id,"id");}

AddArea::AddArea(AreaType type):areaType(type){}
void AddArea::add_location(int x,int y){locations.push_back(position(x,y));}
Result AddArea::wait(Context&) const{return Ready;}
void AddArea::modify(Context& c)
{
	BrushAccumulator acc;for(size_t i=0;i<locations.size();++i)acc.applyBrush(BrushApplication(c.player->map->normalizeX(locations[i].x),c.player->map->normalizeY(locations[i].y),0),c.player->map);if(!acc.getApplicationCount())return;
	if(areaType==ClearingArea)c.push_order(shared_ptr<Order>(new OrderAlterClearArea(c.player->team->teamNumber,BrushTool::MODE_ADD,&acc,c.player->map)));
	else if(areaType==ForbiddenArea)c.push_order(shared_ptr<Order>(new OrderAlterForbidden(c.player->team->teamNumber,BrushTool::MODE_ADD,&acc,c.player->map)));
	else c.push_order(shared_ptr<Order>(new OrderAlterGuardArea(c.player->team->teamNumber,BrushTool::MODE_ADD,&acc,c.player->map)));
}
void AddArea::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(areaType,"area_type");s->writeUint32(locations.size(),"location_count");for(size_t i=0;i<locations.size();++i){s->writeEnterSection(i);s->writeSint32(locations[i].x,"x");s->writeSint32(locations[i].y,"y");s->writeLeaveSection();}}
RemoveArea::RemoveArea(AreaType type):areaType(type){}
void RemoveArea::add_location(int x,int y){locations.push_back(position(x,y));}
Result RemoveArea::wait(Context&) const{return Ready;}
void RemoveArea::modify(Context& c)
{
	BrushAccumulator acc;for(size_t i=0;i<locations.size();++i)acc.applyBrush(BrushApplication(c.player->map->normalizeX(locations[i].x),c.player->map->normalizeY(locations[i].y),0),c.player->map);if(!acc.getApplicationCount())return;
	if(areaType==ClearingArea)c.push_order(shared_ptr<Order>(new OrderAlterClearArea(c.player->team->teamNumber,BrushTool::MODE_DEL,&acc,c.player->map)));
	else if(areaType==ForbiddenArea)c.push_order(shared_ptr<Order>(new OrderAlterForbidden(c.player->team->teamNumber,BrushTool::MODE_DEL,&acc,c.player->map)));
	else c.push_order(shared_ptr<Order>(new OrderAlterGuardArea(c.player->team->teamNumber,BrushTool::MODE_DEL,&acc,c.player->map)));
}
void RemoveArea::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(areaType,"area_type");s->writeUint32(locations.size(),"location_count");for(size_t i=0;i<locations.size();++i){s->writeEnterSection(i);s->writeSint32(locations[i].x,"x");s->writeSint32(locations[i].y,"y");s->writeLeaveSection();}}
ChangeAlliances::ChangeAlliances(int team,OptionalBool allied,OptionalBool enemy,OptionalBool market,OptionalBool inn,OptionalBool other):team(team),allied(allied),enemy(enemy),market(market),inn(inn),other(other){}
Result ChangeAlliances::wait(Context&) const{return Ready;}
void ChangeAlliances::modify(Context& c)
{
	Team* t=c.player->game->teams[team];if(!t)return;
	struct Bit { static void apply(Uint32& mask,Uint32 bit,OptionalBool value){if(value==KeepValue)return;if(value==SetValue)mask|=bit;else mask&=~bit;} };
	Bit::apply(c.allies,t->me,allied);Bit::apply(c.enemies,t->me,enemy);Bit::apply(c.market_view,t->me,market);Bit::apply(c.inn_view,t->me,inn);Bit::apply(c.other_view,t->me,other);
	c.push_order(shared_ptr<Order>(new SetAllianceOrder(c.player->team->teamNumber,c.allies,c.enemies,c.market_view,c.inn_view,c.other_view)));
}
void ChangeAlliances::save_payload(GAGCore::OutputStream* s)const{s->writeSint32(team,"team");s->writeSint32(allied,"allied");s->writeSint32(enemy,"enemy");s->writeSint32(market,"market");s->writeSint32(inn,"inn");s->writeSint32(other,"other");}
UpgradeRepair::UpgradeRepair(int id):id(id){}
Result UpgradeRepair::wait(Context& c) const{return wait_for_building(c,id);}
void UpgradeRepair::modify(Context& c){::Building* b=c.get_building_register().get_building(id);if(b){c.push_order(shared_ptr<Order>(new OrderConstruction(b->gid,1,1)));c.get_building_register().set_upgrading(id);}}
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
void BuildingRegister::set_upgrading(int id)
{ std::map<int,BuildingRecord>::iterator i=foundBuildings.find(id); if(i!=foundBuildings.end()){i->second.upgrading=true;i->second.upgradeSeen=false;} }

void BuildingRegister::tick()
{
	for(std::map<int,BuildingRecord>::iterator i=pendingBuildings.begin();i!=pendingBuildings.end();)
	{
		BuildingRecord& r=i->second;
		if(r.issued)
		{
			if(++r.age>300) { pendingBuildings.erase(i++); continue; }
			int gid=NOGBID;
			if(is_flag_type(r.type))
			{
				for(int b=0;b<Building::MAX_COUNT;++b)
				{
					::Building* candidate=player->team->myBuildings[b];
					if(candidate && candidate->posX==r.x && candidate->posY==r.y
					   && candidate->type->shortTypeNum==r.type) { gid=candidate->gid; break; }
				}
			}
			else gid=player->map->getBuilding(r.x,r.y);
			::Building* found=building_from_gid(player,gid);
			if(found && Building::GIDtoTeam(gid)==player->team->teamNumber
			   && found->type->shortTypeNum==r.type)
			{
				r.gid=gid; r.runtimeIdentity=found->getRuntimeIdentity();
				foundBuildings[i->first]=r; pendingBuildings.erase(i++); continue;
			}
		}
		++i;
	}
	for(std::map<int,BuildingRecord>::iterator i=foundBuildings.begin();i!=foundBuildings.end();)
	{
		BuildingRecord& r=i->second;
		::Building* b=get_building(i->first);
		if(!b) { foundBuildings.erase(i++); continue; }
		r.x=b->posX; r.y=b->posY; r.type=b->type->shortTypeNum;
		if(r.upgrading)
		{
			if(b->constructionResultState!=::Building::NO_CONSTRUCTION) r.upgradeSeen=true;
			// Context drains engine orders before ticking the register. If the
			// engine never entered construction, the dispatched order was rejected;
			// waiting to observe a site would keep this flag set forever.
			else { r.upgrading=false; r.upgradeSeen=false; }
		}
		++i;
	}
}
bool BuildingRegister::is_building_pending(unsigned id) const { return pendingBuildings.count(id)!=0; }
bool BuildingRegister::is_building_found(unsigned id) const { return get_building(id)!=NULL; }
bool BuildingRegister::is_building_upgrading(unsigned id) const
{ std::map<int,BuildingRecord>::const_iterator i=foundBuildings.find(id); return i!=foundBuildings.end() && i->second.upgrading; }
::Building* BuildingRegister::get_building(unsigned id) const
{
	std::map<int,BuildingRecord>::const_iterator i=foundBuildings.find(id);
	if(i==foundBuildings.end()) return NULL;
	::Building* building=building_from_gid(player,i->second.gid);
	return building && building->buildingState!=::Building::DEAD
		&& building->getRuntimeIdentity()==i->second.runtimeIdentity ? building : NULL;
}
::BuildingType* BuildingRegister::get_building_type(unsigned id) const
{ ::Building* b=get_building(id); return b?b->type:NULL; }
int BuildingRegister::get_type(unsigned id) const
{ std::map<int,BuildingRecord>::const_iterator i=foundBuildings.find(id); return i==foundBuildings.end()?0:i->second.type; }
int BuildingRegister::get_level(unsigned id) const { ::Building* b=get_building(id); return b?b->type->level+1:0; }
int BuildingRegister::get_assigned(unsigned id) const { ::Building* b=get_building(id); return b?b->maxUnitWorking:0; }
int BuildingRegister::get_enrolled(unsigned id) const { ::Building* b=get_building(id); return b?static_cast<int>(b->unitsWorking.size()):0; }
int BuildingRegister::get_on_site(unsigned id) const
{
	::Building* b=get_building(id);
	if(!b)return 0;
	const int range=b->unitStayRange+1;
	int result=0;
	for(std::list<Unit*>::const_iterator unit=b->unitsWorking.begin();
		unit!=b->unitsWorking.end();++unit)
		if(*unit && player->map->warpDistSquare(b->posX,b->posY,
			(*unit)->posX,(*unit)->posY)<range*range)++result;
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
		stream->writeSint32(r.x,"x");stream->writeSint32(r.y,"y");stream->writeSint32(r.type,"type");stream->writeSint32(r.gid,"gid");stream->writeUint8(r.upgrading,"upgrading");stream->writeUint8(r.upgradeSeen,"upgrade_seen");stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
bool BuildingRegister::load(GAGCore::InputStream* stream)
{
	pendingBuildings.clear(); foundBuildings.clear(); stream->readEnterSection("V3BuildingRegister");
	nextId=stream->readUint32("next_id"); Uint32 size=stream->readUint32("pending_size");
	for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);int id=stream->readSint32("id");BuildingRecord r;r.x=stream->readSint32("x");r.y=stream->readSint32("y");r.type=stream->readSint32("type");r.gid=stream->readSint32("gid");r.age=stream->readSint32("age");r.issued=stream->readUint8("issued");pendingBuildings[id]=r;stream->readLeaveSection();}
	size=stream->readUint32("found_size");
	for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);int id=stream->readSint32("id");BuildingRecord r;r.x=stream->readSint32("x");r.y=stream->readSint32("y");r.type=stream->readSint32("type");r.gid=stream->readSint32("gid");r.upgrading=stream->readUint8("upgrading");r.upgradeSeen=stream->readUint8("upgrade_seen");foundBuildings[id]=r;stream->readLeaveSection();}
	for(auto& record:foundBuildings)
	{
		::Building* building=building_from_gid(player,record.second.gid);
		if(building) record.second.runtimeIdentity=building->getRuntimeIdentity();
	}
	stream->readLeaveSection(); return true;
}

Constraint* Constraint::load(GAGCore::InputStream* stream)
{
	stream->readEnterSection("Constraint");
	const int kind=stream->readSint32("type");
	Constraint* result=NULL;
	if(kind>=0&&kind<=3)
	{
		Gradients::GradientInfo info;info.load(stream);
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
{ ::Building* b=building_from_gid(c.player,gid); return b && c.player->map->normalizeX(b->posX+b->type->width/2)==x && c.player->map->normalizeY(b->posY+b->type->height/2)==y; }
bool CenterOfBuilding::exact_position(Context& c,int& x,int& y)
{ ::Building* b=building_from_gid(c.player,gid);if(!b)return false;x=c.player->map->normalizeX(b->posX+b->type->width/2);y=c.player->map->normalizeY(b->posY+b->type->height/2);return true; }
void CenterOfBuilding::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");s->writeSint32(gid,"gid");s->writeLeaveSection();}
int SinglePosition::score(Context&,int,int){return 0;}
bool SinglePosition::passes(Context& c,int px,int py)
{return px==c.player->map->normalizeX(x)&&py==c.player->map->normalizeY(y);}
bool SinglePosition::exact_position(Context&,int& px,int& py){px=x;py=y;return true;}
void SinglePosition::save(GAGCore::OutputStream* s)const{s->writeEnterSection("Constraint");s->writeSint32(type(),"type");s->writeSint32(x,"x");s->writeSint32(y,"y");s->writeLeaveSection();}

BuildingOrder::BuildingOrder(int type,int workers):type(type),workers(workers),id(-1),searchCursor(0),searchWidth(0),searchHeight(0),searchBestScore(INT_MIN),searchBest(-1,-1),searchActive(false){}
void BuildingOrder::save(GAGCore::OutputStream* s) const
{
	s->writeEnterSection("BuildingOrder");s->writeSint32(type,"building_type");s->writeSint32(workers,"workers");s->writeSint32(id,"id");
	s->writeUint32(constraints.size(),"constraint_count");for(size_t i=0;i<constraints.size();++i){s->writeEnterSection(i);constraints[i]->save(s);s->writeLeaveSection();}
	s->writeUint32(conditions.size(),"condition_count");for(size_t i=0;i<conditions.size();++i){s->writeEnterSection(i+constraints.size());conditions[i]->save(s);s->writeLeaveSection();}
	s->writeLeaveSection();
}
BuildingOrder* BuildingOrder::load(GAGCore::InputStream* s)
{
	s->readEnterSection("BuildingOrder");const int buildingType=s->readSint32("building_type");const int workerCount=s->readSint32("workers");BuildingOrder* order=new BuildingOrder(buildingType,workerCount);order->id=s->readSint32("id");
	Uint32 count=s->readUint32("constraint_count");for(Uint32 i=0;i<count;++i){s->readEnterSection(i);order->add_constraint(Constraint::load(s));s->readLeaveSection();}
	const Uint32 offset=count;count=s->readUint32("condition_count");for(Uint32 i=0;i<count;++i){s->readEnterSection(i+offset);order->add_condition(Conditions::Condition::load(s));s->readLeaveSection();}
	s->readLeaveSection();return order;
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
bool BuildingOrder::score_location(Context& context,BuildingType* bt,bool flag,
	int x,int y,int& total)
{
	Map* map=context.player->map;
	if(!flag&&!map->isHardSpaceForBuilding(x,y,bt->width,bt->height))return false;
	if(flag)
	{
		for(int b=0;b<Building::MAX_COUNT;++b){::Building* f=context.player->team->myBuildings[b];if(f&&is_flag_type(f->type->shortTypeNum)&&f->posX==x&&f->posY==y)return false;}
	}
	bool ok=true;total=0;
	for(size_t ci=0;ci<constraints.size()&&ok;++ci)
	{
		for(int dx=0;dx<bt->width&&ok;++dx)for(int dy=0;dy<bt->height&&ok;++dy)if(dx==0||dy==0||dx==bt->width-1||dy==bt->height-1)ok=constraints[ci]->passes(context,map->normalizeX(x+dx),map->normalizeY(y+dy));
		if(!flag&&(!map->isMapDiscovered(x,y,context.player->team->allies)||!map->isMapDiscovered(x+bt->width-1,y+bt->height-1,context.player->team->allies)))ok=false;
		if(ok){total+=constraints[ci]->score(context,map->normalizeX(x),map->normalizeY(y));total+=constraints[ci]->score(context,map->normalizeX(x+bt->width-1),map->normalizeY(y));total+=constraints[ci]->score(context,map->normalizeX(x),map->normalizeY(y+bt->height-1));total+=constraints[ci]->score(context,map->normalizeX(x+bt->width-1),map->normalizeY(y+bt->height-1));}
	}
	return ok;
}
PlacementResult BuildingOrder::find_location(Context& context,int cellBudget,
	bool& complete)
{
	Map* map=context.player->map; BuildingType* bt=globalContainer->buildingsTypes.getByType(IntBuildingType::typeFromShortNumber(type),0,true); bool flag=false;
	if(!bt){bt=globalContainer->buildingsTypes.getByType(IntBuildingType::typeFromShortNumber(type),0,false);flag=true;}
	if(!bt){complete=true;reset_search();return PlacementResult();}

	// Most tactical flags already carry an exact coordinate.  Resolving it here
	// turns the former O(map area) scan into one validation without changing the
	// chosen location or the constraint scoring order.
	bool hasExact=false;position exact;
	for(size_t ci=0;ci<constraints.size();++ci)
	{
		int x=0,y=0;
		if(!constraints[ci]->exact_position(context,x,y))continue;
		x=map->normalizeX(x);y=map->normalizeY(y);
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

	const int width=map->getW(),height=map->getH();
	if(!searchActive||searchWidth!=width||searchHeight!=height)
	{reset_search();searchActive=true;searchWidth=width;searchHeight=height;}
	const int end=std::min(width*height,searchCursor+std::max(1,cellBudget));
	for(;searchCursor<end;++searchCursor)
	{
		// Preserve the legacy x-major/y-minor traversal exactly so ties select the
		// same coordinate when the world is unchanged during the bounded search.
		const int x=searchCursor/height,y=searchCursor%height;int score=0;
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
		Team* candidate=context->player->game->teams[team];
		if(candidate&&(context->player->team->enemies&candidate->me))return;
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
	if(ended)return; Team* owner=(team>=0&&team<Team::MAX_COUNT)?context->player->game->teams[team]:NULL;
	if(!owner){ended=true;return;}
	for(++index;index<Building::MAX_COUNT;++index)
	{
		::Building* b=owner->myBuildings[index];if(!b||!(b->seenByMask&context->player->team->me))continue;
		if(type!=-1&&b->type->shortTypeNum!=type)continue;
		if(level!=-1&&b->type->level!=level-1)continue;
		if(construction!=AnyConstruction&&int(construction)!=int(bool(b->type->isBuildingSite)))continue;
		gid=b->gid;return;
	}
	ended=true;
}
unsigned enemy_building_iterator::operator*() const{return gid;}
enemy_building_iterator& enemy_building_iterator::operator++(){advance();return *this;}
bool enemy_building_iterator::operator!=(const enemy_building_iterator& rhs) const
{if(ended||rhs.ended)return ended!=rhs.ended;return context!=rhs.context||team!=rhs.team||index!=rhs.index;}

MapInfo::MapInfo(Context& context):context(context){}
int MapInfo::get_width()const{return context.player->map->getW();}
int MapInfo::get_height()const{return context.player->map->getH();}
bool MapInfo::is_forbidden_area(int x,int y)const{return context.player->map->isForbidden(x,y,context.player->team->me);}
bool MapInfo::is_guard_area(int x,int y)const{return context.player->map->isGuardArea(x,y,context.player->team->me);}
bool MapInfo::is_clearing_area(int x,int y)const{return context.player->map->isClearArea(x,y,context.player->team->me);}
bool MapInfo::is_discovered(int x,int y)const{return context.player->map->isMapDiscovered(x,y,context.player->team->me);}
bool MapInfo::is_resource(int x,int y,int type)const{return context.player->map->isResourceTakeable(x,y,type);}
bool MapInfo::is_resource(int x,int y)const{return context.player->map->isResource(x,y);}
bool MapInfo::is_water(int x,int y)const{return context.player->map->isWater(x,y);}
bool MapInfo::is_sand(int x,int y)const{return context.player->map->isSand(x,y);}
bool MapInfo::is_grass(int x,int y)const{return context.player->map->isGrass(x,y);}
bool MapInfo::backs_onto_sand(int x,int y)const
{for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if((dx||dy)&&is_sand(x+dx,y+dy))return true;return false;}
int MapInfo::get_ammount_resource(int x,int y)const{return context.player->map->getResource(x,y).amount;}
}

Context::Context(Player* player)
	:player(player),allies(0),enemies(0),inn_view(0),market_view(0),other_view(0),activeAI(NULL),buildings(player),gradients(player),nullOrder(new NullOrder()),timer(0),previousBuildingId(-1),initialized(false),fruitOnMap(false),profileAiMicros(0),profileHousekeepingMicros(0),profileBuildingSearchMicros(0),profileBuildingSearchMaxMicros(0),profileBuildingSearchCalls(0){}

void Context::initialize()
{
	buildings.initiate();detect_fruit();allies=player->team->allies;enemies=player->team->enemies;
	market_view=player->team->sharedVisionExchange;inn_view=player->team->sharedVisionFood;other_view=player->team->sharedVisionOther;initialized=true;
}
void Context::detect_fruit()
{
	fruitOnMap=false;for(int x=0;x<player->map->getW()&&!fruitOnMap;++x)for(int y=0;y<player->map->getH();++y)if(player->map->isResourceTakeable(x,y,CHERRY)||player->map->isResourceTakeable(x,y,ORANGE)||player->map->isResourceTakeable(x,y,PRUNE)){fruitOnMap=true;break;}
}
unsigned Context::add_building_order(Construction::BuildingOrder* order)
{
	buildingOrders.push_back(shared_ptr<Construction::BuildingOrder>(order));order->queue_gradients(gradients);order->id=buildings.register_building();return order->id;
}

bool Context::get_building_position(int id, int& x, int& y)
{
	if(::Building* building=buildings.get_building(id))
	{ x=building->posX; y=building->posY; return true; }
	const auto pending=buildings.pending().find(id);
	if(pending==buildings.pending().end()) return false;
	if(pending->second.issued)
	{ x=pending->second.x; y=pending->second.y; return true; }
	for(const auto& order:buildingOrders)
		if(order->id==id)
			for(const auto& constraint:order->constraints)
				if(constraint->exact_position(*this,x,y))
				{ x=player->map->normalizeX(x); y=player->map->normalizeY(y); return true; }
	return false;
}

void Context::cancel_or_destroy_building(int id)
{
	for(size_t i=0; i<buildingOrders.size(); ++i)
		if(buildingOrders[i]->id==id)
		{
			buildingOrders.erase(buildingOrders.begin()+i);
			buildings.remove_building(id);
			return;
		}
	if(buildings.is_building_found(id) || buildings.is_building_pending(id))
		add_management_order(new Management::DestroyBuilding(id));
}

std::vector<int> Context::resource_flags(int resource) const
{
	std::set<int> ids;
	Gradients::GradientInfo source;
	source.add_source(new Gradients::Entities::Resource(resource));
	for(size_t i=0; i<buildingOrders.size(); ++i)
	{
		const Construction::BuildingOrder& order=*buildingOrders[i];
		if(order.type!=IntBuildingType::EXPLORATION_FLAG) continue;
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
			if(record.type!=IntBuildingType::EXPLORATION_FLAG) continue;
			::Building* flag=buildings.get_building(i->first);
			if(group==1 && !flag) continue;
			const int x=flag ? flag->posX : record.x;
			const int y=flag ? flag->posY : record.y;
			if(x>=0 && y>=0 && player->map->getResource(x,y).type==resource)
				ids.insert(i->first);
		}
	return std::vector<int>(ids.begin(), ids.end());
}
int Context::issue_building_at(int shortType,int workers,int x,int y)
{
	BuildingType* site=globalContainer->buildingsTypes.getByType(
		IntBuildingType::typeFromShortNumber(shortType),0,true);
	if(!site || !player->map->isHardSpaceForBuilding(x,y,site->width,site->height)
	   || !player->map->isMapDiscovered(x,y,player->team->allies)
	   || !player->map->isMapDiscovered(x+site->width-1,y+site->height-1,
		player->team->allies))
		return -1;
	const int id=buildings.register_building();
	buildings.issue_order(id,x,y,shortType);
	Management::AssignWorkers* assignment=new Management::AssignWorkers(workers,id);
	assignment->add_condition(new Conditions::ParticularBuilding(
		new Conditions::UnderConstruction,id));
	add_management_order(assignment);
	const int engineType=globalContainer->buildingsTypes.getTypeNum(
		IntBuildingType::reverseConversionMap[shortType],0,true);
	push_order(shared_ptr<Order>(new OrderCreate(player->team->teamNumber,x,y,
		engineType,1,1)));
	previousBuildingId=id;
	return id;
}
bool Context::issue_upgrade_repair(int id,bool repair)
{
	::Building* building=buildings.get_building(id);
	if(!building || building->type->isBuildingSite
	   || building->constructionResultState!=::Building::NO_CONSTRUCTION)
		return false;
	if(repair)
	{
		if(building->hp>=building->type->hpMax
		   || !building->isHardSpaceForBuildingSite(::Building::REPAIR))return false;
	}
	else
	{
		if(building->hp<building->type->hpMax || building->type->nextLevel<0
		   || !building->isHardSpaceForBuildingSite(::Building::UPGRADE))return false;
	}
	push_order(shared_ptr<Order>(new OrderConstruction(building->gid,1,1)));
	buildings.set_upgrading(id);
	return true;
}
void Context::add_management_order(Management::ManagementOrder* order){managementOrders.push_back(shared_ptr<Management::ManagementOrder>(order));}
void Context::add_resource_tracker(Management::ResourceTracker* tracker,int id){trackers[id]=shared_ptr<Management::ResourceTracker>(tracker);}
shared_ptr<Management::ResourceTracker> Context::get_resource_tracker(int id)
{std::map<int,shared_ptr<Management::ResourceTracker> >::iterator i=trackers.find(id);return i==trackers.end()?shared_ptr<Management::ResourceTracker>():i->second;}
TeamStat& Context::get_team_stats(){return *player->team->stats.getLatestStat();}
void Context::dispatch_event(const RuntimeEvent& event){if(activeAI)activeAI->handle_event(*this,event);}

void Context::update_trackers()
{
	for(std::map<int,shared_ptr<Management::ResourceTracker> >::iterator i=trackers.begin();i!=trackers.end();)
	{if(!buildings.is_building_found(i->first)&&!buildings.is_building_pending(i->first)){trackers.erase(i++);continue;}if(buildings.is_building_found(i->first))i->second->tick();++i;}
}
void Context::update_management_orders()
{
	for(size_t i=0;i<managementOrders.size();)
	{
		Conditions::Result result=managementOrders[i]->ready(*this);
		if(result==Conditions::Ready){shared_ptr<Management::ManagementOrder> current=managementOrders[i];managementOrders.erase(managementOrders.begin()+i);current->modify(*this);}
		else if(result==Conditions::Impossible)managementOrders.erase(managementOrders.begin()+i);
		else ++i;
	}
}
void Context::update_building_orders()
{
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
		if(!complete)break;
		if(!placement.found){buildings.remove_building(buildingOrders[i]->id);buildingOrders.erase(buildingOrders.begin()+i);continue;}
		const position p=placement.value;
		const int shortType=buildingOrders[i]->type;const int id=buildingOrders[i]->id;buildings.issue_order(id,p.x,p.y,shortType);
		Sint32 engineType;
		if(is_flag_type(shortType))engineType=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[shortType],0,false);
		else engineType=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[shortType],0,true);
		Management::AssignWorkers* assignment=new Management::AssignWorkers(buildingOrders[i]->workers,id);
		if(!is_flag_type(shortType))assignment->add_condition(new Conditions::ParticularBuilding(new Conditions::UnderConstruction,id));
		add_management_order(assignment);
		// Flags take effect immediately. Honor their initial staffing before any
		// later management order can install a clearing-resource selector.
		const int initialWorkers=is_flag_type(shortType)?buildingOrders[i]->workers:1;
		push_order(shared_ptr<Order>(new OrderCreate(player->team->teamNumber,p.x,p.y,
			engineType,initialWorkers,initialWorkers)));
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
	std::cout<<"MAXIMA_TELEMETRY\t"<<timer<<"\t"
		<<player->team->teamNumber<<"\truntime_performance"
		<<"\tsamples="<<n
		<<"\ttick_p50_us="<<sorted[(n*50)/100]
		<<"\ttick_p95_us="<<sorted[(n*95)/100]
		<<"\ttick_p99_us="<<sorted[(n*99)/100]
		<<"\ttick_max_us="<<sorted[n-1]
		<<"\tai_total_us="<<profileAiMicros
		<<"\thousekeeping_total_us="<<profileHousekeepingMicros
		<<"\tbuilding_search_total_us="<<profileBuildingSearchMicros
		<<"\tbuilding_search_max_us="<<profileBuildingSearchMaxMicros
		<<"\tbuilding_search_calls="<<profileBuildingSearchCalls<<std::endl;
	profileTickMicros.clear();profileAiMicros=profileHousekeepingMicros=0;
	profileBuildingSearchMicros=profileBuildingSearchMaxMicros=0;
	profileBuildingSearchCalls=0;
}

shared_ptr<Order> Context::getOrder(RuntimeAI& ai)
{
	const bool profiling=globalContainer&&globalContainer->nicowarTelemetry;
	const std::chrono::steady_clock::time_point totalStarted=profiling
		?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
	activeAI=&ai;if(!initialized)initialize();gradients.update(player->game->stepCounter);
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
	const bool housekeepingDue=((timer+player->team->teamNumber)&3)==0;
	long long housekeepingMicros=0,buildingSearchMicros=-1;
	std::chrono::steady_clock::time_point phaseStarted;
	if(profiling)phaseStarted=std::chrono::steady_clock::now();
	if(housekeepingDue)
		buildings.tick();
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
	stream->writeEnterSection("V3Runtime");stream->writeSint32(timer,"timer");stream->writeSint32(previousBuildingId,"previous_building_id");stream->writeUint8(initialized,"initialized");stream->writeUint8(fruitOnMap,"fruit_on_map");stream->writeUint32(allies,"allies");stream->writeUint32(enemies,"enemies");stream->writeUint32(inn_view,"inn_view");stream->writeUint32(market_view,"market_view");stream->writeUint32(other_view,"other_view");
	stream->writeEnterSection("orders");stream->writeUint32(orders.size(),"size");unsigned n=0;for(std::list<shared_ptr<Order> >::const_iterator i=orders.begin();i!=orders.end();++i,++n){stream->writeEnterSection(n);stream->writeUint32((*i)->getDataLength(),"size");stream->writeUint8((*i)->getOrderType(),"type");stream->write((*i)->getData(),(*i)->getDataLength(),"data");stream->writeLeaveSection();}stream->writeLeaveSection();
	buildings.save(stream);
	stream->writeEnterSection("building_orders");stream->writeUint32(buildingOrders.size(),"size");for(size_t i=0;i<buildingOrders.size();++i){stream->writeEnterSection(i);buildingOrders[i]->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("management_orders");stream->writeUint32(managementOrders.size(),"size");for(size_t i=0;i<managementOrders.size();++i){stream->writeEnterSection(i);managementOrders[i]->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("trackers");stream->writeUint32(trackers.size(),"size");n=0;for(std::map<int,shared_ptr<Management::ResourceTracker> >::const_iterator i=trackers.begin();i!=trackers.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first,"id");i->second->save(stream);stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeLeaveSection();
}
void Context::saveExecutionState(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("RuntimeExecution95");
    AIMaximaContinuation::Writer archive(stream);
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
    stream->readEnterSection("RuntimeExecution95");
    AIMaximaContinuation::Reader archive(stream);
    const Uint32 size=stream->readUint32("size");
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
    gradients.loadExecutionState(stream);
    if(versionMinor>=92)
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
	stream->readEnterSection("V3Runtime");timer=stream->readSint32("timer");previousBuildingId=stream->readSint32("previous_building_id");initialized=stream->readUint8("initialized");fruitOnMap=stream->readUint8("fruit_on_map");allies=stream->readUint32("allies");enemies=stream->readUint32("enemies");inn_view=stream->readUint32("inn_view");market_view=stream->readUint32("market_view");other_view=stream->readUint32("other_view");
	orders.clear();stream->readEnterSection("orders");Uint32 size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);Uint32 length=stream->readUint32("size");std::vector<Uint8> data(length+1);data[0]=stream->readUint8("type");stream->read(&data[1],length,"data");orders.push_back(Order::getOrder(&data[0],length+1,versionMinor));stream->readLeaveSection();}stream->readLeaveSection();
	buildings.load(stream);gradients.invalidate();
	buildingOrders.clear();stream->readEnterSection("building_orders");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);shared_ptr<Construction::BuildingOrder> order(Construction::BuildingOrder::load(stream));if(order){order->queue_gradients(gradients);buildingOrders.push_back(order);}stream->readLeaveSection();}stream->readLeaveSection();
	managementOrders.clear();stream->readEnterSection("management_orders");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);shared_ptr<Management::ManagementOrder> order(Management::ManagementOrder::load(stream));if(order)managementOrders.push_back(order);stream->readLeaveSection();}stream->readLeaveSection();
	trackers.clear();stream->readEnterSection("trackers");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int id=stream->readSint32("id");trackers[id]=shared_ptr<Management::ResourceTracker>(Management::ResourceTracker::load(*this,stream));stream->readLeaveSection();}stream->readLeaveSection();
	stream->readLeaveSection();return true;
}

}
