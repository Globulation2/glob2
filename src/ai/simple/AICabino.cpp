// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2005-2007 Bradley Arsenault

#include "field/UniformTraversal.h"
#include "AITelemetryFields.h"
#include <Stream.h>

#include "AICabino.h"
#include "ai/engine/AIDecision.h"
#include "ai/observation/OrderSelection.h"
#include "ai/observation/ObservationAreaOrders.h"
#include "AIStateSerialization.h"
#include "OrderMessages.h"
#include "Game.h"
#include "FileFormatVersions.h"
#include "Version.h"
#include <span>
#include "Building.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "AIRuleOrders.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"
#include "Ressource.h"
#include <algorithm>
#include <iterator>
#include "Brush.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

using namespace Cabino;

AIPlanning::BuildingIntent Cabino::intentForDemand(unsigned demand)
{
 using I=AIPlanning::BuildingIntent;
 static constexpr I intents[]={I::ProduceWorker,I::Feed,I::Heal,I::TrainWalk,I::TrainSwim,
  I::TrainAttackStrength,I::TrainConstruction,I::ProjectileDefense,I::AttractExplorers,
  I::AttractWarriors,I::ClearResources,I::ExchangeResources};
 assert(demand < DemandCount);
 return intents[demand];
}
bool Cabino::provides(const AIEngine::AIWorldView& game,const AIEngine::BuildingView& b,unsigned demand)
{
 return game.capabilities().matches(AIEngine::ObservationQueries::buildingType(game,b).isBuildingSite ? AIEngine::ObservationQueries::buildingType(game,b).nextLevel : b.typeNum,intentForDemand(demand));
}
int Cabino::selectBuilding(AICabino& ai,unsigned demand)
{
 const auto& index=ai.game->capabilities();
 const auto intent=intentForDemand(demand);
 int chosen=-1,count=0;
 for (const auto& c:index.placements(intent))
  if (AIEngine::ObservationQueries::available(*ai.game,c,intent) && ai.random()%++count==0) chosen=c.placementType;
 return chosen;
}
unsigned Cabino::upgradeWeight(const AIEngine::AIWorldView& game,const AIEngine::BuildingView& b)
{
 unsigned weight=0;
 for(unsigned demand=0;demand<DemandCount;++demand)
  if(provides(game,b,demand)) weight=std::max(weight,BUILDING_UPGRADE_WEIGHTS[demand]);
 return weight;
}
namespace {
template<class X,class Y> bool placeRallyNear(AICabino& ai,int type,X& x,Y& y)
{
 const int originX=x,originY=y;
 for(int radius=0;radius<=8;++radius)
  for(int dx=-radius;dx<=radius;++dx)
   for(int dy=-radius;dy<=radius;++dy)
    if(AIEngine::ObservationQueries::roomForBuilding(*ai.game,originX+dx,originY+dy,ai.game->catalog->at(type).resolvedType,ai.team->number)) {
     x=(originX+dx)&(ai.map->width-1); y=(originY+dy)&(ai.map->height-1); return true;
    }
 return false;
}
unsigned feedingStock(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& building,bool capacity)
{
 unsigned stock=0;
 for(int resource=0;resource<MAX_NB_RESOURCES;++resource)
  if(AIEngine::ObservationQueries::buildingType(world,building).semantics.feeding.cost[resource]>0)
   stock+=capacity ? AIEngine::ObservationQueries::buildingType(world,building).maxResource[resource] : world.buildingResources(building)[resource];
 return stock;
}
void retireRally(AICabino& ai,unsigned gid)
{
 auto* building=getBuildingFromGid(ai.game,gid);
 if(!building) return;
 const auto& s=AIEngine::ObservationQueries::buildingType(*ai.game,*building).semantics;
 if(s.feeding.enabled || s.healing.enabled || AIEngine::ObservationQueries::buildingType(*ai.game,*building).shootingRange>0
  || s.market.interTeamFruitExchange || s.market.suppliesStock || s.market.suppliesDirectStock
  || std::any_of(s.production.recipes.begin(),s.production.recipes.end(),[](const auto& r){return r.enabled;})
  || std::any_of(s.training.begin(),s.training.end(),[](const auto& r){return r.enabled;})) return;
 if(s.instantPlacement && !s.occupiesGround) ai.enqueueOrder(std::make_shared<OrderDelete>(gid));
 else ai.enqueueOrder(std::make_shared<OrderModifyBuilding>(gid,0));
}
int legacyConcrete(const AIEngine::AIWorldView& game,unsigned family,unsigned level,bool site)
{
 static constexpr const char* names[]={"swarm","inn","hospital","racetrack","swimmingpool","barracks","school","defencetower","explorationflag","warflag","clearingflag","stonewall","market"};
 if(family>=std::size(names)) return -1;
 for(size_t i=0;i<game.catalog->size();++i) {
  const auto* type=(&game.catalog->at(i).resolvedType);
  if(type->key==names[family] && type->level==int(level) && type->isBuildingSite==site) return int(i);
 }
 return -1;
}
}

AICabino::AICabino(Player *player)
{
	init(player);
}




AICabino::AICabino(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
    : AICabino(player)
{
	bool goodLoad=load(stream, player, versionMinor);
	if (!goodLoad) throw std::runtime_error("Invalid saved AI");
}




void AICabino::init(Player *player)
{
	for (auto* module : modules) delete module;
	modules.clear();
	other_modules.clear();
	orders = {};
	gradient_manager.clear();
	timer=0;
	iteration=0;
	center_x=0;
	center_y=0;
	module_timer=0;

	assert(player);

	attack_module=NULL;
	defense_module=NULL;
	new_construction_module=NULL;
	upgrade_repair_module=NULL;
	unit_module=NULL;

	this->player=player;
	const auto view=AIEngine::AIWorldView::capture(*player->game, AIEngine::AIWorldView::captureCatalog(*player->game));
    team=&view->teams[player->teamNumber]; game=view.get(); map=view.get();
    struct Reset { AICabino& ai; ~Reset(){ ai.team=nullptr; ai.game=nullptr; ai.map=nullptr;} } reset{*this};

	gradient_manager.setTeam(this);

	new BasicDistributedSwarmManager(*this);
	new PrioritizedBuildingAttack(*this);
	new SimpleBuildingDefense(*this);
	new DistributedNewConstructionManager(*this);
	new RandomUpgradeRepairModule(*this);
	new ExplorationManager(*this);
	new InnManager(*this);
	new TowerController(*this);
	new BuildingClearer(*this);
	new HappinessHandler(*this);
	new Farmer(*this);
	active_module=modules.end();

	assert(this->team);
	assert(this->game);
	assert(this->map);
}




AICabino::~AICabino()
{
	for(std::vector<Module*>::iterator i=modules.begin(); i!=modules.end(); ++i)
	{
		if(*i)
			delete *i;
	}
}




bool AICabino::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	for (auto* module : modules) delete module;
	modules.clear();
	while (!orders.empty()) orders.pop();
	init(player);
    const auto view=AIEngine::AIWorldView::capture(*player->game, AIEngine::AIWorldView::captureCatalog(*player->game));
    team=&view->teams[player->teamNumber]; game=view.get(); map=view.get();
    struct Reset { AICabino& ai; ~Reset(){ ai.team=nullptr; ai.game=nullptr; ai.map=nullptr;} } reset{*this};
	GAGCore::BinaryInputStream::CheckedReads checked(stream);

	stream->readEnterSection("AICabino");
	timer=stream->readUint32("timer");
	iteration=stream->readUint32("iteration");
	center_x=stream->readUint32("center_x");
	center_y=stream->readUint32("center_y");
	module_timer=stream->readUint32("module_timer");
	const Uint32 moduleIndex = stream->readUint32("active_module");
	if (moduleIndex > modules.size()) return false;
	active_module=modules.begin()+moduleIndex;

	stream->readEnterSection("orders");
	Uint32 ordersSize = stream->readCount("size",65536);
	for (Uint32 ordersIndex = 0; ordersIndex < ordersSize; ordersIndex++)
	{
		stream->readEnterSection(ordersIndex);
		std::shared_ptr<Order> order;
		if (versionMinor >= AI_CABINO_SAVE_FORMAT_CONTINUATION) {
			NetSendOrder envelope;
			envelope.setDecodeVersionMinor(versionMinor);
			envelope.decodeData(stream);
			order = envelope.getOrder();
		} else {
			const auto size=stream->readCount("size");
			if (size == 0 || size > 65536) return false;
			std::vector<Uint8> buffer(size);
			stream->read(buffer.data(),size,"data");
			order = Order::getOrder(buffer.data(),size,versionMinor);
		}
		if (!order) return false;
		AIStateSerialization::normalizeLegacyOrderStaffing(*player->game,*order,versionMinor);
        AIEngine::loadSelectedTarget(*stream,*order,versionMinor);
		orders.push(order);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	char signature[4];

	stream->readEnterSection("modules");
	Uint32 modulesSize = stream->readCount("size");
	if (modulesSize != modules.size()) return false;
	for (Uint32 modulesIndex = 0; modulesIndex < modulesSize; modulesIndex++)
	{
		stream->readEnterSection(modulesIndex);
		stream->read(signature, 4, "signatureStart");
		if (memcmp(signature,"MoSt", 4)!=0)
		{
			diagnosticStream<<"Signature missmatch at begin of module #"<<modulesIndex<<", "<<modules[modulesIndex]->getName()<<". Expected \"MoSt\", recieved \""<<std::string(signature, 4)<<"\"."<<std::endl;
			stream->readLeaveSection();
			return false;
		}

		if (!modules[modulesIndex]->load(stream, player, versionMinor)) return false;

		stream->read(signature, 4, "signatureEnd");
		if (memcmp(signature,"MoEn", 4)!=0)
		{
			diagnosticStream<<"Signature missmatch at end of module #"<<modulesIndex<<", "<<modules[modulesIndex]->getName()<<". Expected \"MoEn\", recieved \""<<std::string(signature, 4)<<"\"."<<std::endl;
			stream->readLeaveSection();
			return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	if (versionMinor >= AI_CABINO_SAVE_FORMAT_CONTINUATION && !gradient_manager.load(stream)) return false;
	stream->readLeaveSection();
	return stream->isValid();
}




void AICabino::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AICabino");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(iteration, "iteration");
	stream->writeUint32(center_x, "center_x");
	stream->writeUint32(center_y, "center_y");
	stream->writeUint32(module_timer, "module_timer");
	stream->writeUint32(active_module-modules.begin(), "active_module");

	stream->writeEnterSection("orders");
	stream->writeUint32(static_cast<Uint32>(orders.size()), "size");
	auto remainingOrders = orders;
	for (Uint32 ordersIndex = 0; !remainingOrders.empty(); ++ordersIndex)
	{
		stream->writeEnterSection(ordersIndex);
		const auto order = remainingOrders.front();
		remainingOrders.pop();
		NetSendOrder(order).encodeData(stream);
        AIEngine::saveSelectedTarget(*stream,*order);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("modules");
	stream->writeUint32(modules.size(), "size");
	Uint32 moduleIndex=0;
	for(std::vector<Module*>::iterator i = modules.begin(); i!=modules.end(); ++i)
	{
		stream->writeEnterSection(moduleIndex++);
		stream->write("MoSt", 4, "signatureStart");
		(*i)->save(stream);
		stream->write("MoEn", 4, "signatureEnd");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	gradient_manager.save(stream);
	stream->writeLeaveSection();
}




std::shared_ptr<Order> AICabino::getOrder()
{
    const auto view=AIEngine::AIWorldView::capture(*player->game, AIEngine::AIWorldView::captureCatalog(*player->game));
    const std::vector<AIEngine::ExecutionReceipt> receipts;
    return getOrder(AIEngine::DecisionContext{*view,unsigned(player->number),unsigned(player->teamNumber),receipts});
}
std::shared_ptr<Order> AICabino::getOrder(const AIEngine::DecisionContext& context)
{
    team=&context.world.teams[context.team];game=&context.world;map=&context.world;
    struct Reset { AICabino& ai; ~Reset(){
        ai.team=nullptr; ai.game=nullptr; ai.map=nullptr;
    } } reset{*this};
    applyReceipts(context);
    auto result=decide();
    const auto text=diagnosticStream.str();
    if(!text.empty()) bufferedDiagnostics.push_back({{},{},text});
    diagnosticStream.str({});diagnosticStream.clear();
    return result;
}
void AICabino::enqueueOrder(std::shared_ptr<Order> order)
{
    if(!game) throw std::logic_error("Cabino command selected outside observation scope");
    AIEngine::selectTarget(*order,*game);
    orders.push(std::move(order));
}
void AICabino::discardInvalidQueuedOrders()
{
    while(!orders.empty()) {
        const auto& order=orders.front();
        if(AIEngine::ObservationQueries::permittedQueuedOrder(*game,*order) &&
            AIEngine::selectedTargetExists(*order,*game)) break;
        // A command canceled before emission never reaches the engine receipt
        // queue. Reconcile its private reservation at this same worker boundary.
        AIEngine::ExecutionReceipt rejection;rejection.status=AIEngine::ExecutionStatus::Canceled;
        rejection.executionTick=game->tick;
        rejection.command.push_back(order->getOrderType());
        if(order->getDataLength()) {
            const auto* bytes=order->getData();rejection.command.insert(rejection.command.end(),bytes,bytes+order->getDataLength());
        }
        const std::vector<AIEngine::ExecutionReceipt> receipts{std::move(rejection)};
        applyReceipts(AIEngine::DecisionContext{*game,0,unsigned(team->number),receipts});
        orders.pop();
    }
}
std::shared_ptr<Order> AICabino::decide()
{

	// Saved orders can predate the capability gates. Drain obsolete work before
	// resuming modules; rejecting it in the engine would keep queue state stale.
    discardInvalidQueuedOrders();
	if (!orders.empty())
	{
		std::shared_ptr<Order> order = orders.front();
		orders.pop();
		return order;
	}

	timer++;

	//Waits for atleast one iteration before it does anything, because of a few odd, 'goes to fast' bugs.
	if (timer<STARTUP_TIME)
		return std::shared_ptr<Order>(new NullOrder());

	//	diagnosticStream<<"timer="<<timer<<";"<<std::endl;

	if(active_module==modules.end() || iteration==0)
	{
		if(iteration==0)
		{
			setCenter();
            static_assert(!SEE_EVERYTHING,"AI decisions cannot mutate visibility");
		}
		iteration+=1;
		active_module=modules.begin();
		if(AICabino_DEBUG)
			diagnosticStream<<"AICabino: getOrder: ******Entering iteration "<<iteration<<" at tick #"<<timer<<". ******"<<std::endl;
		outputDebugMessages();
	}


	if(timer%20==0)
		gradient_manager.updateGradients();

	//The +1 here is because the program would end one tick late, so the tick #'s that
	//it entered each iteration at would be 171, or 441 (for an interval of ten).
	//This -1 corrects that.
	if((timer+1)%TIMER_INTERVAL==0)
	{
		//		diagnosticStream<<"Performing function: timer="<<timer<<"; module_timer="<<module_timer<<";"<<std::endl;
		bool cont = (*active_module)->perform(module_timer);
		if(!cont)
			++module_timer;

		if(module_timer==(*active_module)->numberOfTicks())
		{
			module_timer=0;
			++active_module;
		}
	};

    discardInvalidQueuedOrders();
	if (!orders.empty())
	{
		std::shared_ptr<Order> order = orders.front();
		orders.pop();
		return order;
	}

	return std::shared_ptr<Order>(new NullOrder());
}




void AICabino::setDefenseModule(DefenseModule* module)
{
	modules.erase(remove(modules.begin(), modules.end(), defense_module), modules.end());
	if(defense_module!=NULL)
		delete defense_module;
	defense_module=module;
	modules.push_back(module);
}




void AICabino::setAttackModule(AttackModule* module)
{
	modules.erase(remove(modules.begin(), modules.end(), attack_module), modules.end());
	if(attack_module!=NULL)
		delete attack_module;
	attack_module=module;
	modules.push_back(module);
}




void AICabino::setNewConstructionModule(NewConstructionModule* module)
{
	modules.erase(remove(modules.begin(), modules.end(), new_construction_module), modules.end());
	if(new_construction_module!=NULL)
		delete new_construction_module;
	new_construction_module=module;
	modules.push_back(module);
}




void AICabino::setUpgradeRepairModule(UpgradeRepairModule* module)
{
	modules.erase(remove(modules.begin(), modules.end(), upgrade_repair_module), modules.end());

	if(upgrade_repair_module!=NULL)
		delete upgrade_repair_module;
	upgrade_repair_module=module;
	modules.push_back(module);
}




void AICabino::setUnitModule(UnitModule* module)
{
	modules.erase(remove(modules.begin(), modules.end(), unit_module), modules.end());
	if(unit_module!=NULL)
		delete unit_module;
	unit_module=module;
	modules.push_back(module);
}




void AICabino::addOtherModule(OtherModule* module)
{
	other_modules[module->getName()]=module;
	modules.push_back(module);
}




DefenseModule* AICabino::getDefenseModule()
{
	return defense_module;
}




AttackModule* AICabino::getAttackModule()
{
	return attack_module;
}




NewConstructionModule* AICabino::getNewConstructionModule()
{
	return new_construction_module;
}




UpgradeRepairModule* AICabino::getUpgradeRepairModule()
{
	return upgrade_repair_module;
}




UnitModule* AICabino::getUnitModule()
{
	return unit_module;
}




OtherModule* AICabino::getOtherModule(std::string name)
{
	return other_modules[name];
}




void AICabino::setCenter()
{
 	unsigned int x_total=0;
 	unsigned int y_total=0;
 	unsigned int square_total=0;
	for(int x=0; x < map->width; ++x)
 	{
		for(int y=0; y<map->height; ++y)
 		{
			if(((map->visibilityAt(map->tileIndex(x,y)).discovered&(team->mask))!=0))
 			{
 				x_total+=x;
 				y_total+=y;
 				square_total++;
 			}
 		}
 	}
 	center_x=x_total/square_total;
 	center_y=y_total/square_total;
}




void AICabino::outputDebugMessages()
{
	if(CabinoStatusUpdate)
	{
		size_t wrap_size=60;
		std::ostringstream file;
		for(std::map<std::string, std::map<std::string, std::map<std::string, std::vector<std::string> > > >::iterator i = debug_messages.begin(); i!=debug_messages.end(); ++i)
		{
			size_t size=(wrap_size-2-i->first.size())/2;
			file<<std::string(size, '*')<<" "<<i->first<<" "<<std::string(size, '*')<<std::endl;
			for(std::map<std::string, std::map<std::string, std::vector<std::string> > >::iterator j = i->second.begin(); j!=i->second.end(); ++j)
			{
				file<<"    "<<j->first<<":"<<std::endl;
				for(std::map<std::string, std::vector<std::string> >::iterator k = j->second.begin(); k!=j->second.end(); ++k)
				{
					if(j->second.size()>0)
					{
						file<<"        "<<k->first<<":"<<std::endl;
						for(std::vector<std::string>::iterator l = k->second.begin(); l!=k->second.end(); ++l)
						{
							file<<"            "<<*l<<std::endl;
						}
					}
				}
			}
		}
		bufferedDiagnostics.push_back({"CabinoStatus.txt",{},file.str()});
	}
}




bool Cabino::buildingStillExists(const AIEngine::AIWorldView* game, const AIEngine::BuildingView* building)
{
	for (int i=0; i<Team::MAX_COUNT; i++)
	{
		const AIEngine::TeamView* t = teamAt(*game,i);
		if(t)
		{
			for(int i=0; i<1024; i++)
			{
				const AIEngine::BuildingView* b = game->buildingSlots(t->number)[i];
				if (b)
				{
					if(b == building)
						return true;
				}
			}
		}
	}
	return false;
}




bool Cabino::buildingStillExists(const AIEngine::AIWorldView* game, unsigned int gid)
{
	return getBuildingFromGid(game, gid)!=NULL;
}




GridPollingSystem::GridPollingSystem(AICabino& ai)
{
	map=ai.map;
	team=ai.team;
	game=ai.game;
	center_x=ai.getCenterX();
	center_y=ai.getCenterY();
}




unsigned int GridPollingSystem::pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollModifier mod, pollType poll_type)
{
	unsigned int bound_h=x+width;
	if(static_cast<int>(bound_h)>map->width)
		bound_h-=map->width;

	unsigned int bound_y=y+height;
	if(static_cast<int>(bound_y)>map->height)
		bound_y-=map->height;

	unsigned int orig_y=y;
	unsigned int score=0;

	//This is an optmization, as putting the switch inside the for loop causes it to do log2n checks
	//For every single square, which has become to cumbersome.
	const AIEngine::UnitView* u=NULL;
	const AIEngine::BuildingView* b=NULL;
	switch (poll_type)
	{

		case HIDDEN_SQUARES:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					if(!((map->visibilityAt(map->tileIndex(x,y)).discovered&(team->mask))!=0))
					{
						score++;
					}
				}
			}

			break;
		case VISIBLE_SQUARES:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					if(((map->visibilityAt(map->tileIndex(x,y)).discovered&(team->mask))!=0))
					{
						score++;
					}
				}
			}
			break;
		case ENEMY_BUILDINGS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					b = getBuildingFromGid(game, map->occupancyAt(map->tileIndex(x,y)).building);
					if (b)
					{
						if((game->teams[b->team].mask & team->enemies) && b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
						{
							score++;
						}
					}
				}
			}
			break;
		case FRIENDLY_BUILDINGS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					b = getBuildingFromGid(game, map->occupancyAt(map->tileIndex(x,y)).building);
					if (b)
					{
						if(game->teams[b->team].mask == team->mask && b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
						{
							score++;
						}
					}
				}
			}
			break;
		case ENEMY_UNITS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					u = getUnitFromGid(game, map->occupancyAt(map->tileIndex(x,y)).groundUnit);
					if (u)
					{
						if((game->teams[u->team].mask & team->enemies) && u->posX==static_cast<int>(x) && u->posY == static_cast<int>(y))
						{
							score++;
						}
					}
				}
			}
			break;
		case ENEMY_WARRIORS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					u = getUnitFromGid(game, map->occupancyAt(map->tileIndex(x,y)).groundUnit);
					if (u)
					{
						if((game->teams[u->team].mask & team->enemies) && u->posX==static_cast<int>(x) && u->posY == static_cast<int>(y) && u->typeNum==WARRIOR)
						{
							score++;
						}
					}
				}
			}
			break;
		case POLL_CORN:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					if (resourceTakeable(map->resourceAt(map->tileIndex(x,y)).resource,WHEAT))
						score++;
				}
			}
		case POLL_TREES:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					if (resourceTakeable(map->resourceAt(map->tileIndex(x,y)).resource,WOOD))
						score++;
				}
			}
			break;
		case POLL_STONE:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->width)
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->height)
						y=0;
					if (resourceTakeable(map->resourceAt(map->tileIndex(x,y)).resource,STONE))
						score++;
				}
			}
			break;
		case CENTER_DISTANCE:
			score=intdistance(x, center_x) + intdistance(y, center_y);
			if(mod==MINIMUM)
				score=(map->width+map->height)-score;
			return score;
			break;
		case NONE:
			return 0;
			break;
	}

	switch (mod)
	{
		case MAXIMUM:
			return score;
			break;
		case MINIMUM:
			return width*height-score;
			break;
	}

	return 0;
}




