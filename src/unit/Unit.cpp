// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "unit/planning/MovementShadow.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "EngineTiming.h"
#include "UnitTiming.h"
#include "UnitTraining.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include <Stream.h>
#include <climits>
#include <stdexcept>
#include <limits>
#include <numeric>
#include "field/AirPathfind.h"
#include "field/TerrainMovementCosts.h"

Unit::Unit(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	init(0,0,0,0,owner,0);
	load(stream, owner, versionMinor);
}

Unit::Unit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
{
	init(x, y, gid, typeNum, team, level);
	core.scriptIdentity = owner->game->allocateScriptIdentity(false, gid);
	detail.entityRandom.initialize(owner->game->gameHeader.getRandomSeed(), EntityRandom::Kind::Unit, gid, core.scriptIdentity);
}

Unit::~Unit()
{
	if (owner && owner->game) {
		if (hasCapability(UnitRuntimeTraits::ReleaseClearingClaims) && previousClearingArea && owner->map->getW()>0 && owner->map->isClearingAreaClaimed(previousClearingArea->x,previousClearingArea->y,owner->teamNumber)==core.gid)
			owner->map->setClearingAreaUnclaimed(previousClearingArea->x,previousClearingArea->y,owner->teamNumber);
		owner->game->unitCargo.erase(core.gid);
	}
}

const UnitRuntimeTraits& Unit::runtimeTraits() const { return race->getRuntime(core.typeNum); }
int Unit::foodStepsLeft(int threshold) const
{
	if (detail.hungriness <= 0) return INT_MAX/4;
	const Sint64 remaining = Sint64(core.hungry)-threshold;
	if (remaining >= INT_MIN && remaining <= INT_MAX) return int(remaining)/detail.hungriness;
	return int(std::clamp<Sint64>(remaining/detail.hungriness,INT_MIN,INT_MAX));
}

void Unit::refreshEffectiveAbilities()
{
	const auto& traits=runtimeTraits();
	const std::pair<Abilities,UnitRuntimeTraits::Flag> gates[]={{WALK,UnitRuntimeTraits::Walk},{SWIM,UnitRuntimeTraits::Swim},{FLY,UnitRuntimeTraits::Fly},{ATTACK_SPEED,UnitRuntimeTraits::Melee},{ATTACK_STRENGTH,UnitRuntimeTraits::Melee},{MAGIC_ATTACK_AIR,UnitRuntimeTraits::MagicAir},{MAGIC_ATTACK_GROUND,UnitRuntimeTraits::MagicGround},{MAGIC_CREATE_WOOD,UnitRuntimeTraits::MagicCreateWood},{MAGIC_CREATE_WHEAT,UnitRuntimeTraits::MagicCreateWheat},{MAGIC_CREATE_ALGA,UnitRuntimeTraits::MagicCreateAlga}};
	for (const auto& [ability,flag]:gates) {
		if (!traits.has(flag)) performance(ability)=0;
	}
	if (!traits.has(UnitRuntimeTraits::Construct) && !traits.has(UnitRuntimeTraits::Transport)) performance(BUILD)=0;
	if (!traits.has(UnitRuntimeTraits::Clear) && !traits.has(UnitRuntimeTraits::Transport)) performance(HARVEST)=0;
	for (int ability=0;ability<NB_ABILITY;++ability) detail.canLearn[ability]=(traits.learnableMask&(1u<<ability))!=0;
}

