/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"
#include "Utilities.h"

Unit::Unit(SDL_RWops *stream, Team *owner)
{
	load(stream, owner);
}

Unit::Unit(int x, int y, Sint16 uid, UnitType::TypeNum type, Team *team, int level)
{
	int i;

	// unit specification
	typeNum=type;

	assert(team);
	race=&(team->race);

	// identity
	UID=uid;
	owner=team;
	isDead=false;

	// position
	posX=x;
	posY=y;
	delta=0;
	dx=0;
	dy=0;
	direction=8;
	insideTimeout=0;
	speed=32;

	// states
	needToRecheckMedical=true;
	medical=MED_FREE;
	activity=ACT_RANDOM;
	displacement=DIS_RANDOM;
	movement=MOV_RANDOM;
	targetX=0;
	targetY=0;
	tempTargetX=targetX;
	tempTargetY=targetY;
	bypassDirection=DIR_UNSET;
	obstacleX=0;
	obstacleY=0;
	borderX=0;
	borderY=0;

	// quality parameters
	for (i=0; i<NB_ABILITY; i++)
	{
		this->performance[i]=race->getUnitType(typeNum, level)->performance[i];
		this->level[i]=level;
	}

	// trigger parameters
	hp=0;

	// warriors fight to death
	if (performance[ATTACK_SPEED])
		trigHP=0;
	else
		trigHP=20;

	// warriors wait more tiem before going to eat
	hungry=HUNGRY_MAX;
	if (performance[ATTACK_SPEED])
		trigHungry=(hungry*2)/10;
	else
		trigHungry=hungry/4;
	trigHungryCarying=hungry/10;


	// NOTE : rewrite hp from level
	hp=this->performance[HP];
	trigHP=(hp*3)/10;

	attachedBuilding=NULL;
	destinationPurprose=-1;
	subscribed=false;
	caryedRessource=-1;
	
	// debug vars:
	verbose=false;
}

void Unit::load(SDL_RWops *stream, Team *owner)
{
	int i;

	// unit specification
	typeNum=(UnitType::TypeNum)SDL_ReadBE32(stream);
	race=&(owner->race);
	
	// identity
	UID=SDL_ReadBE32(stream);
	this->owner=owner;
	isDead=SDL_ReadBE32(stream);

	// position
	posX=SDL_ReadBE32(stream);
	posY=SDL_ReadBE32(stream);
	delta=SDL_ReadBE32(stream);
	dx=SDL_ReadBE32(stream);
	dy=SDL_ReadBE32(stream);
	direction=SDL_ReadBE32(stream);
	insideTimeout=SDL_ReadBE32(stream);
	speed=SDL_ReadBE32(stream);

	// states
#	ifdef WIN32
#		pragma warning (disable : 4800)
#	endif
	needToRecheckMedical=(bool)SDL_ReadBE32(stream);
#	ifdef WIN32
#		pragma warning (default : 4800)
#	endif
	medical=(Medical)SDL_ReadBE32(stream);
	activity=(Activity)SDL_ReadBE32(stream);
	displacement=(Displacement)SDL_ReadBE32(stream);
	movement=(Movement)SDL_ReadBE32(stream);
	action=(Abilities)SDL_ReadBE32(stream);
	targetX=(Sint32)SDL_ReadBE32(stream);
	targetY=(Sint32)SDL_ReadBE32(stream);
	tempTargetX=(Sint32)SDL_ReadBE32(stream);
	tempTargetY=(Sint32)SDL_ReadBE32(stream);
	bypassDirection=(BypassDirection)SDL_ReadBE32(stream);
	obstacleX=(Sint32)SDL_ReadBE32(stream);
	obstacleY=(Sint32)SDL_ReadBE32(stream);
	borderX=(Sint32)SDL_ReadBE32(stream);
	borderY=(Sint32)SDL_ReadBE32(stream);

	// trigger parameters
	hp=SDL_ReadBE32(stream);
	trigHP=SDL_ReadBE32(stream);

	// hungry
	hungry=SDL_ReadBE32(stream);
	trigHungry=SDL_ReadBE32(stream);
	trigHungryCarying=(trigHungry*4)/10;

	// quality parameters
	
	// quality parameters
	for (i=0; i<NB_ABILITY; i++)
	{
		performance[i]=SDL_ReadBE32(stream);
		level[i]=SDL_ReadBE32(stream);
	}

	destinationPurprose=(Abilities)SDL_ReadBE32(stream);
#	ifdef WIN32
#		pragma warning (disable : 4800)
#	endif
	subscribed=(bool)SDL_ReadBE32(stream);
#	ifdef WIN32
#		pragma warning (default : 4800)
#	endif
	
	caryedRessource=(Sint32)SDL_ReadBE32(stream);
	verbose=false;
}

void Unit::save(SDL_RWops *stream)
{
	int i;

	// unit specification
	// we drop the unittype pointer, we save only the number
	SDL_WriteBE32(stream, (Uint32)typeNum);

	// identity
	SDL_WriteBE32(stream, UID);
	SDL_WriteBE32(stream, isDead);

	// position
	SDL_WriteBE32(stream, posX);
	SDL_WriteBE32(stream, posY);
	SDL_WriteBE32(stream, delta);
	SDL_WriteBE32(stream, dx);
	SDL_WriteBE32(stream, dy);
	SDL_WriteBE32(stream, direction);
	SDL_WriteBE32(stream, insideTimeout);
	SDL_WriteBE32(stream, speed);

	// states
	SDL_WriteBE32(stream, (Uint32)needToRecheckMedical);
	SDL_WriteBE32(stream, (Uint32)medical);
	SDL_WriteBE32(stream, (Uint32)activity);
	SDL_WriteBE32(stream, (Uint32)displacement);
	SDL_WriteBE32(stream, (Uint32)movement);
	SDL_WriteBE32(stream, (Uint32)action);
	SDL_WriteBE32(stream, (Uint32)targetX);
	SDL_WriteBE32(stream, (Uint32)targetY);
	SDL_WriteBE32(stream, (Uint32)tempTargetX);
	SDL_WriteBE32(stream, (Uint32)tempTargetY);
	SDL_WriteBE32(stream, (Uint32)bypassDirection);
	SDL_WriteBE32(stream, (Uint32)obstacleX);
	SDL_WriteBE32(stream, (Uint32)obstacleY);
	SDL_WriteBE32(stream, (Uint32)borderX);
	SDL_WriteBE32(stream, (Uint32)borderY);
	
	// trigger parameters
	SDL_WriteBE32(stream, hp);
	SDL_WriteBE32(stream, trigHP);

	// hungry
	SDL_WriteBE32(stream, hungry);
	SDL_WriteBE32(stream, trigHungry);

	// quality parameters
	for (i=0; i<NB_ABILITY; i++)
	{
		SDL_WriteBE32(stream, performance[i]);
		SDL_WriteBE32(stream, level[i]);
	}

	SDL_WriteBE32(stream, (Uint32)destinationPurprose);
	SDL_WriteBE32(stream, (Uint32)subscribed);
	SDL_WriteBE32(stream, (Uint32)caryedRessource);
}

void Unit::loadCrossRef(SDL_RWops *stream, Team *owner)
{
	Sint32 UID=SDL_ReadBE32(stream);
	if (UID==NOUID)
		attachedBuilding=NULL;
	else
		attachedBuilding=owner->myBuildings[Building::UIDtoID(UID)];
}

void Unit::saveCrossRef(SDL_RWops *stream)
{
	if (attachedBuilding)
		SDL_WriteBE32(stream, attachedBuilding->UID);
	else
		SDL_WriteBE32(stream, NOUID);
}

void Unit::unsubscribed(void)
{
	if (verbose)
		printf("Unsubscribed.\n");
	subscribed=false;
	switch(medical)
	{
		case MED_HUNGRY :
		case MED_DAMAGED :
		{
			activity=ACT_UPGRADING;
			displacement=DIS_GOING_TO_BUILDING;
		}
		break;
		case MED_FREE:
		{
			switch(activity)
			{
				case ACT_FLAG :
				{
					displacement=DIS_GOING_TO_FLAG;
				}
				break;
				case ACT_UPGRADING :
				{
					displacement=DIS_GOING_TO_BUILDING;
				}
				break;
				case ACT_BUILDING:
				case ACT_HARVESTING :
				{
					if (caryedRessource==destinationPurprose)
					{
						displacement=DIS_GOING_TO_BUILDING;
						targetX=attachedBuilding->getMidX();
						targetY=attachedBuilding->getMidY();
						newTargetWasSet();
					}
					else
						displacement=DIS_GOING_TO_RESSOURCE;
				}
				break;
				case ACT_RANDOM :
				{
					displacement=DIS_RANDOM;
				}
				break;
				default:
					assert(false); 
			}
		}
		break;
	}
}

