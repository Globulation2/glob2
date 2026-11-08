// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameFixtures.h"
#include "sim/snapshot/SnapshotStore.h"
#include "gradient/MapRouting.h"
#include "GradientFixture.h"
#include "HiringPipeline.h"
#include "Building.h"
#include "Unit.h"

#include "AI.h"
#include "Player.h"
#include "Order.h"

namespace glob2test
{
	std::vector<std::string> stepAI(Game &game)
	{
		std::vector<std::string> orders;
		std::vector<std::shared_ptr<Order>> pending;
		for (int p = 0; p < game.gameHeader.getNumberOfPlayers(); ++p)
			if (game.players[p] && game.players[p]->ai)
			{
				auto order = game.players[p]->ai->getOrder(false);
				std::string wire(1, char(order->getOrderType()));
				if (order->getDataLength())
					wire.append(reinterpret_cast<const char *>(order->getData()), order->getDataLength());
				orders.push_back(std::move(wire));
				order->sender = p;
				pending.push_back(std::move(order));
			}
		// Production decisions all observe the same completed simulation tick.
		for (const auto &order : pending) game.executeOrder(order, order->sender);
		game.step(0);
		return orders;
	}
}

class HiringPipelineHarness
{
  public:
    static void discardPremature(Game &game)
    {
        if(!game.hiringPipeline)return;
        game.drainHiring();
        for(unsigned team=0;team<unsigned(game.teamsCount());++team)
            if(const auto &o=game.hiringPipeline->published[team];o &&
               (o->tick+HiringPipeline::Delay>game.stepCounter || o->tick%HiringPipeline::Refresh!=team%HiringPipeline::Refresh))
                game.hiringPipeline->published[team].reset();
    }
	static void publish(Game &game)
	{
		game.drainHiring();
		game.snapshotStore().invalidateBoundary();
		const auto setup = game.captureReadBoundary();
		// Immediate-ranking fixtures establish the same published inputs that real
		// matches obtain over their normal ticks; no unit actions run during setup.
		for (const auto& record : setup.entities->buildings)
		{
			if (!HiringPlanner::needsCandidates(record)) continue;
			auto* building = game.resolveBuilding(record.identity);
			if (!building) continue;
			for (int swim=0; swim<MOVEMENT_CLASS_COUNT; ++swim)
				if (setup.entities->availableMovementClasses[record.team] & (1u<<swim))
				{
					for (int material=0; material<MaterialCount; ++material)
						if (building->materialDeliveryNeed(material)>0)
							glob2test::awaitGradient(game,[&] { return game.map.routing().getMaterialGradientSlot(record.team,material,movementAccessFromLegacy(swim),false,building); });
				}
			const auto& type = setup.catalogs->buildings->at(record.typeNum).resolvedType;
			const bool footprint = std::any_of(std::begin(type.maxMaterial),std::begin(type.maxMaterial)+MaterialCount,[](int value){return value>0;}) ||
				std::any_of(std::begin(record.wishedMaterials),std::begin(record.wishedMaterials)+MaterialCount,[](int value){return value>0;});
			for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
				if (setup.entities->availableMovementClasses[record.team] & (1u<<swim))
				{
					for (auto route : {BuildingRoute::Footprint,BuildingRoute::Clearing,BuildingRoute::Combat})
					{
						if ((route==BuildingRoute::Footprint && !footprint) ||
							(route==BuildingRoute::Clearing && !type.attractsRole(0)) ||
							(route==BuildingRoute::Combat && !type.attractsRole(1) && !type.attractsRole(2))) continue;
						glob2test::awaitGradient(game,[&] {
							game.map.routing().requestBuildingField(building,movementAccessFromLegacy(swim),route);
							return game.map.routing().lookupBuildingField(building,movementAccessFromLegacy(swim),route);
						});
					}
				}
		}
		game.snapshotStore().invalidateBoundary();
		auto input = HiringPipeline::bindRoutes(game, game.captureReadBoundary());
		if (!game.hiringPipeline)
			game.hiringPipeline = std::make_unique<HiringPipeline>(game.map.computeExecutor());
		for (unsigned team = 0; team < unsigned(game.teamsCount()); ++team)
		{
			HiringPlanner::Planner cache;
			auto output = std::make_shared<HiringPlanner::Output>(cache.rank(input, team, 128));
			game.hiringPipeline->published[team] = std::move(output);
		}
	}
};
namespace glob2test
{
void discardPrematureHiringSuggestions(Game &game)
{
    HiringPipelineHarness::discardPremature(game);
}
void publishCurrentHiringSuggestions(Game &game)
{
	HiringPipelineHarness::publish(game);
}
} // namespace glob2test
#include "TurretTargeting.h"
namespace glob2test {
void prepareTurretTargets(Building& turret)
{
    auto& game=*turret.owner->game;
    if (std::find(turret.owner->turrets.begin(),turret.owner->turrets.end(),&turret)==turret.owner->turrets.end())
        turret.owner->addToStaticAbilitiesLists(&turret);
    game.snapshots().invalidateBoundary();
    for (unsigned phase=0;phase<4;++phase) {
        const auto observed=game.captureReadBoundary();
        game.turretTargeting()->admit(game,observed,false);
        ++game.stepCounter;
        game.turretTargeting()->publish(game);
        if (!game.turretTargeting()->entries(turret).empty()) return;
    }
}
}