void Unit::rebindDefinitionForSetup()
{
	// Parallel training derives its reservation from the original learnability.
	// Capture that amount before replacing flags and cached abilities.
	const auto oldServiceCost=detail.serviceResourcesReserved && attachedBuilding
		? attachedBuilding->serviceCost(this,core.destinationPurpose) : BuildingMaterialCost{};
	const int oldHP=std::max(1,performance(HP));
	const int oldFood=std::max(1,detail.configuredFoodCapacity);
	const auto& traits=runtimeTraits();
	bool trainingDefinitionChanged=hasCapability(UnitRuntimeTraits::LearnConstruction)!=traits.has(UnitRuntimeTraits::LearnConstruction);
	for (int ability=0;ability<NB_ABILITY;++ability)
		trainingDefinitionChanged|=detail.canLearn[ability]!=((traits.learnableMask&(1u<<ability))!=0);
	core.capabilityFlags=traits.flags;
	detail.configuredFoodCapacity=traits.foodCapacity;
	detail.configuredVisionRadius=Uint8(traits.visionRadius);
	for (int ability=0;ability<NB_ABILITY;++ability)
		performance(ability)=race->getUnitType(core.typeNum,detail.level[ability])->performance[ability];
	performance(HP)=std::max(1,performance(HP)/owner->game->gameHeader.getGlassCannonScale());
	refreshEffectiveAbilities();
	core.hp=int(std::clamp<Sint64>(Sint64(core.hp)*performance(HP)/oldHP,INT_MIN,performance(HP)));
	core.hungry=int(std::clamp<Sint64>(Sint64(core.hungry)*detail.configuredFoodCapacity/oldFood,INT_MIN,detail.configuredFoodCapacity));
	detail.hungriness=traits.hungerRate;
	detail.trigHungry=traits.has(UnitRuntimeTraits::LegacyPerformancePolicies) ? (performance(ATTACK_SPEED) ? Sint64(detail.configuredFoodCapacity)*UNIT_HUNGRY_TRIG_NUM_WARRIOR/UNIT_HUNGRY_TRIG_DEN : detail.configuredFoodCapacity/UNIT_HUNGRY_TRIG_DIVISOR_DEFAULT) : Sint64(detail.configuredFoodCapacity)*traits.hungerTriggerNumerator/traits.hungerTriggerDenominator;
	detail.trigHungryCarrying=Sint64(detail.configuredFoodCapacity)*traits.carryingTriggerNumerator/traits.carryingTriggerDenominator;
	detail.trigHP=owner->game->gameHeader.isUnitsFearless()?0:Sint64(performance(HP))*traits.medicalTriggerNumerator/traits.medicalTriggerDenominator;
	// Existing map units may have an assignment authored under the preceding
	// definition. A disabled work clock cannot complete that action to release
	// its subscription, so cancel it during setup without spending random draws.
	const bool inService=core.displacement==DIS_ENTERING_BUILDING || core.displacement==DIS_INSIDE
		|| (core.displacement==DIS_EXITING_BUILDING && attachedBuilding);
	bool cancelTask=false;
	if (!inService) {
		if (core.activity==ACT_FILLING)
			cancelTask=!traits.has(UnitRuntimeTraits::Transport) || !performance(BUILD) || !performance(HARVEST)
				|| (attachedBuilding && attachedBuilding->type->isBuildingSite && !traits.has(UnitRuntimeTraits::Construct));
		else if (core.activity==ACT_FLAG) {
			if (core.jobPurpose==UnitJobPurpose::Clear) cancelTask=!traits.has(UnitRuntimeTraits::Clear) || !performance(HARVEST);
			else if (core.jobPurpose==UnitJobPurpose::Explore) cancelTask=!traits.has(UnitRuntimeTraits::Explore);
			else if (core.jobPurpose==UnitJobPurpose::Defend)
				cancelTask=!traits.has(UnitRuntimeTraits::GuardIdle) && !(traits.has(UnitRuntimeTraits::Melee) && performance(ATTACK_SPEED));
		} else if (core.activity==ACT_UPGRADING && core.destinationPurpose>=WALK && core.destinationPurpose<NB_ABILITY)
			cancelTask=trainingDefinitionChanged || !detail.canLearn[core.destinationPurpose];
		cancelTask|=(core.movement==MOV_ATTACKING_TARGET && !performance(ATTACK_SPEED))
			|| (core.movement==MOV_HARVESTING && (!performance(HARVEST) || (core.activity!=ACT_FILLING && !traits.has(UnitRuntimeTraits::Clear))))
			|| (core.movement==MOV_FILLING && !performance(BUILD));
	}
	if (previousClearingArea && (cancelTask || !traits.has(UnitRuntimeTraits::Clear))) {
		owner->map->setClearingAreaUnclaimed(previousClearingArea->x,previousClearingArea->y,owner->teamNumber);
		previousClearingArea.reset();
		previousClearingAreaDistance=UNIT_CLEAR_AREA_DISTANCE_NONE;
	}
	if (cancelTask) {
		if (attachedBuilding) {
			if (detail.serviceResourcesReserved) {
				attachedBuilding->releaseMaterials(oldServiceCost);
				detail.serviceResourcesReserved=false;
			}
			attachedBuilding->removeUnitFromWorking(this);
			attachedBuilding->removeUnitFromInside(this);
		}
		standardRandomActivity();
		core.movement=performance(FLY)?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
		core.dx=core.dy=0;
		core.direction=UNIT_DIRECTION_NONE;
		selectPreferredMovement();
		core.speed=std::max(1,performance(core.action));
	} else if (!inService && core.action>=STOP_WALK && core.action<NB_ABILITY && !performance(core.action)) {
		// Idle exploration, interrupted melee and adjacent clearing can run
		// without a building subscription and still carry an obsolete action.
		core.displacement=DIS_RANDOM;
		core.movement=performance(FLY)?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
		core.validTarget=false;
		core.dx=core.dy=0;
		core.direction=UNIT_DIRECTION_NONE;
		selectPreferredMovement();
		core.speed=std::max(1,performance(core.action));
	}
	if (core.action==WALK || core.action==SWIM || core.action==FLY || core.action==STOP_WALK || core.action==STOP_SWIM || core.action==STOP_FLY) {
		selectPreferredMovement();
		core.speed=std::max(1,performance(core.action));
	}
	detail.needToRecheckMedical=true;
}

