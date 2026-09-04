// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __BULDING_TYPES_H
#define __BULDING_TYPES_H

#include "BuildingType.h"

class BuildingsTypes: public ConfigVector<BuildingType>
{
protected:
	void resolveUpgradeReferences(void);
	void checkIntegrity(void);

public:
	virtual void load();
	virtual ~BuildingsTypes() { }

	Sint32 getTypeNum(const char *type, int level, bool isBuildingSite);
	Sint32 getTypeNum(const std::string &s, int level, bool isBuildingSite);
	BuildingType *getByType(const char *type, int level, bool isBuildingSite);
	BuildingType *getByType(const std::string &s, int level, bool isBuildingSite);
	
	Uint32 checkSum(void);
};

#endif
 
