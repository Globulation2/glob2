// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
	TextInputStream *stream = new TextInputStream(backend);
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