void Unit::init(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
{
	// unit specification
	this->core.typeNum = typeNum;

	assert(team);
	race=&(team->race);
	assert(race);
	core.capabilityFlags=race->getRuntime(typeNum).flags;
	detail.configuredFoodCapacity=race->getRuntime(typeNum).foodCapacity;
	detail.configuredVisionRadius=Uint8(race->getRuntime(typeNum).visionRadius);
	core.jobPurpose=UnitJobPurpose::None;
	detail.regenerationRemainder=0;
	detail.widePrimaryCargo=false;

	// identity
	this->core.gid=gid;
	owner=team;
	core.isDead=false;

	// position
	core.posX=x;
	core.posY=y;
	core.delta=0;
	core.dx=0;
	core.dy=0;
	core.direction=UNIT_DIRECTION_NONE;
	detail.insideTimeout=0;
	detail.serviceResourcesReserved=false;
	core.constructionLevel=level;
	detail.terrainHealthRemainder=0;
	detail.areaServiceRemainders[BuildingAreaEffects::Healing]=0;
	detail.areaServiceRemainders[BuildingAreaEffects::Feeding]=0;
	core.speed=32;

	// Custom-game "glass cannon" rule: cut HP once here, at the source,
	// rather than at every downstream read. A map's starting units predate
	// the match's header; Game::applyStartingRules cuts theirs.
	const int hpDivisor = owner->game->gameHeader.getGlassCannonScale();

	// quality parameters
	for (int i=0; i<NB_ABILITY; i++)
	{
		this->performance(i)=race->getUnitType(typeNum, level)->performance[i];
		if (i==HP)
			this->performance(i)=std::max(1, this->performance(i)/hpDivisor);
		this->detail.level[i]=level;
		this->detail.canLearn[i]=(bool)race->getUnitType(typeNum, 3)->performance[i]; //TODO: is is a better way to hack this?
		// This hack prevent units from unlearning. Units level 3 must have all the abilities of all preceding levels
	}

	refreshEffectiveAbilities();
	detail.experience = 0;
	core.experienceLevel = 0;

	// states
	detail.needToRecheckMedical=true;
	core.medical=MED_FREE;
	core.activity=ACT_RANDOM;
	core.displacement=DIS_RANDOM;
	if (performance(FLY))
		core.movement=MOV_RANDOM_FLY;
	else
		core.movement=MOV_RANDOM_GROUND;

	core.targetX = 0;
	core.targetY = 0;
	core.validTarget = false;
	detail.magicActionTimeout = 0;

	core.underAttackTimer = 0;

	const auto& traits=runtimeTraits();
	core.hungry=traits.foodCapacity;
	detail.hungriness=traits.hungerRate;
	detail.trigHungry=traits.has(UnitRuntimeTraits::LegacyPerformancePolicies) ? (performance(ATTACK_SPEED) ? Sint64(core.hungry)*UNIT_HUNGRY_TRIG_NUM_WARRIOR/UNIT_HUNGRY_TRIG_DEN : core.hungry/UNIT_HUNGRY_TRIG_DIVISOR_DEFAULT) : Sint64(core.hungry)*traits.hungerTriggerNumerator/traits.hungerTriggerDenominator;
	detail.trigHungryCarrying=Sint64(core.hungry)*traits.carryingTriggerNumerator/traits.carryingTriggerDenominator;
	detail.fruitMask = 0;
	detail.fruitCount = 0;

	// NOTE : rewrite hp from level
	core.hp = this->performance(HP);
	// Custom-game "fearless" rule: fight to the death instead of retreating
	// to heal once damaged (reinstates the "warriors fight to death" intent
	// noted above, which trigHP normally overrides for everyone).
	detail.trigHP = owner->game->gameHeader.isUnitsFearless() ? 0 : (Sint64(core.hp)*traits.medicalTriggerNumerator)/traits.medicalTriggerDenominator;

	attachedBuilding=NULL;
	targetBuilding=NULL;
	ownExchangeBuilding=NULL;
	core.destinationPurpose=UNIT_DEST_PURPOSE_NONE;
	core.carriedMaterial=UNIT_CARRIED_RESOURCE_NONE;
	detail.jobTimer = 0;

	previousClearingArea=std::nullopt;
	previousClearingAreaDistance=0;

	// gui
	core.levelUpAnimation = 0;
	core.magicActionAnimation = 0;

	// debug vars:
	verbose=false;
}

void Unit::setTargetBuilding(Building * b)
{
	if(targetBuilding!=NULL) {
		targetBuilding->removeUnitFromHarvesting(this);
	}
	if(b!=NULL)
	{
		core.targetX=b->getMidX();
		core.targetY=b->getMidY();
	}
//TODO: Deal with "validTarget=true;"
    targetBuilding = b;
}

void Unit::subscriptionSuccess(Building* building, bool inside, bool attraction, UnitJobPurpose purpose)
{
	Building* b=building;
	if (attraction && !inside) {
		core.jobPurpose=purpose;
		if (core.jobPurpose==UnitJobPurpose::None)
			core.jobPurpose=hasCapability(UnitRuntimeTraits::Explore)?UnitJobPurpose::Explore:
				hasCapability(UnitRuntimeTraits::Clear)?UnitJobPurpose::Clear:UnitJobPurpose::Defend;
	} else core.jobPurpose=inside?UnitJobPurpose::None:UnitJobPurpose::Transport;

	if (attraction && !inside)
	{
		core.destinationPurpose=UNIT_DEST_PURPOSE_NONE;
		core.activity=ACT_FLAG;
		attachedBuilding=b;
	    setTargetBuilding(b);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", core.gid, core.destinationPurpose, b->gid);
	}
	else if(inside == false)
	{
		assert(core.destinationPurpose>=0);
		assert(b->neededMaterial(core.destinationPurpose));
		core.activity=ACT_FILLING;
		attachedBuilding=b;
		setTargetBuilding(NULL);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", core.gid, core.destinationPurpose, b->gid);
	}
	else
	{
		core.activity=ACT_UPGRADING;
		attachedBuilding=b;
		setTargetBuilding(b);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", core.gid, core.destinationPurpose, b->gid);
	}

	if (verbose)
		printf("guid=(%d), subscriptionSuccess()\n", core.gid);

	switch(core.medical)
	{
		case MED_HUNGRY :
		case MED_DAMAGED :
		case MED_FREE:
		{
			switch(core.activity)
			{
				case ACT_FLAG:
				{
					core.displacement=DIS_GOING_TO_FLAG;
					assert(targetBuilding==attachedBuilding);
					core.validTarget=true;
				}
				break;
				case ACT_UPGRADING:
				{
					core.displacement=DIS_GOING_TO_BUILDING;
					assert(targetBuilding==attachedBuilding);
					core.validTarget=true;
				}
				break;
				case ACT_FILLING:
				{
					assert(attachedBuilding);
					if (hasCarriedMaterial(core.destinationPurpose))
					{
						core.displacement=DIS_GOING_TO_BUILDING;
						setTargetBuilding(attachedBuilding);
						core.validTarget=true;
					}
					else
					{
						core.displacement=DIS_GOING_TO_RESOURCE;
						targetBuilding=NULL;
						findMaterialDestination(core.destinationPurpose,&core.targetX,&core.targetY,nullptr,attachedBuilding->fetchesFromMarkets(),attachedBuilding);
						core.validTarget=true;
					}
				}
				break;
				case ACT_RANDOM :
				{
					core.displacement=DIS_RANDOM;
					core.validTarget=false;
				}
				break;
				default:
					assert(false);
			}
		}
		break;
	}
}

void Unit::applyTerrainHealth()
{
	applyTerrainHealth(owner->map->terrainPropertiesAt(core.posX,core.posY));
}

void Unit::applyTerrainHealth(const TerrainProperties& terrain)
{
	// Entering positions already lie inside the building. An exiting unit is
	// exposed as soon as an exit is found and its building attachment released.
	if (core.isDead || detail.insideTimeout < 0 || core.displacement == DIS_INSIDE || core.displacement == DIS_ENTERING_BUILDING ||
		(core.displacement == DIS_EXITING_BUILDING && attachedBuilding)) return;
	const int rate = performance(FLY) ? terrain.airHealthQ8 : terrain.groundHealthQ8;
	applyTerrainHealthRate(rate);
}

void Unit::applyTerrainHealthRate(int rate)
{
	if (!rate) return;
	// Healing at the cap cannot be banked to cancel later damage. Keep a
	// negative fraction: subsequent healing may legitimately repay that debt.
	if (rate > 0 && core.hp >= performance(HP) && detail.terrainHealthRemainder >= 0)
	{
		detail.terrainHealthRemainder = 0;
		return;
	}
	detail.terrainHealthRemainder += rate;
	const int change = detail.terrainHealthRemainder / 256;
	detail.terrainHealthRemainder %= 256;
	if (change < 0) recordLethalDamage(-change, GameplayMeasurements::UNKNOWN);
	core.hp = int(std::clamp<Sint64>(Sint64(core.hp)+change,INT_MIN,performance(HP)));
	if (core.hp>=performance(HP) && owner->game->areaEffects.enabled()) detail.areaServiceRemainders[BuildingAreaEffects::Healing]=0;
	if (core.hp >= performance(HP) && detail.terrainHealthRemainder > 0) detail.terrainHealthRemainder = 0;
	if (change) detail.needToRecheckMedical = true;
	resolveDeath();
}

void Unit::applyAreaServices()
{
	using namespace BuildingAreaEffects;
	auto &game = *owner->game;
	if (!game.areaEffects.enabled() || (game.stepCounter & (PulseTicks - 1)) ||
		detail.areaLastPulseTick == game.stepCounter || core.isDead || detail.insideTimeout < 0 ||
		core.displacement == DIS_INSIDE || core.displacement == DIS_ENTERING_BUILDING ||
		(core.displacement == DIS_EXITING_BUILDING && attachedBuilding))
		return;
	const auto tile = owner->map->coordToIndex(core.posX, core.posY);
	if (!game.areaEffects.at(Healing, owner->teamNumber, tile) &&
		!game.areaEffects.at(Damage, owner->teamNumber, tile) &&
		!game.areaEffects.at(Feeding, owner->teamNumber, tile))
		return;
	detail.areaLastPulseTick = game.stepCounter;
	const int beforeHp = core.hp, beforeHunger = core.hungry;
	const auto amount = [&](Channel channel)
	{
		unsigned value =
			detail.areaServiceRemainders[channel] + game.areaEffects.at(channel, owner->teamNumber, tile);
		detail.areaServiceRemainders[channel] = value % 256;
		return int(value / 256);
	};
	const int damage = amount(Damage);
	if (damage)
	{
		recordLethalDamage(damage, GameplayMeasurements::COMBAT);
		core.hp = int(std::max<Sint64>(INT_MIN,Sint64(core.hp)-damage));
	}
	// Earlier teams may already have dealt a lethal hit this tick. Resolve it
	// even when this unit's aura damage is zero, before allowing healing.
	resolveDeath();
	if (core.isDead)
		return;
	if (core.hp >= performance(HP))
		detail.areaServiceRemainders[Healing] = 0;
	else
	{
		core.hp = std::min(performance(HP), core.hp + amount(Healing));
		if (core.hp >= performance(HP))
			detail.areaServiceRemainders[Healing] = 0;
	}
	if (game.gameHeader.isHungerDisabled() || core.hungry >= foodCapacity())
		detail.areaServiceRemainders[Feeding] = 0;
	else
	{
		core.hungry = std::min(foodCapacity(), core.hungry + amount(Feeding));
		if (core.hungry >= foodCapacity())
			detail.areaServiceRemainders[Feeding] = 0;
	}
	if (core.hp != beforeHp || core.hungry != beforeHunger)
		detail.needToRecheckMedical = true;
}

void Unit::syncStep(void)
{
	if (owner->map->hasTerrainHealthEffects()) applyTerrainHealth();
	if (core.isDead) return;
	//warrior attacks?
	if (hasCapability(UnitRuntimeTraits::Regenerate) && core.hp<performance(HP) && core.displacement!=DIS_INSIDE) {
		if (core.hp<UNIT_HP_DEATH_THRESHOLD) { resolveDeath(); if (core.isDead) return; }
		const int value=runtimeTraits().regenerationQ8+detail.regenerationRemainder;
		core.hp=std::min(performance(HP), core.hp+value/256);
		detail.regenerationRemainder=core.hp==performance(HP)?0:value%256;
	}
	assert(core.speed>=0);
	if ((core.action==ATTACK_SPEED) && (core.delta>=UNIT_ATTACK_HIT_DELTA) && (Sint64(core.delta)<(Sint64(UNIT_ATTACK_HIT_DELTA)+core.speed)))
	{
		Uint16 enemyGUID=owner->map->getGroundUnit(core.posX+core.dx, core.posY+core.dy);
		if (enemyGUID!=NOGUID)
		{
			int enemyID=GIDtoID(enemyGUID);
			int enemyTeam=GIDtoTeam(enemyGUID);
			Unit *enemy=owner->game->teams[enemyTeam]->myUnits[enemyID];

			const int damage=int(std::clamp<Sint64>(Sint64(getRealAttackStrength())-enemy->getRealArmor(false),1,INT_MAX));
			++owner->stats.measurements.shots[GameplayMeasurements::MELEE];
			TeamStats::recordDamage(owner, enemy->owner, GameplayMeasurements::MELEE,
									GameplayMeasurements::UNIT, enemy->core.hp, damage);
			enemy->recordLethalDamage(damage, GameplayMeasurements::COMBAT);
			enemy->core.hp=int(std::max<Sint64>(INT_MIN,Sint64(enemy->core.hp)-damage));

			enemy->core.underAttackTimer = UNDER_ATTACK_TIMER_TICKS;

			enemy->owner->pushGameEvent(GameEvent::unitUnderAttack(owner->game->stepCounter, enemy->core.posX, enemy->core.posY, enemy->core.typeNum));

			incrementExperience(damage);
		}
		else
		{
			Uint16 enemyGBID=owner->map->getBuilding(core.posX+core.dx, core.posY+core.dy);
			if (enemyGBID!=NOGBID)
			{
				int enemyID=Building::GIDtoID(enemyGBID);
				int enemyTeam=Building::GIDtoTeam(enemyGBID);
				Building *enemy=owner->game->teams[enemyTeam]->myBuildings[enemyID];
				const int damage=int(std::clamp<Sint64>(Sint64(getRealAttackStrength())-enemy->getEffectiveArmor(),1,INT_MAX));
				++owner->stats.measurements.shots[GameplayMeasurements::MELEE];
				TeamStats::recordDamage(owner, enemy->owner, GameplayMeasurements::MELEE,
										GameplayMeasurements::BUILDING, enemy->hp, damage);
				enemy->hp=int(std::max<Sint64>(INT_MIN,Sint64(enemy->hp)-damage));

				enemy->underAttackTimer = UNDER_ATTACK_TIMER_TICKS;

				enemy->owner->pushGameEvent(GameEvent::buildingUnderAttack(owner->game->stepCounter, enemy->posX, enemy->posY, enemy->typeNum));

				if (enemy->hp<0)
					enemy->kill(GameplayMeasurements::DESTROYED);
				incrementExperience(damage);
			}
		}
	}

	//We give globs 32 ticks to wait for a job before moving onto
	//another activity like upgrading
	if (core.medical==MED_FREE && core.activity==ACT_RANDOM)
	{
		detail.jobTimer++;
	}

	if(core.underAttackTimer > 0)
		core.underAttackTimer -= 1;

// Burst mode completes an action every tick and keeps its original unscaled speed.
//#define BURST_UNIT_MODE
	int stepSpeed=core.speed;
#ifdef BURST_UNIT_MODE
	core.delta=0;
#else
	stepSpeed=unitActionStepSpeed(core.speed, core.action, core.dx, core.dy, core.displacement==DIS_INSIDE);
	if (core.delta<=UNIT_DELTA_MAX-stepSpeed)
	{
		core.delta+=stepSpeed;
	}
	else
#endif
	{
		core.delta=int(std::clamp<Sint64>(Sint64(core.delta)+stepSpeed-UNIT_DELTA_QUANTUM,INT_MIN,INT_MAX));

		endOfAction();
        auto* diagnostic = owner->game->movementDiagnostics();
        MovementPlanning::DiagnosticBinding binding(diagnostic ? diagnostic->counters(*this) : nullptr);
        MovementPlanning::PhaseScope visibilityPhase(MovementPlanning::Phase::Visibility);

		// Historical saves derive sight from the unit's cached flight ability,
		// which can differ from its saved Race table or change during training.
		const int r=hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies)
			? (performance(FLY)?UNIT_VISION_RADIUS_FLY:UNIT_VISION_RADIUS_GROUND)
			: detail.configuredVisionRadius;
		const int d=2*r+1;
		owner->map->setMapDiscovered(core.posX-r,core.posY-r,d,d,owner->sharedVisionOther);
		owner->map->setMapBuildingsDiscovered(core.posX-r,core.posY-r,d,d,owner->sharedVisionOther,owner->game->teams);
		owner->map->setMapExploredByUnit(core.posX-r,core.posY-r,d,d,owner->teamNumber);
	}

	// gui
	if (core.levelUpAnimation > 0)
		core.levelUpAnimation--;
	if (core.magicActionAnimation > 0)
		core.magicActionAnimation--;
}