bool Unit::step(void)
{
	assert(speed>0);
	if ((action==ATTACK_SPEED) && (delta>=128) && (delta<(128+speed)))
	{
		Sint32 enemyUID=owner->game->map.getUnit(posX+dx, posY+dy);
		if (enemyUID>=0)
		{
			int enemyID=UIDtoID(enemyUID);
			int enemyTeam=UIDtoTeam(enemyUID);
			Unit *enemy=owner->game->teams[enemyTeam]->myUnits[enemyID];
			int degats=performance[ATTACK_STRENGTH]-enemy->performance[ARMOR];
			if (degats<=0)
				degats=1;
			enemy->hp-=degats;
			enemy->owner->setEvent(posX+dx, posY+dy, Team::UNIT_UNDER_ATTACK_EVENT);
		}
		else  if (enemyUID!=NOUID)
		{
			int enemyID=Building::UIDtoID(enemyUID);
			int enemyTeam=Building::UIDtoTeam(enemyUID);
			Building *enemy=owner->game->teams[enemyTeam]->myBuildings[enemyID];
			int degats=performance[ATTACK_STRENGTH]-enemy->type->armor;
			if (degats<=0)
				degats=1;
			enemy->hp-=degats;
			enemy->owner->setEvent(posX+dx, posY+dy, Team::BUILDING_UNDER_ATTACK_EVENT);
			if (enemy->hp<0)
				enemy->kill();
		}
	}
	
//#define BURST_UNIT_MODE
#ifdef BURST_UNIT_MODE
	delta=0;
#else
	if (delta<=255-speed)
	{
		delta+=speed;
	}
	else
#endif
	{
		//printf("action=%d, speed=%d, perf[a]=%d, t->perf[a]=%d\n", action, speed, performance[action], race->getUnitType(typeNum, 0)->performance[action]);
		delta+=(speed-256);
		bool b=endOfAction();
		if (performance[FLY])
		{
			owner->game->map.setMapDiscovered(posX-3, posY-3, 7, 7, owner->sharedVision);
			owner->game->map.setMapBuldingsDiscovered(posX-3, posY-3, 7, 7, owner->sharedVision, owner->game->teams);
		}
		else
		{
			owner->game->map.setMapDiscovered(posX-1, posY-1, 3, 3, owner->sharedVision);
			owner->game->map.setMapBuldingsDiscovered(posX-1, posY-1, 3, 3, owner->sharedVision, owner->game->teams);
		}
		
		return b;
	}
	return false;
}

void Unit::selectPreferedMovement(void)
{
	if (performance[FLY])
		action=FLY;
	else if ((performance[SWIM]) && (owner->game->map.isWater(posX, posY)) )
		action=SWIM;
	else if ((performance[WALK]) && (!owner->game->map.isWater(posX, posY)) )
		action=WALK;
	else
		assert(false);
}

void Unit::handleMedical(void)
{
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	medical=MED_FREE;

	hungry-=race->unitTypes[0][0].hungryness;
	if (hp<=trigHP)
	{
		//printf("I'm hurt ; healt h %d/%d\n", hp, performance[HP]);
		medical=MED_DAMAGED;
	}

	int realTrigHungry;
	if (caryedRessource==-1)
		realTrigHungry=trigHungry;
	else
		realTrigHungry=trigHungryCarying;

	if (hungry<=realTrigHungry)
		medical=MED_HUNGRY;

	if (hungry<=0)
		hp--;
	if (hp<0)
	{
		if (!isDead)
		{
			//printf("Unit:: Unit(uid%d)(id%d) died. dis=%d, mov=%d, ab=%x, ito=%d \n", UID, UIDtoID(UID), displacement, movement, (int)attachedBuilding, insideTimeout);
			
			if (attachedBuilding)
			{
				assert((displacement!=DIS_ENTERING_BUILDING) && (displacement!=DIS_INSIDE) && (displacement!=DIS_EXITING_BUILDING));
				attachedBuilding->unitsWorking.remove(this);
				attachedBuilding->unitsInside.remove(this);
				attachedBuilding->unitsWorkingSubscribe.remove(this);
				attachedBuilding->unitsInsideSubscribe.remove(this);
				// NOTE : we should NOT be in the building
				attachedBuilding->update();
				attachedBuilding=NULL;
				subscribed=false;
			}
			
			activity=ACT_RANDOM;
			displacement=DIS_RANDOM;
			
			owner->game->map.setUnit(posX, posY, NOUID);
		}
		else
		{
			//printf("Unit:: Unit(uid%d)(id%d) killed twice! dis=%d, mov=%d, ab=%x, ito=%d \n", UID, UIDtoID(UID), displacement, movement, (int)attachedBuilding, insideTimeout);
		}
		isDead=true;
	}
		

	if (medical==MED_FREE)
		needToRecheckMedical=true;
}

void Unit::handleActivity(void)
{
	// freeze unit health when inside a building
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	if (medical==MED_FREE)
	{
		if (activity==ACT_RANDOM)
		{
			// look for a "job"
			// else keep walking around

			bool jobFound=false;
			
			// first we look for a food building to fill:
			if (performance[HARVEST])
			{
				Building *b;
				b=owner->findNearestFillableFood(posX, posY);
				if ( b != NULL)
				{
					jobFound=owner->game->map.nearestRessource(posX, posY, CORN, &targetX, &targetY);
					if (jobFound)
					{
						activity=ACT_HARVESTING;
						displacement=DIS_GOING_TO_RESSOURCE;
						if (verbose)
							printf("(%d)Going to harvest for filling building\n", UID);
						destinationPurprose=(int)CORN;
						attachedBuilding=b;
						//printf("g(%x) unitsWorkingSubscribe dp=(%d), UID=(%d), B(%x)UID=(%d)\n", (int)this, destinationPurprose, UID, (int)b, b->UID);
						b->unitsWorkingSubscribe.push_front(this);
						b->lastWorkingSubscribe=0;
						subscribed=true;
						owner->subscribeForWorkingStep.push_front(b);
						//b->update();
						return;
					}
				}
			}

			// second we look for upgrade
			{
				for (int abilityIterator=(int)WALK; abilityIterator<(int)ARMOR; abilityIterator++)
				{
					if (performance[abilityIterator])
					{
						Building *b=owner->findNearestUpgrade(posX, posY, (Abilities)abilityIterator, level[abilityIterator]);
						if ( b != NULL)
						{
							jobFound=true;
							activity=ACT_UPGRADING;
							displacement=DIS_GOING_TO_BUILDING;
							destinationPurprose=abilityIterator;
							
							//printf("Going to upgrading itself in a building for ability : %d\n", destinationPurprose);
							
							attachedBuilding=b;
							targetX=attachedBuilding->getMidX();
							targetY=attachedBuilding->getMidY();
							newTargetWasSet();
							b->unitsInsideSubscribe.push_front(this);
							b->lastInsideSubscribe=0;
							subscribed=true;
							owner->subscribeForInsideStep.push_front(b);
							///b->update();
							
							return;
						}
					}
				}
			}
			
			// third we harvest for construction
			if (performance[HARVEST])
			{
				Building *b;
				
				// if we have a ressource
				b=owner->findBestConstruction(this);
				if ( b != NULL)
				{
					//do not do this (it's done in findBestConstruction() much nicer)
					//destinationPurprose=b->neededRessource();
					assert(destinationPurprose>=0);
					assert(b->neededRessource(destinationPurprose));
					
					jobFound=owner->game->map.nearestRessource(posX, posY, (RessourceType)destinationPurprose, &targetX, &targetY);
					if (jobFound)
					{
						activity=ACT_BUILDING;
						displacement=DIS_GOING_TO_RESSOURCE;
						newTargetWasSet();
						
						//printf("(%x)Going to harvest to build building\n", (int)this);
						
						attachedBuilding=b;
						//printf("c(%x) unitsWorkingSubscribe dp=(%d), UID=(%d), B(%x)UID=(%d)\n", (int)this, destinationPurprose, UID, (int)b, b->UID);
						b->unitsWorkingSubscribe.push_front(this);
						b->lastWorkingSubscribe=0;
						subscribed=true;
						owner->subscribeForWorkingStep.push_front(b);
						//b->update();
						return;
					}
				}
			}

			
			// fifth we go to flag
			for (int abilityIterator=(int)WALK; abilityIterator<(int)ARMOR; abilityIterator++)
			{
				if (performance[abilityIterator])
				{
					Building *b=owner->findNearestAttract(posX, posY, (Abilities)abilityIterator);
					if ( b != NULL)
					{
						jobFound=true;
						activity=ACT_FLAG;
						displacement=DIS_GOING_TO_FLAG;
						destinationPurprose=abilityIterator;

						attachedBuilding=b;
						targetX=attachedBuilding->getMidX();
						targetY=attachedBuilding->getMidY();
						newTargetWasSet();
						//printf("f(%x) unitsWorkingSubscribe dp=(%d), UID=(%d), B(%x)UID=(%d)\n", (int)this, destinationPurprose, UID, (int)b, b->UID);
						b->unitsWorkingSubscribe.push_front(this);
						b->lastWorkingSubscribe=0;
						subscribed=true;
						owner->subscribeForWorkingStep.push_front(b);
						//b->update();
						//printf("Going to flag for ability : %d - pos (%d, %d)\n", destinationPurprose, targetX, targetY);

						return;
					}
				}
			}
			
			
			if ( (!jobFound) )
			{
				// find another job.
			}
			// nothing to do:
			// we go to a heal building if we'r not fully healed:
			if (hp<performance[HP])
			{
				Building *b;
				b=owner->findNearestHeal(posX, posY);
				if ( b != NULL)
				{
					//printf("Going to heal building\n");
					activity=ACT_UPGRADING;
					displacement=DIS_GOING_TO_BUILDING;
					destinationPurprose=HEAL;
					needToRecheckMedical=false;

					attachedBuilding=b;
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
					newTargetWasSet();
					b->unitsInsideSubscribe.push_front(this);
					b->lastInsideSubscribe=0;
					subscribed=true;
					owner->subscribeForInsideStep.push_front(b);
					//b->update();
				}
				else
					activity=ACT_RANDOM;
			}
		}
		else
		{
			// we keep the job
		}
	}
	else if (needToRecheckMedical)
	{
		if (attachedBuilding)
		{
			if (verbose)
				printf("Need medical assistant while working, abort work\n");
			attachedBuilding->unitsWorking.remove(this);
			attachedBuilding->unitsInside.remove(this);
			attachedBuilding->unitsWorkingSubscribe.remove(this);
			attachedBuilding->unitsInsideSubscribe.remove(this);
			attachedBuilding->update();
			attachedBuilding=NULL;
			subscribed=false;
		}
		
		if (medical==MED_HUNGRY)
		{
			Building *b;
			b=owner->findNearestFood(posX, posY);
			if ( b != NULL)
			{
				
				activity=ACT_UPGRADING;
				displacement=DIS_GOING_TO_BUILDING;
				destinationPurprose=FEED;
				needToRecheckMedical=false;
    			
				attachedBuilding=b;
				targetX=attachedBuilding->getMidX();
				targetY=attachedBuilding->getMidY();
				newTargetWasSet();
				b->unitsInsideSubscribe.push_front(this);
				b->lastInsideSubscribe=0;
				subscribed=true;
				owner->subscribeForInsideStep.push_front(b);
				if (verbose)
					printf("Subscribed to food building %d\n", b->UID);
				//b->update();
			}
			else
				activity=ACT_RANDOM;
		}
		else if (medical==MED_DAMAGED)
		{
			Building *b;
			b=owner->findNearestHeal(posX, posY);
			if ( b != NULL)
			{
				if (verbose)
					printf("Subscribed to heal building %d\n", b->UID);
				activity=ACT_UPGRADING;
				displacement=DIS_GOING_TO_BUILDING;
				destinationPurprose=HEAL;
				needToRecheckMedical=false;
    			
				attachedBuilding=b;
				targetX=attachedBuilding->getMidX();
				targetY=attachedBuilding->getMidY();
				newTargetWasSet();
				b->unitsInsideSubscribe.push_front(this);
				b->lastInsideSubscribe=0;
				subscribed=true;
				owner->subscribeForInsideStep.push_front(b);
				//b->update();
			}
			else
				activity=ACT_RANDOM;
		}
	}
}

