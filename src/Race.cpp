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
	
	for (int j2=0; j2<3; j2++)
	{
		int offset=SDL_RWtell(stream);
		limits[0][j2].loadText(stream);
		
		SDL_RWseek(stream, offset, SEEK_SET);
		limits[1][j2].loadText(stream);
		//limits[1][j2] = limits[0][j2]; // TODO : seek !! zzz
		
		limits[1][j2].loadText(stream);
		costs[j2].loadText(stream);	
	}
		
	SDL_RWclose(stream);
	
	// Now we custom our race, by a minLevel and a maxLevel.
	UnitType choosed[2][3];
	
	// Here we should popup a big dialog window toallow the player to custom his own race.
	// The evolvable parabetter will have a range, and the other only a point.
	// But now we only create a default race.
	// Our default units will be based on the minUnit and maxUnit.
	for (int i2=0; i2<2; i2++)
	{
		for (int j2=0; j2<3; j2++)
		{
			choosed[i2][j2].copyIf(limits[i2][j2], evolvable); 
			choosed[i2][j2].copyIfNot( (limits[0][j2]+limits[1][j2])/2 , evolvable);
		}
	}
				
	// The weight is based on the medUnits.
	// medUnits = (minUnits+maxUnits)/2	
	UnitType middle[3];
	for (int j3=0; j3<3; j3++)
		middle[j3]=(choosed[0][j3]+choosed[1][j3])/2;
	
	int weight[3];
	for (int j4=0; j4<3; j4++)
		weight[j4]=middle[j4]*costs[j4];
	
	// BETA : we still don't know what's the best thing to do between:
	// 1) The 3 weight of the 3 units are separated.
	// 2) The 3 weight of the 3 unts are the sames.
	// 3) A linear interpolation of both.
	// Now we do a 50% interpolation of both.
	
	int averageWeight=0;
	for (int j5=0; j5<3; j5++)
		averageWeight+=weight[j5];
	averageWeight/=3;	
	for (int j6=0; j6<3; j6++)
		weight[j6]=(averageWeight+weight[j6])/2;
			
		
	// Here we can add any linear or non-linear function.	
	int hungry[3];
	for (int j7=0; j7<3; j7++)
		hungry[j7]=weight[j7]*1;	
	
	// we calculate a linear interpolation between the choosedMin and the choosedMax,
	// to create the 4 diferents levels of units.
	UnitType unitTypes[3][4];
	for (int j8=0; j8<3; j8++)
	{
		for (int i3=0; i3<4; i3++)
		{
			unitTypes[j8][i3]=(choosed[0][j8]*(3-i3)+choosed[1][j8]*(i3))/3;
			unitTypes[j8][i3].hungryness=hungry[j8];
	    }
	}
		
	//we copy the hungryness to all 12 UnitType-s to the race.
	// TODO : delete his, it serves no purprose
	for (int j9=0; j9<3; j9++)
		for (int i4=0; i4<4; i4++)
			this->unitTypes[j9][i4]=unitTypes[j9][i4];
	
	
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