GridPollingSystem::zone GridPollingSystem::getZone(unsigned int x, unsigned int y, unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap)
{
	int free_width=area_width-horizontal_overlap;
	int free_height=area_height-vertical_overlap;
	zone z;
	z.x = x-(x%free_width);
	z.y = y-(x%free_height);
	z.width=area_width;
	z.height=area_height;
	return z;
}




int GridPollingSystem::getPositionScore(const std::vector<pollRecord>& polls, const std::vector<pollRecord>::const_iterator& iter)
{
	int score=0;
	std::vector<pollRecord>::const_iterator last = polls.begin();
	for(std::vector<pollRecord>::const_iterator i = polls.begin(); i!=polls.end(); ++i)
	{
		if(i==iter)
		{
			break;
		}
		if(i->score != last->score)
		{
			score++;
			last=i;
		}
	}
	return score;
}




GridPollingSystem::getBestZonesSplit* GridPollingSystem::getBestZones(poll p, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height)
{
	getBestZonesSplit* split=new getBestZonesSplit;
	split->p=p;
	split->width=width;
	split->height=height;
	split->horizontal_overlap=horizontal_overlap;
	split->vertical_overlap=vertical_overlap;
	split->extention_width=extention_width;
	split->extention_height=extention_height;
	std::vector<pollRecord>& a_list=*new std::vector<pollRecord>;
	std::vector<pollRecord>& b_list=*new std::vector<pollRecord>;
	std::vector<pollRecord>& c_list=*new std::vector<pollRecord>;
	split->a_list=&a_list;
	split->b_list=&b_list;
	split->c_list=&c_list;

	for (unsigned int x=0; x<=static_cast<unsigned int>(map->width-horizontal_overlap); x+=width-horizontal_overlap)
	{
		for (unsigned int y=0; y<=static_cast<unsigned int>(map->height-vertical_overlap); y+=height-vertical_overlap)
		{
			int full_x=x-extention_width;
			if(full_x<0)
				full_x=map->width+full_x;
			int full_y=y-extention_height;
			if(full_y<0)
				full_y=map->height+full_y;

			int full_w=width+2*extention_width;
			int full_h=height+2*extention_height;

			pollRecord pr_a;
			pr_a.failed_constraint=false;
			if(pollArea(full_x, full_y, full_w, full_h, p.mod_minimum, p.minimum_type)<p.minimum_score)
				if(p.is_strict_minimum)
					continue;
			else
				pr_a.failed_constraint=true;

			if(pollArea(full_x, full_y, full_w, full_h, p.mod_maximum, p.maximum_type)>p.maximum_score)
				if(p.is_strict_minimum)
					continue;
			else
				pr_a.failed_constraint=true;

			pr_a.x=x;
			pr_a.y=y;
			pr_a.width=width;
			pr_a.height=height;
			pollRecord pr_b=pr_a;
			pollRecord pr_c=pr_b;
			pr_a.score=pollArea(full_x, full_y, full_w, full_h, p.mod_1, p.type_1);
			pr_b.score=pollArea(full_x, full_y, full_w, full_h, p.mod_2, p.type_2);
			pr_c.score=pollArea(full_x, full_y, full_w, full_h, p.mod_3, p.type_3);
			a_list.push_back(pr_a);
			b_list.push_back(pr_b);
			c_list.push_back(pr_c);
		}
	}

	std::sort(a_list.begin(), a_list.end());
	std::sort(b_list.begin(), b_list.end());
	std::sort(c_list.begin(), c_list.end());
	return split;
}




TeamStatsGenerator::TeamStatsGenerator(const AIEngine::AIWorldView* world,const AIEngine::TeamView* team) : world(world),team(team)
{

}




std::vector<GridPollingSystem::zone> GridPollingSystem::getBestZones(GridPollingSystem::getBestZonesSplit* split_calc)
{
	std::vector<pollRecord>& a_list=*split_calc->a_list;
	std::vector<pollRecord>& b_list=*split_calc->b_list;
	std::vector<pollRecord>& c_list=*split_calc->c_list;

	std::vector<threeTierRecord> final;
	unsigned int pos_a=0;
	for (std::vector<pollRecord>::iterator i1=a_list.begin(); i1!=a_list.end(); ++i1)
	{
		pos_a++;

		threeTierRecord ttr;
		ttr.x=i1->x;
		ttr.y=i1->y;
		ttr.width=i1->width;
		ttr.height=i1->height;

		for(std::vector<pollRecord>::iterator s1=i1; s1!=a_list.end() && s1->score == i1->score; ++s1)
			pos_a++;

		ttr.score_a=pos_a;

		unsigned int pos_b=0;
		for (std::vector<pollRecord>::iterator i2=b_list.begin(); i2!=b_list.end(); ++i2)
		{
			pos_b++;
			if(i2->x==ttr.x && i2->y==ttr.y)
			{
				for(std::vector<pollRecord>::iterator s2=i2; s2!=b_list.end() && s2->score == i2->score; ++s2)
					pos_b++;
				ttr.score_b=pos_b;
				break;
			}
		}

		unsigned int pos_c=0;
		for (std::vector<pollRecord>::iterator i3=c_list.begin(); i3!=c_list.end(); ++i3)
		{
			pos_c++;
			if(i3->x==ttr.x && i3->y==ttr.y)
			{
				for(std::vector<pollRecord>::iterator s3=i3; s3!=c_list.end() && s3->score == i3->score; ++s3)
					pos_c++;
				ttr.score_c=pos_c;
				break;
			}
		}
		final.push_back(ttr);
	}

	std::sort(final.begin(), final.end());

	std::vector<zone> return_list;
	//For some reason, the final list is being sorted backwards. It seems to be because of my lack of thoroughly thinking out
	//how the various comparisons relate.
	for(std::vector<threeTierRecord>::reverse_iterator i=final.rbegin(); i!=final.rend(); ++i)
	{
		zone z;
		z.x=i->x;
		z.y=i->y;
		z.width=i->width;
		z.height=i->height;
		return_list.push_back(z);
	}

	delete split_calc->a_list;
	delete split_calc->b_list;
	delete split_calc->c_list;
	delete split_calc;

	return return_list;
}




unsigned int TeamStatsGenerator::getUnits(unsigned int type, Unit::Medical medical_state, Unit::Activity activity, unsigned int ability, unsigned int level, bool isMinimum)
{
	//This is because ability levels are counted from 0, not 1. I'm not sure why,
	//though, because 0 would mean that the unit does not have that skill, which
	//would be more appropriette.
	level-=1;
	unsigned int free_workers=0;
	const auto myUnits=world->unitSlots(team->number);

	for (int i=0; i<1024; i++)
	{
		const AIEngine::UnitView* u = myUnits[i];
		if (u)
		{
			if (u->typeNum == static_cast<int>(type) && u->activity==activity && ((!isMinimum && (ability==BUILD ? u->constructionLevel : u->level[ability])==static_cast<int>(level)) ||
				(isMinimum && (ability==BUILD ? u->constructionLevel : u->level[ability])>=static_cast<int>(level))) && u->medical==medical_state)
			{
				free_workers+=1;
			}
		}
	}
	return free_workers;
}




unsigned int TeamStatsGenerator::getUnits(unsigned int type, unsigned int ability, unsigned int level, bool isMinimum)
{
	level-=1;
	unsigned int free_workers=0;
	const auto myUnits=world->unitSlots(team->number);

	for (int i=0; i<1024; i++)
	{
		const AIEngine::UnitView* u = myUnits[i];
		if (u)
		{
			if (u->typeNum == static_cast<int>(type) && 	((!isMinimum && (ability==BUILD ? u->constructionLevel : u->level[ability])==static_cast<int>(level)) ||
									(  isMinimum && (ability==BUILD ? u->constructionLevel : u->level[ability])>=static_cast<int>(level))))
			{
				free_workers+=1;
			}
		}
	}
	return free_workers;
}




// width/height read ai.map directly rather than the not-yet-initialized
// `map` member: member init order follows declaration order (width, height,
// ..., map), so `map->width` here would dereference an uninitialized pointer.
Gradient::Gradient(AICabino& ai, unsigned sources, unsigned obstacles) : width(ai.map->width), height(ai.map->height), sources(sources), obstacles(obstacles), ai(&ai), gradient(width*height)
{

}




void Gradient::reset(AICabino& aAi, unsigned aSources, unsigned aObstacles)
{
	ai=&aAi;
	sources=aSources;
	obstacles=aObstacles;
	width=ai->map->width;
	height=ai->map->height;
	gradient.resize(width*height);
}




void Gradient::update(field::Frontier& frontier)
{
	std::fill(gradient.begin(), gradient.end(), 0);
	frontier.clear();
	for(unsigned x=0; x<width; ++x)
		for(unsigned y=0; y<height; ++y)
	{
		if(isObstacle(x, y))
		{
			gradient[y*width+x]=1;
			continue;
		}
		if(isSource(x, y))
		{
			gradient[y*width+x]=2;
			frontier.push_back(y*width+x);
			continue;
		}
	}

	// Preserve Cabino's diagonal-first neighbour order.
	static constexpr std::array<field::Offset,8> neighbors={{{-1,-1},{1,-1},
		{-1,1},{1,1},{-1,0},{1,0},{0,-1},{0,1}}};
	field::expandDistances(gradient,frontier,{int(width),int(height)},neighbors,short(0));
}




int Gradient::getHeight(int x, int y) const
{
	// Modulo wraparound (not a single +/- width|height) because callers can
	// probe a multi-tile building footprint's far corner (x+buildingWidth-1),
	// which a single correction doesn't always bring back in range.
	x %= static_cast<int>(width);
	if(x<0)
		x+=width;
	y %= static_cast<int>(height);
	if(y<0)
		y+=height;
	return gradient[y*width+x]-1;
}




bool Gradient::isSource(unsigned x, unsigned y)
{
    auto* map=ai->map; auto* team=ai->team;
	const auto index=map->tileIndex(x,y);
	const auto sourceResource=map->resourceAt(index).resource;
	const int resource=sourceResource.type;
	const auto isTakeable=[&](int type) { return sourceResource.type==type && sourceResource.amount>0; };
 if(resource>=0 && resource<MAX_NB_RESOURCES && (sources&(1u<<(8+resource))) && isTakeable(resource)) return true;
 if(sources&VillageCenter && x==ai->getCenterX() && y==ai->getCenterY())
		return true;
	if(sources&Wheat && isTakeable(WHEAT))
		return true;
	if(sources&Wood && isTakeable(WOOD))
		return true;
	if(sources&Stone && isTakeable(STONE))
		return true;
	if(sources&TeamBuildings)
	{
		const auto building=map->occupancyAt(index).building;
		if(building!=NOGBID && getBuildingFromGid(map, building)->team==team->number)
			return true;
	}
	if(sources&Water && terrainProvidesFertility(map->terrain->properties(map->terrainAt(index).type)))
		return true;
	return false;
}




bool Gradient::isObstacle(unsigned x, unsigned y)
{
    auto* map=ai->map; auto* team=ai->team;
	if(obstacles&Resource && (map->resourceAt(map->tileIndex(x,y)).resource.type!=NO_RES_TYPE))
		return true;
	if(obstacles&Building && map->occupancyAt(map->tileIndex(x,y)).building!=NOGBID)
		return true;
	return false;
}




void Gradient::output()
{
	for(unsigned int y=0; y<height; ++y)
	{
		for(unsigned int x=0; x<width; ++x)
		{
			ai->diagnosticStream<<std::setw(3)<<std::setfill('0')<<gradient[x*width+y]<<" ";
		}
		ai->diagnosticStream<<std::endl;
	}
	ai->diagnosticStream<<std::endl;
}





Gradient& GradientManager::getGradient(unsigned sources, unsigned obstacles)
{
	gradientSignature sig(sources, obstacles);
	if(gradients.count(sig))
		return gradients[sig];
	Gradient& gradient=gradients[sig];
	gradient.reset(*team, sources, obstacles);
	gradient.update(frontier);
	update_queue.push(gradients.find(sig));
	return gradient;
}




void GradientManager::updateGradients()
{
	if(update_queue.size())
	{
		update_queue.front()->second.update(frontier);
		update_queue.push(update_queue.front());
		update_queue.pop();
	}
}




SimpleBuildingDefense::SimpleBuildingDefense(AICabino& ai) : ai(ai)
{
	ai.setDefenseModule(this);
}




bool SimpleBuildingDefense::perform(unsigned int time_slice_n)
{
	// Peaceful combat cannot injure colonies. Skip before reserving defenders
	// or creating flags, so economic modules can still use the standing army.
	if (ai.game->configuration->isPeacefulModeEnabled()) return false;
	ai.telemetry.set(AITrace::AI8::SimpleBuildingDefense_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::SimpleBuildingDefense_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::SimpleBuildingDefense_perform_result,
											 AITrace::AI8::SimpleBuildingDefense_perform_true,
											 findDefense());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::SimpleBuildingDefense_perform_result,
											 AITrace::AI8::SimpleBuildingDefense_perform_true,
											 updateFlags());
		case 2:
			return ai.telemetry.returnedBool(AITrace::AI8::SimpleBuildingDefense_perform_result,
											 AITrace::AI8::SimpleBuildingDefense_perform_true,
											 findCreatedDefenseFlags());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::SimpleBuildingDefense_perform_result,
									 AITrace::AI8::SimpleBuildingDefense_perform_true, false);
}




std::string SimpleBuildingDefense::getName() const
{
	return "SimpleBuildingDefense";
}




bool SimpleBuildingDefense::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{

	stream->readEnterSection("SimpleBuildingDefense");
	stream->readEnterSection("defending_zones");
	Uint32 defenseRecordSize = stream->readCount("size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defenseRecordSize; defenseRecordIndex++)
	{
		stream->readEnterSection(defenseRecordIndex);
		defenseRecord dr;
		dr.flag=stream->readUint32("flag");
		dr.flagx=stream->readUint32("flagx");
		dr.flagy=stream->readUint32("flagy");
		dr.zonex=stream->readUint32("zonex");
		dr.zoney=stream->readUint32("zoney");
		dr.width=stream->readUint32("width");
		dr.height=stream->readUint32("height");
		dr.assigned=stream->readUint32("assigned");
		dr.building=stream->readUint32("building");
		defending_zones.push_back(dr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if (versionMinor >= AI_CABINO_SAVE_FORMAT_CONTINUATION)
	{
		building_health.clear();
		stream->readEnterSection("building_health");
		const auto count = stream->readUint32("size");
		if (count > Building::MAX_COUNT) return false;
		for (Uint32 i=0; i<count; ++i) {
			stream->readEnterSection(i);
			const auto gid = stream->readUint32("gid");
			building_health[gid] = stream->readUint32("hp");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
}




void SimpleBuildingDefense::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("SimpleBuildingDefense");
	stream->writeEnterSection("defending_zones");
	stream->writeUint32(defending_zones.size(), "size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defending_zones.size(); defenseRecordIndex++)
	{
		stream->writeEnterSection(defenseRecordIndex);
		std::vector<defenseRecord>::const_iterator i = defending_zones.begin() + defenseRecordIndex;
		stream->writeUint32(i->flag, "flag");
		stream->writeUint32(i->flagx, "flagx");
		stream->writeUint32(i->flagy, "flagy");
		stream->writeUint32(i->zonex, "zonex");
		stream->writeUint32(i->zoney, "zoney");
		stream->writeUint32(i->width, "width");
		stream->writeUint32(i->height, "height");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->building, "building");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("building_health");
	stream->writeUint32(building_health.size(), "size");
	Uint32 healthIndex=0;
	for (const auto& [gid,hp] : building_health) {
		stream->writeEnterSection(healthIndex++);
		stream->writeUint32(gid,"gid"); stream->writeUint32(hp,"hp");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool SimpleBuildingDefense::findDefense()
{
 const int typeNum=selectBuilding(ai,AttractWarriors);
 if(typeNum<0) return false;
	ai.telemetry.count(AITrace::AI8::SimpleBuildingDefense_findDefense_calls);
	ai.getUnitModule()->changeUnits("SimpleBuildingDefense", WARRIOR, BASE_DEFENSE_WARRIORS, ATTACK_STRENGTH, 1);
	GridPollingSystem gps(ai);
	for(std::map<unsigned int, unsigned int>::iterator i=building_health.begin(); i!=building_health.end();)
	{
		const AIEngine::BuildingView* b = getBuildingFromGid(ai.game, i->first);
		if(b && AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.occupiesGround)
		{
			if(building_health.find(b->identity.gid) != building_health.end())
			{
				if(b->hp < static_cast<int>(building_health[b->identity.gid]))
				{
					defenseRecord dr;
					dr.flag=NOGBID;
					dr.flagx=b->posX+(AIEngine::ObservationQueries::buildingType(*ai.game,*b).width/2);
					dr.flagy=b->posY+(AIEngine::ObservationQueries::buildingType(*ai.game,*b).height/2);
					if(static_cast<int>(dr.flagx) > ai.map->width)
						dr.flagx-=ai.map->width;
					if(static_cast<int>(dr.flagy) > ai.map->height)
						dr.flagy-=ai.map->height;

					bool found=false;
					for(std::vector<defenseRecord>::iterator j = defending_zones.begin(); j != defending_zones.end(); ++j)
					{
						if(b->identity.gid == j->building)
						{
							found=true;
							break;
						}
					}
					if(found)
					{
						++i;
						continue;
					}
					dr.zonex=b->posX-DEFENSE_ZONE_BUILDING_PADDING;
					dr.zoney=b->posY-DEFENSE_ZONE_BUILDING_PADDING;
					dr.width=AIEngine::ObservationQueries::buildingType(*ai.game,*b).width+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.height=AIEngine::ObservationQueries::buildingType(*ai.game,*b).height+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.assigned=gps.pollArea(dr.zonex, dr.zoney, dr.width, dr.height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_WARRIORS);
					dr.assigned= std::min(20u, dr.assigned*2);
					dr.building=b->identity.gid;
					if(!placeRallyNear(ai,typeNum,dr.flagx,dr.flagy)) { ++i; continue; }
     defending_zones.push_back(dr);

					if(AICabino_DEBUG)
						ai.diagnosticStream<<"AICabino: findDefense: Creating a defense flag at "<<dr.flagx<<", "<<dr.flagy<<", to combat "<<dr.assigned<<" units that are attacking our "<<AIEngine::ObservationQueries::buildingType(*ai.game,*b).key<<" at "<<b->posX<<","<<b->posY<<"."<<std::endl;
					ai.enqueueOrder(AIEngine::ObservationQueries::createOrder(*ai.game, ai.team->number, dr.flagx, dr.flagy, typeNum, 1, 1));
				}
			}
			++i;
		}
		else
		{
			// erase(i) invalidates i; use its return value instead of the
			// (removed) for-loop auto-increment, which would then act on a
			// dangling iterator.
			i = building_health.erase(i);
		}
	}

	for(unsigned int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			if(AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.occupiesGround)
			{
				building_health[b->identity.gid]=std::min(b->maxHp, b->hp);
			}
		}
	}
	return ai.telemetry.returnedBool(AITrace::AI8::SimpleBuildingDefense_findDefense_result,
									 AITrace::AI8::SimpleBuildingDefense_findDefense_true, false);
}




bool SimpleBuildingDefense::updateFlags()
{
	ai.telemetry.count(AITrace::AI8::SimpleBuildingDefense_updateFlags_calls);
	GridPollingSystem gps(ai);
	for (std::vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end();)
	{
		if(i->flag!=NOGBID)
		{
			const AIEngine::BuildingView* flag=getBuildingFromGid(ai.game, i->flag);
			if(!flag)
			{
				// The flag is gone: release its units and drop the record.
				ai.getUnitModule()->request("SimpleBuildingDefense", WARRIOR, ATTACK_STRENGTH, 1, 0, i->flag);
				i=defending_zones.erase(i);
				continue;
			}
			unsigned int score = gps.pollArea(i->zonex, i->zoney, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_WARRIORS);
			if(score==0)
			{
				if(AICabino_DEBUG)
					ai.diagnosticStream<<"AICabino: updateFlags: Found a flag at "<<i->flagx<<","<<i->flagy<<" that is no longer defending against any enemy units. Removing this flag."<<std::endl;
				retireRally(ai,i->flag);
				ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, 1, 0, i->flag);
				i=defending_zones.erase(i);
				continue;
			}
			else
			{
				score=score*2;
				i->assigned=std::min(20u, score);
				if(static_cast<int>(score)!=flag->maxUnitWorking)
				{
					ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(i->flag, std::min(20u, score))));
					ai.getUnitModule()->request("SimpleBuildingDefense", WARRIOR, ATTACK_STRENGTH, 1, std::min(20u, score), i->flag);
				}
			}
		}
		++i;
	}
	return ai.telemetry.returnedBool(AITrace::AI8::SimpleBuildingDefense_updateFlags_result,
									 AITrace::AI8::SimpleBuildingDefense_updateFlags_true, false);
}




bool SimpleBuildingDefense::findCreatedDefenseFlags()
{
	ai.telemetry.count(AITrace::AI8::SimpleBuildingDefense_findCreatedDefenseFlags_calls);
	for(unsigned int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			if(provides(*ai.game,*b,AttractWarriors))
			{
				for (std::vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end(); ++i)
				{
					if(i->flag == NOGBID && b->posX == static_cast<int>(i->flagx) && b->posY == static_cast<int>(i->flagy))
					{
						if(AICabino_DEBUG)
							ai.diagnosticStream<<"AICabino: findCreatedDefenseFlags: Found created flag at "<<i->flagx<<","<<i->flagy<<", adding it to the defense records."<<std::endl;
						i->flag=b->identity.gid;
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyFlag(b->identity.gid, std::max(i->width, i->height)/2)));
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, i->assigned)));
						ai.getUnitModule()->request("SimpleBuildingDefense", WARRIOR, ATTACK_STRENGTH, 1, i->assigned, i->flag);
						break;
					}
				}
			}
		}
	}
	return ai.telemetry.returnedBool(
		AITrace::AI8::SimpleBuildingDefense_findCreatedDefenseFlags_result,
		AITrace::AI8::SimpleBuildingDefense_findCreatedDefenseFlags_true, false);
}




