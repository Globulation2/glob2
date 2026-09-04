// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __RESSOURCES_TYPES_H
#define __RESSOURCES_TYPES_H

#include "EntitiesTypes.h"
#include "RessourceType.h"

class RessourcesTypes: public EntitiesTypes<RessourceType>
{
	public:
		Uint32 checkSum(void);
};

#endif