void Unit::handleDisplacement(void)
{
	if (subscribed)
	{
		displacement=DIS_RANDOM;
	}
	else switch (activity)
	{
		case ACT_RANDOM:
		{
			if ((medical==MED_FREE)&&((displacement==DIS_RANDOM)||(displacement==DIS_REMOVING_BLACK_AROUND)||(displacement==DIS_ATTACKING_AROUND)))
			{
				if (performance[FLY])
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (performance[ATTACK_SPEED])
					displacement=DIS_ATTACKING_AROUND;
			}
			else
				displacement=DIS_RANDOM;
			break;
		}
		
		case ACT_BUILDING:
		case ACT_HARVESTING:
		{
			assert(attachedBuilding);
			if (displacement==DIS_GOING_TO_RESSOURCE)
			{
				if (owner->game->map.doesUnitTouchRessource(this, (RessourceType)destinationPurprose, &dx, &dy))
				{
					displacement=DIS_HARVESTING;
					//printf("I found ressource\n");
				}
			}
			else if (displacement==DIS_HARVESTING)
			{
				// we got the ressource.
				caryedRessource=destinationPurprose;
				owner->game->map.decRessource(posX+dx, posY+dy, (RessourceType)caryedRessource);
				
				if (owner->game->map.doesUnitTouchUID(this, attachedBuilding->UID, &dx, &dy))
				{
					if (activity==ACT_HARVESTING)
						displacement=DIS_GIVING_TO_BUILDING;
					else if (activity==ACT_BUILDING)
						displacement=DIS_BUILDING;
					else
						assert(false);
				}
				else
				{
					displacement=DIS_GOING_TO_BUILDING;
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
					newTargetWasSet();
				}
			}
			else if (displacement==DIS_GOING_TO_BUILDING)
			{
				if (owner->game->map.doesUnitTouchUID(this, attachedBuilding->UID, &dx, &dy))
				{
					if (activity==ACT_HARVESTING)
						displacement=DIS_GIVING_TO_BUILDING;
					else if (activity==ACT_BUILDING)
						displacement=DIS_BUILDING;
					else
						assert(false);
				}
			}
			else if ( (displacement==DIS_GIVING_TO_BUILDING) || (displacement==DIS_BUILDING))
			{
				if (attachedBuilding->ressources[caryedRessource] < attachedBuilding->type->maxRessource[caryedRessource])
				{
					
					assert(attachedBuilding);
					//printf("Giving ressource to building : res : %d\n", attachedBuilding->ressources[(int)destinationPurprose]);
					attachedBuilding->ressources[caryedRessource]++;
					caryedRessource=-1;
					if (displacement==DIS_BUILDING)
						attachedBuilding->hp+=attachedBuilding->type->hpInc;

					assert(attachedBuilding);
					attachedBuilding->update();
					// NOTE : this assert is wrong because Building->update() can free the unit
					//assert(attachedBuilding);
					// replaced by
					if (!attachedBuilding)
					{
						//printf("The building doesn't need me any more.\n");
						activity=ACT_RANDOM;
						displacement=DIS_RANDOM;
						subscribed=false;
						return;
					}
				}
				destinationPurprose=attachedBuilding->neededRessource();
				if (verbose)
				{
					printf("(%d) destinationPurprose=%d.\n", UID, destinationPurprose);
				}
				if ((destinationPurprose>=0)&&(owner->game->map.nearestRessource(posX, posY, (RessourceType)destinationPurprose, &targetX, &targetY)))
				{
					newTargetWasSet();
					if (owner->game->map.doesUnitTouchRessource(this, (RessourceType)destinationPurprose, &dx, &dy))
					{
						displacement=DIS_HARVESTING;
						//printf("I found ressource\n");
					}
					else
					{
						//activity=ACT_HARVESTING;
						displacement=DIS_GOING_TO_RESSOURCE;
					}
					//printf("Keep going to harvest for filling building\n");
				}
				else
				{
					//printf("Don't find any ressource !!!!\n");
					activity=ACT_RANDOM;
					displacement=DIS_RANDOM;
					attachedBuilding->unitsWorking.remove(this);
					attachedBuilding->unitsWorkingSubscribe.remove(this);
					attachedBuilding->update();
					attachedBuilding=NULL;
					subscribed=false;

				}
			}
			else
				displacement=DIS_RANDOM;
			
			break;
		}
		
		case ACT_UPGRADING:
		{
			assert(attachedBuilding);
			
			if (displacement==DIS_GOING_TO_BUILDING)
			{
				if (owner->game->map.doesUnitTouchUID(this, attachedBuilding->UID, &dx, &dy))
				{
					displacement=DIS_ENTERING_BUILDING;
				}
			}
			else if (displacement==DIS_ENTERING_BUILDING)
			{
				// The unit has already its room in the building,
				// then we are sure that the unit can enter.
				
				owner->game->map.setUnit(posX-dx, posY-dy, NOUID);
				displacement=DIS_INSIDE;
				
				if (destinationPurprose==FEED)
				{
					insideTimeout=-attachedBuilding->type->timeToFeedUnit;
				}
				else if (destinationPurprose==HEAL)
				{
					insideTimeout=-attachedBuilding->type->timeToHealUnit;
				}
				else
				{
					insideTimeout=-attachedBuilding->type->upgradeTime[destinationPurprose];
				}
				speed=attachedBuilding->type->insideSpeed;
			}
			else if (displacement==DIS_INSIDE)
			{
				// we stay inside while the unit upgrades.
				if (insideTimeout>=0)
				{
					//printf("Exiting building\n");
					displacement=DIS_EXITING_BUILDING;

					if (destinationPurprose==FEED)
					{
						hungry=HUNGRY_MAX;
						attachedBuilding->ressources[CORN]--;
						assert(attachedBuilding->ressources[CORN]>=0);
						//printf("I'm not hungry any more :-)\n");
						needToRecheckMedical=true;
					}
					else if (destinationPurprose==HEAL)
					{
						hp=performance[HP];
						//printf("I'm healed : healt h %d/%d\n", hp, performance[HP]);
						needToRecheckMedical=true;
					}
					else
					{
						//printf("Ability %d got level %d\n", destinationPurprose, attachedBuilding->type->level+1);
						level[destinationPurprose]=attachedBuilding->type->level+1;
						UnitType *ut=race->getUnitType(typeNum, level[destinationPurprose]);
						performance[destinationPurprose]=ut->performance[destinationPurprose];
						
						//printf("New performance[%d]=%d\n", destinationPurprose, performance[destinationPurprose]);
					}
				}
				else
				{
					insideTimeout++;
				}
			}
			else if (displacement==DIS_EXITING_BUILDING)
			{
				// we wants to get out, so we still stay in displacement==DIS_EXITING_BUILDING.
			}
			else
				displacement=DIS_RANDOM;
			break;
		}

		case ACT_FLAG:
		{
			assert(attachedBuilding);

			targetX=attachedBuilding->posX;
			targetY=attachedBuilding->posY;
			int distance=owner->game->map.warpDistSquare(targetX, targetY, posX, posY);
			int range=(Sint32)((attachedBuilding->unitStayRange)*(attachedBuilding->unitStayRange));
			//printf("%d <? %d :-)\n", dist1, dist2);
			if (distance<=range)
			{
				//printf("I'm in flag\n");
				if (destinationPurprose==FLY)
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (destinationPurprose==ATTACK_SPEED)
					displacement=DIS_ATTACKING_AROUND;
				else if (destinationPurprose==HARVEST)
					displacement=DIS_CLEARING_RESSOURCES;
				else
					assert(false);
			}
			else if (attachedBuilding->unitsWorking.size()>(unsigned)attachedBuilding->maxUnitWorking)
			{
				// FIXME : this code is often used, we should do a methode with it !!
				activity=ACT_RANDOM;
				displacement=DIS_RANDOM;
				attachedBuilding->unitsWorking.remove(this);
				attachedBuilding->unitsWorkingSubscribe.remove(this);
				attachedBuilding->update();
				attachedBuilding=NULL;
				subscribed=false;
			}
			else
			{
				displacement=DIS_GOING_TO_FLAG;
			}

			break;
		}

		default:
		{
			assert (false);
			break;
		}
	}
}

