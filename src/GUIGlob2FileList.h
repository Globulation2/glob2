// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GUIFileList.h>
using namespace GAGGUI;

class Glob2FileList: public FileList
{
public:
	Glob2FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, 
								const std::string dir,
								const std::string extension="", const bool recurse=false);

	Glob2FileList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, 
								const std::string dir, const std::string& tooltip, const std::string &tooltipFont,
								const std::string extension="", const bool recurse=false);
	virtual ~Glob2FileList();

	//! converts glob2 file name to displayed name
	virtual std::string fileToList(const std::string fileName) const;
	//! converts displayed name to glob2 file name
	virtual std::string listToFile(const std::string listName) const;

};