void Unit::resetAtLevel(Sint32 newLevel)
{
	// Reset abilities and activity without changing this entity's identity.
	clearCargo();
	init(core.posX, core.posY, core.gid, core.typeNum, owner, newLevel);
}

void Unit::setWorkerLevel(Sint32 newLevel)
{
	core.constructionLevel = newLevel;
	for (int ability : {(int)BUILD, (int)HARVEST})
	{
		detail.level[ability] = newLevel;
		performance(ability) = race->getUnitType(core.typeNum, newLevel)->performance[ability];
	}
	refreshEffectiveAbilities();
}

bool Unit::needsTraining(const BuildingTrainingSpec& training, int ability) const
{
	return UnitTraining::needed(*this,training,ability);
}

bool Unit::trainingVisitSafe(const Building& building,int purpose) const
{
	if(hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies))return true;
	const auto& spec=building.type->semantics;
	const auto movementCourses=UnitTraining::movementCourses(*this,spec,purpose);
	if(!movementCourses)return true;
	const unsigned before=UnitTraining::movementModes(*this);
	const unsigned after=UnitTraining::movementAfter(*this,movementCourses,spec.training,race->getCatalog()->levels(core.typeNum));
	return UnitTraining::exitTerrainSafe(before,after,building.posX,building.posY,building.type->width,building.type->height,
		[&](int x,int y)->const TerrainProperties& { return owner->map->terrainPropertiesAt(x,y); });
}