void Unit::handleMovement(void)
{
	switch (displacement)
	{
		case DIS_REMOVING_BLACK_AROUND:
		{
			if (verbose)
				printf("DIS_REMOVING_BLACK_AROUND\n");
			if (attachedBuilding)
			{
				movement=MOV_GOING_DXDY;
				int bposX=attachedBuilding->posX;
				int bposY=attachedBuilding->posY;

				int ldx=bposX-posX;
				int ldy=bposY-posY;
				int cdx, cdy;
				simplifyDirection(ldx, ldy, &cdx, &cdy);


				dx=-cdy;
				dy=cdx;
				if (!owner->game->map.isMapDiscovered(posX+4*cdx, posY+4*cdy, owner->sharedVision))
				{
					dx=cdx;
					dy=cdy;
				}
			}
			else if ((movement!=MOV_GOING_DXDY)||((syncRand()&0xFF)<0xEF))
			{
				int dist[8];
				int minDist=32;
				int minDistj=8;
				for (int j=0; j<8; j++)
				{
					dist[j]=32;
					// WARNING : the i=4 is linked to the sight range of the explorer.
					for (int i=4; i<32; i+=4)
					{
						int dx, dy;
						dxdyfromDirection(j, &dx, &dy);
						if (!owner->game->map.isMapDiscovered(posX+i*dx, posY+j*dy, owner->sharedVision))
						{
							dist[j]=i;
							break;
						}
					}
					if (dist[j]<minDist)
					{
						minDist=dist[j];
						minDistj=j;
					}
					if (verbose)
						printf("dist[%d]=%d.\n", j, dist[j]);
				}
				
				if (minDist==32)
				{
					if (verbose)
						printf("I can''t see black.\n");
					if ((syncRand()&0xFF)<0xEF)
					{
						movement=MOV_GOING_DXDY;
					}
					else if ((syncRand()&0xFF)<0xEF)
					{
						directionFromDxDy();
						direction=(direction+((syncRand()&1)<<1)+7)&7;
						dxdyfromDirection();
						movement=MOV_GOING_DXDY;
					}
					else 
					{
						movement=MOV_RANDOM;
					}
				}
				else
				{
					int decj=syncRand()&7;
					if (verbose)
						printf("minDist=%d, minDistj=%d.\n", minDist, minDistj);
					for (int j=0; j<8; j++)
					{
						int d=(decj+j)&7;

						if (dist[d]<=minDist)
						{
							movement=MOV_GOING_DXDY;
							dxdyfromDirection(d, &dx, &dy);
							break;
						}
					}
				}
			}
			if ((movement!=MOV_GOING_DXDY)||(owner->game->map.getUnit(posX+dx, posY+dy)!=NOUID))
			{
				movement=MOV_RANDOM;
			}
			break;
		}

		case DIS_ATTACKING_AROUND:
		{
			if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (owner->game->map.doesUnitTouchEnemy(this, &dx, &dy)))
			{
				movement=MOV_ATTACKING_TARGET;
			}
			else
			{
				movement=MOV_RANDOM;
				for (int x=-8; x<=8; x++)
					for (int y=-8; y<=8; y++)
						if (owner->game->map.isFOW(posX+x, posY+y, owner->sharedVision))
						{
							if (attachedBuilding&&
								owner->game->map.warpDistSquare(posX+x, posY+y, attachedBuilding->posX, attachedBuilding->posY)
									>((int)attachedBuilding->unitStayRange*(int)attachedBuilding->unitStayRange))
								continue;
							int uid=owner->game->map.getUnit(posX+x, posY+y);
							if (uid!=NOUID)
							{
								int team, id;
								if (uid<0)
								{
									team=Building::UIDtoTeam(uid);
									id=Building::UIDtoID(uid);
									if (owner->game->teams[team]->myBuildings[id]->type->isVirtual)
										continue;
								}
								else
								{
									team=Unit::UIDtoTeam(uid);
								}
								Uint32 tm=1<<team;
								if (owner->enemies & tm)
								{
									movement=MOV_GOING_TARGET;
									targetX=posX+x;
									targetY=posY+y;
									break;
								}
							}
						}
					
			}
			break;
		}
		
		case DIS_CLEARING_RESSOURCES:
		{
			if (movement==MOV_HARVESTING)
				owner->game->map.decRessource(posX+dx, posY+dy);
			
			if (owner->game->map.doesUnitTouchRessource(this, &dx, &dy))
			{	
				movement=MOV_HARVESTING;
			}
			else if (owner->game->map.nearestRessourceInCircle(posX, posY,
				attachedBuilding->posX, attachedBuilding->posY, attachedBuilding->unitStayRange,
				&targetX, &targetY))
			{
				newTargetWasSet();
				//printf("pos=(%d, %d), flag=(%d, %d), range=%d, target=(%d, %d), wds=%d.\n", posX, posY,
				//	attachedBuilding->posX, attachedBuilding->posY, attachedBuilding->unitStayRange,
				//	targetX, targetY,
				//	owner->game->map.warpDistSquare(attachedBuilding->posX, attachedBuilding->posY, targetX, targetY));
				movement=MOV_GOING_TARGET;
			}
			else
			{
				movement=MOV_RANDOM;
			}
			break;
		}

		case DIS_RANDOM:
		{
			if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (owner->game->map.doesUnitTouchEnemy(this, &dx, &dy)))
			{
				movement=MOV_ATTACKING_TARGET;
			}
			else
			{
				movement=MOV_RANDOM;
			}
		}
		break;

		case DIS_GOING_TO_FLAG:
		case DIS_GOING_TO_BUILDING:
		{
			if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (owner->game->map.doesUnitTouchEnemy(this, &dx, &dy)))
			{
				movement=MOV_ATTACKING_TARGET;
			}
			else
			{
				movement=MOV_GOING_TARGET;
			}
			break;
		}

		case DIS_ENTERING_BUILDING:
		{
			movement=MOV_ENTERING_BUILDING;
			break;
		}

		case DIS_INSIDE:
		{
			movement=MOV_INSIDE;
			break;
		}

		case DIS_EXITING_BUILDING:
		{
#			ifdef WIN32
#				pragma warning (disable : 4800)
#			endif
			if (attachedBuilding->findExit(&posX, &posY, &dx, &dy, performance[FLY]))
#			ifdef WIN32
#				pragma warning (default : 4800)
#			endif
			{
				//printf("Exit found : (%d,%d) delta (%d,%d)\n", posX, posY, dx, dy);
				// OK, we have finished the ACT_BUILDING displacement.
				activity=ACT_RANDOM;
				displacement=DIS_RANDOM;
				movement=MOV_EXITING_BUILDING;

				attachedBuilding->unitsInside.remove(this);
				attachedBuilding->unitsInsideSubscribe.remove(this);
				attachedBuilding->update();
				attachedBuilding=NULL;
				subscribed=false;
			}
			else
			{
				//printf("Can't find exit : (%d,%d) delta (%d,%d)\n", posX, posY, dx, dy);
				posX=0; // zzz
				posY=0;
				movement=MOV_INSIDE;
			}
			break;
		}

		case DIS_GOING_TO_RESSOURCE:
		{
			movement=MOV_GOING_TARGET;
			break;
		}

		case DIS_HARVESTING:
		{
			movement=MOV_HARVESTING;
			break;
		}
		
		case DIS_GIVING_TO_BUILDING:
		{
			movement=MOV_GIVING;
			break;
		}
		
		case DIS_BUILDING:
		{
			movement=MOV_BUILDING;
			break;
		}
		
		default:
		{
			assert (false);
			break;
		}
	}
}

