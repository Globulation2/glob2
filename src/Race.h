/*
 * Globulation 2 race support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __RACE_H
#define __RACE_H

#include "GAG.h"
#include "UnitType.h"

class Race
{
public:
	enum CreationType
	{
		USE_DEFAULT,
		USE_GUI,
		USE_AI
	};

	UnitType unitTypes[UnitType::NB_UNIT_TYPE][UnitType::NB_UNIT_LEVELS];

public:
	virtual ~Race();
	
	void create(CreationType creationType);
	UnitType *getUnitType(UnitType::TypeNum type, int level);
	
	void save(SDL_RWops *stream);
	void load(SDL_RWops *stream);
};

#endif
 
