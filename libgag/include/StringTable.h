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

#ifndef __STRINGTABLE_H
#define __STRINGTABLE_H

#include <vector>
#include <map>
#include <string>

namespace GAGCore
{
	class OneStringToken
	{
	public:
		std::vector<std::string> data;
	};
	
	class StringTable
	{
	public:
		StringTable();
		~StringTable();
		void setLang(int l) { actLang = l; }
		void setDefaultLang(int l) { defaultLang = l; }
		int getLang(void) { return actLang; }
		bool isLangComplete(int l) { return !incomplete[l]; }
		int getNumberOfLanguage(void) { return languageCount; }
		bool loadIncompleteList(const char *filename);
		bool load(const char *filename);
		const char *getString(const char *stringname) const;
		bool doesStringExist(const char *stringname) const;
		const char *getStringInLang(const char *stringname, int lang) const;
		void print();
	
	private:
		std::vector<OneStringToken *> strings;
		std::map<std::string, size_t> stringAccess;
		int actLang;
		int defaultLang;
		int languageCount;
		std::vector<bool> incomplete;
		
	public:
		enum {AI_NAME_SIZE=4};
	};

}

#endif

