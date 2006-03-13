/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "UnitsSkins.h"
#include "UnitSkin.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <TextStream.h>
#include <iostream>

UnitsSkins::UnitsSkins()
{
	stream = new TextInputStream(Toolkit::getFileManager()->openInputStreamBackend("data/unitsSkins.txt"));
	if (stream->isEndOfStream())
	{
		std::cerr << "UnitsSkins::UnitsSkins() : error, can't open file data/unitsSkins.txt." << std::endl;
		delete stream;
		stream = NULL;
		abort();
		return;
	}
}

UnitsSkins::~UnitsSkins()
{
	for (std::map<std::string, UnitSkin *>::iterator it = unitsSkins.begin(); it != unitsSkins.end(); ++it)
		delete it->second;
	delete stream;
}

UnitSkin *UnitsSkins::getSkin(const std::string &name)
{
	std::map<std::string, UnitSkin *>::const_iterator it = unitsSkins.find(name);
	if (it != unitsSkins.end())
		return it->second;
	else
	{
		stream->readEnterSection(name.c_str());
		unitsSkins[name] = new UnitSkin(stream);
		stream->readLeaveSection();
		return unitsSkins[name];
	}
}
