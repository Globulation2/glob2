// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include "map/claims/ResourceClaimProbe.h"
#include <algorithm>
#include <climits>
#include "Unit.h"
#include "UnitTiming.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "Utilities.h"
#include "GlobalContainer.h"
#include "render/GameAnimations.h"
#include "render/UnitEffects.h"

namespace {
std::optional<Offset> materialPickup(Unit& unit)
{
    auto& map=*unit.owner->map;
    if(!map.activeClaimsEnabled())return map.doesUnitTouchMaterialSource(&unit,static_cast<MaterialId>(unit.destinationPurpose));
    // Contention-triggered claims: a worker without a valid claim picks up
    // exactly as with claims off; its harvest then uses takeHarvest too.
    if(map.claimContentionEnabled()) {
        unit.releaseInvalidActiveMaterialClaim();
        if(!unit.activeMaterialClaim)return map.doesUnitTouchMaterialSource(&unit,static_cast<MaterialId>(unit.destinationPurpose));
    }
    const auto* observer=map.resourceClaimProbe();
    if(observer && !observer->observesDecisionOutcomes())observer=nullptr;
    std::optional<ResourceClaims::Probe::DecisionOutcome> outcome;
    unit.releaseInvalidActiveMaterialClaim();
    if(observer){outcome=observer->beginDecisionOutcome(unit,true);outcome->noAttempt="no_adjacent_candidate";}
    bool observed=false;
    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
        if(map.isMaterialTakeableSlot(unit.posX+dx,unit.posY+dy,unit.destinationPurpose)
            && !map.isForbidden(unit.posX+dx,unit.posY+dy,unit.owner->me)) {
            if(!unit.activeMaterialClaim && !observed) {
                const auto token=map.observeMaterialClaimPickupDecision(unit,unit.posX+dx,unit.posY+dy);observed=true;
                if(observer){outcome=observer->beginDecisionOutcome(unit,true);outcome->token=token;}
            }
            if(observer)outcome->attempted=true;
            if(unit.prepareActiveMaterialPickup(unit.posX+dx,unit.posY+dy)) {
                if(observer){outcome->selected=true;observer->selectedDecisionOutcome(*outcome,unit);observer->finishDecisionOutcome(*outcome,unit);}
                return Offset{dx,dy};
            }
        }
    if(observer){observer->selectedDecisionOutcome(*outcome,unit);observer->finishDecisionOutcome(*outcome,unit);}
    return std::nullopt;
}

// Crowding eligibility changes independently of tile movement when a warrior
// enters/leaves a building or finishes a capability upgrade. Stack-only and
// owner-thread; this records a conservative event, not a weighted seed change.
struct GuardCostEligibility {
    Unit& unit;
    bool before;
    static bool eligible(const Unit& unit) {
        return !unit.isDead && unit.displacement!=Unit::DIS_INSIDE && unit.hasCapability(UnitRuntimeTraits::GuardIdle);
    }
    explicit GuardCostEligibility(Unit& value):unit(value),before(eligible(value)) {}
    ~GuardCostEligibility() {if(before!=eligible(unit))unit.owner->map->gradientGuardCostsChanged(unit.owner->teamNumber);}
};
}

