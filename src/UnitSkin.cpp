/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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