void Unit::handleAction(void)
{
	switch (movement)
	{
		case MOV_RANDOM:
		{
			owner->game->map.setUnit(posX, posY, NOUID);
			dx=-1+syncRand()%3;
			dy=-1+syncRand()%3;
			directionFromDxDy();
			setNewValidDirection();
			posX=(posX+dx)&(owner->game->map.getMaskW());
			posY=(posY+dy)&(owner->game->map.getMaskH());
			selectPreferedMovement();
			speed=performance[action];
			owner->game->map.setUnit(posX, posY, UID);
			break;
		}
		
		case MOV_GOING_TARGET:
		{
			owner->game->map.setUnit(posX, posY, NOUID);
			
			pathFind();
			//printf("%d d=(%d, %d)!\n", (int)this, dx, dy);
			posX=(posX+dx)&(owner->game->map.getMaskW());
			posY=(posY+dy)&(owner->game->map.getMaskH());
			
			selectPreferedMovement();
			speed=performance[action];
			
			owner->game->map.setUnit(posX, posY, UID);
			break;
		}

		case MOV_GOING_DXDY:
		{
			owner->game->map.setUnit(posX, posY, NOUID);
			
			directionFromDxDy();
			posX=(posX+dx)&(owner->game->map.getMaskW());
			posY=(posY+dy)&(owner->game->map.getMaskH());
			
			selectPreferedMovement();
			speed=performance[action];
			
			owner->game->map.setUnit(posX, posY, UID);
			
			if (verbose)
			{
				printf("MOV_GOING_DXDY d=(%d, %d; %d).\n", direction, dx, dy);
			}
			
			break;
		}
		
		case MOV_ENTERING_BUILDING:
		{
			// NOTE : this is a hack : We don't delete the unit on the map
			// because we have to draw it while it is entering.
			// owner->game->map.setUnit(posX, posY, NOUID);
			posX+=dx;
			posY+=dy;
			directionFromDxDy();
			selectPreferedMovement();
			speed=performance[action];
			break;
		}
		
		case MOV_EXITING_BUILDING:
		{
			directionFromDxDy();
			selectPreferedMovement();
			speed=performance[action];
			owner->game->map.setUnit(posX, posY, UID);
			break;
		}
		
		case MOV_INSIDE:
		{
			break;
		}
		
		case MOV_BUILDING:
		case MOV_GIVING:
		{
			directionFromDxDy();
			action=BUILD;
			speed=performance[action];
			break;
		}

		case MOV_ATTACKING_TARGET:
		{
			directionFromDxDy();
			action=ATTACK_SPEED;
			speed=performance[action];
			break;
		}
		
		case MOV_HARVESTING:
		{
			directionFromDxDy();
			action=HARVEST;
			speed=performance[action];
			break;
		}
		
		default:
		{
			assert (false);
			break;
		}
	}
}

void Unit::setNewValidDirection(void)
{
	int i=0;
	while (	(i<8) &&
			((owner->game->map.getUnit(posX+dx, posY+dy)!=NOUID) ||
			( (!performance[FLY]) && (owner->game->map.isRessource(posX+dx, posY+dy)) )) )
	{
		direction=(direction+1)&7;
		dxdyfromDirection();
		i++;
	}
	if (i==8)
	{
		direction=8;
		dxdyfromDirection();
	}		
}

bool Unit::valid(int x, int y)
{
	// Is there anythig that could block an unit?
	return ((owner->game->map.getUnit(x, y)==NOUID) &&
		( (performance[FLY]) || (!owner->game->map.isRessource(x, y)) ));
}

bool Unit::validHard(int x, int y)
{
	// Is there anythig that could block an unit? (except other units)
	Sint32 u=owner->game->map.getUnit(x, y);
	return (((u>=0) || (u==NOUID)) &&
		( (performance[FLY]) || (!owner->game->map.isRessource(x, y)) ));
}

bool Unit::validMed(int x, int y)
{
	// Is it reasonabely free ?
	
	if ((!performance[FLY]) && (owner->game->map.isRessource(x, y)))
		return false;
	
	Sint32 u=owner->game->map.getUnit(x, y);
	if (u==NOUID)
		return true;
	else if (u<0)
		return false;
	else
	{
		int c=0;
		{
			for (int i=0; i<8; i++)
			{
				int dx, dy;
				dxdyfromDirection(i, &dx, &dy);
				if (valid(x+dx, x+dy))
					c++;
			}
		}
		return (c>=4);
	}
}

