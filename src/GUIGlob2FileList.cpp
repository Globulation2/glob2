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

#include "GUIGlob2FileList.h"
#include "Game.h"

Glob2FileList::Glob2FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font,
														 const char *dir,
														 const char *extension, const bool recurse)
	: FileList(x, y, w, h, hAlign, vAlign, font, dir, extension, recurse)
{
	this->generateList();
}

Glob2FileList::Glob2FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font,
														 const char *dir, const std::string& tooltip, const std::string &tooltipFont,
														 const char *extension, const bool recurse)
	: FileList(x, y, w, h, hAlign, vAlign, font, dir, tooltip, tooltipFont, extension, recurse)
{
	this->generateList();
}

Glob2FileList::~Glob2FileList()
{}

std::string Glob2FileList::fileToList(const char* fileName) const
{
	return glob2FilenameToName(fullName(fileName).c_str());
}

std::string Glob2FileList::listToFile(const char* listName) const
{
	return glob2NameToFilename(fullDir().c_str(), listName, extension.c_str());
}
