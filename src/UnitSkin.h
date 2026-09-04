// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __UNITSKIN_H
#define __UNITSKIN_H

#include <GAGSys.h>
#include "UnitConsts.h"

namespace GAGCore
{
	class InputStream;
	class Sprite;
}
using namespace GAGCore;

class UnitSkin
{
public:
	Sprite *sprite;
	Uint32 startImage[NB_MOVE];
	
public:
	bool load(GAGCore::InputStream *stream);
};

#endif