std::optional<Uint32> Unit::trainingVisitCourses(const Building& building,int purpose) const
{
	if(!trainingVisitSafe(building,purpose))return std::nullopt;
	return UnitTraining::courses(*this,building.type->semantics,purpose);
}

bool Unit::applyTraining(const BuildingTrainingSpec& training,int ability)
{
	assert(ability>=0 && ability<NB_ABILITY);
	if(ability>=WALK && ability<=FLY && !hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies)
		&& UnitTraining::movementModes(*this)) {
		// A single direct grant has no building terrain context. It must leave
		// an effective movement mode; bundled visits use the prospective exit check.
		const unsigned bit=1u<<(ability-WALK);
		const int target=detail.level[ability]<training.targetLevel
			? race->getUnitType(core.typeNum,training.targetLevel)->performance[ability] : performance(ability);
		const auto gate=ability==WALK?UnitRuntimeTraits::Walk:ability==SWIM?UnitRuntimeTraits::Swim:UnitRuntimeTraits::Fly;
		const unsigned after=(UnitTraining::movementModes(*this)&~bit)|(target>0 && hasCapability(gate)?bit:0u);
		if(!after)return false;
	}
	applyTrainingUnchecked(training,ability);
	return true;
}

void Unit::applyTrainingUnchecked(const BuildingTrainingSpec& training, int ability)
{
	if (detail.level[ability] < training.targetLevel)
	{
		detail.level[ability] = training.targetLevel;
		performance(ability) = race->getUnitType(core.typeNum, training.targetLevel)->performance[ability];
		if (ability == HP) performance(ability) = std::max(1, performance(ability) / owner->game->gameHeader.getGlassCannonScale());
	}
	if (hasCapability(UnitRuntimeTraits::LearnConstruction)) core.constructionLevel = std::max(core.constructionLevel, training.constructionLevel);
	refreshEffectiveAbilities();
}