void Unit::handleDisplacement(void)
{
    const MaterialClaimObservation claimObservation{*this};
    const GuardCostEligibility guardCostEligibility(*this);
	switch (activity)
	{
		case ACT_RANDOM:
		{
			if ((medical==MED_FREE)&&((displacement==DIS_RANDOM)||(displacement==DIS_REMOVING_BLACK_AROUND)||(displacement==DIS_ATTACKING_AROUND)))
			{
				if (hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies) ? performance[FLY]>0 : hasCapability(UnitRuntimeTraits::ExploreIdle))
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies) ? performance[ATTACK_SPEED]>0 : hasCapability(UnitRuntimeTraits::GuardIdle))
					displacement=DIS_ATTACKING_AROUND;
				else if (!hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies))
					displacement=DIS_RANDOM;
			}
			else
				displacement=DIS_RANDOM;
			setValidTarget(false);
		}
		break;

		case ACT_FILLING:
		{
			assert(attachedBuilding);
			assert(displacement!=DIS_RANDOM);

			if (verbose)
				printf("guid=(%d) handleDisplacement() ACT_FILLING, displacement=%d\n", gid, displacement);

			if (displacement==DIS_GOING_TO_RESOURCE)
			{
				if (auto off = materialPickup(*this))
				{
                    if(!owner->map->activeClaimsEnabled())owner->map->observeMaterialClaimPickupDecision(*this,posX+off->dx,posY+off->dy);
					dx = off->dx;
					dy = off->dy;
                    // During harvesting these coordinates identify the touched
                    // pickup tile, which can differ from the projected goal.
                    targetX=(posX+dx)&(owner->map->getW()-1);
                    targetY=(posY+dy)&(owner->map->getH()-1);
					displacement=DIS_HARVESTING;
					setValidTarget(false);
				}
				else if ((attachedBuilding->runtime->fetchesStockMask | attachedBuilding->runtime->fetchesDirectStockMask)&(1u<<destinationPurpose))
				{
					// The gradient led here to a stocked market of ours: take the
					// resource at its door and carry it home.
					if (Building *market = owner->map->touchedStockedMarketSlot(this, destinationPurpose))
					{
						if (hasCapability(UnitRuntimeTraits::ExtendedCargo) && !canCarryMaterial(destinationPurpose)) { stopAttachedForBuilding(false); break; }
						receiveCarriedMaterial(destinationPurpose,market->withdrawMaterialPacket(destinationPurpose));
						if (continueCargoCollection()) break;
						setTargetBuilding(attachedBuilding);
						displacement=DIS_GOING_TO_BUILDING;
						setValidTarget(true);
						if (verbose)
							printf("guid=(%d) took material (%d) out of market gbid=(%d)\n", gid, destinationPurpose, market->gid);
					}
				}
			}
			else if (displacement==DIS_HARVESTING)
			{
				// Inside a farm area (farm-areas experiment) the grain comes off
				// the ripest tile of the connected field rather than off the tile
				// under the animation, and an exhausted field hands out nothing.
				// Outside pooled fields, harvest the touched deposit directly.
				if (hasCapability(UnitRuntimeTraits::ExtendedCargo) && !canCarryMaterial(destinationPurpose)) {
					// This pickup never consumed its source. Do not let the old
					// harvest animation become an idle clearing completion.
					movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
					stopAttachedForBuilding(false); break;
				}
				const bool gotResource = usesMaterialClaimPickup()?takeActiveMaterialClaim():owner->map->takeHarvest(posX, posY, dx, dy,
					static_cast<MaterialId>(destinationPurpose), owner->me);
				assert(movement == MOV_HARVESTING);
				movement = MOV_RANDOM_GROUND; // we do this to avoid the handleMovement() to additionally decResource() the same resource.

				if (!gotResource)
				{
					// The field has nothing above its seed. Go back to looking
					// rather than walking home empty-handed: the touch test in
					// DIS_GOING_TO_RESOURCE needs a tile that really holds the
					// resource, so this either harvests again next step or
					// resumes the gradient walk.
					displacement=DIS_GOING_TO_RESOURCE;
					setValidTarget(false);
				}
				else
				{
					// we got the resource.
					receiveCarriedMaterial(destinationPurpose,{});
					++owner->stats.events().harvested[destinationPurpose];

					if (continueCargoCollection()) break;

					setTargetBuilding(attachedBuilding);
					if (auto off = owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid))
					{
						dx = off->dx;
						dy = off->dy;
						displacement=DIS_FILLING_BUILDING;
						setValidTarget(false);
					}
					else
					{
						displacement=DIS_GOING_TO_BUILDING;
						targetX=targetBuilding->getMidX();
						targetY=targetBuilding->getMidY();
						setValidTarget(true);
					}
				}
			}
			else if (displacement==DIS_GOING_TO_BUILDING)
			{
				assert(targetBuilding);
				if (auto off = owner->map->doesUnitTouchBuilding(this, targetBuilding->gid))
				{
					dx = off->dx;
					dy = off->dy;
					displacement=DIS_FILLING_BUILDING;
					setValidTarget(false);
				}
			}
			else if (displacement==DIS_FILLING_BUILDING)
			{
				bool loopMove=false;
				bool exchangeReady=false;
				bool cargoDeliveryProgress=false;
				assert(targetBuilding);
				if (targetBuilding==ownExchangeBuilding)
				{
					assert(targetBuilding);
					assert(ownExchangeBuilding);
					if (!(targetBuilding->runtime->suppliesDirectStockMask&(1u<<destinationPurpose)) || !(attachedBuilding->runtime->fetchesDirectStockMask&(1u<<destinationPurpose))
						|| targetBuilding->buildingState != Building::ALIVE || targetBuilding->sharesMaterialPool(*attachedBuilding))
					{
						stopAttachedForBuilding(false);
						break;
					}
					assert(targetBuilding->type->runtimeSuppliesDirectStock);
					assert(ownExchangeBuilding->type->runtimeSuppliesDirectStock);
					assert(owner==targetBuilding->owner);
					assert(owner==ownExchangeBuilding->owner);

					assert(attachedBuilding);
					assert(attachedBuilding->type->runtimeFetchesDirectStock);
					assert(destinationPurpose>=0 && destinationPurpose<MaterialCount);

					// Let's grab the right resource.

					if (targetBuilding->availableMaterial(destinationPurpose)>0)
					{
						if (hasCapability(UnitRuntimeTraits::ExtendedCargo) && !canCarryMaterial(destinationPurpose)) { stopAttachedForBuilding(false); break; }
						receiveCarriedMaterial(destinationPurpose,targetBuilding->withdrawMaterialPacket(destinationPurpose));
						if (continueCargoCollection()) break;

						setTargetBuilding(attachedBuilding);
						displacement=DIS_GOING_TO_BUILDING;
						targetX=targetBuilding->getMidX();
						targetY=targetBuilding->getMidY();
						setValidTarget(true);
						exchangeReady=true;
						if (verbose)
							printf("guid=(%d) took a foreign fruit in our exhange building to food\n", gid);
					}
				}
				else if (carriedMaterial>=0 && (hasCapability(UnitRuntimeTraits::ExtendedCargo) || targetBuilding->materialDeliveryNeed(carriedMaterial)>0))
				{
					if (verbose)
						printf("guid=(%d) Giving material (%d) to building gbid=(%d) old-amount=(%d)\n", gid, destinationPurpose, targetBuilding->gid, targetBuilding->material(carriedMaterial));
					cargoDeliveryProgress=deliverCargo(*targetBuilding);
				}

				if (!loopMove && !exchangeReady)
				{
					//NOTE: if attachedBuilding has become NULL; it's because the building doesn't need me anymore.
					if (!attachedBuilding)
					{
						if (verbose)
							printf("guid=(%d) The building doesn't need me any more.\n", gid);
						jobPurpose=UnitJobPurpose::None;
						activity=ACT_RANDOM;
						displacement=DIS_RANDOM;
						setValidTarget(false);
						assert(needToRecheckMedical());
					}
					else
					{
						///Find a resource that the building wants and a location to get it from
						///The location may be a market, or the harvesting the resource from the
						///map.

						if (hasCapability(UnitRuntimeTraits::ExtendedCargo) && carriedMaterial>=0) {
							bool useful=false;
							for (int material=0;material<MaterialCount;++material)
								if (hasCarriedMaterial(material) && attachedBuilding->materialDeliveryNeed(material)>0) { useful=true; destinationPurpose=material; break; }
							if (useful && cargoDeliveryProgress) { displacement=DIS_FILLING_BUILDING; break; }
							if (carriedPacketCount()>=unsigned(runtimeTraits().cargoCapacity)) { stopAttachedForBuilding(false); break; }
						}
						// Reconsider inventory-driven staffing only after the load has finished
						// unloading, before committing this worker to another delivery trip.
						if (attachedBuilding->finishesDeliveryTrips() && attachedBuilding->unitsWorking.size()>
							unsigned(std::max(0,attachedBuilding->desiredMaxUnitWorking))) {
							stopAttachedForBuilding(false); break;
						}
						if (!performance[HARVEST]) { stopAttachedForBuilding(false); break; }
						int needs[MaterialSlotCount];
						attachedBuilding->computeWishedMaterials(needs);
						int teamNumber=owner->teamNumber;
						int timeLeft = numberOfStepsLeftUntilHungry();
						if (timeLeft > 0)
						{
							int bestResource=-1, pendingResource=-1;
							int minValue=owner->map->getW()+owner->map->getW();

							Map* map=owner->map;
							for (int r=0; r<MaterialCount; ++r)
							{
								const int need=needs[r];
								if (need<=0 || (hasCapability(UnitRuntimeTraits::ExtendedCargo) && !canCarryMaterial(r))) continue;
								int distance;
								const auto route=performance[FLY]
									? queryMaterialDestination(r,nullptr,nullptr,&distance,false,attachedBuilding)
									: map->queryMaterialDistance(teamNumber,r,movementAccess(),posX,posY,&distance,false,attachedBuilding);
								if (route==RouteResult::Pending) { if (pendingResource<0) pendingResource=r; continue; }
								if (route!=RouteResult::Reachable || (distance<<1)>=timeLeft) continue;

								const int value=distance/need;
								if (value<minValue) { bestResource=r; minValue=value; }
							}

							if (verbose)
								printf("guid=(%d) bestResource=%d, minValue=%d\n", gid, bestResource, minValue);

							if (bestResource<0 && pendingResource>=0) {
								destinationPurpose=pendingResource;
								setTargetBuilding(nullptr);
								displacement=DIS_GOING_TO_RESOURCE;
								setValidTarget(false);
								break;
							}
							if (bestResource>=0)
							{
								destinationPurpose=bestResource;
								assert(activity==ACT_FILLING);
								{
									int dummyDist;
									if (auto off = materialPickup(*this))
									{
                                        if(!owner->map->activeClaimsEnabled())owner->map->observeMaterialClaimPickupDecision(*this,posX+off->dx,posY+off->dy);
										dx = off->dx;
										dy = off->dy;
                                        targetX=(posX+dx)&(owner->map->getW()-1);
                                        targetY=(posY+dy)&(owner->map->getH()-1);
										displacement=DIS_HARVESTING;
										setValidTarget(false);
									}
                                    else {
                                        const auto route=queryMaterialDestination(destinationPurpose,&targetX,&targetY,&dummyDist,attachedBuilding->fetchesFromMarkets(),attachedBuilding);
                                        if (route==RouteResult::Pending) {
                                            displacement=DIS_GOING_TO_RESOURCE; setValidTarget(false);
                                        } else if (route==RouteResult::Reachable) {
                                            displacement=DIS_GOING_TO_RESOURCE; setValidTarget(true);
                                        } else stopAttachedForBuilding(false);
                                    }
								}
							}
							else
							{
								if (verbose)
									printf("guid=(%d) can't find any requested material, unsubscribing.\n", gid);
								stopAttachedForBuilding(false);
							}
						}
						else
						{
							if (verbose)
								printf("guid=(%d) not enough time for anything, unsubscribing.\n", gid);
							stopAttachedForBuilding(false);
						}
					}
				}
			}
			else
			{
				displacement=DIS_RANDOM;
				setValidTarget(false);
			}
		}
		break;

		case ACT_UPGRADING:
		{
			assert(attachedBuilding);

			if (displacement==DIS_GOING_TO_BUILDING)
			{
				if (auto off = owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid))
				{
					dx = off->dx;
					dy = off->dy;
					displacement=DIS_ENTERING_BUILDING;
					setValidTarget(false);
				}
			}
			else if (displacement==DIS_ENTERING_BUILDING)
			{
				// The unit has already its room in the building,
				// then we are sure that the unit can enter.

				if (performance[FLY])
					owner->map->setAirUnit(posX-dx, posY-dy, NOGUID);
				else
					owner->map->setGroundUnit(posX-dx, posY-dy, NOGUID);
				displacement=DIS_INSIDE;
				setValidTarget(false);

				// Loading can cancel a visit whose stock was normalized below
				// its cost. Finish removing the entry occupancy, then leave
				// without starting its timer or granting any service benefit.
				if (!serviceResourcesReserved())
				{
					insideTimeout=0;
					displacement=DIS_EXITING_BUILDING;
					setNeedToRecheckMedical(true);
					break;
				}

				if (destinationPurpose==FEED)
				{
					insideTimeout=-attachedBuilding->type->semantics.feeding.duration;
					speed=std::max(1,int(Sint64(attachedBuilding->type->insideSpeed)*runtimeTraits().feedingSpeedQ8/256));
				}
				else if (destinationPurpose==HEAL)
				{
					//insideTimeout=-(attachedBuilding->type->semantics.healing.duration*(performance[HP]-hp))/performance[HP];
					insideTimeout=-attachedBuilding->type->semantics.healing.duration;
					const Sint64 baseSpeed=(Sint64(attachedBuilding->type->insideSpeed)*performance[HP])
						/std::max<Sint64>(1, Sint64(performance[HP])-hp);
					speed=int(std::clamp<Sint64>(baseSpeed*runtimeTraits().healingSpeedQ8/256,1,INT_MAX));
				}
				else
				{
					const auto& spec=attachedBuilding->type->semantics;
					const auto trainingSpeed=[&](int ability) {
						return std::max(1,int(Sint64(std::max(1, attachedBuilding->type->insideSpeed/
							std::max(1, spec.training[ability].targetLevel-level[ability])))*runtimeTraits().trainingSpeedQ8/256));
					};
					int duration=spec.training[destinationPurpose].duration;
					speed=trainingSpeed(destinationPurpose);
					const auto remainingTicks=[&](int courseDuration,int courseSpeed) {
						const int advance=unitActionStepSpeed(courseSpeed,action,dx,dy,true);
						const Sint64 remaining=(Sint64(courseDuration)+1)*UNIT_DELTA_QUANTUM-delta;
						// Legacy saves may carry surplus phase, but syncStep still
						// completes at most one inside action per tick.
						return std::max(Sint64(courseDuration)+1,(remaining+advance-1)/advance);
					};
					Sint64 slowestTicks=remainingTicks(duration,speed);
					// Entry phase and integer delta advances affect the final action.
					// Keep the first course when their actual remaining ticks tie.
					if (spec.trainingInParallel)
						for (int ability=WALK; ability<NB_ABILITY; ++ability)
							if (needsTraining(spec.training[ability],ability))
							{
								const int candidateSpeed=trainingSpeed(ability);
								const Sint64 candidateTicks=remainingTicks(spec.training[ability].duration,candidateSpeed);
								if (candidateTicks>slowestTicks)
								{
									duration=spec.training[ability].duration;
									speed=candidateSpeed;
									slowestTicks=candidateTicks;
								}
							}
					insideTimeout=-duration;
				}
			}
			else if (displacement==DIS_INSIDE)
			{
				// Feed and heal visits share this displacement state with training.
				// A saved trainee must use the normal exit path to release its room,
				// without waiting for an upgrade timer or recording a training gain.
				if (insideTimeout>=0 || (owner->game->gameHeader.isUnitUpgradesDisabled()
					&& destinationPurpose!=FEED && destinationPurpose!=HEAL))
				{
					displacement=DIS_EXITING_BUILDING;
					setValidTarget(false);
					// A restored or reconfigured visit may now offer an unsafe
					// course. Release its original reservation before changing any
					// levels, then exit using the existing movement clocks.
					const bool trainingVisit=destinationPurpose!=FEED && destinationPurpose!=HEAL;
					const auto courses=trainingVisit && !owner->game->gameHeader.isUnitUpgradesDisabled()
						? trainingVisitCourses(*attachedBuilding,destinationPurpose) : std::optional<Uint32>{};
					if (trainingVisit && !owner->game->gameHeader.isUnitUpgradesDisabled() && courses)
						++owner->stats.events().trainingVisits[typeNum];

					if (destinationPurpose==FEED)
					{
						hungry=foodCapacity();
						if (owner->game->areaEffects.enabled()) areaServiceRemainders[BuildingAreaEffects::Feeding]=0;
						fruitCount=attachedBuilding->eatOnce(&fruitMask, this);
						setNeedToRecheckMedical(true);
						owner->game->animations->onUnitServiced(gid, Uint8(UnitEffect::Kind::Feed),
							attachedBuilding->gid, attachedBuilding->typeNum, 0);
					}
					else if (destinationPurpose==HEAL)
					{
						attachedBuilding->settleService(this);
						++attachedBuilding->owner->stats.events().healingVisits;
						attachedBuilding->owner->stats.events().hpRestored +=
							std::max<Sint64>(0, Sint64(performance[HP]) - hp);
						hp=performance[HP];
						if (owner->game->areaEffects.enabled()) areaServiceRemainders[BuildingAreaEffects::Healing]=0;
						setNeedToRecheckMedical(true);
						owner->game->animations->onUnitServiced(gid, Uint8(UnitEffect::Kind::Heal),
							attachedBuilding->gid, attachedBuilding->typeNum, 0);
					}
					// Restored units already inside exit safely without gaining a level.
					// New training visits are rejected by Team::findBestUpgrade.
					else if (!owner->game->gameHeader.isUnitUpgradesDisabled())
					{
						if(!courses) { attachedBuilding->releaseService(this); setNeedToRecheckMedical(true); break; }
						attachedBuilding->settleService(this);
						Sint32 previousLevels[NB_ABILITY];
						std::copy(level, level + NB_ABILITY, previousLevels);
						if (attachedBuilding->type->semantics.trainingInParallel)
						{
							for (int ability = (int)WALK; ability < NB_ABILITY; ability++)
							{
								const auto& training = attachedBuilding->type->semantics.training[ability];
								// Migrated tables retain the historical reevaluation order.
								if (hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies)
									? needsTraining(training,ability) : ((*courses&(1u<<ability))!=0))
									applyTrainingUnchecked(training,ability);
							}
						}
						else
							applyTrainingUnchecked(attachedBuilding->type->semantics.training[destinationPurpose], destinationPurpose);

						// The exit effect floats the highest new level, displayed 1-based
						// except SWIM, which is stored that way.
						int shownLevel = 0;
						for (int a = 0; a < NB_ABILITY; ++a)
						{
							owner->stats.events().abilityGains[typeNum][a] +=
								std::max(0, level[a] - previousLevels[a]);
							if (level[a] > previousLevels[a])
								shownLevel = std::max(shownLevel, a == SWIM ? level[a] : 1 + level[a]);
						}
						if (shownLevel)
							owner->game->animations->onUnitServiced(gid, Uint8(UnitEffect::Kind::Train),
								attachedBuilding->gid, attachedBuilding->typeNum, Uint8(std::min(shownLevel, 255)));
					}
				}
				else
				{
					insideTimeout++;
				}
			}
			else if (displacement==DIS_EXITING_BUILDING)
			{
				// we want to get out, so we still stay in displacement==DIS_EXITING_BUILDING.
			}
			else
			{
				displacement=DIS_RANDOM;
				setValidTarget(false);
			}
		}
		break;

		case ACT_FLAG:
		{
			assert(attachedBuilding);
			displacement=DIS_GOING_TO_FLAG;
			targetX=attachedBuilding->posX;
			targetY=attachedBuilding->posY;
			setValidTarget(true);
			int distance=owner->map->warpDistSquare(targetX, targetY, posX, posY);
			int usr=attachedBuilding->unitStayRange;
			int usr2=usr*usr;
			if (verbose)
				printf("guid=(%d) ACT_FLAG distance=%d, usr2=%d\n", gid, distance, usr2);

			if (distance<=usr2)
			{
				setValidTarget(false);
				if (jobPurpose==UnitJobPurpose::Clear)
					displacement=DIS_CLEARING_RESOURCES;
				else if (jobPurpose==UnitJobPurpose::Explore)
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (jobPurpose==UnitJobPurpose::Defend)
					displacement=DIS_ATTACKING_AROUND;
				else
					assert(false);
			}
			else if (jobPurpose==UnitJobPurpose::Clear)
			{
				int usr2plus=1+(usr+1)*(usr+1);
				if (distance<=usr2plus)
				{
					Map *map=owner->map;
					for (int tdx=-1; tdx<=1; tdx++)
						for (int tdy=-1; tdy<=1; tdy++)
						{
							int x=posX+tdx;
							int y=posY+tdy;
							if (map->warpDistSquare(x, y, targetX, targetY)<=usr2
								&& map->isClearableResourceForMaterials(x, y, attachedBuilding->clearingMaterialMask))
							{
								dx=tdx;
								dy=tdy;
								setValidTarget(false);
								displacement=DIS_CLEARING_RESOURCES;
								//movement=MOV_HARVESTING;
								return;
							}
						}
				}
			}
		}
		break;

		default:
		{
			assert(false);
			break;
		}
	}
}