void Unit::pathFind(void)
{
	// TODO: do something more clever when you know you'r locked.
	int odx=dx;
	int ody=dy;
	bool broken=false;
	int mapw=owner->game->map.getW();
	int maph=owner->game->map.getH();

	if (bypassDirection==DIR_UNSET)
	{
		if ((displacement==DIS_GOING_TO_RESSOURCE)&&(!owner->game->map.isRessource(targetX, targetY, (RessourceType)destinationPurprose)))
		{
			owner->game->map.nearestRessource(targetX, targetY, (RessourceType)destinationPurprose, &targetX, &targetY);
			if (verbose)
				printf("(%d), no ressouces here! pos=(%d, %d), target=(%d, %d).\n", UID, posX, posY, targetX, targetY);
		}
		// Now we first try the 3 simplest directions:
		int ldx=targetX-posX;
		int ldy=targetY-posY;
		int cdx, cdy;
		
		if (verbose)
			printf("(%d), pos=(%d, %d), target=(%d, %d), ld=(%d, %d).\n", UID, posX, posY, targetX, targetY, ldx, ldy);
		
		simplifyDirection(ldx, ldy, &cdx, &cdy);
		
		dx=cdx;
		dy=cdy;
		directionFromDxDy();
		
		int cDirection=direction;

		if (direction==8)
			return;
		
		if (valid(posX+dx, posY+dy))
			return;	
		
		direction=(cDirection+1)&7;
		dxdyfromDirection();
		if (valid(posX+dx, posY+dy))
			return;	
		
		direction=(cDirection+7)&7;
		dxdyfromDirection();
		if (valid(posX+dx, posY+dy))
			return;	
		
		if (areOnlyUnitsInFront(cdx, cdy))
		{
			if (verbose)
				printf("look at front and simply go!\n");
			gotoTarget(targetX, targetY);
			return;
		}
		if (areOnlyUnitsAround())
		{
			if (verbose)
				printf("Simple go, only units around!\n");
			gotoTarget(targetX, targetY);
			return;
		}
		
		
		if (verbose)
			printf("0x%lX no simple direction found pos=(%d, %d) cd=(%d, %d)!\n", (unsigned long)this, posX, posY, cdx, cdy);
		
		// we look for a center:
		int tdx=cdx;
		int tdy=cdy;
		int c=0;
		// we ignore the units as obstacle:
		while(validHard(posX+tdx, posY+tdy))
		{
			tdx+=cdx;
			tdy+=cdy;
			if ((c++)>256)
			{
				gotoTarget(targetX, targetY);
				return;
			}
		}
		int startObstacleX=posX+tdx;
		int startObstacleY=posY+tdy;
		tdx=cdx;
		tdy=cdy;
		c=0;
		while(!validHard(startObstacleX+tdx, startObstacleY+tdy) && (owner->game->map.warpDistSquare(posX, posY, startObstacleX+tdx, startObstacleY+tdy)<owner->game->map.warpDistSquare(posX, posY, targetX, targetY)))
		{
			tdx+=cdx;
			tdy+=cdy;
			if ((c++)>256)
			{
				gotoTarget(targetX, targetY);
				return;
			}
		}
		int centerX=startObstacleX+(tdx/2);
		int centerY=startObstacleY+(tdy/2);
		if (verbose)
			printf("center=(%d, %d)!\n", centerX, centerY);
		tempTargetX=centerX;
		tempTargetY=centerY;
		
		// we turn around the obstacle:
		
		int ptlx=posX;
		int ptly=posY;
		int ldir=cDirection;
		int lDist;
		
		int ptrx=posX;
		int ptry=posY;
		int rdir=cDirection;
		int rDist;
		
		int centerSquareDist=owner->game->map.warpDistSquare(centerX, centerY, targetX, targetY);
		if (verbose)
			printf("centerSquareDist=%d\n", centerSquareDist);
		
		assert(bypassDirection==DIR_UNSET);
		int count=0;
		while(true)
		{
			int dx, dy, c;
			
			// by the left:
			dxdyfromDirection(ldir, &dx, &dy);
			c=0;
			while (!valid(ptlx+dx, ptly+dy))
			{
				ldir=(ldir+7)&7;
				dxdyfromDirection(ldir, &dx, &dy);
				
				if ((++c)>=8)
				{
					if (verbose)
						printf("l:nowhere to go!\n");
					// nowhere to go!
					this->dx=0;
					this->dy=0;
					direction=8;
					return;
				}
			}
			ptlx+=dx;
			ptly+=dy;
			ldir=(ldir+1)&7;
			lDist=owner->game->map.warpDistSquare(ptlx, ptly, targetX, targetY);
			
			// by the right:
			c=0;
			dxdyfromDirection(rdir, &dx, &dy);
			while (!valid(ptrx+dx, ptry+dy))
			{
				rdir=(rdir+1)&7;
				dxdyfromDirection(rdir, &dx, &dy);
				if ((++c)>=8)
				{
					if (verbose)
						printf("r:nowhere to go!\n");
					this->dx=0;
					this->dy=0;
					direction=8;
					return;
				}
			}
			ptrx+=dx;
			ptry+=dy;
			rdir=(rdir+7)&7;
			rDist=owner->game->map.warpDistSquare(ptrx, ptry, targetX, targetY);
			
			obstacleX=startObstacleX;
			obstacleY=startObstacleY;
			borderX=startObstacleX-cdx;
			borderY=startObstacleY-cdy;
			
			if (verbose)
				printf("0x%lX pl=(%d, %d) lD=%d, pr=(%d, %d) rD=%d\n", (unsigned long)this, ptlx, ptly, lDist, ptrx, ptry, rDist);
			if ((lDist<=centerSquareDist)
				||((displacement==DIS_GOING_TO_RESSOURCE)&&(owner->game->map.doesPosTouchRessource(ptlx, ptly, (RessourceType)destinationPurprose, &dx, &dy)))
				||((displacement==DIS_GOING_TO_BUILDING)&&(owner->game->map.doesPosTouchUID(ptlx, ptly, attachedBuilding->UID)))
				)
			{
				bypassDirection=DIR_LEFT;
				if ((displacement==DIS_GOING_TO_RESSOURCE)&&(owner->game->map.doesPosTouchRessource(ptlx, ptly, (RessourceType)destinationPurprose, &dx, &dy)))
				{
					targetX=ptlx;
					targetY=ptly;
				}
				if (verbose)
					printf("0x%lX Let's turn by left. cd=(%d, %d) o=(%d, %d) b=(%d, %d)\n", (unsigned long)this, cdx, cdy, obstacleX, obstacleY, borderX, borderY);
				break;
			}
			if ((rDist<=centerSquareDist)
				||((displacement==DIS_GOING_TO_RESSOURCE)&&(owner->game->map.doesPosTouchRessource(ptrx, ptry, (RessourceType)destinationPurprose, &dx, &dy)))
				||((displacement==DIS_GOING_TO_BUILDING)&&(owner->game->map.doesPosTouchUID(ptrx, ptry, attachedBuilding->UID)))
				|| ((ptlx==ptrx)&&(ptly==ptry))
				|| ((count++)>1024)
				)
			{
				bypassDirection=DIR_RIGHT;
				if ((displacement==DIS_GOING_TO_RESSOURCE)&&(owner->game->map.doesPosTouchRessource(ptrx, ptry, (RessourceType)destinationPurprose, &dx, &dy)))
				{
					targetX=ptrx;
					targetY=ptry;
				}
				if (verbose)
					printf("0x%lX Let's turn by right. cd=(%d, %d) o=(%d, %d) b=(%d, %d)\n", (unsigned long)this, cdx, cdy, obstacleX, obstacleY, borderX, borderY);
				break;
			}

		}
		if (verbose)
			printf("bypassDirection=%d\n", bypassDirection);
	}

	if (bypassDirection!=DIR_UNSET)
	{
		while (obstacleX<0)
			obstacleX+=mapw;
		while (obstacleY<0)
			obstacleY+=maph;
		while (borderX<0)
			borderX+=mapw;
		while (borderY<0)
			borderY+=maph;
		
		while (obstacleX>=mapw)
			obstacleX-=mapw;
		while (obstacleY>=maph)
			obstacleY-=maph;
		while (borderX>=mapw)
			borderX-=mapw;
		while (borderY>=maph)
			borderY-=maph;
		
		if (verbose)
			printf("p=(%d, %d) \n", posX, posY);
		
		// WARNING : both cases above are very similars
		if (bypassDirection==DIR_LEFT)
		{
			int c=0;
			if (verbose)
				printf("l o=(%d, %d) b=(%d, %d) \n", obstacleX, obstacleY, borderX, borderY);
			int bdx;
			int bdy;
			int bDirection;
			int odx;
			int ody;
			
			if (validHard(obstacleX, obstacleY) || (!validHard(borderX, borderY)))
			{
				if (verbose)
				{
					printf("l obstacle not hard! .n");
					printf("l(%d) o=(%d, %d) b=(%d, %d) \n", UID, obstacleX, obstacleY, borderX, borderY);
				}
				int ctpx=tempTargetX-posX;
				int ctpy=tempTargetY-posY;
				if ((ctpx==0)&&(ctpy==0))
				{
					ctpx=targetX-posX;
					ctpy=targetY-posY;
				}
				if ((ctpx==0)&&(ctpy==0))
				{
					printf("l damn, I''m at destimation! .n");
					gotoTarget(targetX, targetY);
					bypassDirection=DIR_UNSET;
					return;
				}
				
				int dctpx, dctpy;
				simplifyDirection(ctpx, ctpy, &dctpx, &dctpy);

				obstacleX=posX;
				obstacleY=posY;
				int ci=0;
				while(validHard(obstacleX, obstacleY))
				{
					obstacleX+=dctpx;
					obstacleY+=dctpy;
					if (ci++>8)
					{
						printf("(%d) The obstacle suddenly disappeared\n", UID);
						gotoTarget(targetX, targetY);
						bypassDirection=DIR_UNSET;
						return;
					}
				}
				borderX=obstacleX-dctpx;
				borderY=obstacleY-dctpy;
			}
			
			bdx=obstacleX-borderX;
			bdy=obstacleY-borderY;
			if (abs(bdx)>1)
				bdx=-SIGN(bdx);
			if (abs(bdy)>1)
				bdy=-SIGN(bdy);
			bDirection=directionFromDxDy(bdx, bdy);
			odx=borderX-obstacleX;
			ody=borderY-obstacleY;
			
			int ldx=borderX-posX;
			int ldy=borderY-posY;
			simplifyDirection(ldx, ldy, &dx, &dy);
			directionFromDxDy();
			
			if (verbose)
				printf("l d=(%d, %d) bd=(%d, %d) od=(%d, %d) ld=(%d, %d) \n", dx, dy, bdx, bdy, odx, ody, ldx, ldy);
			
			int bapx=posX; // BorderAdvancePossiblitiy
			int bapy=posY;
			int bapdx=dx;
			int bapdy=dy;
			int maxDist=0;

			if (verbose)
				printf("l bapd=(%d, %d), border-bapd=(%d, %d)\n", bapdx, bapdy, borderX-bapdx, borderY-bapdy);
			if (((bapdx!=0)||(bapdy!=0))&&(!validHard(posX+bapdx, posY+bapdy)))
			{
				int bapDir=directionFromDxDy(bapdx, bapdy);
				
				int bapdxr, bapdyr, bapdxl, bapdyl;
				dxdyfromDirection((bapDir+1)&7, &bapdxr, &bapdyr);
				dxdyfromDirection((bapDir+7)&7, &bapdxl, &bapdyl);
				if (verbose)
					printf("bapDir=%d, bapd=(%d, %d), bapdr=(%d, %d), bapdl=(%d, %d)\n", bapDir, bapdx, bapdy, bapdxr, bapdyr, bapdxl, bapdyl); 
					
				if ((!validHard(posX+bapdxr, posY+bapdyr)) && (!validHard(posX+bapdxl, posY+bapdyl)))
					broken=true;
				maxDist=0;
			}
			else if (!validHard(borderX-bapdx, borderY-bapdy))
			{
				maxDist=0;
			}
			else if ((ldx==0)&&(ldy==0))
				maxDist=2;
			else if ((bapdx!=0)||(bapdy!=0))
				while(validHard(bapx, bapy))
				{
					bapx+=bapdx;
					bapy+=bapdy;
					if (maxDist++>64)
						break;
				}
			else
				maxDist=1;
			if (verbose)
				printf("l maxDist=%d \n", maxDist);
			
			maxDist*=maxDist;
			
			int distSq=owner->game->map.warpDistSquare(posX, posY, borderX, borderY);
			int testObstacleX=obstacleX;
			int testObstacleY=obstacleY;
			int testBorderX=borderX;
			int testBorderY=borderY;
			{
				for(int i=0; i<16 && distSq<maxDist; i++)
				{
					// Ok, the border is not too far.
					
					while (!validHard(testBorderX+bdx, testBorderY+bdy))
					{
						testObstacleX=testBorderX+bdx;
						testObstacleY=testBorderY+bdy;
						if ((++c)>8)
						{
							if (verbose)
								printf("l c:bad state, gotoTarget now\n");
							gotoTarget(targetX, targetY);
							bypassDirection=DIR_UNSET;
							return;
						}
						bDirection=(bDirection+7)&7;
						if (verbose)
							printf("l tobstacle=(%d, %d) tborder=(%d, %d) bd=(%d, %d) \n", testObstacleX, testObstacleY, testBorderX, testBorderY, bdx, bdy);
						
						dxdyfromDirection(bDirection, &bdx, &bdy);
					}
					testBorderX+=bdx;
					testBorderY+=bdy;
					if (verbose)
						printf("l new tobstacle=(%d, %d) tborder=(%d, %d) bd=(%d, %d) \n", testObstacleX, testObstacleY, testBorderX, testBorderY, bdx, bdy);
					distSq=owner->game->map.warpDistSquare(posX, posY, borderX, borderY);
					bdx=testObstacleX-testBorderX;
					bdy=testObstacleY-testBorderY;
					if (abs(bdx)>1)
						bdx=-SIGN(bdx);
					if (abs(bdy)>1)
						bdy=-SIGN(bdy);
					
					ldx=testBorderX-posX;
					ldy=testBorderY-posY;
					if (ldx>(mapw>>1))
						ldx=mapw-ldx;
					if (ldy>(maph>>1))
						ldy=maph-ldy;
					bapdx=sign(ldx);
					bapdy=sign(ldy);
					bDirection=directionFromDxDy(bdx, bdy);
					if (verbose)
						printf("l testBorder-bapd=(%d, %d).\n", testBorderX-bapdx, testBorderY-bapdy);
					c=0;
					int centerSquareDist=owner->game->map.warpDistSquare(tempTargetX, tempTargetY, targetX, targetY);
					int currentDistSquare=owner->game->map.warpDistSquare(testBorderX, testBorderY, targetX, targetY);

					if(validHard(testBorderX-bapdx, testBorderY-bapdy) && (distSq<maxDist))
					{
						if (verbose)
							printf("l o=(%d, %d), b=(%d, %d).\n", obstacleX, obstacleY, borderX, borderY);
						obstacleX=testObstacleX;
						obstacleY=testObstacleY;
						borderX=testBorderX;
						borderY=testBorderY;
						
						if ((currentDistSquare>centerSquareDist)||((displacement==DIS_GOING_TO_RESSOURCE)&&(owner->game->map.isRessource(testObstacleX, testObstacleY, (RessourceType)destinationPurprose))))
							break;
					}
					else
						break;
				}
			}
			gotoTarget(borderX, borderY);
		}
		else if (bypassDirection==DIR_RIGHT)
		{
			int c=0;
			if (verbose)
				printf("r o=(%d, %d) b=(%d, %d) \n", obstacleX, obstacleY, borderX, borderY);
			int bdx;
			int bdy;
			int bDirection;
			int odx;
			int ody;
			
			if (validHard(obstacleX, obstacleY) || (!validHard(borderX, borderY)))
			{
				if (verbose)
				{
					printf("r obstacle not hard! \n");
					printf("r(%d) o=(%d, %d) b=(%d, %d) \n", UID, obstacleX, obstacleY, borderX, borderY);
				}
				int ctpx=tempTargetX-posX;
				int ctpy=tempTargetY-posY;
				if ((ctpx==0)&&(ctpy==0))
				{
					ctpx=targetX-posX;
					ctpy=targetY-posY;
				}
				if ((ctpx==0)&&(ctpy==0))
				{
					printf("r damn, I''m at destimation! \n");
					gotoTarget(targetX, targetY);
					bypassDirection=DIR_UNSET;
					return;
				}
				
				int dctpx, dctpy;
				simplifyDirection(ctpx, ctpy, &dctpx, &dctpy);

				obstacleX=posX;
				obstacleY=posY;
				int ci=0;
				while(validHard(obstacleX, obstacleY))
				{
					obstacleX+=dctpx;
					obstacleY+=dctpy;
					if (ci++>8)
					{
						printf("(%d) The obstacle suddenly disappeared\n", UID);
						gotoTarget(targetX, targetY);
						bypassDirection=DIR_UNSET;
						return;
					}
				}

				borderX=obstacleX-dctpx;
				borderY=obstacleY-dctpy;
			}
			
			bdx=obstacleX-borderX;
			bdy=obstacleY-borderY;
			if (abs(bdx)>1)
				bdx=-SIGN(bdx);
			if (abs(bdy)>1)
				bdy=-SIGN(bdy);
			bDirection=directionFromDxDy(bdx, bdy);
			odx=borderX-obstacleX;
			ody=borderY-obstacleY;
			
			int ldx=borderX-posX;
			int ldy=borderY-posY;
			simplifyDirection(ldx, ldy, &dx, &dy);
			directionFromDxDy();
			
			if (verbose)
				printf("r d=(%d, %d) bd=(%d, %d) od=(%d, %d) ld=(%d, %d) \n", dx, dy, bdx, bdy, odx, ody, ldx, ldy);
			
			int bapx=posX; // BorderAdvancePossiblitiy
			int bapy=posY;
			int bapdx=dx;
			int bapdy=dy;
			int maxDist=0;
			
			if (verbose)
				printf("r bapd=(%d, %d), border-bapd=(%d, %d)\n", bapdx, bapdy, borderX-bapdx, borderY-bapdy);
			if (((bapdx!=0)||(bapdy!=0))&&(!validHard(posX+bapdx, posY+bapdy)))
			{
				int bapDir=directionFromDxDy(bapdx, bapdy);
				int bapdxr, bapdyr, bapdxl, bapdyl;
				dxdyfromDirection((bapDir+1)&7, &bapdxr, &bapdyr);
				dxdyfromDirection((bapDir+7)&7, &bapdxl, &bapdyl);
				if (verbose)
					printf("bapDir=%d, bapd=(%d, %d), bapdr=(%d, %d), bapdl=(%d, %d)\n", bapDir, bapdx, bapdy, bapdxr, bapdyr, bapdxl, bapdyl); 

				if ((!validHard(posX+bapdxr, posY+bapdyr)) && (!validHard(posX+bapdxl, posY+bapdyl)))
					broken=true;
				maxDist=0;
			}
			else if (!validHard(borderX-bapdx, borderY-bapdy))
			{
				maxDist=0;
			}
			else if ((ldx==0)&&(ldy==0))
				maxDist=2;
			else if ((bapdx!=0)||(bapdy!=0))
				while(validHard(bapx, bapy))
				{
					bapx+=bapdx;
					bapy+=bapdy;
					if (maxDist++>64)
						break;
				}
			else
				maxDist=1;
			if (verbose)
				printf("r maxDist=%d \n", maxDist);
			
			maxDist*=maxDist;
			
			int distSq=owner->game->map.warpDistSquare(posX, posY, borderX, borderY);
			int testObstacleX=obstacleX;
			int testObstacleY=obstacleY;
			int testBorderX=borderX;
			int testBorderY=borderY;
			{
				for(int i=0; i<16 && distSq<maxDist; i++)
				{
					// Ok, the border is not too far.
					
					while (!validHard(testBorderX+bdx, testBorderY+bdy))
					{
						testObstacleX=testBorderX+bdx;
						testObstacleY=testBorderY+bdy;
						if ((++c)>8)
						{
							if (verbose)
								printf("r c:bad state, gotoTarget now\n");
							gotoTarget(targetX, targetY);
							bypassDirection=DIR_UNSET;
							return;
						}
						bDirection=(bDirection+1)&7;
						if (verbose)
							printf("r tobstacle=(%d, %d) tborder=(%d, %d) bd=(%d, %d) \n", testObstacleX, testObstacleY, testBorderX, testBorderY, bdx, bdy);

						dxdyfromDirection(bDirection, &bdx, &bdy);
					}
					testBorderX+=bdx;
					testBorderY+=bdy;
					if (verbose)
						printf("r new tobstacle=(%d, %d) tborder=(%d, %d) bd=(%d, %d) \n", testObstacleX, testObstacleY, testBorderX, testBorderY, bdx, bdy);
					distSq=owner->game->map.warpDistSquare(posX, posY, borderX, borderY);
					bdx=testObstacleX-testBorderX;
					bdy=testObstacleY-testBorderY;
					if (abs(bdx)>1)
						bdx=-SIGN(bdx);
					if (abs(bdy)>1)
						bdy=-SIGN(bdy);
					
					ldx=testBorderX-posX;
					ldy=testBorderY-posY;
					if (ldx>(mapw>>1))
						ldx=mapw-ldx;
					if (ldy>(maph>>1))
						ldy=maph-ldy;
					bapdx=sign(ldx);
					bapdy=sign(ldy);
					bDirection=directionFromDxDy(bdx, bdy);
					if (verbose)
						printf("r testBorder-bapd=(%d, %d).\n", testBorderX-bapdx, testBorderY-bapdy);
					c=0;
					int centerSquareDist=owner->game->map.warpDistSquare(tempTargetX, tempTargetY, targetX, targetY);
					int currentDistSquare=owner->game->map.warpDistSquare(testBorderX, testBorderY, targetX, targetY);
					
					if(validHard(testBorderX-bapdx, testBorderY-bapdy) && (distSq<maxDist))
					{
						if (verbose)
							printf("r o=(%d, %d), b=(%d, %d).\n", obstacleX, obstacleY, borderX, borderY);
						obstacleX=testObstacleX;
						obstacleY=testObstacleY;
						borderX=testBorderX;
						borderY=testBorderY;
						
						if ((currentDistSquare>centerSquareDist)||((displacement==DIS_GOING_TO_RESSOURCE)&&(owner->game->map.isRessource(testObstacleX, testObstacleY, (RessourceType)destinationPurprose))))
							break;
					}
					else
						break;
				}
			}
			gotoTarget(borderX, borderY);
		}
		else
			assert(false);
			
		if ((broken)&&(dx==-odx)&&(dy==-ody))
		{
			if (verbose)
				printf("(%d) path is broken\n", UID);
			bypassDirection=DIR_UNSET;
		}
		else
		{
			int centerSquareDist=owner->game->map.warpDistSquare(tempTargetX, tempTargetY, targetX, targetY);
			int currentDistSquare=owner->game->map.warpDistSquare(posX, posY, targetX, targetY);

			if (currentDistSquare<=centerSquareDist)
			{
				if (verbose)
					printf("close enough(%d<%d) to change mode=%d\n", currentDistSquare, centerSquareDist, bypassDirection);
				tempTargetX=targetX;
				tempTargetY=targetY;
				bypassDirection=DIR_UNSET;
			}
		}

	}
	
}

