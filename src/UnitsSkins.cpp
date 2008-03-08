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

#include "UnitsSkins.h"
#include "UnitSkin.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <TextStream.h>
#include <GUIButton.h>
#include <iostream>

//! Constructor, open a stream from data/unitsSkins.txt
UnitsSkins::UnitsSkins()
{
	StreamBackend *backend = Toolkit::getFileManager()->openInputStreamBackend("data/unitsSkins.txt");
	stream = new TextInputStream(backend);
	delete backend;
	if (stream->isEndOfStream())
	{
		std::cerr << "UnitsSkins::UnitsSkins() : error, can't open file data/unitsSkins.txt." << std::endl;
		delete stream;
		abort();
		return;
	}
	
	// read all entries
	std::set<std::string> entries;
	stream->getSubSections("", &entries);
	
	for (std::set<std::string>::const_iterator it = entries.begin(); it != entries.end(); ++it)
	{
		const std::string &name = *it;
		UnitSkin *unitSkin = new UnitSkin;
		
		stream->readEnterSection(name.c_str());
		bool result = unitSkin->load(stream);
		stream->readLeaveSection();
		
		if (result)
			unitsSkins[name] = unitSkin;
		else
			delete unitSkin;
	}
	
	delete stream;
}

//! Destructor, close the stream from data/unitsSkins.txt
UnitsSkins::~UnitsSkins()
{
	for (std::map<std::string, UnitSkin *>::iterator it = unitsSkins.begin(); it != unitsSkins.end(); ++it)
		delete it->second;
	
}

//! Return the skin corresponding to name. If no such skin exist, return NULL
UnitSkin *UnitsSkins::getSkin(const std::string &name)
{
	std::map<std::string, UnitSkin *>::const_iterator it = unitsSkins.find(name);
	if (it != unitsSkins.end())
		return it->second;
	else
		return NULL;
	/*else
	{
		UnitSkin *unitSkin = new UnitSkin;
		stream->readEnterSection(name.c_str());
		bool result = unitSkin->load(stream);
		stream->readLeaveSection();
		if (result)
		{
			unitsSkins[name] = unitSkin;
			return unitSkin;
		}
		else
		{
			delete unitSkin;
			return NULL;
		}
	}*/
}

//! Fill target with the list of names of all available skins
void UnitsSkins::buildSkinsList(MultiTextButton *target) const
{
	assert(target);
	for (std::map<std::string, UnitSkin *>::const_iterator it = unitsSkins.begin(); it != unitsSkins.end(); ++it)
	{
		target->addText(it->first);
	}
}