void Unit::recordLethalDamage(int damage, int cause)
{
	if (core.hp >= UNIT_HP_DEATH_THRESHOLD && Sint64(core.hp) - damage < UNIT_HP_DEATH_THRESHOLD)
		diagnosticDeathCause = cause;
}

unsigned Unit::carriedPacketCount() const
{
	if (!hasCapability(UnitRuntimeTraits::ExtendedCargo)) return core.carriedMaterial>=0;
	const auto* extra=owner->game->unitCargo.find(core.gid);
	return (core.carriedMaterial>=0)+(extra?extra->size():0)-(detail.widePrimaryCargo?1:0);
}

bool Unit::hasCarriedMaterial(int material) const
{
	if (core.carriedMaterial==material) return true;
	if (!hasCapability(UnitRuntimeTraits::ExtendedCargo)) return false;
	const auto* extra=owner->game->unitCargo.find(core.gid);
	if (extra) for (const auto& entry:*extra) if (entry.material==material) return true;
	return false;
}

bool Unit::hasDeliverableCargo(const Building& building, int material) const
{
	if (building.materialDeliveryNeed(material)<=0 || building.materials[material]==std::numeric_limits<Sint32>::max()) return false;
	const Uint64 multiplier=building.type->materialMultiplier[material];
	const auto usable=[&](WideMaterialPacket packet) {
		const Uint64 divisor=std::gcd(packet.numerator,packet.denominator);
		packet.numerator/=divisor; packet.denominator/=divisor;
		const Uint64 common=std::gcd(packet.denominator,multiplier);
		const Uint64 scale=multiplier/common;
		const Uint64 maximum=std::numeric_limits<Uint64>::max();
		return packet.numerator<=maximum/scale && packet.denominator<=maximum/scale
			&& packet.numerator*scale>=packet.denominator/common;
	};
	if (core.carriedMaterial==material && !detail.widePrimaryCargo && usable({detail.carriedPacket.numerator,detail.carriedPacket.denominator})) return true;
	if (const auto* extra=owner->game->unitCargo.find(core.gid))
		for (const auto& entry:*extra) if (entry.material==material && usable(entry.packet)) return true;
	return false;
}

bool Unit::canCarryMaterial(int material) const
{
	const auto& traits=runtimeTraits();
	if (material<0 || material>=int(MaterialCount) || traits.cargoKinds<=0
        || carriedPacketCount()>=unsigned(traits.cargoCapacity)) return false;
	if (hasCarriedMaterial(material) || core.carriedMaterial<0) return true;
	unsigned kinds=1;
	MaterialMask mask=MaterialMask(1u<<core.carriedMaterial);
	if (const auto* extra=owner->game->unitCargo.find(core.gid))
		for (const auto& entry:*extra) if (!(mask&(1u<<entry.material))) { mask|=1u<<entry.material; ++kinds; }
	return kinds<unsigned(traits.cargoKinds);
}