bool Unit::areOnlyUnitsAround(void)
{
	for (int i=0; i<8; i++)
	{
		int dx, dy;
		dxdyfromDirection(i, &dx, &dy);
		//printf("p=(%d, %d), p+d=(%d, %d), i=%d\n", posX, posY, posX+dx, posY+dy, i);
		if (!validHard(posX+dx, posY+dy))
		{
			return false;
		}
	}
	return true;
}

bool Unit::areOnlyUnitsInFront(int dx, int dy)
{
	int ldx, ldy;
	int dir=directionFromDxDy(dx, dy);
	
	if (!validHard(posX+dx, posY+dy))
		return false;
	
	dxdyfromDirection((dir+1)&7, &ldx, &ldy);
	
	if (!validHard(posX+ldx, posY+ldy))
		return false;
	
	dxdyfromDirection((dir+7)&7, &ldx, &ldy);
	
	if (!validHard(posX+ldx, posY+ldy))
		return false;
	
	return true;
}

void Unit::gotoTarget(int targetX, int targetY)
{
	
	int ldx=targetX-posX;
	int ldy=targetY-posY;
	
	simplifyDirection(ldx, ldy, &dx, &dy);

	directionFromDxDy();
	
	if (verbose)
		printf("gotoTarget pos=(%d, %d) target=(%d, %d) ld=(%d, %d) d=(%d, %d) \n", posX, posY, targetX, targetY, ldx, ldy, dx, dy);
	
	int cDirection=direction;
		
	if (valid(posX+dx, posY+dy))
		return;	
		
	direction=(cDirection+1)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
		
	direction=(cDirection+7)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
	
	direction=(cDirection+2)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
		
	direction=(cDirection+6)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
	
	direction=(cDirection+3)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
		
	direction=(cDirection+5)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
	
	direction=(cDirection+4)&7;
	dxdyfromDirection();
	if (valid(posX+dx, posY+dy))
		return;	
			
	dx=0;
	dy=0;
	direction=8;
	if (verbose)
		printf("0x%lX: goto failed pos=(%d, %d) \n", (unsigned long)this, posX, posY);
}