GeneralsDefense::GeneralsDefense(AICabino& ai) : ai(ai)
{
	ai.setDefenseModule(this);
}




bool GeneralsDefense::perform(unsigned int time_slice_n)
{
	// Retaliation flags and their unit reservations have no purpose without
	// combat; do not let this defense module consume economic planning turns.
	if (ai.game->configuration->isPeacefulModeEnabled()) return false;
	ai.telemetry.set(AITrace::AI8::GeneralsDefense_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::GeneralsDefense_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::GeneralsDefense_perform_result,
											 AITrace::AI8::GeneralsDefense_perform_true,
											 findEnemyFlags());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::GeneralsDefense_perform_result,
											 AITrace::AI8::GeneralsDefense_perform_true,
											 updateDefenseFlags());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::GeneralsDefense_perform_result,
									 AITrace::AI8::GeneralsDefense_perform_true, false);
}




std::string GeneralsDefense::getName() const
{
	return "GeneralsDefense";
}




bool GeneralsDefense::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("GeneralsDefense");
	stream->readEnterSection("defending_flags");
	Uint32 defenseRecordSize = stream->readCount("size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defenseRecordSize ; defenseRecordIndex++)
	{
		stream->readEnterSection(defenseRecordIndex);
		defenseRecord dr;
		dr.flag=stream->readUint32("flag");
		dr.enemy_flag=stream->readUint32("enemy_flag");
		if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
		{
			dr.x=stream->readSint32("x"); dr.y=stream->readSint32("y");
		}
		else if (auto* enemy=getBuildingFromGid(ai.game,dr.enemy_flag))
		{ dr.x=enemy->posX; dr.y=enemy->posY; }
		defending_flags.push_back(dr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void GeneralsDefense::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GeneralsDefense");
	stream->writeEnterSection("defending_flags");
	stream->writeUint32(defending_flags.size(), "size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defending_flags.size(); defenseRecordIndex++)
	{
		stream->writeEnterSection(defenseRecordIndex);
		std::vector<defenseRecord>::const_iterator i = defending_flags.begin() + defenseRecordIndex;
		stream->writeUint32(i->flag, "flag");
		stream->writeUint32(i->enemy_flag, "enemy_flag");
		stream->writeSint32(i->x, "x"); stream->writeSint32(i->y, "y");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool GeneralsDefense::findEnemyFlags()
{
 const int typeNum=selectBuilding(ai,AttractWarriors);
 if(typeNum<0) return false;
	ai.telemetry.count(AITrace::AI8::GeneralsDefense_findEnemyFlags_calls);
	GridPollingSystem gps(ai);
	for (unsigned int t=0; t<static_cast<unsigned int>(Team::MAX_COUNT); t++)
	{
		const AIEngine::TeamView* team = teamAt(*ai.game,t);
		if(team)
		{
			if(team->mask & ai.team->enemies)
			{
				for(unsigned int n=0; n<1024; ++n)
				{
					const AIEngine::BuildingView* b = ai.game->buildingSlots(team->number)[n];
					if(b)
					{
						if(provides(*ai.game,*b,AttractWarriors))
						{

							unsigned int score = gps.pollArea((b->posX)-(b->unitStayRange)-DEFENSE_ZONE_SIZE_INCREASE,
								(b->posY)-(b->unitStayRange)-DEFENSE_ZONE_SIZE_INCREASE,
								b->unitStayRange*2+DEFENSE_ZONE_SIZE_INCREASE*2,
								b->unitStayRange*2+DEFENSE_ZONE_SIZE_INCREASE*2, GridPollingSystem::MAXIMUM,
								GridPollingSystem::FRIENDLY_BUILDINGS);

							if(score>0)
							{
								bool found=false;
								for(std::vector<defenseRecord>::iterator i = defending_flags.begin(); i!= defending_flags.end(); ++i)
								{
									if(i->enemy_flag == b->identity.gid)
										found=true;
								}
								if(found)
									continue;

								defenseRecord dr;
								dr.flag=NOGBID;
								dr.enemy_flag=b->identity.gid;
								int rallyX=b->posX,rallyY=b->posY;
        if(!placeRallyNear(ai,typeNum,rallyX,rallyY)) continue;
        dr.x=rallyX; dr.y=rallyY;
        defending_flags.push_back(dr);
								if(AICabino_DEBUG)
									ai.diagnosticStream<<"AICabino: findEnemyFlags: Creating new flag at "<<b->posX<<","<<b->posY<<" to combat an enemy attack!"<<std::endl;

								ai.enqueueOrder(AIEngine::ObservationQueries::createOrder(*ai.game, ai.team->number, rallyX, rallyY, typeNum, 1, 1));
							}
						}
					}
				}
			}
		}
	}
	return ai.telemetry.returnedBool(AITrace::AI8::GeneralsDefense_findEnemyFlags_result,
									 AITrace::AI8::GeneralsDefense_findEnemyFlags_true, false);
}




bool GeneralsDefense::updateDefenseFlags()
{
	ai.telemetry.count(AITrace::AI8::GeneralsDefense_updateDefenseFlags_calls);
	for(std::vector<defenseRecord>::iterator i = defending_flags.begin(); i!= defending_flags.end();)
	{
		if(buildingStillExists(ai.game, i->enemy_flag)==false)
		{
			i=defending_flags.erase(i);
			continue;
		}

		if(i->flag==NOGBID)

		{
			const AIEngine::BuildingView* eb = getBuildingFromGid(ai.game, i->enemy_flag);
			for(unsigned int n=0; n<1024; ++n)
			{
				const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[n];
				if(b)
				{
					if(provides(*ai.game,*b,AttractWarriors) && b->posX==i->x && b->posY==i->y)
					{
						i->flag=b->identity.gid;
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyFlag(i->flag, std::min(eb->unitStayRange,AIEngine::ObservationQueries::buildingType(*ai.game,*b).maxUnitStayRange))));
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(i->flag, std::min(eb->maxUnitWorking,AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.assignmentLimit))));
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyMinLevelToFlag(i->flag, ai.game->configuration->isUnitUpgradesDisabled() ? 0 : eb->minLevelToFlag)));
						break;
					}
				}
			}
		}
		++i;
	}
	return ai.telemetry.returnedBool(AITrace::AI8::GeneralsDefense_updateDefenseFlags_result,
									 AITrace::AI8::GeneralsDefense_updateDefenseFlags_true, false);
}




PrioritizedBuildingAttack::PrioritizedBuildingAttack(AICabino& ai) : ai(ai)
{
	ai.setAttackModule(this);
	enemyTeamNumber=255;
}




bool PrioritizedBuildingAttack::perform(unsigned int time_slice_n)
{
	// Target selection also reserves warriors. Gate the whole attack pipeline
	// rather than dropping its flag order after those reservations are made.
	if (ai.game->configuration->isPeacefulModeEnabled()) return false;
	ai.telemetry.set(AITrace::AI8::PrioritizedBuildingAttack_perform_input_time_slice_n,
					 time_slice_n);
	ai.telemetry.count(AITrace::AI8::PrioritizedBuildingAttack_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_perform_result,
											 AITrace::AI8::PrioritizedBuildingAttack_perform_true,
											 targetEnemy());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_perform_result,
											 AITrace::AI8::PrioritizedBuildingAttack_perform_true,
											 updateAttackFlags());
		case 2:
			return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_perform_result,
											 AITrace::AI8::PrioritizedBuildingAttack_perform_true,
											 attack());
		case 3:
			return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_perform_result,
											 AITrace::AI8::PrioritizedBuildingAttack_perform_true,
											 updateAttackFlags());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_perform_result,
									 AITrace::AI8::PrioritizedBuildingAttack_perform_true, false);
}




std::string PrioritizedBuildingAttack::getName() const
{
	return "PrioritizedBuildingAttack";
}




bool PrioritizedBuildingAttack::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("PrioritizedBuildingAttack");
	stream->readEnterSection("attacks");
	// 255 is the "no enemy chosen yet" sentinel written below: it's outside
	// Team::MAX_COUNT_ON_DISK (32), so it can never collide with a real team.
	Uint8 enemyTeamNumber = stream->readUint8("teamNumber");
	if (enemyTeamNumber != 255 && (enemyTeamNumber >= ai.game->teams.size() || !teamAt(*ai.game,enemyTeamNumber))) return false;
	this->enemyTeamNumber=enemyTeamNumber;
	Uint32 attackRecordSize = stream->readCount("size");
	for (Uint32 attackRecordIndex = 0; attackRecordIndex < attackRecordSize; attackRecordIndex++)
	{
		stream->readEnterSection(attackRecordIndex);
		attackRecord ar;
		ar.target=stream->readUint32("target");
		ar.target_x=stream->readUint32("target_x");
		ar.target_y=stream->readUint32("target_y");
		ar.flag=stream->readUint32("flag");
		ar.flagx=stream->readUint32("flagx");
		ar.flagy=stream->readUint32("flagy");
		ar.zonex=stream->readUint32("zonex");
		ar.zoney=stream->readUint32("zoney");
		ar.width=stream->readUint32("width");
		ar.height=stream->readUint32("height");
		ar.unitx=stream->readUint32("unitx");
		ar.unity=stream->readUint32("unity");
		ar.unit_width=stream->readUint32("unit_width");
		ar.unit_height=stream->readUint32("unit_height");
		ar.assigned_units=stream->readUint32("assigned_units");
		ar.assigned_level=stream->readUint32("assigned_level");
		attacks.push_back(ar);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void PrioritizedBuildingAttack::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("PrioritizedBuildingAttack");
	stream->writeEnterSection("attacks");
	// enemy is legitimately null until targetEnemy() picks a target (e.g. a
	// save taken right at game start); 255 marks "no enemy" for load() above.
	stream->writeUint8(enemyTeamNumber, "teamNumber");
	stream->writeUint32(attacks.size(), "size");
	for (Uint32 attackRecordIndex = 0; attackRecordIndex < attacks.size(); attackRecordIndex++)
	{
		stream->writeEnterSection(attackRecordIndex);
		std::vector<attackRecord>::const_iterator i = attacks.begin() + attackRecordIndex;
		stream->writeUint32(i->target, "target");
		stream->writeUint32(i->target_x, "target_x");
		stream->writeUint32(i->target_y, "target_y");
		stream->writeUint32(i->flag, "flag");
		stream->writeUint32(i->flagx, "flagx");
		stream->writeUint32(i->flagy, "flagy");
		stream->writeUint32(i->zonex, "zonex");
		stream->writeUint32(i->zoney, "zoney");
		stream->writeUint32(i->width, "width");
		stream->writeUint32(i->height, "height");
		stream->writeUint32(i->unitx, "unitx");
		stream->writeUint32(i->unity, "unity");
		stream->writeUint32(i->unit_width, "unit_width");
		stream->writeUint32(i->unit_height, "unit_height");
		stream->writeUint32(i->assigned_units, "assigned_units");
		stream->writeUint32(i->assigned_level, "assigned_level");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool PrioritizedBuildingAttack::targetEnemy()
{
	if(enemy()==NULL || !enemy()->alive)
	{
		for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
		{
			if(j->flag!=NOGBID)
			{
				retireRally(ai,j->flag);
				j=attacks.erase(j);
			}
			else
			{
				++j;
			}
		}

		std::vector<const AIEngine::TeamView*> targets;
		for(int i=0; i<Team::MAX_COUNT; ++i)
		{
			const AIEngine::TeamView* t = teamAt(*ai.game,i);
			if(t)
			{
				if((t->mask & ai.team->enemies) && t->alive)
				{
					targets.push_back(t);
				}
			}
		}

		if(targets.size()>0)
		{
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: targetEnemy: A new enemy has been chosen."<<std::endl;
			enemyTeamNumber=targets[ai.random()%targets.size()]->number;
		}
	}
	return false;
}




bool PrioritizedBuildingAttack::attack()
{
 const int typeNum=selectBuilding(ai,AttractWarriors);
 if(typeNum<0) return false;
	ai.telemetry.count(AITrace::AI8::PrioritizedBuildingAttack_attack_calls);
	// targetEnemy() (time_slice_n==0) leaves enemy NULL when no enemy team is
	// currently alive to target; nothing to attack this cycle in that case.
	if(enemy()==NULL)
		return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_attack_result,
										 AITrace::AI8::PrioritizedBuildingAttack_attack_true,
										 false);

	GridPollingSystem gps(ai);

	//The following gets the highest barracks level the player has
	unsigned int max_barracks_level=0;
	unsigned int found_barracks=0;
	for(int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			if(provides(*ai.game,*b,TrainAttack))
			{
				if(b->constructionResultState==Building::NO_CONSTRUCTION)
				{
					max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.training[ATTACK_STRENGTH].targetLevel));
					found_barracks++;
				}
			}
		}
	}
	///The average strength level of the teams warriors
	unsigned int average_unit_strength_level=0;
	unsigned int total=0;
	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_LEVELS; ++i)
	{
		if(i<MINIMUM_BARRACKS_LEVEL+1)
			continue;
		total+=ai.team->statistics.upgradeState[ATTACK_STRENGTH][i];
		average_unit_strength_level+=ai.team->statistics.upgradeState[ATTACK_STRENGTH][i]*(i+1);
	}
	if(total>0)
		average_unit_strength_level=static_cast<unsigned int>(average_unit_strength_level/total)-1;

	// The standing army cannot train to the barracks-derived recruitment level.
	unsigned int strength_level=ai.game->configuration->isUnitUpgradesDisabled() ? 0
		: (USE_MAX_BARRACKS_LEVEL ? max_barracks_level : average_unit_strength_level);

	//If we don't have enough barracks, don't bother doing anything, otherwise, make sure where producing warriors.
	if(!ai.game->configuration->isUnitUpgradesDisabled() && (max_barracks_level<MINIMUM_BARRACKS_LEVEL+1 || found_barracks==0))
		return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_attack_result,
										 AITrace::AI8::PrioritizedBuildingAttack_attack_true,
										 false);
	else
	{
		for(int i=0; i<NB_UNIT_LEVELS; ++i)
		{
			ai.getUnitModule()->changeUnits("PrioritizedBuildingAttack", WARRIOR, 0, ATTACK_STRENGTH, i+1);
		}
		unsigned int numWarriors=ai.team->statistics.numberUnitPerType[WARRIOR];
		ai.getUnitModule()->changeUnits("PrioritizedBuildingAttack", WARRIOR,
			std::min(BASE_ATTACK_WARRIORS, round_up(numWarriors, WARRIOR_DEVELOPMENT_CHUNK_SIZE)+WARRIOR_DEVElOPMENT_CONSISTANT_SIZE),
			ATTACK_STRENGTH, strength_level+1);
	}

	//Check if we have enough units of the right level
	unsigned int available_units = ai.getUnitModule()->available("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, strength_level+1, true);
	if(available_units<MINIMUM_TO_ATTACK)
		return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_attack_result,
										 AITrace::AI8::PrioritizedBuildingAttack_attack_true,
										 false);

	//Don't use units that are needed by other flags
	for(std::vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
	{
		if(i->assigned_level == strength_level)
		{
			unsigned int needed=0;
			if(i->flag!=NOGBID)
			{
				const AIEngine::BuildingView* b = getBuildingFromGid(ai.game, i->flag);
				if(b && b->working.count<i->assigned_units)
					needed=i->assigned_units-b->working.count;
			}
			else
			{
				needed=i->assigned_units;
			};
			if(available_units > needed)
				available_units-=needed;
			else if(available_units <= needed)
				available_units=0;
		}
	}

	//The following goes through each of the buildings in the enemies foothold, and adds them to the appropriette list based on their position in the
	//ATTACK_PRIORITY variable.
	std::vector<std::vector<const AIEngine::BuildingView*> > buildings(std::size(ATTACK_PRIORITY)+1);
	for(int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(enemy()->number)[i];
		if(b)
		{
			if(!b->locked[1])
			{
    if(AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.occupiesGround) {
     unsigned pos=0;
     while(pos<std::size(ATTACK_PRIORITY) && !provides(*ai.game,*b,ATTACK_PRIORITY[pos])) ++pos;
     buildings[pos].push_back(b);
    }
			}
		}
	}

	//And now we shuffle then for added randomness
	for(unsigned int i=0; i<buildings.size(); ++i)
	{
		for (size_t count=buildings[i].size();count>1;--count)
   std::swap(buildings[i][count-1],buildings[i][ai.random()%count]);
  std::stable_partition(buildings[i].begin(),buildings[i].end(),[](const AIEngine::BuildingView* b){return b->constructionResultState==Building::NO_CONSTRUCTION;});
	}

	//Iterate through the buildings, starting attacks as neccecary, and stopping when
	//we run out of available units, or we have reached the maximum number of attacks
	//at once.
	unsigned int attack_count=attacks.size();
	for(std::vector<std::vector<const AIEngine::BuildingView*> >::iterator i = buildings.begin(); i != buildings.end(); ++i)
	{
		for(std::vector<const AIEngine::BuildingView*>::iterator j = i->begin(); j!=i->end(); ++j)
		{
			const AIEngine::BuildingView* b = *j;
			if(attack_count!=MAX_ATTACKS_AT_ONCE && available_units>=ATTACK_WARRIOR_MINIMUM)
			{
				//Make sure where not attacking this building already
				bool found=false;
				for(std::vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
				{
					if(b->identity.gid==i->target)
					{
						found=true;
						break;
					}
				}
				if(found)
					continue;

				//Ok! Launch an attack
				attackRecord ar;
				ar.target=b->identity.gid;
				ar.target_x=b->posX;
				ar.target_y=b->posY;
				ar.flag=NOGBID;
				ar.flagx=b->posX+(AIEngine::ObservationQueries::buildingType(*ai.game,*b).width/2);
				ar.flagy=b->posY+(AIEngine::ObservationQueries::buildingType(*ai.game,*b).height/2);
				if(static_cast<int>(ar.flagx) >= ai.map->width)
					ar.flagx-=ai.map->width;
				if(static_cast<int>(ar.flagy) >= ai.map->height)
					ar.flagy-=ai.map->height;
				ar.zonex=b->posX-ATTACK_ZONE_BUILDING_PADDING;
				ar.zoney=b->posY-ATTACK_ZONE_BUILDING_PADDING;
				ar.width=AIEngine::ObservationQueries::buildingType(*ai.game,*b).width+ATTACK_ZONE_BUILDING_PADDING*2;
				ar.height=AIEngine::ObservationQueries::buildingType(*ai.game,*b).height+ATTACK_ZONE_BUILDING_PADDING*2;
				ar.unitx=b->posX-ATTACK_ZONE_EXAMINATION_PADDING;
				ar.unity=b->posY-ATTACK_ZONE_EXAMINATION_PADDING;
				ar.unit_width=AIEngine::ObservationQueries::buildingType(*ai.game,*b).width+ATTACK_ZONE_EXAMINATION_PADDING*2;
				ar.unit_height=AIEngine::ObservationQueries::buildingType(*ai.game,*b).height+ATTACK_ZONE_EXAMINATION_PADDING*2;
				ar.assigned_units=std::min(std::min(static_cast<unsigned int>(20), available_units), std::max(gps.pollArea(ar.unitx, ar.unity, ar.unit_width, ar.unit_height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_UNITS), ATTACK_WARRIOR_MINIMUM));
				ar.assigned_level=strength_level;
				if(!placeRallyNear(ai,typeNum,ar.flagx,ar.flagy)) continue;
    attacks.push_back(ar);
				if(AICabino_DEBUG)
					ai.diagnosticStream<<"AICabino: attack: Creating a war flag at "<<ar.flagx<<", "<<ar.flagy<<" and assigning "<<ar.assigned_units<<" units to fight and kill the building at "<<b->posX<<","<<b->posY<<"."<<std::endl;

				ai.enqueueOrder(AIEngine::ObservationQueries::createOrder(*ai.game, ai.team->number, ar.flagx, ar.flagy, typeNum, 1, 1));
				++attack_count;
				available_units-=ar.assigned_units;
			}
			else
			{
				return ai.telemetry.returnedBool(
					AITrace::AI8::PrioritizedBuildingAttack_attack_result,
					AITrace::AI8::PrioritizedBuildingAttack_attack_true, false);
			}
		}
	}
	return ai.telemetry.returnedBool(AITrace::AI8::PrioritizedBuildingAttack_attack_result,
									 AITrace::AI8::PrioritizedBuildingAttack_attack_true, false);
}




bool PrioritizedBuildingAttack::updateAttackFlags()
{
	ai.telemetry.count(AITrace::AI8::PrioritizedBuildingAttack_updateAttackFlags_calls);
	GridPollingSystem gps(ai);

	//Go through all of the buildings, checking each one to see if we have an
	//attack record that is not connected to its flag. If the record is missing
	//its flag, and this building is in the right spot to be the flag that we
	//are missing, then it must be our flag. Merge it into the records, and
	//change it to match up with our records in size, assigned units, and
	//assigned level
	for(int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			if(provides(*ai.game,*b,AttractWarriors))
			{
				for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end(); j++)
				{
					if(j->flag==NOGBID && b->posX == static_cast<int>(j->flagx) && b->posY == static_cast<int>(j->flagy))
					{
						j->flag=b->identity.gid;
						unsigned int radius=std::max(j->width, j->height)/2;
						if(AICabino_DEBUG)
							ai.diagnosticStream<<"AICabino: updateAttackFlags: Found a flag that attack() had created. Giving it a "<<radius<<" radius. Assigning "<<j->assigned_units<<" units to it, and setting it to use level "<<j->assigned_level<<" warriors."<<std::endl;

						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyFlag(b->identity.gid, radius)));
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, j->assigned_units)));
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyMinLevelToFlag(b->identity.gid, ai.game->configuration->isUnitUpgradesDisabled() ? 0 : j->assigned_level)));
						ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, j->assigned_level+1, j->assigned_units, j->flag);
						break;
					}
				}
			}
		}
	}

	//Go through the list of records looking for any buildings that where successfully destroyed, erasing the record if neccecary
	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		if(j->flag!=NOGBID)
		{
			//We need the additional location checks because sometimes the player where attacking builds a building,
			//which quickly assumes the same gid before we detect and remove the flag here. If we destroyed a building,
			//and the ai quickly remakes the same building in the same spot, we don't stop attacking that spot, there
			//is still a building there.
			const AIEngine::BuildingView* b = getBuildingFromGid(ai.game, j->target);
			if((b==NULL || b->posX!=static_cast<int>(j->target_x) || b->posY!=static_cast<int>(j->target_y)))
			{
				if(AICabino_DEBUG)
					ai.diagnosticStream<<"AICabino: updateAttackFlags: Stopping attack on a building, removing the "<<j->flagx<<","<<j->flagy<<" flag."<<std::endl;
				ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, j->assigned_level+1, 0, j->flag);
				retireRally(ai,j->flag);
				j=attacks.erase(j);
				continue;
			}
		}
		++j;
	}

	//The following gets the highest barracks level the player has
	unsigned int max_barracks_level=0;
	unsigned int found_barracks=0;
	for(int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			if(provides(*ai.game,*b,TrainAttack))
			{
				if(b->constructionResultState==Building::NO_CONSTRUCTION)
				{
					max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.training[ATTACK_STRENGTH].targetLevel));
					found_barracks++;
				}
			}
		}
	}
	///The average strength level of the teams warriors
	unsigned int average_unit_strength_level=0;
	unsigned int total=0;
	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_LEVELS; ++i)
	{
		if(i<MINIMUM_BARRACKS_LEVEL+1)
			continue;
		total+=ai.team->statistics.upgradeState[ATTACK_STRENGTH][i];
		average_unit_strength_level+=ai.team->statistics.upgradeState[ATTACK_STRENGTH][i]*(i+1);
	}
	if(total>0)
		average_unit_strength_level=static_cast<unsigned int>(average_unit_strength_level/total)-1;
	// The standing army cannot train to the barracks-derived recruitment level.
	unsigned int strength_level=ai.game->configuration->isUnitUpgradesDisabled() ? 0
		: (USE_MAX_BARRACKS_LEVEL ? max_barracks_level : average_unit_strength_level);

	//Get the number of available units, and go though the record, modifying the number of units assigned to each as
	//neccessary in order to keep up with the defending soldiers
	unsigned int available_units = ai.getUnitModule()->available("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, strength_level+1, true);
	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		if(j->flag != NOGBID)
		{
			//			const AIEngine::BuildingView* flag=getBuildingFromGid(ai.game, j->flag);
			//Add the number of units that are assigned to this flag to the total number of units available
			available_units+=j->assigned_units;

			unsigned int score = gps.pollArea(j->unitx, j->unity, j->unit_width, j->unit_height, GridPollingSystem::MAXIMUM,
				GridPollingSystem::ENEMY_UNITS);
			unsigned int new_assigned=std::min(std::min(static_cast<unsigned int>(20), available_units),
				std::max(score, ATTACK_WARRIOR_MINIMUM));

			//If we don't have enough free units to continue the fight, remove the flag
			if(new_assigned<ATTACK_WARRIOR_MINIMUM)
			{
				if(AICabino_DEBUG)
					ai.diagnosticStream<<"AICabino: updateAttackFlags: Stopping attack, not enough free warriors, removing the "<<j->flagx<<","<<j->flagy<<" flag."<<std::endl;
				retireRally(ai,j->flag);
				j=attacks.erase(j);
				continue;
			}

			//If the number of units that should be attacking the building has changed,
			//update the flag.
			if(new_assigned != j->assigned_units)
			{
				if(AICabino_DEBUG)
					ai.diagnosticStream<<"AICabino: updateAttackFlags: Changing the "<<j->flagx<<","<<j->flagy<<" flag from "<<j->assigned_units<<" to "<<new_assigned<<"."<<std::endl;
				j->assigned_units=new_assigned;
				ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(j->flag, new_assigned)));
				available_units-=new_assigned;
				ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, strength_level+1, new_assigned, j->flag);
			}

			//If the maximum barracks level has changed, then update the flag
			if(strength_level != j->assigned_level)
			{
				j->assigned_level=strength_level;
				ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyMinLevelToFlag(j->flag, strength_level)));
			}
		}
		++j;
	}

	return ai.telemetry.returnedBool(
		AITrace::AI8::PrioritizedBuildingAttack_updateAttackFlags_result,
		AITrace::AI8::PrioritizedBuildingAttack_updateAttackFlags_true, false);
}




