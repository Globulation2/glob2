// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GUIGlob2FileList.h"
#include "Game.h"

Glob2FileList::Glob2FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font,
														 const std::string dir,
														 const std::string extension, const bool recurse)
	: FileList(x, y, w, h, hAlign, vAlign, font, dir, extension, recurse)
{
	this->generateList();
}

Glob2FileList::Glob2FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font,
														 const std::string dir, const std::string& tooltip, const std::string &tooltipFont,
														 const std::string extension, const bool recurse)
	: FileList(x, y, w, h, hAlign, vAlign, font, dir, tooltip, tooltipFont, extension, recurse)
{
	this->generateList();
}

Glob2FileList::~Glob2FileList()
{}

std::string Glob2FileList::fileToList(const std::string fileName) const
{
	return glob2FilenameToName(fullName(fileName));
}

std::string Glob2FileList::listToFile(const std::string listName) const
{
	return glob2NameToFilename(fullDir(), listName, extension);
}
