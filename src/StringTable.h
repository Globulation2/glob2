/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __STRINGTABLE_H
#define __STRINGTABLE_H

#include <vector>

class OneStringToken
{
public:
	OneStringToken(const char *name);
	~OneStringToken();
	void addData(char *data);
	
public:
	char *name;
	std::vector<char *> data;
};

class StringTable
{
public:
	StringTable();
	~StringTable();
	void setLang(int l) { actlang=l; }
	int getLang(void) { return actlang; }
	int getNumberOfLanguage(void) { return numberoflanguages; }
	bool load(char *filename);
	const char *getString(const char *stringname);
	const char *getString(const char *stringname, int index);
	const char *getStringInLang(const char *stringname, int lang);
	void print();

private:
	std::vector<OneStringToken *> strings;
	int actlang;
	int numberoflanguages;
	
public:
	enum {AI_NAME_SIZE=4};
};

#endif