DistributedNewConstructionManager::DistributedNewConstructionManager(AICabino& ai) : ai(ai)
{
	ai.setNewConstructionModule(this);
}




bool DistributedNewConstructionManager::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::DistributedNewConstructionManager_perform_input_time_slice_n,
					 time_slice_n);
	ai.telemetry.count(AITrace::AI8::DistributedNewConstructionManager_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(
				AITrace::AI8::DistributedNewConstructionManager_perform_result,
				AITrace::AI8::DistributedNewConstructionManager_perform_true, constructBuildings());
		case 1:
			return ai.telemetry.returnedBool(
				AITrace::AI8::DistributedNewConstructionManager_perform_result,
				AITrace::AI8::DistributedNewConstructionManager_perform_true, updateBuildings());
		case 2:
			return ai.telemetry.returnedBool(
				AITrace::AI8::DistributedNewConstructionManager_perform_result,
				AITrace::AI8::DistributedNewConstructionManager_perform_true, calculateBuildings());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::DistributedNewConstructionManager_perform_result,
									 AITrace::AI8::DistributedNewConstructionManager_perform_true,
									 false);
}




std::string DistributedNewConstructionManager::getName() const
{
	return "DistributedNewConstructionManager";
}




bool DistributedNewConstructionManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DistributedNewConstructionManager");
	stream->readEnterSection("new_buildings");
	Uint32 newConstructionRecordSize = stream->readCount("newConstructionRecordSize");
	for (Uint32 newConstructionRecordIndex = 0; newConstructionRecordIndex < newConstructionRecordSize; newConstructionRecordIndex++)
	{
		stream->readEnterSection(newConstructionRecordIndex);
		newConstructionRecord ncr;
		ncr.building = stream->readUint32("building");
		ncr.x = stream->readUint32("x");
		ncr.y = stream->readUint32("y");
		ncr.assigned = stream->readUint32("assigned");
		ncr.building_type = stream->readUint32("building_type");
  if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) {
   ncr.concreteType=legacyConcrete(*ai.game,ncr.building_type,0,true);
   if(ncr.building_type==12) ncr.building_type=ExchangeResources;
  } else ncr.concreteType=stream->readSint32("concreteType");
  if(ncr.building_type>=DemandCount || ncr.concreteType<0 || size_t(ncr.concreteType)>=ai.game->catalog->size()) return false;
		ncr.no_build_timeout = stream->readUint32("no_build_timeout");
		new_buildings.push_back(ncr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();



	stream->readEnterSection("num_buildings_wanted");
	Uint32 numBuildingWantedSize = stream->readCount("numBuildingWantedSize");
	for (Uint32 numBuildingWantedIndex = 0; numBuildingWantedIndex < numBuildingWantedSize; numBuildingWantedIndex++)
	{
		stream->readEnterSection(numBuildingWantedIndex);
		Uint32 first=stream->readUint32("first");
		Uint32 second=stream->readUint32("second");
		if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) {
   if(first==11) { stream->readLeaveSection(); continue; }
   if(first==12) first=ExchangeResources;
  }
  if(first>=DemandCount) return false;
  num_buildings_wanted[first] = second;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}

	stream->readLeaveSection();
	stream->readLeaveSection();

	return true;
}




void DistributedNewConstructionManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("DistributedNewConstructionManager");
	stream->writeEnterSection("new_buildings");
	stream->writeUint32(new_buildings.size(), "newConstructionRecordSize");
	for (Uint32 newConstructionRecordIndex = 0; newConstructionRecordIndex < new_buildings.size(); newConstructionRecordIndex++)
	{
		stream->writeEnterSection(newConstructionRecordIndex);
		std::vector<newConstructionRecord>::const_iterator i = new_buildings.begin() + newConstructionRecordIndex;
		stream->writeUint32(i->building, "building");
		stream->writeUint32(i->x, "x");
		stream->writeUint32(i->y, "y");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->building_type, "building_type");
  stream->writeSint32(i->concreteType,"concreteType");
		stream->writeUint32(i->no_build_timeout, "no_build_timeout");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();




	stream->writeEnterSection("num_buildings_wanted");
	stream->writeUint32(num_buildings_wanted.size(), "numBuildingWantedSize");
	Uint32 numBuildingWantedIndex = 0;
	for (std::map<unsigned int, unsigned int>::const_iterator i = num_buildings_wanted.begin(); i != num_buildings_wanted.end(); ++i)
	{
		stream->writeEnterSection(numBuildingWantedIndex++);
		stream->writeUint32(i->first, "first");
		stream->writeUint32(i->second, "second");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




DistributedNewConstructionManager::upgradeData DistributedNewConstructionManager::findMaxSize(unsigned int concreteType)
{
 if(footprints.size()!=ai.game->catalog->size()) {
  footprints.resize(ai.game->catalog->size());
  for(size_t id=0;id<footprints.size();++id) {
   const auto* current=(&ai.game->catalog->at(id).resolvedType);
   unsigned width=current->width,height=current->height;
   if(!ai.game->configuration->isUnitUpgradesDisabled()) {
    const auto* next=current;
    for(size_t count=0;next->nextLevel>=0 && count<footprints.size();++count) {
     next=(&ai.game->catalog->at(next->nextLevel).resolvedType);
     width=std::max<unsigned>(width,next->width); height=std::max<unsigned>(height,next->height);
    }
   }
   auto& size=footprints[id]; size.width=width; size.height=height;
   size.horizontal_offset=(width-current->width)/2; size.vertical_offset=(height-current->height)/2;
  }
 }
 return footprints.at(concreteType);
}




DistributedNewConstructionManager::point DistributedNewConstructionManager::findBestPlace(unsigned int building_type,unsigned int concreteType)
{
	ai.telemetry.set(
		AITrace::AI8::DistributedNewConstructionManager_findBestPlace_input_building_type,
		building_type);
	ai.telemetry.count(AITrace::AI8::DistributedNewConstructionManager_findBestPlace_calls);
	point top_point;
	top_point.x=NO_POSITION;
	top_point.y=NO_POSITION;
	unsigned top_score=0;
	unsigned width=ai.map->width;
	unsigned height=ai.map->height;
	upgradeData size=findMaxSize(concreteType);
 std::array<GradientPoll,CONSTRUCTOR_FACTORS_COUNT> factors;
 std::copy(std::begin(CONSTRUCTION_FACTORS[building_type]),std::end(CONSTRUCTION_FACTORS[building_type]),factors.begin());
 const auto* placement=(&ai.game->catalog->at(concreteType).resolvedType);
 const auto* completed=placement->isBuildingSite ? (&ai.game->catalog->at(placement->nextLevel).resolvedType) : placement;
 unsigned recurring=0,construction=0;
 for(int resource=0;resource<MAX_NB_RESOURCES;++resource) {
  const auto& spec=completed->semantics;
  bool consumes=(spec.feeding.enabled && spec.feeding.cost[resource]>0) || (spec.healing.enabled && spec.healing.cost[resource]>0);
  for(const auto& recipe:spec.production.recipes) consumes|=recipe.enabled && recipe.cost[resource]>0;
  for(const auto& training:spec.training) consumes|=training.enabled && training.cost[resource]>0;
  consumes|=completed->shootingRange>0 && spec.ammunitionResource==resource && spec.ammunitionCost>0;
  if(consumes) recurring|=1u<<(8+resource);
  if(placement->semantics.constructionCost[resource]>0) construction|=1u<<(8+resource);
 }
 const unsigned inputs=recurring ? recurring : construction;
 for(auto& factor:factors)
  if(!factor.is_null && (factor.source==Gradient::Wheat || factor.source==Gradient::Wood || factor.source==Gradient::Stone))
   factor.source=static_cast<Gradient::Sources>(inputs ? inputs : Gradient::VillageCenter);

	for(unsigned int x=0; x<width; ++x)
	{
		for(unsigned int y=0; y<height; ++y)
		{
			if(imap[x*height+y]==1)
				continue;
			if(ai.getGradientManager().getGradient(Gradient::VillageCenter, Gradient::Resource).getHeight(x, y)<=0)
				continue;
			unsigned int score=0;
			bool failed=false;
			for(unsigned int factor=0; factor<CONSTRUCTOR_FACTORS_COUNT; ++factor)
			{
				if(!factors[factor].is_null)
				{
					const unsigned source=factors[factor].source;
					const unsigned obstacle=factors[factor].obstacle;
					const int weight = factors[factor].weight;
					const int min_dist = factors[factor].min_dist;
					const int max_dist = factors[factor].max_dist;
					const Gradient& gradient = ai.getGradientManager().getGradient(source, obstacle);
					const int top_left=gradient.getHeight(x, y);
					const int top_right=gradient.getHeight(x+size.width-1, y);
					const int bottom_left=gradient.getHeight(x, y+size.height-1);
					const int bottom_right=gradient.getHeight(x+size.width-1, y+size.height-1);
					if(min_dist!=-1 || max_dist!=-1)
					{
						if(min_dist!=-1)
						{
							int min=std::numeric_limits<int>::max();
							min=std::min(min, top_left);
							min=std::min(min, top_right);
							min=std::min(min, bottom_left);
							min=std::min(min, bottom_right);
							if(min<min_dist)
							{
								failed=true;
								break;
							}
						}
						if(max_dist!=-1)
						{
							int max=std::numeric_limits<int>::min();
							max=std::max(max, top_left);
							max=std::max(max, top_right);
							max=std::max(max, bottom_left);
							max=std::max(max, bottom_right);
							if(max>max_dist)
							{
								failed=true;
								break;
							}
						}
					}
					else
					{
						///Get the scores of each of the four corners of the building
						score+=top_left*weight;
						score+=top_right*weight;
						score+=bottom_left*weight;
						score+=bottom_right*weight;
					}
				}
			}
			if(failed)
				continue;
			if(score>0 && (top_score==0 || score<top_score))
			{
				unsigned endx=x+size.width;
				if(endx>width)
					endx-=width;
				unsigned endy=y+size.height;
				if(endy>height)
					endy-=height;
				bool failed=false;
				for(unsigned x2=x; x2!=endx; ++x2)
				{
					if(x2>=width)
						x2-=width;
					for(unsigned y2=y; y2!=endy; ++y2)
					{
						if(y2>=height)
							y2-=height;
						if(imap[x2*height+y2]==1)
							failed=true;
					}
				}
				if(!failed)
				{
					top_score=static_cast<int>(std::floor(score+0.5));
					top_point=point(x+size.horizontal_offset, y+size.vertical_offset);
				}
			}
		}
	}
	
	return top_point;
}




bool DistributedNewConstructionManager::constructBuildings()
{
	updateNoBuildCache();
	updateImap();

	ai.getUnitModule()->changeUnits("DistributedNewConstructionManager", WORKER, MAXIMUM_TO_CONSTRUCT_NEW*MAX_NEW_CONSTRUCTION_AT_ONCE, BUILD, 1);

	//Enabling this will turn on verbose debugging mode for this function,
	//it will tell you various things like why its not cosntructing buildings
	//for various reasons.
	const bool local_debug=false;

	//Get the total number of free workers, since a worker of any level can construct a new building
	unsigned total_free_workers=ai.getUnitModule()->available("DistributedNewConstructionManager", WORKER, BUILD, 1, true);

	//Counts out the number of buildings allready existing, including ones under construction
	unsigned int counts[DemandCount];

	//The totals of the number of builinds under construction only
	unsigned int under_construction_counts[DemandCount];

	//The total amount of construction
	unsigned int total_construction=new_buildings.size();

	//Holds the order in which it should decide to build buildings.
	std::vector<typePercent> construction_priorities;
	if(CabinoStatusUpdate)
	{
		ai.clearDebugMessages("DistributedNewConstructionManager", "Construction", "General");
		std::stringstream str;
		str<<"There are "<<total_free_workers<<" units available to us.";
		ai.addDebugMessage("DistributedNewConstructionManager", "Construction", "General", str.str());
	}

	if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
	{
		if(CabinoStatusUpdate)
			ai.addDebugMessage("DistributedNewConstructionManager", "Construction", "General", "There is already too much construction, I will not try to start anymore.");
		if(local_debug)
			ai.diagnosticStream<<"Fail 1, too much construction."<<std::endl;
		return false;
	}

	//Count up the numbers of buildings not under construction on a per-building type basis
	for(unsigned i = 0; i<DemandCount; ++i)
	{
		counts[i]=0;
  for(auto* building:ai.game->buildingSlots(ai.team->number))
   if(building && !AIEngine::ObservationQueries::buildingType(*ai.game,*building).isBuildingSite && provides(*ai.game,*building,i)) ++counts[i];
		under_construction_counts[i]=0;
	}

	//Now count up the number of buildings under construction, as well adding to the number of buildings
	//not under construction, and taking away any units that are needed for other construction sites
	//from the total number of free units. Ignore buildings
	for(std::vector<newConstructionRecord>::iterator i=new_buildings.begin(); i!=new_buildings.end(); ++i)
	{
		counts[i->building_type]++;
		under_construction_counts[i->building_type]++;
		if(total_free_workers>i->assigned)
			total_free_workers-=i->assigned;
		else
			total_free_workers=0;

		if(i->building!=NOGBID && getBuildingFromGid(ai.game, i->building)!=NULL)
			total_free_workers+=getBuildingFromGid(ai.game, i->building)->working.count;
	}

	//Develop a list of percentages used for sorting what buildings this ai should develop first, and sort this list
	for (unsigned int i = 0; i<DemandCount; ++i)
	{
		if(STRICT_NEW_CONSTRUCTION_PRIORITIES[i]==0)
			continue;
		typePercent tp;
		tp.building_type=i;
		if(num_buildings_wanted[i]==0)
			tp.percent=100;
		else
			tp.percent=static_cast<unsigned int>((counts[i]*100)/num_buildings_wanted[i]);
		construction_priorities.push_back(tp);
	}
	std::sort(construction_priorities.begin(), construction_priorities.end());

	//These numbers are big because I don't believe a building size will ever exceed them.
	unsigned int min_failed_width=512;
	unsigned int min_failed_height=512;
	for(std::vector<typePercent>::iterator i = construction_priorities.begin(); i!=construction_priorities.end(); ++i)
	{
		if (!AIPlanning::BuildingCapabilityIndex::allowed(intentForDemand(i->building_type),*ai.game->configuration)
			|| (ai.game->configuration->isHungerDisabled() && i->building_type==FeedUnits)) continue;
		const int concreteType=selectBuilding(ai,i->building_type);
  if(concreteType<0) continue;
  std::string building_name=(&ai.game->catalog->at(concreteType).resolvedType)->key;
		if(CabinoStatusUpdate)
		{
			ai.clearDebugMessages("DistributedNewConstructionManager", "Construction", building_name);
			std::stringstream str;
			str<<"I'm currently constructing "<<under_construction_counts[i->building_type]<<" of this building.";
			ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, str.str());
			str.str("");
			str<<"I have "<<counts[i->building_type]-under_construction_counts[i->building_type]<<" of these completed.";
			ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, str.str());
			str.str("");
			str<<"I want "<<num_buildings_wanted[i->building_type]<<" of this building.";
			ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, str.str());
		}
		//Keep constructing buildings of this type untill one of the failure conditions have been reached
		while(true)
		{
			bool should_break=false;
			if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
			{
				if(local_debug)
					ai.diagnosticStream<<"Fail 1, too much construction, for "<<std::to_string(i->building_type)<<"."<<std::endl;
				if(!CabinoStatusUpdate)
					return false;
				else
					ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I won't construct this because there is already too much construction.");
				should_break=true;
			}
			if(total_free_workers<MINIMUM_TO_CONSTRUCT_NEW && !CHEAT_INSTANT_BUILDING)
			{
				if(local_debug)
					ai.diagnosticStream<<"Fail 2, too few units, for "<<std::to_string(i->building_type)<<"."<<std::endl;
				if(!CabinoStatusUpdate)
					return false;
				else
					ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I won't construct this because I don't have enough units.");
				should_break=true;
			}
			if(under_construction_counts[i->building_type]>=MAX_NEW_CONSTRUCTION_PER_BUILDING[i->building_type])
			{
				if(local_debug)
					ai.diagnosticStream<<"Fail 3, too many buildings of this type under constructon, for "<<std::to_string(i->building_type)<<"."<<std::endl;
				if(!CabinoStatusUpdate)
					break;
				else
					ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I won't construct this because I'm already over my maximum amount of construction for this building type.");
				should_break=true;
			}
			if(counts[i->building_type]>=num_buildings_wanted[i->building_type])
			{
				if(local_debug)
					ai.diagnosticStream<<"Fail 4, building cap reached, for "<<std::to_string(i->building_type)<<"."<<std::endl;
				if(!CabinoStatusUpdate)
					break;
				else
					ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I won't construct this because I already have enough of this building type.");
				should_break=true;
			}

			//Find the largest size that this building can have
			upgradeData size=findMaxSize(concreteType);
			//If there wasn't a place for a smaller building than this one, than there certainly won't be a place for this one
			if(size.width>=min_failed_width && size.height>=min_failed_height)
			{
				ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I won't construct this because there is no place to put this building.");
				break;
			}

			if(should_break)
				break;

			point p = findBestPlace(i->building_type,concreteType);
			if(p.x == NO_POSITION || p.y==NO_POSITION)
			{
				min_failed_width=size.width;
				min_failed_height=size.height;

				if(local_debug)
					ai.diagnosticStream<<"Fail 5, no suitable positon found, for "<<std::to_string(i->building_type)<<"."<<std::endl;
				if(CabinoStatusUpdate)
					ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I won't construct this because there is no place to put this building.");
				break;
			}

			ai.addDebugMessage("DistributedNewConstructionManager", "Construction", building_name, "I'm starting another construction of this building.");
			//We have the ok on everything, start constructing a building
			newConstructionRecord ncr;
			ncr.building=NOGBID;
			ncr.x=p.x;
			ncr.y=p.y;

			//Don't assign more units than the total amount of resources needed to construct the building.
			unsigned int needed_resource_total=0;
			const BuildingType* t=(&ai.game->catalog->at(concreteType).resolvedType);
			for(unsigned int n=0; n<MAX_NB_RESOURCES; ++n)
			{
				needed_resource_total+=t->semantics.constructionCost[n];
			}
			ncr.assigned=std::min(total_free_workers, std::min(MAXIMUM_TO_CONSTRUCT_NEW, needed_resource_total));

			ncr.building_type=i->building_type;
   ncr.concreteType=concreteType;
			ncr.no_build_timeout=BUILDING_RECORD_TIMEOUT;
			new_buildings.push_back(ncr);

			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: constructBuildings: Starting construction on a "<<std::to_string(i->building_type)<<", at position "<<p.x<<","<<p.y<<"."<<std::endl;
			Sint32 type=concreteType;
			ai.enqueueOrder(AIEngine::ObservationQueries::createOrder(*ai.game, ai.team->number, p.x, p.y, type, 1, 1));
			total_construction+=1;
			total_free_workers-=ncr.assigned;
			under_construction_counts[i->building_type]++;
			counts[i->building_type]++;
			///Update the imap
			updateImap(ncr.x, ncr.y, concreteType);
		}
	}
	return false;
}