void Unit::clearCargo()
{
	core.carriedMaterial=UNIT_CARRIED_RESOURCE_NONE;
	detail.carriedPacket={};
	detail.widePrimaryCargo=false;
	owner->game->unitCargo.erase(core.gid);
}

void Unit::receiveCarriedMaterial(int resource, MaterialPacket packet)
{
	if (!hasCapability(UnitRuntimeTraits::ExtendedCargo)) {
		if (core.carriedMaterial>=0) ++owner->stats.measurements.materialSpillageEvents;
		if (detail.widePrimaryCargo) clearCargo();
		core.carriedMaterial=resource; detail.carriedPacket=packet; return;
	}
	receiveCargoPacket(resource,{packet.numerator,packet.denominator});
}

void Unit::receiveCargoPacket(int resource, WideMaterialPacket packet)
{
	if (!packet.numerator || !packet.denominator || packet.numerator>packet.denominator || !canCarryMaterial(resource))
		throw std::runtime_error("Invalid unit cargo packet or capacity exceeded");
	const Uint64 divisor=std::gcd(packet.numerator,packet.denominator);
	packet.numerator/=divisor; packet.denominator/=divisor;
	if (core.carriedMaterial<0) {
		core.carriedMaterial=resource;
		if (packet.denominator<=1000000 && packet.numerator<=std::numeric_limits<Uint32>::max())
			detail.carriedPacket={Uint32(packet.numerator),Uint32(packet.denominator)};
		else { detail.widePrimaryCargo=true; detail.carriedPacket={}; owner->game->unitCargo.overflow(core.gid).push_back({resource,packet}); }
	} else owner->game->unitCargo.overflow(core.gid).push_back({resource,packet});
}

bool Unit::deliverCargo(Building& building)
{
	// Keep the one-packet path identical, including the building's established
	// fractional packet settlement and discard accounting.
	if (!hasCapability(UnitRuntimeTraits::ExtendedCargo) && !detail.widePrimaryCargo) {
		if (core.carriedMaterial<0 || building.materialDeliveryNeed(core.carriedMaterial)<=0) return false;
		building.deliverMaterialPacket(core.carriedMaterial,detail.carriedPacket);
		core.carriedMaterial=UNIT_CARRIED_RESOURCE_NONE; detail.carriedPacket={}; return true;
	}
	UnitCargoStore::Inventory retained;
	bool delivered=false;
	const auto offer=[&](UnitCargoEntry entry) {
		if (!delivered && building.materialDeliveryNeed(entry.material)>0) {
			const auto result=building.deliverCargoPacket(entry.material,entry.packet);
			delivered |= result.acceptedStock>0;
			if (result.residual.numerator) retained.push_back({entry.material,result.residual});
		} else retained.push_back(entry);
	};
	if (core.carriedMaterial>=0 && !detail.widePrimaryCargo) offer({core.carriedMaterial,{detail.carriedPacket.numerator,detail.carriedPacket.denominator}});
	if (const auto* extra=owner->game->unitCargo.find(core.gid)) for (const auto& entry:*extra) offer(entry);
	clearCargo();
	for (const auto& entry:retained) receiveCargoPacket(entry.material,entry.packet);
	return delivered;
}

// Batching is optional and is entirely skipped by the shipped one-packet units.
// Select once per completed pickup, in material-ID order for deterministic ties.
bool Unit::continueExtendedCargoCollection()
{
	if (!performance(HARVEST) || runtimeTraits().cargoCapacity<=1 || !attachedBuilding || carriedPacketCount()>=unsigned(runtimeTraits().cargoCapacity)) return false;
	// The completed harvest marked a ground carrier immobile. Route planning
	// runs before handleAction clears that mark, so a newly seeded field would
	// otherwise reject the carrier's own starting tile. The later clear is a no-op.
	if (!performance(FLY)) owner->map->clearImmobileUnit(core.posX,core.posY);
	int wished[MaterialSlotCount]; attachedBuilding->computeWishedMaterials(wished);
	const auto subtract=[&](int material,WideMaterialPacket packet) {
		const Uint64 multiplier=attachedBuilding->type->materialMultiplier[material];
		const int stock=packet.numerator>std::numeric_limits<Uint64>::max()/std::max(Uint64(1),multiplier)?1:int(packet.numerator*multiplier/packet.denominator);
		wished[material]=int(std::max<Sint64>(0,Sint64(wished[material])-std::max(1,stock)));
	};
	if (core.carriedMaterial>=0 && !detail.widePrimaryCargo) subtract(core.carriedMaterial,{detail.carriedPacket.numerator,detail.carriedPacket.denominator});
	if (const auto* extra=owner->game->unitCargo.find(core.gid)) for (const auto& entry:*extra) subtract(entry.material,entry.packet);
	int selected=-1, best=INT_MAX;
	for (int material=0;material<MaterialCount;++material) {
		if (wished[material]<=0 || !canCarryMaterial(material)) continue;
		int distance;
		if (!findMaterialDestination(material,nullptr,nullptr,&distance,attachedBuilding->fetchesFromMarkets(),attachedBuilding)) continue;
		if (distance>=foodStepsLeft(detail.trigHungry)/2) continue;
		const int score=int(std::min(Sint64(INT_MAX),Sint64(distance)*256/wished[material]));
		if (score<best) { selected=material; best=score; }
	}
	if (selected<0) return false;
	int distance;
	if (!findMaterialDestination(selected,&core.targetX,&core.targetY,&distance,attachedBuilding->fetchesFromMarkets(),attachedBuilding)) return false;
	core.destinationPurpose=selected; setTargetBuilding(nullptr);
	core.displacement=DIS_GOING_TO_RESOURCE; core.validTarget=true;
	return true;
}