bool Unit::locationIsInEnemyGuardTowerRange(int x, int y)const
{
	//TODO: totally fix this totally hacky implementation.
	for(int i=0;i<Team::MAX_COUNT;i++)
	{
		Team *t = owner->game->teams[i];
		if((t)&&(owner->enemies & t->me))
		{
			// The live index contains every occupied slot in the original slot order.
			for(Building *b : t->liveBuildings.entries())
			{
				if((b)&&(b->runtime->shootingRange>0)&&(owner->map->warpDistMax(b->posX,b->posY,x,y) <= b->runtime->shootingRange + 1)
                    && b->hasClearShotTo(x,y)) return true;
			}
		}
	}
	return false;
}

// Training is all-or-nothing; a meal or a healing is granted pro rata, and
// a started meal costs a whole wheat.
void Unit::applyPartialInsideBenefit()
{
	if (displacement!=DIS_INSIDE)
		return;
	int total;
	if (destinationPurpose==FEED)
		total=attachedBuilding->type->semantics.feeding.duration;
	else if (destinationPurpose==HEAL)
		total=attachedBuilding->type->semantics.healing.duration;
	else
		return;
	const int elapsed=int(std::min<Sint64>(total, Sint64(total)+insideTimeout));
	if (total<=0 || elapsed<=0)
		return;
	if (destinationPurpose==FEED)
	{
		if (attachedBuilding->type->semantics.feeding.partial == BuildingPartialService::None) return;
		hungry=int(Sint64(hungry)+((Sint64(foodCapacity())-hungry)*elapsed)/total);
		if (hungry>=foodCapacity() && owner->game->areaEffects.enabled()) areaServiceRemainders[BuildingAreaEffects::Feeding]=0;
		fruitCount=attachedBuilding->eatOnce(&fruitMask, this);
	}
	else
	{
		if (attachedBuilding->type->semantics.healing.partial == BuildingPartialService::None) return;
		attachedBuilding->settleService(this);
		const Sint64 restored = ((Sint64(performance[HP]) - hp) * elapsed) / total;
		attachedBuilding->owner->stats.events().hpRestored += std::max<Sint64>(0, restored);
		hp=int(Sint64(hp)+restored);
		if (hp>=performance[HP] && owner->game->areaEffects.enabled()) areaServiceRemainders[BuildingAreaEffects::Healing]=0;
	}
}

void Unit::expelFromBuilding(int x, int y, int dx, int dy)
{
    const GuardCostEligibility guardCostEligibility(*this);
	// A unit already on its way out had its whole service; one expelled mid-stay
	// gets a partial benefit and no exit effect.
	const bool served = displacement==DIS_EXITING_BUILDING;
	applyPartialInsideBenefit();
	standardRandomActivity();
	insideTimeout=0;
	posX=x;
	posY=y;
	this->dx=dx;
	this->dy=dy;
	delta=0;
	movement=MOV_EXITING_BUILDING;
	if (served)
		owner->game->animations->onUnitLeftBuilding(*owner->map, gid, posX, posY, dx, dy, owner, UnitEffect::emergeTicks(speed));
	else
		owner->game->animations->onUnitGone(gid);
	handleActionExitingBuilding();
}