bool DistributedNewConstructionManager::updateBuildings()
{
	ai.telemetry.count(AITrace::AI8::DistributedNewConstructionManager_updateBuildings_calls);
	//Remove records of buildings that are no longer under construction, or ones for buildings that where destroyed be the enemy
	for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end();)
	{
		if(i->building!=NOGBID)
		{
			const AIEngine::BuildingView* b = getBuildingFromGid(ai.game, i->building);
			if(b==NULL || b->constructionResultState == Building::NO_CONSTRUCTION)
			{
				///If its been changed since it was finished (perhaps by another module),
				///don't undo the changes
				if(b!=NULL && b->maxUnitWorking==static_cast<int>(i->assigned))
					ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(i->building, 1)));
				ai.getUnitModule()->request("DistributedNewConstructionManager", WORKER, BUILD, 1, 0, i->building);
				i = new_buildings.erase(i);
				continue;
			}
		}
		if(i->no_build_timeout>-1)
		{
			if(i->no_build_timeout==0)
			{
				i = new_buildings.erase(i);
				continue;
			}
			i->no_build_timeout-=1;
		}
		++i;
	}

	//Update buildings that have just been created.
	for(int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView*b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); ++i)
			{
				if(i->building==NOGBID && (b->typeNum == i->concreteType || b->typeNum == (&ai.game->catalog->at(i->concreteType).resolvedType)->nextLevel) && b->posX == static_cast<int>(i->x) && b->posY == static_cast<int>(i->y))
				{
					i->building=b->identity.gid;
					i->no_build_timeout=-1;
					ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(i->building, i->assigned)));
					ai.getUnitModule()->request("DistributedNewConstructionManager", WORKER, BUILD, 1, i->assigned, i->building);
				}
			}
		}
	}
//	for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); ++i)
//	{
//		if(i->building==NOGBID)
//		{
//			ai.pause();
//			ai.flare(i->x, i->y);
//		}
//	}
	return ai.telemetry.returnedBool(
		AITrace::AI8::DistributedNewConstructionManager_updateBuildings_result,
		AITrace::AI8::DistributedNewConstructionManager_updateBuildings_true, false);
}




bool DistributedNewConstructionManager::calculateBuildings()
{
	unsigned int total_units=ai.team->statistics.totalUnit;
	for (int i=0; i<DemandCount; ++i)
	{
		if(AIPlanning::BuildingCapabilityIndex::allowed(intentForDemand(i),*ai.game->configuration)
			&& !(ai.game->configuration->isHungerDisabled() && i==FeedUnits)
			&& UNITS_FOR_BUILDING[i]!=0)
			num_buildings_wanted[i]=total_units/UNITS_FOR_BUILDING[i]+1;
		else
			num_buildings_wanted[i]=0;
	}
	return false;
}




void DistributedNewConstructionManager::updateNoBuildCache()
{
	ai.telemetry.count(AITrace::AI8::DistributedNewConstructionManager_updateNoBuildCache_calls);
	for(std::map<GridPollingSystem::zone, noBuildRecord>::iterator i=no_build_cache.begin(); i!=no_build_cache.end();)
	{
		i->second.turns++;
		if(i->second.turns > NO_BUILD_CACHE_TIMEOUT)
			// erase(i) invalidates i; its return value is the next valid
			// iterator, so skip the (removed) for-loop auto-increment below.
			i = no_build_cache.erase(i);
		else
			++i;
	}
}




void DistributedNewConstructionManager::updateImap()
{
	ai.telemetry.count(AITrace::AI8::DistributedNewConstructionManager_updateImap_calls);
	const unsigned width=ai.map->width;
	const unsigned height=ai.map->height;
	imap.resize(width*height);
	std::fill(imap.begin(), imap.end(), 0);

	//Go through each square checking off squares that are used
	for(unsigned int x=0; x<width; ++x)
	{
		for(unsigned int y=0; y<height; ++y)
		{
			//Check off hidden or occupied squares
			if(!AIEngine::ObservationQueries::hardBuildingSpace(*ai.map,x, y) || !((ai.map->visibilityAt(ai.map->tileIndex(x,y)).discovered&(ai.team->mask))!=0))
			{
				imap[x*height+y]=1;
			}

			//If we find a building here (or a building that is in the list of buildings to be constructed)
			bool found_building=false;
			upgradeData bsize;
			if(ai.map->occupancyAt(ai.map->tileIndex(x,y)).building!=NOGBID)
			{
				const AIEngine::BuildingView* b=getBuildingFromGid(ai.game, ai.map->occupancyAt(ai.map->tileIndex(x,y)).building);
				bsize=findMaxSize(b->typeNum);
				found_building=true;
			}
			else
				for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); i++)
			{
				if(i->x == x && i->y == y)
				{
					found_building=true;
					bsize=findMaxSize(i->concreteType);
					break;
				}
			}
			//Then mark all the squares this building occupies in its largest upgrade with an extra padding
			if(found_building)
			{

				int startx=x-bsize.horizontal_offset-BUILDING_PADDING;
				if(startx<0)
					startx+=width;;
				int starty=y-bsize.vertical_offset-BUILDING_PADDING;
				if(starty<0)
					starty+=height;
				unsigned endx=startx+bsize.width+BUILDING_PADDING*2;
				if(endx>width)
					endx-=width;
				unsigned endy=starty+bsize.height+BUILDING_PADDING*2;
				if(endy>height)
					endy-=height;
				for(unsigned x2=startx; x2!=endx; ++x2)
				{
					if(x2>=width)
						x2=0;
					for(unsigned y2=starty; y2!=endy; ++y2)
					{
						if(y2>=height)
							y2=0;
						imap[x2*height+y2]=1;
					}
				}
			}
		}
	}
}




void DistributedNewConstructionManager::updateImap(unsigned x, unsigned y, unsigned building_type)
{
	ai.telemetry.count(AITrace::AI8::DistributedNewConstructionManager_updateImap_calls);
	upgradeData bsize;
	bsize=findMaxSize(building_type);
	//Then mark all the squares this building occupies in its largest upgrade with an extra padding

	const unsigned width=ai.map->width;
	const unsigned height=ai.map->height;
	int startx=x-bsize.horizontal_offset-BUILDING_PADDING;
	if(startx<0)
		startx+=width;;
	int starty=y-bsize.vertical_offset-BUILDING_PADDING;
	if(starty<0)
		starty+=height;
	unsigned endx=startx+bsize.width+BUILDING_PADDING*2;
	if(endx>width)
		endx-=width;
	unsigned endy=starty+bsize.height+BUILDING_PADDING*2;
	if(endy>height)
		endy-=height;
	for(unsigned x2=startx; x2!=endx; ++x2)
	{
		if(x2>=width)
			x2=0;
		for(unsigned y2=starty; y2!=endy; ++y2)
		{
			if(y2>=height)
				y2=0;
			imap[x2*height+y2]=1;
		}
	}
}




bool DistributedNewConstructionManager::typePercent::operator<(const typePercent& tp) const
{
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]>STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return true;
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]<STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return false;

	return percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[building_type]<tp.percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[tp.building_type];
}




bool DistributedNewConstructionManager::typePercent::operator>(const typePercent& tp) const
{
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]<STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return true;
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]>STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return false;
	return percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[building_type]>tp.percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[tp.building_type];
}




RandomUpgradeRepairModule::RandomUpgradeRepairModule(AICabino& ai) : ai(ai)
{
	ai.setUpgradeRepairModule(this);
}




bool RandomUpgradeRepairModule::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::RandomUpgradeRepairModule_perform_input_time_slice_n,
					 time_slice_n);
	ai.telemetry.count(AITrace::AI8::RandomUpgradeRepairModule_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::RandomUpgradeRepairModule_perform_result,
											 AITrace::AI8::RandomUpgradeRepairModule_perform_true,
											 removeOldConstruction());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::RandomUpgradeRepairModule_perform_result,
											 AITrace::AI8::RandomUpgradeRepairModule_perform_true,
											 updatePendingConstruction());
		case 2:
			return ai.telemetry.returnedBool(AITrace::AI8::RandomUpgradeRepairModule_perform_result,
											 AITrace::AI8::RandomUpgradeRepairModule_perform_true,
											 startNewConstruction());
		case 3:
			return ai.telemetry.returnedBool(AITrace::AI8::RandomUpgradeRepairModule_perform_result,
											 AITrace::AI8::RandomUpgradeRepairModule_perform_true,
											 reassignConstruction());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::RandomUpgradeRepairModule_perform_result,
									 AITrace::AI8::RandomUpgradeRepairModule_perform_true, false);
}




std::string RandomUpgradeRepairModule::getName() const
{
	return "RandomUpgradeRepairModule";
}




bool RandomUpgradeRepairModule::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RandomUpgradeRepairModule");
	stream->readEnterSection("active_construction");
	Uint32 constructionRecordSize = stream->readCount("size");
	for (Uint32 constructionRecordIndex = 0; constructionRecordIndex < constructionRecordSize; constructionRecordIndex++)
	{
		stream->readEnterSection(constructionRecordIndex);
		constructionRecord cr;
		cr.building=stream->readUint32("gid");
		cr.assigned=stream->readUint32("assigned");
		cr.original=stream->readUint32("original");
		cr.is_repair=stream->readUint8("is_repair");
  if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) cr.requiredLevel=stream->readUint32("requiredLevel");
  else if(auto* building=getBuildingFromGid(ai.game,cr.building))
   cr.requiredLevel=AIEngine::ObservationQueries::buildingType(*ai.game,*building).semantics.requiredWorkerLevel;
  if(cr.requiredLevel>=NB_UNIT_LEVELS) return false;
		active_construction.push_back(cr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("pending_construction");
	constructionRecordSize = stream->readCount("size");
	for (Uint32 constructionRecordIndex = 0; constructionRecordIndex < constructionRecordSize; constructionRecordIndex++)
	{
		stream->readEnterSection(constructionRecordIndex);
		constructionRecord cr;
		cr.building=stream->readUint32("gid");
		cr.assigned=stream->readUint32("assigned");
		cr.original=stream->readUint32("original");
		cr.is_repair=stream->readUint8("is_repair");
  if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) cr.requiredLevel=stream->readUint32("requiredLevel");
  else if(auto* building=getBuildingFromGid(ai.game,cr.building))
   {
   const int target=cr.is_repair ? AIEngine::ObservationQueries::buildingType(*ai.game,*building).prevLevel : AIEngine::ObservationQueries::buildingType(*ai.game,*building).nextLevel;
   cr.requiredLevel=(target >= 0 ? (&ai.game->catalog->at(target).resolvedType) : (&AIEngine::ObservationQueries::buildingType(*ai.game,*building)))->semantics.requiredWorkerLevel;
  }
  if(cr.requiredLevel>=NB_UNIT_LEVELS) return false;
		pending_construction.push_back(cr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void RandomUpgradeRepairModule::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("RandomUpgradeRepairModule");
	stream->writeEnterSection("active_construction");
	stream->writeUint32(active_construction.size(), "size");
	Uint32 constructionRecordIndex = 0;
	for(std::list<constructionRecord>::const_iterator i = active_construction.begin(); i!=active_construction.end(); ++i)
	{
		stream->writeEnterSection(constructionRecordIndex++);
		stream->writeUint32(i->building, "gid");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->original, "original");
		stream->writeUint8(i->is_repair, "is_repair");
  stream->writeUint32(i->requiredLevel,"requiredLevel");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("pending_construction");
	stream->writeUint32(pending_construction.size(), "size");
	constructionRecordIndex = 0;
	for(std::list<constructionRecord>::const_iterator i = pending_construction.begin(); i!=pending_construction.end(); ++i)
	{
		stream->writeEnterSection(constructionRecordIndex++);
		stream->writeUint32(i->building, "gid");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->original, "original");
		stream->writeUint8(i->is_repair, "is_repair");
  stream->writeUint32(i->requiredLevel,"requiredLevel");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool RandomUpgradeRepairModule::removeOldConstruction(void)
{
	for (std::list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end();)
	{
		const AIEngine::BuildingView*b=getBuildingFromGid(ai.game, i->building);
		if(!b)
		{
			i=active_construction.erase(i);
			continue;
		}

		unsigned int original = i->original;
		if (b->constructionResultState!=Building::UPGRADE && b->constructionResultState!=Building::REPAIR )
		{
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: removeOldConstruction: Removing an old "<<AIEngine::ObservationQueries::buildingType(*ai.game,*b).key<<" from the active construction list, changing assigned number of units back to "<<original<<" from "<<i->assigned<<"."<<std::endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, i->requiredLevel+1, 0, b->identity.gid);
			i=active_construction.erase(i);
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, original)));
			continue;
		}
		i++;
	}
	return false;
}




bool RandomUpgradeRepairModule::updatePendingConstruction(void)
{
	ai.telemetry.count(AITrace::AI8::RandomUpgradeRepairModule_updatePendingConstruction_calls);
	for (std::list<constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end();)
	{
		const AIEngine::BuildingView*b=getBuildingFromGid(ai.game, i->building);
		if(!b)
		{
			// i belongs to pending_construction (this loop's own container),
			// not active_construction: erasing it there instead used i, an
			// iterator from a different list instance, as undefined behavior.
			ai.getUnitModule()->unreserve("RandomUpgradeRepairModule",WORKER,BUILD,i->requiredLevel+1,i->assigned);
			i=pending_construction.erase(i);
			continue;
		}
        // Generating an upgrade does not change the observed building. Wait
        // until execution starts construction before publishing its staffing.
        if(b->constructionResultState==Building::NO_CONSTRUCTION) { ++i; continue; }
		unsigned int assigned = i->assigned;
		if (b->buildingState != Building::WAITING_FOR_CONSTRUCTION && b->buildingState != Building::WAITING_FOR_CONSTRUCTION_ROOM)
		{
			constructionRecord u=*i;
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: updatePendingConstruction: The "<<AIEngine::ObservationQueries::buildingType(*ai.game,*b).key<<" was found that it is no longer pending construction, I am assigning number of requested units, "<<assigned<<", to it."<<std::endl;
			active_construction.push_back(u);
			i=pending_construction.erase(i);
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, assigned)));
			ai.getUnitModule()->unreserve("RandomUpgradeRepairModule", WORKER, BUILD, u.requiredLevel+1, u.assigned);
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, u.requiredLevel+1, assigned, b->identity.gid);
			continue;
		}
		i++;
	}
	return ai.telemetry.returnedBool(
		AITrace::AI8::RandomUpgradeRepairModule_updatePendingConstruction_result,
		AITrace::AI8::RandomUpgradeRepairModule_updatePendingConstruction_true, false);
}




bool RandomUpgradeRepairModule::reassignConstruction(void)
{
	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=ai.getUnitModule()->available("RandomUpgradeRepairModule", WORKER, BUILD, j+1, false);
	}

	//Finally, iterate through the shuffled list of records changing the number of units allocated to upgrade the buildings.
	for (std::list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		const AIEngine::BuildingView*b=getBuildingFromGid(ai.game, i->building);
		if(!b)
			continue;
		// A dying building is still in myBuildings, and cancelling construction on it
		// trips Building::cancelConstruction's ALIVE assertion.
		if(b->buildingState!=Building::ALIVE)
			continue;
		if(b->constructionResultState!=Building::UPGRADE && b->constructionResultState!=Building::REPAIR)
			continue;

		free_workers[i->requiredLevel]+=b->maxUnitWorking;

		unsigned int assigned=i->assigned;
		bool is_repair=i->is_repair;

		//Find the number of workers with enough of a level to upgrade this building
		int available_upgrade = 0;
		int available_repair  = 0;
		for (int j = 0; j<NB_UNIT_LEVELS; j++)
		{
			//Important, AIEngine::ObservationQueries::buildingType(*ai.game,*b).level takes on the level number of the building its being upgraded to.
			//So in this function, AIEngine::ObservationQueries::buildingType(*ai.game,*b).level will be different then in startNewConstruction for
			//buildings being upgraded. I have copy and pasted this section of code a thousand times
			//between the two when I change it and each time it causes me trouble.
			if (j >= static_cast<int>(i->requiredLevel))
			{
				available_upgrade+=free_workers[j];
			}
			if (j >= static_cast<int>(i->requiredLevel))
			{
				available_repair+=free_workers[j];
			}
		}

		if (!is_repair && available_upgrade==0)
		{
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: reassignConstruction: There are not enough available units. Canceling upgrade on the "<<AIEngine::ObservationQueries::buildingType(*ai.game,*b).key<<"."<<std::endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, i->requiredLevel+1, 0, b->identity.gid);
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderCancelConstruction(b->identity.gid, 1)));
			continue;
		}

		else if (is_repair && available_repair==0)
		{
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: reassignConstruction: There are not enough available units. Canceling repair on the "<<AIEngine::ObservationQueries::buildingType(*ai.game,*b).key<<"."<<std::endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, i->requiredLevel+1, 0, b->identity.gid);
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderCancelConstruction(b->identity.gid, 1)));
			continue;
		}

		//Issue the command to change the number of units working on the building to the new amount
		unsigned int num_to_assign=0;
		unsigned int generic_available=0;
		if (!is_repair)
		{
			num_to_assign=available_upgrade;
			generic_available=available_upgrade;
			if (num_to_assign>MAXIMUM_TO_UPGRADE)
				num_to_assign=MAXIMUM_TO_UPGRADE;
		}
		else if (is_repair)
		{
			num_to_assign=available_repair;
			generic_available=available_repair;
			if (num_to_assign>MAXIMUM_TO_REPAIR)
				num_to_assign=MAXIMUM_TO_REPAIR;
		}

		if (num_to_assign != assigned)
		{
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: reassignConstruction: Retasking "<<AIEngine::ObservationQueries::buildingType(*ai.game,*b).key<<" that is under construction. Number of units available: "<<generic_available<< ". Number of units originally assigned: "<<assigned<<". Number of units assigning: "<<num_to_assign<<"."<<std::endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, i->requiredLevel+1, num_to_assign, b->identity.gid);
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, num_to_assign)));
			i->assigned=num_to_assign;
		}
		reduce(free_workers, i->requiredLevel, num_to_assign);
	}
	return false;
}