void Unit::newTargetWasSet(void)
{
	tempTargetX=targetX;
	tempTargetY=targetY;
	bypassDirection=DIR_UNSET;
}

bool Unit::endOfAction(void)
{
	handleMedical();
	if (isDead)
		return true;
	handleActivity();
	handleDisplacement();
	handleMovement();
	handleAction();
	return false;
}

// NOTE : position 0 is top left (-1, -1) then run clockwise

void Unit::directionFromDxDy(void)
{
	const int tab[3][3]={	{0, 1, 2},
							{7, 8, 3},
							{6, 5, 4} };
	assert(dx>=-1);
	assert(dx<=1);
	assert(dy>=-1);
	assert(dy<=1);
	direction=tab[dy+1][dx+1];
}

void Unit::dxdyfromDirection(void)
{
	const int tab[9][2]={	{ -1, -1},
							{ 0, -1},
							{ 1, -1},
							{ 1, 0},
							{ 1, 1},
							{ 0, 1},
							{ -1, 1},
							{ -1, 0},
							{ 0, 0} };
	assert(direction>=0);
	assert(direction<=8);
	dx=tab[direction][0];
	dy=tab[direction][1];
}

int Unit::directionFromDxDy(int dx, int dy)
{
	const int tab[3][3]={	{0, 1, 2},
							{7, 8, 3},
							{6, 5, 4} };
	assert(dx>=-1);
	assert(dx<=1);
	assert(dy>=-1);
	assert(dy<=1);
	return tab[dy+1][dx+1];
}

void Unit::dxdyfromDirection(int direction, int *dx, int *dy)
{
	const int tab[9][2]={	{ -1, -1},
							{ 0, -1},
							{ 1, -1},
							{ 1, 0},
							{ 1, 1},
							{ 0, 1},
							{ -1, 1},
							{ -1, 0},
							{ 0, 0} };
	assert(direction>=0);
	assert(direction<=8);
	*dx=tab[direction][0];
	*dy=tab[direction][1];
}

void Unit::simplifyDirection(int ldx, int ldy, int *cdx, int *cdy)
{
	int mapw=owner->game->map.getW();
	int maph=owner->game->map.getH();
	if (ldx>(mapw>>1))
		ldx-=mapw;
	else if (ldx<-(mapw>>1))
		ldx+=mapw;
	if (ldy>(maph>>1))
		ldy-=maph;
	else if (ldy<-(maph>>1))
		ldy+=maph;

	if (abs(ldx)>(2*abs(ldy)))
	{
		*cdx=sign(ldx);
		*cdy=0;
	}
	else if (abs(ldy)>(2*abs(ldx)))
	{
		*cdx=0;
		*cdy=sign(ldy);
	}
	else
	{
		*cdx=sign(ldx);
		*cdy=sign(ldy);
	}
}


void Unit::secondaryDirection(int ldx, int ldy, int *cdx, int *cdy)
{
	int mapw=owner->game->map.getW();
	int maph=owner->game->map.getH();
	if (ldx>(mapw>>1))
		ldx-=mapw;
	else if (ldx<-(mapw>>1))
		ldx+=mapw;
	if (ldy>(maph>>1))
		ldy-=maph;
	else if (ldy<-(maph>>1))
		ldy+=maph;
		
	if ((abs(ldx)>(2*abs(ldy)))||(abs(ldy)>(2*abs(ldx))))
	{
		if (ldy>0)
			*cdy= 1;
		else
			*cdy=-1;
		if (ldx>0)
			*cdx= 1;
		else
			*cdx=-1;
	}
	else
	{
		if (abs(ldx)>abs(ldy))
		{
			*cdx=sign(ldx);
			*cdy=0;
		}
		else
		{
			*cdx=0,
			*cdy=sign(ldy);
		}
	}
}

Sint32 Unit::UIDtoID(Sint32 uid)
{
	return (uid%1024);
}

Sint32 Unit::UIDtoTeam(Sint32 uid)
{
	return (uid/1024);
}

Sint32 Unit::UIDfrom(Sint32 id, Sint32 team)
{
	return id+team*1024;
}

Sint32 Unit::checkSum()
{
	Sint32 cs=0;
	
	cs^=typeNum;
	
	cs^=UID;
	cs^=isDead;

	cs^=posX;
	cs^=posY;
	cs^=delta;
	cs^=dx;
	cs^=dy;
	cs^=direction;
	cs^=insideTimeout;
	cs^=speed;
	cs=(cs<<1)|(cs>>31);
	//printf("%d,1,%x\n", UID, cs);

	cs^=(int)needToRecheckMedical;
	//printf("%d,1a,%x\n", UID, cs);
	cs^=medical;
	cs^=activity;
	cs^=displacement;
	cs^=movement;
	//printf("%d,1b,%x\n", UID, cs);
	cs^=action;
	//printf("%d,1c,%x\n", UID, cs);
	cs^=targetX;
	cs^=targetY;
	//printf("%d,1d,%x\n", UID, cs);
	cs^=tempTargetX;
	cs^=tempTargetY;
	//printf("%d,1e,%x\n", UID, cs);
	cs^=bypassDirection;
	//printf("%d,1f,%x\n", UID, cs);
	cs^=obstacleX;
	cs^=obstacleY;
	//printf("%d,1g,%x\n", UID, cs);
	cs^=borderX;
	cs^=borderY;
	cs=(cs<<1)|(cs>>31);
	//printf("%d,2,%x\n", UID, cs);

	cs^=hp;
	cs^=trigHP;

	cs^=hungry;
	cs^=trigHungry;

	{
		for (int i=0; i<NB_ABILITY; i++)
		{
			cs^=performance[i];
			cs^=level[i];
			cs=(cs<<1)|(cs>>31);
		}
	}
	
	cs^=(attachedBuilding!=NULL ? 1:0);
	cs^=destinationPurprose;
	//printf("%d,3,%x***\n", UID, cs);
	
	return cs;
}


