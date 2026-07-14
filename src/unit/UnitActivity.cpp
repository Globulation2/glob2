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
        if ((displacement==DIS_EXITING_BUILDING)
            && (typeNum == EXPLORER)) {
          // fprintf (stderr, "exiting explorer: gid: %d, medical: %d, destinationPurpose: %d\n", gid, medical, destinationPurpose);
        }

	// freeze unit health when inside a building
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE)
            || ((displacement==DIS_EXITING_BUILDING)
                && ! ((typeNum == EXPLORER) && (medical != MED_FREE))))
		return;

	if (verbose)
		printf("guid=(%d) handleActivity (medical=%d, activity=%d) (needToRecheckMedical=%d) (attachedBuilding=%p)...\n",
			gid, medical, activity, needToRecheckMedical, attachedBuilding);

	if(activity!=ACT_RANDOM)
		jobTimer=0;

	if (medical==MED_FREE)
	{
		handleMagic();

		if (activity==ACT_RANDOM)
		{
			// nothing to do:
			//Wait for 32 ticks before doing something else, to allow buildings time to hire units
			if(jobTimer>32)
			{
				// We look for an upgrade
				Building* b=owner->findBestUpgrade(this);
				if (b)
				{
					assert(destinationPurpose>=WALK);
					assert(destinationPurpose<ARMOR);
					activity=ACT_UPGRADING;
					attachedBuilding=b;
					setTargetBuilding(b);
					if (verbose)
						printf("guid=(%d) going to upgrade at dp=(%d), gbid=(%d)\n", gid, destinationPurpose, b->gid);
					b->subscribeUnitForInside(this);
					return;
				}

				// we go to a heal building if we'r not fully healed: (1/8 trigger)
				if (hp+(performance[HP]/UNIT_HEAL_TRIGGER_INV_RATIO) < performance[HP])
				{
					Building *b;
					b=owner->findNearestHeal(this);
					if (b)
					{
						destinationPurpose=HEAL;
						activity=ACT_UPGRADING;
						attachedBuilding=b;
						setTargetBuilding(b);
						needToRecheckMedical=false;
						if (verbose)
							printf("guid=(%d) Going to heal building\n", gid);
						targetX=attachedBuilding->getMidX();
						targetY=attachedBuilding->getMidY();
						validTarget=true;
						b->subscribeUnitForInside(this);
					}
					else
						activity=ACT_RANDOM;
				}
			}
		}
	}
	else if (needToRecheckMedical)
	{
		// disconnect from building
		if (attachedBuilding)
		{
			if (verbose)
				printf("guid=(%d) Need medical while working, abort work\n", gid);
			attachedBuilding->removeUnitFromWorking(this);
			attachedBuilding->removeUnitFromInside(this);
			attachedBuilding=NULL;
			ownExchangeBuilding=NULL;
		}
		setTargetBuilding(NULL);

		if (medical==MED_HUNGRY)
		{
			Building *b;
			b=owner->findNearestFood(this);
                        /*if (typeNum == EXPLORER) {
                           fprintf (stderr, "gid: %d, b: %x\n", gid, b);
                        }*/

			if (b!=NULL)
			{
				Team *currentTeam=owner;
				Team *targetTeam=b->owner;
				if (currentTeam != targetTeam)
				{
					// Unit conversion code

					// Send events and keep track of number of unit converted
					currentTeam->pushGameEvent(GameEvent::unitLostConversion(owner->game->stepCounter, posX, posY, targetTeam->teamNumber));
					currentTeam->unitConversionLost++;

					targetTeam->pushGameEvent(GameEvent::unitGainedConversion(owner->game->stepCounter, posX, posY, currentTeam->teamNumber));
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
						Sint32 currentID=Unit::GIDtoID(gid);
						assert(currentTeam->myUnits[currentID]);
						currentTeam->myUnits[currentID]=NULL;
						targetTeam->myUnits[targetID]=this;
						Uint16 targetGID=(GIDfrom(targetID, targetTeam->teamNumber));
						if (verbose)
							printf("Unit guid=%d (%d) switched to guid=%d (%d)\n", gid, Unit::GIDtoTeam(gid), targetGID, Unit::GIDtoTeam(targetGID));
						if (performance[FLY])
						{
							assert(owner->map->getAirUnit(posX, posY)==gid);
							owner->map->setAirUnit(posX, posY, targetGID);
						}
						else
						{
							assert(owner->map->getGroundUnit(posX, posY)==gid);
							owner->map->setGroundUnit(posX, posY, targetGID);
						}
						gid=targetGID;
						owner=targetTeam;
					}
				}

				destinationPurpose=FEED;
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				setTargetBuilding(b);
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to food at building gbid=(%d)\n", gid, b->gid);
				b->subscribeUnitForInside(this);
			}
			else
				activity=ACT_RANDOM;
		}
		else if (medical==MED_DAMAGED)
		{
			Building *b;
			b=owner->findNearestHeal(this);
			if (b!=NULL)
			{
				destinationPurpose=HEAL;
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				setTargetBuilding(b);
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to heal at building gbid=(%d)\n", gid, b->gid);
				b->subscribeUnitForInside(this);
			}
			else
				activity=ACT_RANDOM;
		}
		else
			assert(false);
	}
}
