// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "UnitSkin.h"
#include <Stream.h>
#include <Toolkit.h>
#include <iostream>

bool UnitSkin::load(GAGCore::InputStream *stream)
{
	std::string spriteName = stream->readText("spriteName");
	if (spriteName == "")
		return false;
		
	sprite = Toolkit::getSprite(spriteName);
	if (!sprite)
	{
		std::cerr << "Can't load unit sprite " << spriteName << ", abording" << std::endl;
		return false;
	}
	startImage[STOP_WALK] = stream->readUint32("startImageStopWalk");
	startImage[STOP_SWIM] = stream->readUint32("startImageStopSwim");
	startImage[STOP_FLY] = stream->readUint32("startImageStopFly");
	startImage[WALK] = stream->readUint32("startImageWalk");
	startImage[SWIM] = stream->readUint32("startImageSwim");
	startImage[FLY] = stream->readUint32("startImageFly");
	startImage[BUILD] = stream->readUint32("startImageBuild");
	startImage[HARVEST] = stream->readUint32("startImageHarvest");
	startImage[ATTACK_SPEED] = stream->readUint32("startImageAttack");
	
	return true;
}
