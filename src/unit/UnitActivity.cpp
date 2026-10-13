// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "Utilities.h"

void Unit::handleActivity(void)
{
	// A pre-rule save may still have a unit walking to training. Cancel its
	// subscription before entering; feeding and healing use the same activity.
	if (owner->game->gameHeader.isUnitUpgradesDisabled() && core.activity==ACT_UPGRADING
		&& core.destinationPurpose!=FEED && core.destinationPurpose!=HEAL
		&& core.displacement==DIS_GOING_TO_BUILDING && attachedBuilding)
		stopAttachedForBuilding(true);
	// freeze unit health when inside a building
	if ((core.displacement==DIS_ENTERING_BUILDING) || (core.displacement==DIS_INSIDE)
            || ((core.displacement==DIS_EXITING_BUILDING)
                && ! (hasCapability(UnitRuntimeTraits::ServiceRebound) && (core.medical != MED_FREE))))
		return;

	if (verbose)
		printf("guid=(%d) handleActivity (medical=%d, activity=%d) (needToRecheckMedical=%d) (attachedBuilding=%p)...\n",
			core.gid, core.medical, core.activity, detail.needToRecheckMedical, attachedBuilding);

	if(core.activity!=ACT_RANDOM)
		detail.jobTimer=0;

	if (core.medical==MED_FREE)
	{
		handleMagic();

		if (core.activity==ACT_RANDOM)
		{
			// nothing to do:
			//Wait for 32 ticks before doing something else, to allow buildings time to hire units
			if(detail.jobTimer>32)
			{
				// We look for an upgrade
				Building* b=owner->findBestUpgrade(this);
				if (b)
				{
					assert(core.destinationPurpose>=WALK);
					assert(core.destinationPurpose<NB_ABILITY);
					core.activity=ACT_UPGRADING;
					attachedBuilding=b;
					setTargetBuilding(b);
					if (verbose)
						printf("guid=(%d) going to upgrade at dp=(%d), gbid=(%d)\n", core.gid, core.destinationPurpose, b->gid);
					b->subscribeUnitForInside(this);
					return;
				}

				// Seek healing when missing health exceeds the configured idle threshold.
				if (hasCapability(UnitRuntimeTraits::MedicalIdle) && core.hp+(Sint64(performance(HP))*runtimeTraits().idleHealMissingNumerator/runtimeTraits().idleHealMissingDenominator) < performance(HP))
				{
					Building *b;
					b=owner->findNearestHeal(this);
					if (b)
					{
						core.destinationPurpose=HEAL;
						core.activity=ACT_UPGRADING;
						attachedBuilding=b;
						setTargetBuilding(b);
						detail.needToRecheckMedical=false;
						if (verbose)
							printf("guid=(%d) Going to heal building\n", core.gid);
						core.targetX=attachedBuilding->getMidX();
						core.targetY=attachedBuilding->getMidY();
						core.validTarget=true;
						b->subscribeUnitForInside(this);
					}
					else
						core.activity=ACT_RANDOM;
				}
			}
		}
	}
	else if (detail.needToRecheckMedical)
	{
		// disconnect from building
		if (attachedBuilding)
		{
			if (verbose)
				printf("guid=(%d) Need medical while working, abort work\n", core.gid);
			attachedBuilding->removeUnitFromWorking(this);
			attachedBuilding->removeUnitFromInside(this);
			attachedBuilding=NULL;
			ownExchangeBuilding=NULL;
		}
		setTargetBuilding(NULL);
		// Medical service replaces the assignment even when no service is available.
		core.jobPurpose=UnitJobPurpose::None;

		if (core.medical==MED_HUNGRY)
		{
			Building *b;
			b=owner->findNearestFood(this);

			if (b!=NULL)
			{
				Team *currentTeam=owner;
				Team *targetTeam=b->owner;
				if (currentTeam != targetTeam && hasCapability(UnitRuntimeTraits::Convert))
				{
					// Unit conversion code

					// Send events and keep track of number of unit converted
					currentTeam->pushGameEvent(GameEvent::unitLostConversion(owner->game->stepCounter, core.posX, core.posY, targetTeam->teamNumber));
					currentTeam->unitConversionLost++;

					targetTeam->pushGameEvent(GameEvent::unitGainedConversion(owner->game->stepCounter, core.posX, core.posY, currentTeam->teamNumber));
					targetTeam->unitConversionGained++;

					// Find free slot in other team
					int targetID=UNIT_TARGETID_NONE;
					for (int i=0; i<Unit::MAX_COUNT; i++)//we search for a free place for a unit.
						if (targetTeam->myUnits[i]==NULL)
						{
							targetID=i;
							break;
						}

					// If free slot, do the conversion, change owner and ID
					if (targetID!=UNIT_TARGETID_NONE)
					{
						if (hasCapability(UnitRuntimeTraits::ReleaseClearingClaims) && previousClearingArea) {
							if (currentTeam->map->isClearingAreaClaimed(previousClearingArea->x,previousClearingArea->y,currentTeam->teamNumber)==core.gid)
								currentTeam->map->setClearingAreaUnclaimed(previousClearingArea->x,previousClearingArea->y,currentTeam->teamNumber);
							previousClearingArea.reset(); previousClearingAreaDistance=UNIT_CLEAR_AREA_DISTANCE_NONE;
						}
						const Uint16 targetGID = GIDfrom(targetID, targetTeam->teamNumber);
						const Uint32 identity = owner->game->allocateScriptIdentity(false, targetGID);
						++currentTeam->stats.measurements.conversionsOut[core.typeNum];
						++targetTeam->stats.measurements.conversionsIn[core.typeNum];
						Sint32 currentID=Unit::GIDtoID(core.gid);
						assert(currentTeam->myUnits[currentID]);
						currentTeam->myUnits[currentID]=NULL;
						currentTeam->detachUnit(currentID);
						targetTeam->myUnits[targetID]=this;
						targetTeam->attachUnit(targetID);

						if (verbose)
							printf("Unit guid=%d (%d) switched to guid=%d (%d)\n", core.gid, Unit::GIDtoTeam(core.gid), targetGID, Unit::GIDtoTeam(targetGID));
						if (performance(FLY))
						{
							assert(owner->map->getAirUnit(core.posX, core.posY)==core.gid);
							owner->map->setAirUnit(core.posX, core.posY, targetGID);
						}
						else
						{
							assert(owner->map->getGroundUnit(core.posX, core.posY)==core.gid);
							owner->map->setGroundUnit(core.posX, core.posY, targetGID);
						}
						// A client selection follows the unit across the conversion.
						owner->game->publishClientEvent(ClientEvent::UnitConverted{
							UnitRef{core.gid, core.scriptIdentity}, UnitRef{targetGID, identity}});
						core.scriptIdentity = identity;
						owner->game->unitCargo.transfer(core.gid,targetGID);
						core.gid=targetGID;
						owner=targetTeam;
						race=&targetTeam->race;
					}
				}

				core.destinationPurpose=FEED;
				core.activity=ACT_UPGRADING;
				attachedBuilding=b;
				setTargetBuilding(b);
				detail.needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to food at building gbid=(%d)\n", core.gid, b->gid);
				b->subscribeUnitForInside(this);
				owner->swapInn(this);
			}
			else
				core.activity=ACT_RANDOM;
		}
		else if (core.medical==MED_DAMAGED)
		{
			Building *b;
			b=owner->findNearestHeal(this);
			if (b!=NULL)
			{
				core.destinationPurpose=HEAL;
				core.activity=ACT_UPGRADING;
				attachedBuilding=b;
				setTargetBuilding(b);
				detail.needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to heal at building gbid=(%d)\n", core.gid, b->gid);
				b->subscribeUnitForInside(this);
			}
			else
				core.activity=ACT_RANDOM;
		}
		else
			assert(false);
	}
}