bool RandomUpgradeRepairModule::startNewConstruction(void)
{
 std::array<unsigned,NB_UNIT_LEVELS> ratios{};
 std::array<unsigned,DemandCount> constructionCounts{};
 auto requiredLevel=[&](const AIEngine::BuildingView& building,bool repair) {
  const int next=repair ? AIEngine::ObservationQueries::buildingType(*ai.game,building).prevLevel : AIEngine::ObservationQueries::buildingType(*ai.game,building).nextLevel;
  return next<0 ? AIEngine::ObservationQueries::buildingType(*ai.game,building).semantics.requiredWorkerLevel : (&ai.game->catalog->at(next).resolvedType)->semantics.requiredWorkerLevel;
 };
 auto countBuilding=[&](const AIEngine::BuildingView& building) {
  for(unsigned demand=0;demand<DemandCount;++demand)
   if(provides(*ai.game,building,demand)) ++constructionCounts[demand];
 };
 for(const auto* list : {&active_construction,&pending_construction})
  for(const auto& record:*list)
   if(auto* building=getBuildingFromGid(ai.game,record.building)) {
    ++ratios[requiredLevel(*building,record.is_repair)];
    countBuilding(*building);
   }
 struct Candidate { const AIEngine::BuildingView* building; bool repair; unsigned level; unsigned score; };
 std::vector<Candidate> candidates;
 for(auto* building:ai.game->buildingSlots(ai.team->number)) {
  if(!building || building->constructionResultState!=Building::NO_CONSTRUCTION || building->buildingState!=Building::ALIVE) continue;
  if(std::any_of(pending_construction.begin(),pending_construction.end(),[&](const auto& pending){return pending.building==building->identity.gid;})) continue;
  const bool repair=AIEngine::ObservationQueries::buildingType(*ai.game,*building).semantics.repairable && building->hp<building->maxHp;
  const unsigned weight=upgradeWeight(*ai.game,*building);
  if(!repair && (weight==0 || !ai.game->isUpgradeAvailable(*building) || ai.game->configuration->isUnitUpgradesDisabled())) continue;
  bool hasBudget=repair;
  for(unsigned demand=0;demand<DemandCount;++demand)
   if(provides(*ai.game,*building,demand) && constructionCounts[demand]<unsigned(MAX_BUILDING_SPECIFIC_CONSTRUCTION_LIMITS[demand])) hasBudget=true;
  if(!hasBudget) continue;
  const unsigned level=requiredLevel(*building,repair);
  ++ratios[level];
  candidates.push_back({building,repair,level,weight ? ai.random()%weight : 0});
 }
 // Sample each candidate once. The comparator is pure and deterministic.
 std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
  if(a.score!=b.score) return a.score>b.score;
  return a.building->identity.gid<b.building->identity.gid;
 });
 unsigned totalMaximum=0;
 std::array<int,NB_UNIT_LEVELS> freeWorkers{};
 for(unsigned level=0;level<NB_UNIT_LEVELS;++level) {
  if(ratios[level]) ratios[level]=ratios[level]/BUILDINGS_FOR_UPGRADE+1;
  totalMaximum+=ratios[level];
  ai.getUnitModule()->changeUnits("RandomUpgradeRepairModule",WORKER,ratios[level]*MAXIMUM_TO_UPGRADE,BUILD,level+1);
  freeWorkers[level]=ai.getUnitModule()->available("RandomUpgradeRepairModule",WORKER,BUILD,level+1,false);
 }
 for(const auto& candidate:candidates) {
  if(active_construction.size()+pending_construction.size()>=totalMaximum) break;
  unsigned available=0;
  for(unsigned level=candidate.level;level<NB_UNIT_LEVELS;++level) available+=freeWorkers[level];
  if(available<(candidate.repair ? MINIMUM_TO_REPAIR : MINIMUM_TO_UPGRADE)) continue;
  auto* building=candidate.building;
  const unsigned assigned=std::min(available,candidate.repair ? MAXIMUM_TO_REPAIR : MAXIMUM_TO_UPGRADE);
  constructionRecord record;
  record.building=building->identity.gid; record.assigned=assigned; record.original=building->maxUnitWorking; record.is_repair=candidate.repair; record.requiredLevel=candidate.level;
  pending_construction.push_back(record);
  ai.enqueueOrder(AIEngine::ObservationQueries::constructionOrder(*ai.game, *building,1,1));
  ai.getUnitModule()->reserve("RandomUpgradeRepairModule",WORKER,BUILD,candidate.level+1,assigned);
  reduce(freeWorkers.data(),candidate.level,assigned);
  countBuilding(*building);
 }
 return false;
}




DistributedUnitManager::DistributedUnitManager(AICabino& ai) : ai(ai)
{
	unit_names[WORKER]="workers";
	unit_names[WARRIOR]="warriors";
	unit_names[EXPLORER]="explorers";
	ability_names[STOP_WALK]="stop walking";
	ability_names[STOP_SWIM]="stop swimming";
	ability_names[STOP_FLY]="stop flying";
	ability_names[WALK]="walking";
	ability_names[SWIM]="swimming";
	ability_names[FLY]="flying";
	ability_names[BUILD]="building";
	ability_names[HARVEST]="harvesting";
	ability_names[ATTACK_SPEED]="attacking";
	ability_names[ATTACK_STRENGTH]="attacking";

	ability_names[MAGIC_ATTACK_AIR]="air-air attacking";
	ability_names[MAGIC_ATTACK_GROUND]="air-ground attacking";
	ability_names[MAGIC_CREATE_WOOD]="creating wood";
	ability_names[MAGIC_CREATE_WHEAT]="creating wheat";
	ability_names[MAGIC_CREATE_ALGA]="creating algae";
	ability_names[ARMOR]="armor";
	ability_names[HP]="hp";
	ability_names[HEAL]="healing";
	ability_names[FEED]="feeding";
}




std::string DistributedUnitManager::getName() const
{
	return "DistributedUnitManager";
}




bool DistributedUnitManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DistributedUnitManager");
	stream->readEnterSection("module_records");
	Uint32 unitRecordSize = stream->readCount("size");
	for (Uint32 unitRecordIndex = 0; unitRecordIndex < unitRecordSize; unitRecordIndex++)
	{
		stream->readEnterSection(unitRecordIndex);
		moduleRecord mr;
		std::string name=stream->readText("name");
		for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; ++i)
		{
			stream->readEnterSection(i);
			for(unsigned int j=0; static_cast<int>(j)<NB_ABILITY; ++j)
			{
				stream->readEnterSection(j);
				for(unsigned int k=0; static_cast<int>(k)<NB_UNIT_LEVELS; ++k)
				{
					stream->readEnterSection(k);
					mr.requested[i][j][k]=stream->readUint32("requested");
					mr.reservedUnits[i][j][k]=stream->readUint32("reservedUnits");
					mr.usingUnits[i][j][k]=stream->readUint32("usingUnits");
					stream->readLeaveSection();
				}
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
		}
		// Older no-upgrades saves can retain higher-level warrior reservations.
		// All live requests now recruit from the base level; migrate the matching
		// counters too, or releasing an old reservation would subtract from zero
		// and wrap the unsigned count, permanently withholding the army.
		if (ai.game->configuration->isUnitUpgradesDisabled())
			for (int ability=0; ability<NB_ABILITY; ++ability)
				for (int level=1; level<NB_UNIT_LEVELS; ++level)
				{
					mr.requested[WARRIOR][ability][0]+=mr.requested[WARRIOR][ability][level];
					mr.reservedUnits[WARRIOR][ability][0]+=mr.reservedUnits[WARRIOR][ability][level];
					mr.usingUnits[WARRIOR][ability][0]+=mr.usingUnits[WARRIOR][ability][level];
					mr.requested[WARRIOR][ability][level]=0;
					mr.reservedUnits[WARRIOR][ability][level]=0;
					mr.usingUnits[WARRIOR][ability][level]=0;
				}
		module_records[name]=mr;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("buildings");
	unitRecordSize = stream->readCount("size");
	for (Uint32 unitRecordIndex = 0; unitRecordIndex < unitRecordSize; unitRecordIndex++)
	{
		stream->readEnterSection(unitRecordIndex);
		usageRecord ur;
		unsigned int gid=stream->readUint32("gid");
		ur.owner= stream->readText("owner");
		ur.x=stream->readUint32("x");
		ur.y=stream->readUint32("y");
		ur.type=stream->readUint32("type");
		ur.level=stream->readUint32("level");
  if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) {
   auto* building=getBuildingFromGid(ai.game,gid);
   const int concrete=building ? building->typeNum : legacyConcrete(*ai.game,ur.type,ur.level,false);
   if(concrete<0) return false;
   ur.type=concrete;
  }
		ur.ability=stream->readUint32("ability");
		ur.unit_type=stream->readUint32("unit_type");
		ur.minimum_level=stream->readUint32("minimum_level");
		ur.number=stream->readUint32("number");
		if (ur.unit_type >= NB_UNIT_TYPE || ur.ability >= NB_ABILITY || ur.minimum_level >= NB_UNIT_LEVELS || ur.level >= NB_UNIT_LEVELS || ur.type >= ai.game->catalog->size()) return false;
		// Keep each usage record in the same bucket as its migrated usingUnits
		// counter; reassignment and removal subtract through this stored index.
		if (ai.game->configuration->isUnitUpgradesDisabled() && ur.unit_type==WARRIOR)
			ur.minimum_level=0;
		buildings[gid]=ur;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void DistributedUnitManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("DistributedUnitManager");
	stream->writeEnterSection("module_records");
	stream->writeUint32(module_records.size(), "size");
	Uint32 unitRecordIndex = 0;
	for (std::map<std::string, moduleRecord>::const_iterator i=module_records.begin(); i!=module_records.end(); ++i)
	{
		stream->writeEnterSection(unitRecordIndex++);
		stream->writeText(i->first, "name");
		for(unsigned int j=0; static_cast<int>(j)<NB_UNIT_TYPE; ++j)
		{
			stream->writeEnterSection(j);
			for(unsigned int k=0; static_cast<int>(k)<NB_ABILITY; ++k)
			{
				stream->writeEnterSection(k);
				for(unsigned int l=0; static_cast<int>(l)<NB_UNIT_LEVELS; ++l)
				{
					stream->writeEnterSection(l);
					stream->writeUint32(i->second.requested[j][k][l], "requested");
					stream->writeUint32(i->second.reservedUnits[j][k][l], "reservedUnits");
					stream->writeUint32(i->second.usingUnits[j][k][l], "usingUnits");
					stream->writeLeaveSection();
				}
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("buildings");
	stream->writeUint32(buildings.size(), "size");
	unitRecordIndex = 0;
	for(std::map<int, usageRecord>::const_iterator i = buildings.begin(); i!=buildings.end(); ++i)
	{
		stream->writeEnterSection(unitRecordIndex++);
		stream->writeUint32(i->first, "gid");
		stream->writeText(i->second.owner, "owner");
		stream->writeUint32(i->second.x, "x");
		stream->writeUint32(i->second.y, "y");
		stream->writeUint32(i->second.type, "type");
		stream->writeUint32(i->second.level, "level");
		stream->writeUint32(i->second.ability, "ability");
		stream->writeUint32(i->second.unit_type, "unit_type");
		stream->writeUint32(i->second.minimum_level, "minimum_level");
		stream->writeUint32(i->second.number, "number");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




void DistributedUnitManager::changeUnits(std::string moduleName, unsigned int unitType, unsigned int numUnits, unsigned int ability, unsigned int level)
{
	// Disabled training can supply a standing army, but cannot satisfy an attack-level request.
	if (ai.game->configuration->isUnitUpgradesDisabled() && unitType==WARRIOR) level=1;
	level-=1;
	module_records[moduleName].requested[unitType][ability][level]=numUnits;
}




unsigned int DistributedUnitManager::available(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int level, bool is_minimum)
{
	if (ai.game->configuration->isUnitUpgradesDisabled() && unit_type==WARRIOR) level=1;
	TeamStatsGenerator stat(ai.game,ai.team);
	int num_available=stat.getUnits(unit_type, Unit::MED_FREE, Unit::ACT_RANDOM, ability, level, is_minimum);
	level-=1;
	int needed = getNeededUnits(unit_type, ability, level, is_minimum);
	num_available-=needed;
	for(std::map<std::string, moduleRecord>::iterator i=module_records.begin(); i!=module_records.end(); ++i)
        {
                num_available-=i->second.reservedUnits[unit_type][ability][level];
        }

	std::string min_module=getMinModule(module_name, unit_type, ability, level);

	if(min_module!=module_name)
		return 0;

	if(num_available<0)
		return 0;

	return num_available;
}




bool DistributedUnitManager::request(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number,  int building)
{
	if (ai.game->configuration->isUnitUpgradesDisabled() && unit_type==WARRIOR) minimum_level=1;
	assert(unit_type<NB_UNIT_TYPE && ability<NB_ABILITY && minimum_level>=1 && minimum_level<=NB_UNIT_LEVELS);
	minimum_level-=1;
	usageRecord ur;
	const AIEngine::BuildingView* b=getBuildingFromGid(ai.game, building);
	if(buildings.find(building)!=buildings.end())
	{
		ur=buildings[building];
		module_records[ur.owner].usingUnits[ur.unit_type][ur.ability][ur.minimum_level]-=ur.number;
		if(b==NULL || number==0)
		{
			buildings.erase(buildings.find(building));
		}
	}
	if(b==NULL)
		return false;

	if(number>0)
	{
		ur.owner=module_name;
		ur.x=b->posX;
		ur.y=b->posY;
		ur.type=b->typeNum;
		ur.level=AIEngine::ObservationQueries::buildingType(*ai.game,*b).level;
		ur.ability=ability;
		ur.unit_type=unit_type;
		ur.minimum_level=minimum_level;
		ur.number=number;
		buildings[building]=ur;
		module_records[ur.owner].usingUnits[ur.unit_type][ur.ability][ur.minimum_level]+=number;
		return true;
	}
	return true;
}



void DistributedUnitManager::reserve(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number)
{
	if (ai.game->configuration->isUnitUpgradesDisabled() && unit_type==WARRIOR) minimum_level=1;
	assert(unit_type<NB_UNIT_TYPE && ability<NB_ABILITY && minimum_level>=1 && minimum_level<=NB_UNIT_LEVELS);
	module_records[module_name].reservedUnits[unit_type][ability][minimum_level-1]+=number;
}




void DistributedUnitManager::unreserve(std::string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number)
{
	if (ai.game->configuration->isUnitUpgradesDisabled() && unit_type==WARRIOR) minimum_level=1;
	assert(unit_type<NB_UNIT_TYPE && ability<NB_ABILITY && minimum_level>=1 && minimum_level<=NB_UNIT_LEVELS);
	module_records[module_name].reservedUnits[unit_type][ability][minimum_level-1]-=number;
}




void DistributedUnitManager::writeDebug()
{
	TeamStatsGenerator stat(ai.game,ai.team);
	for(std::map<std::string, moduleRecord>::iterator i=module_records.begin(); i!=module_records.end(); ++i)
	{
		ai.clearDebugMessages("DistributedUnitManager", i->first, "Requested Units");
		ai.clearDebugMessages("DistributedUnitManager", i->first, "Using Units");
		ai.clearDebugMessages("DistributedUnitManager", i->first, "Reserved Units");
		for(int j=0; j<NB_UNIT_TYPE; ++j)
		{
			for(int k=0; k<NB_ABILITY; ++k)
			{
				for(int l=0; l<NB_UNIT_LEVELS; ++l)
				{

					if(i->second.reservedUnits[j][k][l]!=0)
					{
						std::stringstream s;
						s<<"This module has "<<i->second.reservedUnits[j][k][l]<<" "<<unit_names[j];
						s<<" reserved, with a minimum level of "<<l+1<<" in "<<ability_names[k]<<".";
						ai.addDebugMessage("DistributedUnitManager", i->first, "Reserved Units", s.str());
					}
					if(i->second.requested[j][k][l]!=0)
					{
						std::stringstream s;
						s<<"This module requests "<<i->second.requested[j][k][l]<<" "<<unit_names[j];
						s<<", with a minimum level of "<<l+1<<" in "<<ability_names[k]<<".";
						ai.addDebugMessage("DistributedUnitManager", i->first, "Requested Units", s.str());
						s.str("");
						s<<"It is using "<<getUsagePercent(i->first, j, k, l)<<"% of these units. There are "<<stat.getUnits(j, Unit::MED_FREE, Unit::ACT_RANDOM, k, l+1, true)<<" units *free* that meet the criteria, and "<<stat.getUnits(j, k, l+1, true)<<" units in total.";
						ai.addDebugMessage("DistributedUnitManager", i->first, "Requested Units", s.str());
						s.str("");
						s<<getNeededUnits(j, k, l, true)<<" of these units are needed for other buildings. The remaining units are being given to "<<getMinModule(i->first, j, k, l)<<".";
						ai.addDebugMessage("DistributedUnitManager", i->first, "Requested Units", s.str());
					}

					if(i->second.usingUnits[j][k][l]!=0)
					{
						std::stringstream s;
						s<<"This module is using "<<i->second.usingUnits[j][k][l]<<" "<<unit_names[j];
						s<<", with a minimum level of "<<l+1<<" in "<<ability_names[k]<<".";
						ai.addDebugMessage("DistributedUnitManager", i->first, "Using Units", s.str());
					}
				}
			}
		}
	}
}




int DistributedUnitManager::getUsagePercent(const std::string& module, int unit_type, int ability, int level)
{
	unsigned int total_requested=0;
	unsigned int total_used=0;
	const moduleRecord& mod = module_records[module];
	total_used+=mod.reservedUnits[unit_type][ability][level];
	total_requested+=mod.requested[unit_type][ability][level];
	total_used+=mod.usingUnits[unit_type][ability][level];
	unsigned int percent=0;
	if(total_requested>0)
		percent=total_used*100/total_requested;
	else
		percent=100;
	return percent;
}




std::string DistributedUnitManager::getMinModule(const std::string& bias, int unit_type, int ability, int level)
{
	///This is given as the 'unreachable' highest value.
	unsigned int min_percent=100000;
	std::string min_module="";
	for(std::map<std::string, moduleRecord>::iterator i=module_records.begin(); i!=module_records.end(); ++i)
	{
		if(i->second.requested[unit_type][ability][level]==0)
			continue;
		unsigned int percent=getUsagePercent(i->first, unit_type, ability, level);
		if(percent<min_percent || (i->first==bias && percent==min_percent))
		{
			min_percent=percent;
			min_module=i->first;
		}
	}
	return min_module;
}




int DistributedUnitManager::getNeededUnits(int unit_type, int ability, int level, bool is_minimum)
{
	int needed=0;
	for(std::map<int, usageRecord>::iterator i=buildings.begin(); i!=buildings.end();)
	{
		const AIEngine::BuildingView* b = getBuildingFromGid(ai.game, i->first);
		if(b==NULL ||  b->posX != static_cast<int>(i->second.x) || b->posY != static_cast<int>(i->second.y) || b->typeNum != static_cast<int>(i->second.type) || AIEngine::ObservationQueries::buildingType(*ai.game,*b).level!=static_cast<int>(i->second.level))
		{
			module_records[i->second.owner].usingUnits[i->second.unit_type][i->second.ability][i->second.minimum_level]-=i->second.number;
			// erase(i) invalidates i; its return value is the next valid
			// iterator, so skip the (removed) for-loop auto-increment below.
			i = buildings.erase(i);
			continue;
		}
		const auto& usage=i->second;
		if(static_cast<int>(usage.unit_type)==unit_type && static_cast<int>(usage.ability)==ability
			&& (is_minimum ? static_cast<int>(usage.minimum_level)<=level : static_cast<int>(usage.minimum_level)==level))
		{
			int assigned=0;
			for(const auto reference:ai.game->workers(*b))
			{
				const auto* unit=ai.game->unit(reference);
				if(unit && unit->typeNum==unit_type
					&& (ability==BUILD ? unit->constructionLevel : unit->level[ability])>=static_cast<int>(usage.minimum_level))++assigned;
			}
			needed+=std::max(0,static_cast<int>(usage.number)-assigned);
		}
		++i;
	}
	return needed;
}




BasicDistributedSwarmManager::BasicDistributedSwarmManager(AICabino& ai) : DistributedUnitManager(ai), ai(ai)
{
	ai.setUnitModule(this);
}




bool BasicDistributedSwarmManager::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::BasicDistributedSwarmManager_perform_input_time_slice_n,
					 time_slice_n);
	ai.telemetry.count(AITrace::AI8::BasicDistributedSwarmManager_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(
				AITrace::AI8::BasicDistributedSwarmManager_perform_result,
				AITrace::AI8::BasicDistributedSwarmManager_perform_true, moderateSwarms());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::BasicDistributedSwarmManager_perform_result,
									 AITrace::AI8::BasicDistributedSwarmManager_perform_true,
									 false);
}




std::string BasicDistributedSwarmManager::getName() const
{
	return "BasicDistributedSwarmManager";
}




bool BasicDistributedSwarmManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BasicDistributedSwarmManager");
	DistributedUnitManager::load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}




void BasicDistributedSwarmManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BasicDistributedSwarmManager");
	DistributedUnitManager::save(stream);
	stream->writeLeaveSection();
}




bool BasicDistributedSwarmManager::moderateSwarms()
{
	writeDebug();
	//The number of units we want for each priority level
	unsigned int num_wanted[NB_UNIT_TYPE];
	unsigned int total_available[NB_UNIT_TYPE];
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		total_available[i]=ai.team->statistics.numberUnitPerType[i];
		num_wanted[i]=0;
	}

	//Counts out the requested units from each of the modules
	for (std::map<std::string, moduleRecord>::iterator i = module_records.begin(); i!=module_records.end(); i++)
	{
		for(unsigned int j=0; static_cast<int>(j)<NB_UNIT_TYPE; ++j)
			for(unsigned int k=0; static_cast<int>(k)<NB_ABILITY; ++k)
				for(unsigned int l=0; static_cast<int>(l)<NB_UNIT_LEVELS; ++l)
					num_wanted[j]+=i->second.requested[j][k][l];
	}

	//Substract the already-existing amount of units from the numbers requested, and then move these totals multiplied by their respective score
	//into the ratios. ratios[NB_UNIT_TYPE] can't be unsigned or it won't pass into OrderModifySwarm properly
	int ratios[NB_UNIT_TYPE];
	unsigned int total_wanted_score=0;
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		ratios[i]=0;
		if(total_available[i] > num_wanted[i])
		{
			total_available[i]-=num_wanted[i];
			num_wanted[i]=0;
		}
		else
		{
			num_wanted[i]-=total_available[i];
		}
		ratios[i]+=num_wanted[i];
		total_wanted_score+=ratios[i];
	}

	if (ai.game->configuration->isPeacefulModeEnabled()) ratios[WARRIOR]=0;
	int max=*std::max_element(ratios, ratios+NB_UNIT_TYPE);
	int devisor=1;
	if(max>16)
		devisor=max/16;

	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; ++i)
	{
		unsigned int num=ratios[i];
		ratios[i]=static_cast<int>(std::floor(num/devisor+0.5));
		if(num>0 && ratios[i]==0)
			ratios[i]=1;
	}

	unsigned int assigned_per_swarm=MAXIMUM_UNITS_FOR_SWARM;
    AIEngine::ResourceInitializations scratch;
    AIEngine::WorldQueries queries(*ai.game,ai.team->number,scratch);
    if(auto order=queries.missingProductionOrder({ratios[0],ratios[1],ratios[2]},assigned_per_swarm,assigned_per_swarm)) {
        ai.enqueueOrder(order);return true;
    }


	bool need_to_output=true;
	for (const auto reference:ai.team->swarms)
	{
		const auto* swarm=ai.game->building(reference);
		if (!swarm) continue;
  Sint32 desired[NB_UNIT_TYPE];
  for(int unit=0;unit<NB_UNIT_TYPE;++unit) desired[unit]=AIEngine::ObservationQueries::buildingType(*ai.game,*swarm).semantics.production.recipes[unit].enabled ? ratios[unit] : 0;
  const int assigned=std::min<int>(assigned_per_swarm,AIEngine::ObservationQueries::buildingType(*ai.game,*swarm).semantics.assignmentLimit);
  bool changed=false;
		for (int x = 0; x<NB_UNIT_TYPE; x++)
			if (swarm->ratio[x]!=desired[x])
				changed=true;

		if(swarm->maxUnitWorking < assigned)
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(swarm->identity.gid, assigned)));

		if(!changed)
			continue;

		if(AICabino_DEBUG && need_to_output)
			ai.diagnosticStream<<"AICabino: moderateSpawns: Turning changing production ratios on a swarm from {Worker:"<<swarm->ratio[0]<<", Explorer:"<<swarm->ratio[1]<<", Warrior:"<<swarm->ratio[2]<<"} to {Worker:"<<ratios[0]<<", Explorer:"<<ratios[1]<<", Warrior:"<<ratios[2]<<"}. Assigning "<<assigned_per_swarm<<" workers."<<std::endl;
		need_to_output=false;
		ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifySwarm(swarm->identity.gid, desired)));
	}
	return false;
}




