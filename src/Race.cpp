/*
 * Globulation 2 race support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include <vector>
#include "Race.h"

Race::~Race()
{
	
			
}

void Race::create(CreationType creationType)
{
	// First we load units.txt (into 6 differents usable UnitType-s).
		
	UnitType baseUnit[2];//[min, max]
	UnitType limits[2][3];//[min, max], [worker, explorer, warrior]
	UnitType baseCost;
	UnitType evolvable;
	UnitType costs[3];//[worker, explorer, warrior]
	
	SDL_RWops *stream=SDL_RWFromFile("data/units.txt","rb");	
		
	baseUnit[0].loadText(stream);
	baseUnit[1]=baseUnit[0];
	baseUnit[1].loadText(stream);
	
	for (int i=0; i<2; i++)
		for (int j=0; j<3; j++)
			limits[i][j]=baseUnit[i];
	
	baseCost.loadText(stream);
	for (int j=0; j<3; j++)
		costs[j]=baseCost;
	
	
	evolvable.loadText(stream);
	
	for (int j=0; j<3; j++)
	{
		int offset=SDL_RWtell(stream);
		limits[0][j].loadText(stream);
		
		SDL_RWseek(stream, offset, SEEK_SET);
		limits[1][j].loadText(stream);
		//limits[1][j] = limits[0][j]; // TODO : seek !! zzz
		
		limits[1][j].loadText(stream);
		costs[j].loadText(stream);	
	}
		
	SDL_RWclose(stream);
	
	// Now we custom our race, by a minLevel and a maxLevel.
	UnitType choosed[2][3];
	
	// Here we should popup a big dialog window toallow the player to custom his own race.
	// The evolvable parabetter will have a range, and the other only a point.
	// But now we only create a default race.
	// Our default units will be based on the minUnit and maxUnit.
	for (int i=0; i<2; i++)
		for (int j=0; j<3; j++)
		{
			choosed[i][j].copyIf(limits[i][j], evolvable); 
			choosed[i][j].copyIfNot( (limits[0][j]+limits[1][j])/2 , evolvable);
		}
				
	// The weight is based on the medUnits.
	// medUnits = (minUnits+maxUnits)/2	
	UnitType middle[3];
	for (int j=0; j<3; j++)
		middle[j]=(choosed[0][j]+choosed[1][j])/2;
	
	int weight[3];
	for (int j=0; j<3; j++)
		weight[j]=middle[j]*costs[j];
	
	// BETA : we still don't know what's the best thing to do between:
	// 1) The 3 weight of the 3 units are separated.
	// 2) The 3 weight of the 3 unts are the sames.
	// 3) A linear interpolation of both.
	// Now we do a 50% interpolation of both.
	
	int averageWeight=0;
	for (int j=0; j<3; j++)
		averageWeight+=weight[j];
	averageWeight/=3;	
	for (int j=0; j<3; j++)
		weight[j]=(averageWeight+weight[j])/2;
			
		
	// Here we can add any linear or non-linear function.	
	int hungry[3];
	for (int j=0; j<3; j++)
		hungry[j]=weight[j]*1;	
	
	// we calculate a linear interpolation between the choosedMin and the choosedMax,
	// to create the 4 diferents levels of units.
	UnitType unitTypes[3][4];
	for (int j=0; j<3; j++)
		for (int i=0; i<4; i++)
		{
			unitTypes[j][i]=(choosed[0][j]*(3-i)+choosed[1][j]*(i))/3;
			unitTypes[j][i].hungryness=hungry[j];
	    }
		
	//we copy the hungryness to all 12 UnitType-s to the race.
	// TODO : delete his, it serves no purprose
	for (int j=0; j<3; j++)
		for (int i=0; i<4; i++)
			this->unitTypes[j][i]=unitTypes[j][i];
	
	
}

UnitType *Race::getUnitType(UnitType::TypeNum type, int level)
{
	int typeint=(int)type;
	assert (level>=0);
	assert (level<UnitType::NB_UNIT_LEVELS);
	assert (typeint>=0);
	assert (typeint<UnitType::NB_UNIT_TYPE);
	return &(unitTypes[typeint][level]);
}

void Race::save(SDL_RWops *stream)
{
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
		for(int j=0; j<UnitType::NB_UNIT_LEVELS; j++)
			unitTypes[i][j].save(stream);
}

void Race::load(SDL_RWops *stream)
{
	//printf("loading race\n");
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
		for(int j=0; j<UnitType::NB_UNIT_LEVELS; j++)
			unitTypes[i][j].load(stream);
}

/*
NOTE : never use this, it is depreciated, replaced by create
void Race::loadText(const char *filename)
{
	SDL_RWops *stream=SDL_RWFromFile(filename, "r");
	
    for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
		for(int j=0; j<UnitType::NB_UNIT_LEVELS; j++)
		{
			unitTypes[i][j].init();
			unitTypes[i][j].loadText(stream);
        }
	SDL_FreeRW(stream);
}
*/