bool Unit::findMaterialDestination(int material,Sint32* x,Sint32* y,int* distance,bool withMarkets,const Building* consumer)
{
	Map& map=*owner->map;
	if (!performance(FLY)) {
		if (!x || !y) return distance ? map.materialAvailableSlot(owner->teamNumber,material,swimClass(),core.posX,core.posY,distance,withMarkets,consumer)
			: map.materialAvailableSlot(owner->teamNumber,material,swimClass(),core.posX,core.posY,withMarkets,consumer);
		return map.materialAvailableUpdateSlot(owner->teamNumber,material,swimClass(),core.posX,core.posY,x,y,distance,withMarkets,consumer);
	}
	field::AirDistanceField air(map.getW(),map.getH(),core.posX,core.posY,
		[&](int px,int py) { return map.terrainPropertiesAt(px,py).flyable; },
		[&](int px,int py) { const auto& terrain=map.terrainPropertiesAt(px,py); return gradient_kernel::scaledTerrainStep(GRADIENT_STEP,terrain.airSpeedQ8); }, map.hasAirTerrainConstraints());
	unsigned best=UINT_MAX; int bestX=0,bestY=0;
	const unsigned modes=consumer?map.materialSupplyModesSlot(consumer,material):1;
	for (int py=0;py<map.getH();++py) for (int px=0;px<map.getW();++px) {
		if (map.isForbidden(px,py,owner->me)) continue;
		const Resource resource=map.getResource(px,py);
		bool source=map.isMaterialTakeableSlot(px,py,material)
			&& (!map.resourcePropertiesByIndex(resource.type).visibleToHarvest
				|| map.isFOWDiscovered(px,py,owner->me));
		if (!source && withMarkets) {
			const Uint16 building=map.getBuilding(px,py);
			if (building!=NOGBID && Building::GIDtoTeam(building)==owner->teamNumber)
				source=map.stockSupplierEligibleSlot(owner->myBuildings[Building::GIDtoID(building)],consumer,material,modes);
		}
		if (!source) continue;
		const unsigned cost=air.enabled()?air.costTo(px,py):unsigned(map.warpDistMax(core.posX,core.posY,px,py))*GRADIENT_STEP;
		if (cost<best) { best=cost; bestX=px; bestY=py; }
	}
	if (best==UINT_MAX) return false;
	if (x) *x=bestX; if (y) *y=bestY;
	if (distance) *distance=(best+GRADIENT_STEP-1)/GRADIENT_STEP;
	return true;
}

// Air clearing has no ground gradient. This cold capability combination scans
// goals once per completed action and shares the constrained air-distance field.
bool Unit::findAirClearingDestination(const Building* zone,Sint32* x,Sint32* y,int* distance)
{
	Map& map=*owner->map;
	field::AirDistanceField air(map.getW(),map.getH(),core.posX,core.posY,
		[&](int px,int py) { return map.terrainPropertiesAt(px,py).flyable; },
		[&](int px,int py) { const auto& terrain=map.terrainPropertiesAt(px,py); return gradient_kernel::scaledTerrainStep(GRADIENT_STEP,terrain.airSpeedQ8); }, map.hasAirTerrainConstraints());
	unsigned best=UINT_MAX; int bestX=0,bestY=0;
	const bool farms=map.farmAreasEnabled();
	for (int py=0;py<map.getH();++py) for (int px=0;px<map.getW();++px) {
		if (map.isForbidden(px,py,owner->me)) continue;
		if (zone) {
			if (map.warpDistSquare(px,py,zone->posX,zone->posY)>zone->unitStayRange*zone->unitStayRange
				|| !map.isClearableResourceForMaterials(px,py,zone->clearingMaterials)) continue;
		} else if (!map.isClearingTarget(map.coordToIndex(px,py),owner->me,farms)) continue;
		const unsigned cost=air.enabled()?air.costTo(px,py):unsigned(map.warpDistMax(core.posX,core.posY,px,py))*GRADIENT_STEP;
		if (!zone) {
			const int claimant=map.isClearingAreaClaimed(px,py,owner->teamNumber);
			if (claimant!=NOGUID && claimant!=core.gid) {
				const Unit* other=owner->myUnits[GIDtoID(claimant)];
				if (other && Uint64(other->previousClearingAreaDistance)*GRADIENT_STEP<=cost) continue;
			}
		}
		if (cost<best) { best=cost; bestX=px; bestY=py; }
	}
	if (best==UINT_MAX) return false;
	*x=bestX; *y=bestY; *distance=(best+GRADIENT_STEP-1)/GRADIENT_STEP; return true;
}

bool Unit::findAirGuardDestination(Sint32* x,Sint32* y)
{
	Map& map=*owner->map;
	field::AirDistanceField air(map.getW(),map.getH(),core.posX,core.posY,
		[&](int px,int py) { return map.terrainPropertiesAt(px,py).flyable; },
		[&](int px,int py) { return gradient_kernel::scaledTerrainStep(GRADIENT_STEP,map.terrainPropertiesAt(px,py).airSpeedQ8); },map.hasAirTerrainConstraints());
	unsigned best=UINT_MAX;
	for (int py=0;py<map.getH();++py) for (int px=0;px<map.getW();++px) {
		if (!map.isGuardArea(px,py,owner->me) || map.isForbidden(px,py,owner->me) || !map.terrainPropertiesAt(px,py).flyable) continue;
		const unsigned cost=air.enabled()?air.costTo(px,py):unsigned(map.warpDistMax(core.posX,core.posY,px,py))*GRADIENT_STEP;
		if (cost<best) { best=cost; *x=px; *y=py; }
	}
	return best!=UINT_MAX;
}