ExplorationManager::ExplorationManager(AICabino& ai) : ai(ai)
{
	ai.addOtherModule(this);
	explorers_wanted=0;
	original_explorers_wanted=0;
}




bool ExplorationManager::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::ExplorationManager_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::ExplorationManager_perform_calls);
	explorers_wanted = TOTAL_EXPLORERS;
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::ExplorationManager_perform_result,
											 AITrace::AI8::ExplorationManager_perform_true,
											 moderateSwarmsForExplorers());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::ExplorationManager_perform_result,
									 AITrace::AI8::ExplorationManager_perform_true, false);
}




std::string ExplorationManager::getName() const
{
	return "ExplorationManager";
}




bool ExplorationManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ExplorationManager");
	explorers_wanted=stream->readUint32("explorers_wanted");
	if (versionMinor >= AI_CABINO_SAVE_FORMAT_CONTINUATION)
	{
		original_explorers_wanted = stream->readUint32("original_explorers_wanted");
	}
	stream->readLeaveSection();
	return true;
}




void ExplorationManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("ExplorationManager");
	stream->writeUint32(explorers_wanted, "explorers_wanted");
	stream->writeUint32(original_explorers_wanted,"original_explorers_wanted");
	stream->writeLeaveSection();
}



bool ExplorationManager::moderateSwarmsForExplorers(void)
{
	//I've raised the priority on explorers temporarily for testing.
	//	changeUnits("aircontrol", EXPLORER, static_cast<int>(desired_explorers/2) , desired_explorers, 0);
	if(original_explorers_wanted!=explorers_wanted)
	{
		if(original_explorers_wanted>0)
			ai.getUnitModule()->unreserve("ExplorationManager", EXPLORER, FLY, 1, original_explorers_wanted);
		ai.getUnitModule()->changeUnits("ExplorationManager", EXPLORER, explorers_wanted, FLY, 1);
		ai.getUnitModule()->reserve("ExplorationManager", EXPLORER, FLY, 1, explorers_wanted);
		original_explorers_wanted=explorers_wanted;
	}
	return false;
}




InnManager::InnManager(AICabino& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool InnManager::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::InnManager_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::InnManager_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::InnManager_perform_result,
											 AITrace::AI8::InnManager_perform_true, recordInns());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::InnManager_perform_result,
											 AITrace::AI8::InnManager_perform_true, modifyInns());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::InnManager_perform_result,
									 AITrace::AI8::InnManager_perform_true, false);
}




std::string InnManager::getName() const
{
	return "InnManager";
}




bool InnManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("InnManager");
	stream->readEnterSection("inns");
	Uint32 innRecordSize = stream->readCount("size");
	for (Uint32 innRecordIndex = 0; innRecordIndex < innRecordSize; innRecordIndex++)
	{
		stream->readEnterSection(innRecordIndex);
		innRecord ir;
		unsigned int gid=stream->readUint32("gid");
		ir.pos=stream->readUint32("pos");
		unsigned int size=stream->readCount("size");
		// While the ring fills, pos is the next append index (equal to size).
		// An empty record is valid too; a full ring must point inside the ring.
		if (size > INN_RECORD_MAX || ir.pos > size || ir.pos >= INN_RECORD_MAX) return false;
		for(unsigned int i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			ir.records.push_back(singleInnRecord(stream->readUint32("food_amount")));
			stream->readLeaveSection();
		}
		inns[gid]=ir;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void InnManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("InnManager");
	stream->writeEnterSection("inns");
	stream->writeUint32(static_cast<Uint32>(inns.size()), "size");
	Uint32 innRecordIndex = 0;
	for(std::map<int, innRecord>::const_iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		stream->writeEnterSection(innRecordIndex++);
		stream->writeUint32(i->first, "gid");
		stream->writeUint32(i->second.pos, "pos");
		stream->writeUint32(i->second.records.size(), "size");
		Uint32 recordIndex = 0;
		for(std::vector<singleInnRecord>::const_iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			stream->writeEnterSection(recordIndex++);
			stream->writeUint32(record->food_amount, "food_amount");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




InnManager::innRecord::innRecord() : pos(0), records() {}

bool InnManager::recordInns()
{
	for(int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b=ai.game->buildingSlots(ai.team->number)[i];
		if (b)
		{
			if(provides(*ai.game,*b,FeedUnits) && b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				innRecord& i = inns[b->identity.gid];
				if(i.records.size()<INN_RECORD_MAX)
					i.records.push_back(singleInnRecord(feedingStock(*ai.game,*b,false)));
				else
					i.records[i.pos].food_amount=feedingStock(*ai.game,*b,false);
				i.pos+=1;
				if (i.pos==INN_RECORD_MAX)
				{
					i.pos=0;
				}
			}
		}
	}
	return false;
}




bool InnManager::modifyInns()
{
	unsigned int total_workers_needed=0;

	for(std::map<int, innRecord>::iterator i = inns.begin(); i!=inns.end();)
	{
		const AIEngine::BuildingView* inn=getBuildingFromGid(ai.game, i->first);
		if (inn==NULL || !provides(*ai.game,*inn,FeedUnits))
		{
			ai.getUnitModule()->request("InnManager", WORKER, HARVEST, 1, 0, i->first);
			// erase(i) invalidates i; its return value is the next valid
			// iterator, so re-check the loop condition without also ++i-ing.
			i = inns.erase(i);
			continue;
		}

		if (inn->constructionResultState!=Building::NO_CONSTRUCTION)
		{
			++i;
			continue;
		}

		unsigned int average=0;
		for (std::vector<singleInnRecord>::iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			average+=record->food_amount;
		}
		if(i->second.records.empty()) { ++i; continue; }
  average/=i->second.records.size();

		const unsigned capacity=feedingStock(*ai.game,*inn,true);
  const unsigned deficit=capacity>average ? capacity-average : 0;
  const unsigned level=std::clamp(ai.game->capabilities().lineagePosition(inn->typeNum)-1,0,2);
  unsigned to_assign=capacity ? std::max(INN_MINIMUM[level],std::min(INN_MAX[level],deficit/WHEAT_NEEDED_FOR_UNIT)) : 0;
  unsigned services=0;
  for(unsigned demand=0;demand<DemandCount;++demand) services+=provides(*ai.game,*inn,demand);
  if(services>1) to_assign=std::max<unsigned>(to_assign,inn->maxUnitWorking);
  to_assign=std::min<unsigned>(to_assign,AIEngine::ObservationQueries::buildingType(*ai.game,*inn).semantics.assignmentLimit);
		total_workers_needed+=to_assign;
		if(static_cast<int>(to_assign)!=inn->maxUnitWorking)
		{
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: modifyInns: Changing the number of units assigned to an inn from "<<inn->maxUnitWorking<<" to "<<to_assign<<"."<<std::endl;
			ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(inn->identity.gid, to_assign)));
			ai.getUnitModule()->request("InnManager", WORKER, HARVEST, 1, to_assign, inn->identity.gid);
		}
		++i;
	}

	ai.getUnitModule()->changeUnits("InnManager", WORKER, total_workers_needed, HARVEST, 1);
	return false;
}




TowerController::TowerController(AICabino& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool TowerController::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::TowerController_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::TowerController_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::TowerController_perform_result,
											 AITrace::AI8::TowerController_perform_true,
											 controlTowers());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::TowerController_perform_result,
									 AITrace::AI8::TowerController_perform_true, false);
}




std::string TowerController::getName() const
{
	return "TowerController";
}




bool TowerController::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("TowerController");
	stream->readLeaveSection();
	return true;
}




void TowerController::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("TowerController");
	stream->writeLeaveSection();
}




bool TowerController::controlTowers()
{
	ai.telemetry.count(AITrace::AI8::TowerController_controlTowers_calls);

	int count=0;
	for(int i=0; i<1024; i++)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if (b)
		{
			if(provides(*ai.game,*b,DefendWithProjectiles) &&
				b->buildingState==Building::ALIVE &&
				b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				count+=1;
				if(b->maxUnitWorking < std::min<int>(NUM_PER_TOWER,AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.assignmentLimit))
				{
					if(AICabino_DEBUG)
						ai.diagnosticStream<<"AICabino: controlTowers: Changing number of units assigned to a tower, from "<<b->maxUnitWorking<<" to "<<NUM_PER_TOWER<<"."<<std::endl;
					ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, std::min<int>(NUM_PER_TOWER,AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.assignmentLimit))));
				}
			}
		}
	}
	ai.getUnitModule()->changeUnits("TowerController", WORKER, count*NUM_PER_TOWER, HARVEST, 1);

	return ai.telemetry.returnedBool(AITrace::AI8::TowerController_controlTowers_result,
									 AITrace::AI8::TowerController_controlTowers_true, false);
}




BuildingClearer::BuildingClearer(AICabino& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool BuildingClearer::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::BuildingClearer_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::BuildingClearer_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::BuildingClearer_perform_result,
											 AITrace::AI8::BuildingClearer_perform_true,
											 removeOldPadding());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::BuildingClearer_perform_result,
											 AITrace::AI8::BuildingClearer_perform_true,
											 updateClearingAreas());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::BuildingClearer_perform_result,
									 AITrace::AI8::BuildingClearer_perform_true, false);
}




std::string BuildingClearer::getName() const
{
	return "BuildingClearer";
}




bool BuildingClearer::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingClearer");
	Uint32 clearingRecordSize = stream->readCount("size");
	for (Uint32 clearingRecordIndex = 0; clearingRecordIndex < clearingRecordSize; clearingRecordIndex++)
	{
		stream->readEnterSection(clearingRecordIndex);
		clearingRecord cr;
		cr.x=stream->readUint32("x");
		cr.y=stream->readUint32("y");
		cr.width=stream->readUint32("width");
		cr.height=stream->readUint32("height");
		cr.level=stream->readUint32("level");
		cleared_buildings[stream->readUint32("first")]=cr;
		// FIXME : clear the container before load

		stream->readLeaveSection();
	}

	stream->readLeaveSection();
	return true;
}




void BuildingClearer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BuildingClearer");
	stream->writeUint32(cleared_buildings.size(), "size");
	Uint32 clearingRecordIndex = 0;
	for(std::map<int, clearingRecord>::const_iterator i=cleared_buildings.begin(); i!=cleared_buildings.end(); ++i)
	{
		stream->writeEnterSection(clearingRecordIndex++);
		stream->writeUint32(i->second.x, "x");
		stream->writeUint32(i->second.y, "y");
		stream->writeUint32(i->second.width, "width");
		stream->writeUint32(i->second.height, "height");
		stream->writeUint32(i->second.level, "level");
		stream->writeUint32(i->first, "first");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}




bool BuildingClearer::removeOldPadding()
{
	for(std::map<int, clearingRecord>::iterator i=cleared_buildings.begin(); i!=cleared_buildings.end();)
	{
		const AIEngine::BuildingView* b = getBuildingFromGid(ai.game, i->first);
		if(b==NULL || AIEngine::ObservationQueries::buildingType(*ai.game,*b).level != static_cast<int>(i->second.level) || i->second.x!=b->posX-CLEARING_AREA_BUILDING_PADDING || i->second.y!=b->posY-CLEARING_AREA_BUILDING_PADDING)
		{

			unsigned int x_bound=i->second.x+i->second.width;
			unsigned int y_bound=i->second.y+i->second.height;
			if(static_cast<int>(x_bound) > ai.map->width)
				x_bound-=ai.map->width;
			if(static_cast<int>(y_bound) > ai.map->height)
				y_bound-=ai.map->height;

			BrushAccumulator acc;
			for(unsigned int x=i->second.x; x!=x_bound; ++x)
			{
				if(static_cast<int>(x)==ai.map->width)
					x=0;
				for(unsigned int y=i->second.y; y!=y_bound; ++y)
				{
					if(static_cast<int>(y)==ai.map->height)
						y=0;
					if(((ai.map->areasAt(ai.map->tileIndex(x,y)).clear&(ai.team->mask))!=0))
					{
						acc.applyBrush(BrushApplication(x,y,0), ai.map->width, ai.map->height);
					}
				}
			}
			if(acc.getApplicationCount()>0)
				ai.enqueueOrder(AIEngine::observationAreaOrder<OrderAlterClearArea>(ai.team->number, BrushTool::MODE_DEL, acc));
			// erase(i) invalidates i; its return value is the next valid
			// iterator, so skip the (removed) for-loop auto-increment below.
			i = cleared_buildings.erase(i);
		}
		else
		{
			++i;
		}
	}
	return false;
}




bool BuildingClearer::updateClearingAreas()
{
	ai.telemetry.count(AITrace::AI8::BuildingClearer_updateClearingAreas_calls);
	for(unsigned int i=0; i<1024; ++i)
	{
		const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[i];
		if(b)
		{
			if( AIEngine::ObservationQueries::buildingType(*ai.game,*b).semantics.occupiesGround &&
				cleared_buildings.find(b->identity.gid) == cleared_buildings.end() &&
				b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				clearingRecord cr;
				cr.x=b->posX-CLEARING_AREA_BUILDING_PADDING;
				cr.y=b->posY-CLEARING_AREA_BUILDING_PADDING;
				cr.width=AIEngine::ObservationQueries::buildingType(*ai.game,*b).width+CLEARING_AREA_BUILDING_PADDING*2;
				cr.height=AIEngine::ObservationQueries::buildingType(*ai.game,*b).height+CLEARING_AREA_BUILDING_PADDING*2;
				cr.level=AIEngine::ObservationQueries::buildingType(*ai.game,*b).level;

				unsigned int x_bound=cr.x+cr.width;
				unsigned int y_bound=cr.y+cr.height;
				if(static_cast<int>(x_bound) > ai.map->width)
					x_bound-=ai.map->width;
				if(static_cast<int>(y_bound) > ai.map->height)
					y_bound-=ai.map->height;

				BrushAccumulator acc;
				for(unsigned int x=cr.x; x!=x_bound; ++x)
				{
					if(static_cast<int>(x)==ai.map->width)
						x=0;
					for(unsigned int y=cr.y; y!=y_bound; ++y)
					{
						if(static_cast<int>(y)==ai.map->height)
							y=0;
						if(!((ai.map->areasAt(ai.map->tileIndex(x,y)).clear&(ai.team->mask))!=0))
						{
							acc.applyBrush(BrushApplication(x, y, 0), ai.map->width, ai.map->height);
						}
					}
				}
				if(AICabino_DEBUG)
					ai.diagnosticStream<<"AICabino: updateClearingAreas: Adding clearing area around the building at "<<b->posX<<","<<b->posY<<"."<<std::endl;
				if(acc.getApplicationCount()>0)
					ai.enqueueOrder(AIEngine::observationAreaOrder<OrderAlterClearArea>(ai.team->number, BrushTool::MODE_ADD, acc));
				cleared_buildings[b->identity.gid]=cr;
			}
		}
	}
	return ai.telemetry.returnedBool(AITrace::AI8::BuildingClearer_updateClearingAreas_result,
									 AITrace::AI8::BuildingClearer_updateClearingAreas_true, false);
}




HappinessHandler::HappinessHandler(AICabino& ai) : ai(ai)
{
	ai.addOtherModule(this);
	is_fruit_trees_computed=false;
}




HappinessHandler::~HappinessHandler()
{

}




bool HappinessHandler::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::HappinessHandler_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::HappinessHandler_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::HappinessHandler_perform_result,
											 AITrace::AI8::HappinessHandler_perform_true,
											 adjustAlliances());
		case 1:
			return ai.telemetry.returnedBool(AITrace::AI8::HappinessHandler_perform_result,
											 AITrace::AI8::HappinessHandler_perform_true,
											 searchFruitTrees());
	}

	return ai.telemetry.returnedBool(AITrace::AI8::HappinessHandler_perform_result,
									 AITrace::AI8::HappinessHandler_perform_true, false);
}




std::string HappinessHandler::getName() const
{
	return "HappinessHandler";
}




