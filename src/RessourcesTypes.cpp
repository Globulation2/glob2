// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <assert.h>
#include <vector>
#include <iostream>

#include "RessourcesTypes.h"
#include "GlobalContainer.h"

Uint32 RessourcesTypes::checkSum(void)
{
	Uint32 cs = 0;
	
	for (std::vector <RessourceType *>::iterator it=entitiesTypes.begin(); it!=entitiesTypes.end(); ++it)
	{
		cs ^= (*it)->checkSum();
		cs = (cs<<1) | (cs>>31);
	}
	
	return cs;
}