bool HappinessHandler::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("HappinessHandler");
	if (versionMinor >= AI_CABINO_SAVE_FORMAT_CONTINUATION)
	{
		is_fruit_trees_computed = stream->readUint8("is_fruit_trees_computed") != 0;
		stream->readEnterSection("fruit_trees");
		fruit_trees.clear();
		const auto fruit_treesCount = stream->readUint32("size");
		if (fruit_treesCount > Uint32(ai.map->width*ai.map->height)) return false;
		for (Uint32 i=0; i<fruit_treesCount; ++i) {
			stream->readEnterSection(i);
			fruitTreeRecord record;
			record.fruit_tree_max_x = AIStateSerialization::readSint32(stream,"fruit_tree_max_x");
			record.fruit_tree_max_y = AIStateSerialization::readSint32(stream,"fruit_tree_max_y");
			record.fruit_tree_min_x = AIStateSerialization::readSint32(stream,"fruit_tree_min_x");
			record.fruit_tree_min_y = AIStateSerialization::readSint32(stream,"fruit_tree_min_y");
			record.fruit_tree_type = AIStateSerialization::readSint32(stream,"fruit_tree_type");
			fruit_trees.push_back(record);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("exploring_fruit_trees");
		exploring_fruit_trees.clear();
		const auto exploring_fruit_treesCount = stream->readUint32("size");
		if (exploring_fruit_treesCount > Uint32(ai.map->width*ai.map->height)) return false;
		for (Uint32 i=0; i<exploring_fruit_treesCount; ++i) {
			stream->readEnterSection(i);
			fruitTreeExplorationRecord record;
			record.flag = AIStateSerialization::readSint32(stream,"flag");
			record.pos_x = AIStateSerialization::readSint32(stream,"pos_x");
			record.pos_y = AIStateSerialization::readSint32(stream,"pos_y");
			record.radius = AIStateSerialization::readSint32(stream,"radius");
			exploring_fruit_trees.push_back(record);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
}




void HappinessHandler::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("HappinessHandler");
	stream->writeUint8(is_fruit_trees_computed,"is_fruit_trees_computed");
	stream->writeEnterSection("fruit_trees");
	stream->writeUint32(fruit_trees.size(),"size");
	for (Uint32 i=0; i<fruit_trees.size(); ++i) {
		stream->writeEnterSection(i);
		stream->writeSint32(fruit_trees[i].fruit_tree_max_x,"fruit_tree_max_x");
		stream->writeSint32(fruit_trees[i].fruit_tree_max_y,"fruit_tree_max_y");
		stream->writeSint32(fruit_trees[i].fruit_tree_min_x,"fruit_tree_min_x");
		stream->writeSint32(fruit_trees[i].fruit_tree_min_y,"fruit_tree_min_y");
		stream->writeSint32(fruit_trees[i].fruit_tree_type,"fruit_tree_type");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("exploring_fruit_trees");
	stream->writeUint32(exploring_fruit_trees.size(),"size");
	for (Uint32 i=0; i<exploring_fruit_trees.size(); ++i) {
		stream->writeEnterSection(i);
		stream->writeSint32(exploring_fruit_trees[i].flag,"flag");
		stream->writeSint32(exploring_fruit_trees[i].pos_x,"pos_x");
		stream->writeSint32(exploring_fruit_trees[i].pos_y,"pos_y");
		stream->writeSint32(exploring_fruit_trees[i].radius,"radius");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool HappinessHandler::adjustAlliances()
{
	Uint32 food_mask=ai.team->mask;
	unsigned int total_happiness=0;
	unsigned int total_units=std::max(1, ai.team->statistics.numberUnitPerType[WORKER]);
	for(unsigned int i=0; i<HAPPINESS_COUNT+1; ++i)
	{
		total_happiness+=ai.team->statistics.happiness[i]*i;
	}
	int average_happiness=total_happiness/total_units;

	for(unsigned int i=0; i<static_cast<unsigned int>(Team::MAX_COUNT); ++i)
	{
		const AIEngine::TeamView* t=teamAt(*ai.game,i);
		if(t)
		{
			if(t->mask & ai.team->enemies)
			{
				if(ai.team->mask & t->foodVision)
				{
					food_mask=food_mask|t->mask;
					continue;
				}
				unsigned int enemy_happiness=0;
				unsigned int enemy_units=std::max(1, t->statistics.numberUnitPerType[WORKER]);
				for(unsigned int i=0; i<HAPPINESS_COUNT+1; ++i)
				{
					enemy_happiness+=t->statistics.happiness[i]*i;
				}
				int enemy_average_happiness=enemy_happiness/enemy_units;
				if(average_happiness>enemy_average_happiness)
				{
					food_mask=food_mask|t->mask;
				}
			}
		}
	}

	if(food_mask!=ai.team->foodVision)
	{
		if(AICabino_DEBUG)
			ai.diagnosticStream<<"AICabino: adjustAlliances: Adjusting food vision alliance."<<std::endl;
		ai.enqueueOrder(std::shared_ptr<Order>(new SetAllianceOrder(ai.team->number, ai.team->allies, ai.team->enemies, ai.team->exchangeVision, food_mask, ai.team->otherVision)));
	}

	return false;
}




bool HappinessHandler::searchFruitTrees()
{
 const int typeNum=selectBuilding(ai,AttractExplorers);
 if(typeNum<0) return false;
	ai.getUnitModule()->changeUnits("HappinessHandler", EXPLORER, REQUESTED_EXPLORERES*HAPPINESS_COUNT, FLY, 1);
	computeFruitTrees();
	int closest_tree_score[HAPPINESS_COUNT];
	point flagLocation[HAPPINESS_COUNT];
	int flagRadius[HAPPINESS_COUNT];
	std::fill(closest_tree_score, closest_tree_score+HAPPINESS_COUNT, -1);
	for(std::vector<fruitTreeRecord>::iterator i=fruit_trees.begin(); i!=fruit_trees.end(); ++i)
	{
		unsigned center_x=i->fruit_tree_min_x+(i->fruit_tree_max_x-i->fruit_tree_min_x)/2;
		unsigned center_y=i->fruit_tree_min_y+(i->fruit_tree_max_y-i->fruit_tree_min_y)/2;
		int nearness_score = std::min(intdistance(ai.getCenterX(), center_x), ai.map->width-intdistance(ai.getCenterX(), center_x))
					+std::min(intdistance(ai.getCenterY(), center_y), ai.map->height-intdistance(ai.getCenterY(), center_y));
		if(closest_tree_score[i->fruit_tree_type]==-1 || closest_tree_score[i->fruit_tree_type]>nearness_score)
		{
			closest_tree_score[i->fruit_tree_type]=nearness_score;
			flagLocation[i->fruit_tree_type]=point(center_x, center_y);
			flagRadius[i->fruit_tree_type]=std::max((i->fruit_tree_max_x-i->fruit_tree_min_x)/2, (i->fruit_tree_max_y-i->fruit_tree_min_y)/2);
		}
	}

	int available_units=ai.getUnitModule()->available("HappinessHandler", EXPLORER, FLY, 1, true);
	for(unsigned int n=0; n<HAPPINESS_COUNT; ++n)
	{
		if(closest_tree_score[n]==-1)
			continue;

		if(available_units<static_cast<int>(EXPLORERS_PER_GROUP))
			break;

		bool found=false;
		for(std::vector<fruitTreeExplorationRecord>::iterator i = exploring_fruit_trees.begin(); i!=exploring_fruit_trees.end(); ++i)
		{
			if(i->pos_x == flagLocation[n].x && i->pos_y == flagLocation[n].y)
			{
				found=true;
				break;
			}
		}
		if(!found)
		{
			if(!placeRallyNear(ai,typeNum,flagLocation[n].x,flagLocation[n].y)) continue;
   fruitTreeExplorationRecord fter;
			if(AICabino_DEBUG)
				ai.diagnosticStream<<"AICabino: searchFruitTrees: Creating a new exploration flag for a group of trees."<<std::endl;

			ai.enqueueOrder(AIEngine::ObservationQueries::createOrder(*ai.game, ai.team->number, flagLocation[n].x, flagLocation[n].y, typeNum, 1, 1));
			fter.flag=NOGBID;
			fter.pos_x=flagLocation[n].x;
			fter.pos_y=flagLocation[n].y;
			fter.radius=std::max(flagRadius[n], static_cast<int>(MINIMUM_FLAG_SIZE));
			exploring_fruit_trees.push_back(fter);
			available_units-=EXPLORERS_PER_GROUP;
		}
	}

	for(std::vector<fruitTreeExplorationRecord>::iterator i = exploring_fruit_trees.begin(); i!=exploring_fruit_trees.end(); ++i)
	{
		if(i->flag==NOGBID)
		{
			for(unsigned int n=0; n<1024; ++n)
			{
				const AIEngine::BuildingView* b = ai.game->buildingSlots(ai.team->number)[n];
				if(b)
				{
					if(b->posX == i->pos_x && b->posY == i->pos_y && provides(*ai.game,*b,AttractExplorers))
					{
						i->flag=b->identity.gid;
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyFlag(i->flag, i->radius)));
						ai.enqueueOrder(std::shared_ptr<Order>(new OrderModifyBuilding(i->flag, EXPLORERS_PER_GROUP)));
						ai.getUnitModule()->request("HappinessHandler", EXPLORER, FLY, 1, EXPLORERS_PER_GROUP, i->flag);
					}
				}
			}
		}
	}
	return false;
}




void HappinessHandler::computeFruitTrees()
{
	if(is_fruit_trees_computed==false)
	{
		is_fruit_trees_computed=true;
		std::set<point> examined_points;
		for(int x=0; x<ai.map->width; ++x)
		{
			for(int y=0; y<ai.map->height; ++y)
			{
				int res_type=ai.map->resourceAt(ai.map->tileIndex(x,y)).resource.type;
				if(res_type>=HAPPINESS_BASE && res_type<MAX_RESOURCES && examined_points.count(point(x, y))==0)
				{
					examined_points.insert(point(x, y));
					int max_x=x;
					int max_y=y;
					int min_x=x;
					int min_y=y;
					std::vector<point> points_to_examine;
					points_to_examine.push_back(point(x, y));
					field::breadthFirst(points_to_examine,
						[&](const point& p) {
							if(p.x>max_x)
								max_x=p.x;
							else if(p.x<min_x)
								min_x=p.x;
							if(p.y>min_y)
								max_y=p.y;
							else if(p.y<min_y)
								min_y=p.y;
	
							return field::Visit::Expand;
						},[&](const point& p) {
							const int xl=ai.map->normalizeX(p.x-1),xr=ai.map->normalizeX(p.x+1);
							const int yu=ai.map->normalizeY(p.y-1),yd=ai.map->normalizeY(p.y+1);
							// These cardinal coordinates are deliberately anchored to the seed.
							const point neighbors[8]={point(xl,yu),point(x,yu),point(xr,yu),
								point(xl,y),point(xr,y),point(xl,yd),point(x,yd),point(xr,yd)};
							for(const point& next:neighbors)
								if(ai.map->resourceAt(ai.map->tileIndex(next.x,next.y)).resource.type==res_type && examined_points.count(next)==0)
								{examined_points.insert(next);points_to_examine.push_back(next);}
							return field::Visit::Expand;
						});
					fruitTreeRecord ftr;
					ftr.fruit_tree_max_x=max_x;
					ftr.fruit_tree_min_x=min_x;
					ftr.fruit_tree_max_y=max_y;
					ftr.fruit_tree_min_y=min_y;
					ftr.fruit_tree_type=res_type-HAPPINESS_BASE;
					fruit_trees.push_back(ftr);
				}
			}
		}
	}
}




Farmer::Farmer(AICabino& ai) : ai(ai), is_water_gradient_computed(false)
{
	ai.addOtherModule(this);
}




Farmer::~Farmer()
{

}




bool Farmer::perform(unsigned int time_slice_n)
{
	ai.telemetry.set(AITrace::AI8::Farmer_perform_input_time_slice_n, time_slice_n);
	ai.telemetry.count(AITrace::AI8::Farmer_perform_calls);
	switch(time_slice_n)
	{
		case 0:
			return ai.telemetry.returnedBool(AITrace::AI8::Farmer_perform_result,
											 AITrace::AI8::Farmer_perform_true, updateFarm());
	}
	return ai.telemetry.returnedBool(AITrace::AI8::Farmer_perform_result,
									 AITrace::AI8::Farmer_perform_true, false);
}




std::string Farmer::getName() const
{
	return "Farmer";
}




bool Farmer::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Farmer");
	Uint32 pointsSize = stream->readCount("size");
	for (Uint32 pointsIndex = 0; pointsIndex < pointsSize; pointsIndex++)
	{
		stream->readEnterSection(pointsIndex);
		point p;
		p.x=stream->readUint16("x");
		p.y=stream->readUint16("y");
		resources.insert(p);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	if (versionMinor >= AI_CABINO_SAVE_FORMAT_CONTINUATION)
	{
		is_water_gradient_computed = stream->readUint8("is_water_gradient_computed") != 0;
		if (is_water_gradient_computed && !water_gradient.load(stream, ai)) return false;
	}
	stream->readLeaveSection();
	return true;
}




void Farmer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("Farmer");
	stream->writeUint32(resources.size(), "size");
	Uint32 pointsIndex = 0;
	for(std::set<point>::const_iterator i=resources.begin(); i!=resources.end(); ++i)
	{
		stream->writeEnterSection(pointsIndex++);
		stream->writeUint16(i->x, "x");
		stream->writeUint16(i->y, "y");
		stream->writeLeaveSection();
	}
	stream->writeUint8(is_water_gradient_computed,"is_water_gradient_computed");
	if (is_water_gradient_computed) water_gradient.save(stream);
	stream->writeLeaveSection();
}




bool Farmer::updateFarm()
{
	ai.telemetry.count(AITrace::AI8::Farmer_updateFarm_calls);
	if(!is_water_gradient_computed)
	{
		water_gradient.reset(ai, Gradient::Water, Gradient::None);
		field::Frontier frontier;
		water_gradient.update(frontier);
		is_water_gradient_computed=true;
	}

	BrushAccumulator del_acc;
	BrushAccumulator add_acc;
	// With the farm-areas experiment, wheat near water is farmed with a farm
	// area on the same pattern; forbidden paint continues to protect wood.
	const bool farms=ai.map->farmAreasEnabled;
	BrushAccumulator farm_del_acc;
	BrushAccumulator farm_add_acc;
	for(unsigned int x=0; static_cast<int>(x)<ai.map->width; ++x)
	{
		for(unsigned int y=0; static_cast<int>(y)<ai.map->height; ++y)
		{
			const bool farm_spot = ((x%2!=y%2) && FARMING_METHOD==CheckerBoard) ||
				((x%2==1 && y%2==1) && FARMING_METHOD==CrossSpacing) ||
				((x%6<4 && y%3==0) && FARMING_METHOD==Row4) ||
				((x%3==0 && y%6<4) && FARMING_METHOD==Column4);
			if(farms && ((ai.map->visibilityAt(ai.map->tileIndex(x,y)).discovered&(ai.team->mask))!=0))
			{
				const bool wheat_farm=farm_spot && resourceTakeable(ai.map->resourceAt(ai.map->tileIndex(x,y)).resource,WHEAT)
					&& !((ai.map->areasAt(ai.map->tileIndex(x,y)).clear&(ai.team->mask))!=0)
					&& ai.map->canPaintFarmAt(ai.map->tileIndex(x,y))
					&& water_gradient.getHeight(x, y)<=static_cast<int>(MAX_DISTANCE_FROM_WATER+2);
				const bool farmed=((ai.map->areasAt(ai.map->tileIndex(x,y)).farm&(ai.team->mask))!=0);
				if(wheat_farm && !farmed)
					farm_add_acc.applyBrush(BrushApplication(x, y, 0), ai.map->width, ai.map->height);
				else if(!wheat_farm && farmed)
					farm_del_acc.applyBrush(BrushApplication(x, y, 0), ai.map->width, ai.map->height);
			}

			if(farm_spot)
			{
				const bool protectable=farms
					? resourceTakeable(ai.map->resourceAt(ai.map->tileIndex(x,y)).resource,WOOD)
					: resourceTakeable(ai.map->resourceAt(ai.map->tileIndex(x,y)).resource,WOOD) || resourceTakeable(ai.map->resourceAt(ai.map->tileIndex(x,y)).resource,WHEAT);
				if(!protectable || ((ai.map->areasAt(ai.map->tileIndex(x,y)).clear&(ai.team->mask))!=0))
				{
					if(resources.find(point(x, y))!=resources.end())
					{
						del_acc.applyBrush(BrushApplication(x, y, 0), ai.map->width, ai.map->height);
						resources.erase(resources.find(point(x, y)));
					}
				}
				else
				{
					if(resources.find(point(x, y))==resources.end() && ((ai.map->visibilityAt(ai.map->tileIndex(x,y)).discovered&(ai.team->mask))!=0) && water_gradient.getHeight(x, y)<=static_cast<int>(MAX_DISTANCE_FROM_WATER+2))
					{
						add_acc.applyBrush(BrushApplication(x, y, 0), ai.map->width, ai.map->height);
						resources.insert(point(x, y));
					}
				}
			}
		}
	}

	if(del_acc.getApplicationCount()>0)
		ai.enqueueOrder(AIEngine::observationAreaOrder<OrderAlterForbidden>(ai.team->number, BrushTool::MODE_DEL, del_acc));
	if(add_acc.getApplicationCount()>0)
		ai.enqueueOrder(AIEngine::observationAreaOrder<OrderAlterForbidden>(ai.team->number, BrushTool::MODE_ADD, add_acc));
	if(farm_del_acc.getApplicationCount()>0)
		ai.enqueueOrder(AIEngine::observationAreaOrder<OrderAlterFarmArea>(ai.team->number, BrushTool::MODE_DEL, farm_del_acc));
	if(farm_add_acc.getApplicationCount()>0)
		ai.enqueueOrder(AIEngine::observationAreaOrder<OrderAlterFarmArea>(ai.team->number, BrushTool::MODE_ADD, farm_add_acc));
	return ai.telemetry.returnedBool(AITrace::AI8::Farmer_updateFarm_result,
									 AITrace::AI8::Farmer_updateFarm_true, false);
}

// Cached gradients can be older than the map. Preserve their cells and the
// rotating refresh FIFO, rather than recomputing them when loading a game.
void Gradient::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("Gradient");
	stream->writeUint32(sources,"sources");
	stream->writeUint32(obstacles,"obstacles");
	stream->writeUint32(gradient.size(),"cells");
	for (Uint32 i=0; i<gradient.size(); ++i) {
		stream->writeEnterSection(i);
		stream->writeSint32(gradient[i],"height");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

bool Gradient::load(GAGCore::InputStream *stream, AICabino& owner)
{
	stream->readEnterSection("Gradient");
	const auto savedSources=stream->readUint32("sources");
	const auto savedObstacles=stream->readUint32("obstacles");
	const unsigned validSources = 63u | (((1u << MAX_NB_RESOURCES) - 1u) << 8);
	if ((savedSources & ~validSources) || savedObstacles > 3) return false;
	const auto count=stream->readUint32("cells");
	if (count != Uint32(owner.map->width*owner.map->height)) return false;
	reset(owner,savedSources,savedObstacles);
	for (Uint32 i=0; i<count; ++i) {
		stream->readEnterSection(i);
		const auto value=AIStateSerialization::readSint32(stream,"height");
		if (value < -32768 || value > 32767) return false;
		gradient[i]=static_cast<short>(value);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return stream->isValid();
}

void GradientManager::clear()
{
	update_queue={};
	gradients.clear();
}

void GradientManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GradientManager");
	// Serializing in refresh order encodes both the map and its FIFO without
	// persisting iterators or changing insertion/refresh order on load.
	auto pending=update_queue;
	stream->writeUint32(pending.size(),"size");
	Uint32 index=0;
	while (!pending.empty()) {
		stream->writeEnterSection(index++);
		stream->writeUint32(pending.front()->first.sources,"sources");
		stream->writeUint32(pending.front()->first.obstacles,"obstacles");
		pending.front()->second.save(stream);
		pending.pop();
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

bool GradientManager::load(GAGCore::InputStream *stream)
{
	clear();
	stream->readEnterSection("GradientManager");
	const auto count=stream->readUint32("size");
	// Existing source combinations plus one compiled resource-set mask per demand.
	if (count > 4u * (64u + team->game->catalog->size())) return false;
	for (Uint32 i=0; i<count; ++i) {
		stream->readEnterSection(i);
		const auto sources=stream->readUint32("sources");
		const auto obstacles=stream->readUint32("obstacles");
		gradientSignature signature(sources,obstacles);
		if (gradients.count(signature)) return false;
		Gradient field;
		if (!field.load(stream,*team) || field.sources != sources || field.obstacles != obstacles) return false;
		auto result=gradients.emplace(signature,std::move(field));
		update_queue.push(result.first);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return stream->isValid();
}

// Scheduling outcomes reconcile private strategic reservations on the worker
// stream. The existing module records remain the saved continuation format.
void AICabino::applyReceipts(const AIEngine::DecisionContext& context)
{
    for(const auto& receipt:context.receipts) {
        if(receipt.command.empty()) continue;
        auto order=Order::getOrder(receipt.command.data(),receipt.command.size(),VERSION_MINOR);
        if(!order) continue;
        if(order->getOrderType()==ORDER_CONSTRUCTION) {
            auto* upgrades=dynamic_cast<RandomUpgradeRepairModule*>(upgrade_repair_module);
            const auto gid=static_cast<OrderConstruction&>(*order).gid;
            if(!upgrades) continue;
            if(receipt.status==AIEngine::ExecutionStatus::Accepted) {
                const auto* building=getBuildingFromGid(game,gid);
                // A completed or canceled instant operation can disappear before
                // this module's next slice. An older receipt is reflected in
                // this observation, so release its otherwise stranded claim.
                if(receipt.executionTick>=context.world.tick || (building && building->constructionResultState!=Building::NO_CONSTRUCTION)) continue;
            }
            for(auto i=upgrades->pending_construction.begin();i!=upgrades->pending_construction.end();) {
                if(i->building!=gid) {++i;continue;}
                unit_module->unreserve("RandomUpgradeRepairModule",WORKER,BUILD,i->requiredLevel+1,i->assigned);
                i=upgrades->pending_construction.erase(i);
            }
        } else if(receipt.status==AIEngine::ExecutionStatus::Accepted) {
            continue;
        } else if(order->getOrderType()==ORDER_CREATE) {
            const auto& create=static_cast<OrderCreate&>(*order);
            const auto x=map->normalizeX(create.posX),y=map->normalizeY(create.posY);
            auto* construction=dynamic_cast<DistributedNewConstructionManager*>(new_construction_module);
            std::erase_if(construction->new_buildings,[&](const auto& r){return r.building==NOGBID && r.x==unsigned(x) && r.y==unsigned(y);});
            construction->updateImap();
            auto* defense=dynamic_cast<SimpleBuildingDefense*>(defense_module);
            if(defense) std::erase_if(defense->defending_zones,[&](const auto& r){return r.flag==NOGBID && r.flagx==unsigned(x) && r.flagy==unsigned(y);});
            if(auto* generals=dynamic_cast<GeneralsDefense*>(defense_module))
                std::erase_if(generals->defending_flags,[&](const auto& r){return r.flag==NOGBID && r.x==x && r.y==y;});
            auto* attacks=dynamic_cast<PrioritizedBuildingAttack*>(attack_module);
            if(attacks) std::erase_if(attacks->attacks,[&](const auto& r){return r.flag==NOGBID && r.flagx==unsigned(x) && r.flagy==unsigned(y);});
            if(auto* happiness=dynamic_cast<HappinessHandler*>(getOtherModule("HappinessHandler")))
                std::erase_if(happiness->exploring_fruit_trees,[&](const auto& r){return r.flag==NOGBID && r.pos_x==x && r.pos_y==y;});
        } else if(order->getOrderType()==ORDER_ALTER_FORBIDDEN) {
            if(auto* farmer=dynamic_cast<Farmer*>(getOtherModule("Farmer"))) {
                const auto& area=static_cast<OrderAlterForbidden&>(*order);
                size_t bit=0;
                for(int y=area.centerY+area.minY;y<area.centerY+area.maxY;++y)
                    for(int x=area.centerX+area.minX;x<area.centerX+area.maxX;++x,++bit)
                        if(area.mask.get(bit)) {
                            const Farmer::point p(map->normalizeX(x),map->normalizeY(y));
                            if(((map->areasAt(map->tileIndex(x,y)).forbidden&(team->mask))!=0)) farmer->resources.insert(p);
                            else farmer->resources.erase(p);
                        }
            }
        }
    }
}

std::optional<Uint64> Cabino::AICabino::retainedQueryVectorBytes() const
{
    Uint64 bytes=gradient_manager.retainedQueryVectorBytes();
    for(const auto* module:modules) bytes+=module->retainedQueryVectorBytes();
    return bytes;
}
