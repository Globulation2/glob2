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

#include "StringTable.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "assert.h"
#include <iostream>

StringTable::StringTable()
{
	languageCount = 0;
	actLang = 0;
	defaultLang = 0;
}


StringTable::~StringTable()
{
	for (size_t i=0; i<strings.size(); i++)
		delete strings[i];
}


/* file format is :

		number of language
		name of string
		string lang 1
		.
		.
		.
		string lang n


	And fist space of each line is removed, to allow emptih strings.

*/

inline void terminateLine(char *s, int n)
{
	for (int i=0; i<n; i++)
	{
		char c=s[i];
		if (c=='\n')
		{
			s[i]=0;
			return;
		}
		else if (c=='\r')
		{
			s[i]=0;
			return;
		}
		else if (c==0)
			return;
	}
}

bool StringTable::load(char *filename)
{
	FILE *fp;
	char temp[1024];
	unsigned line=0;

	if ((fp = Toolkit::getFileManager()->openFP(filename, "r"))==NULL)
	{
		return false;
	}
	else
	{
		actLang = 0;
		
		// get number of language
		if (fgets(temp, 1024, fp)==NULL)
		{
			fclose(fp);
			return true;
		}
		line++;
		terminateLine(temp, 1024);
		languageCount = atoi(temp);
		actLang = std::min(actLang, languageCount);
		defaultLang = std::min(defaultLang, languageCount);
		
		// read entries
		while (!feof(fp))
		{
			OneStringToken *myString;
			std::string key;

			// read from file and check format
			if (fgets(temp, 1024, fp)==NULL)
				break;
			line++;
			terminateLine(temp, 1024);
			if (temp[0] != '[')
				std::cerr << "StringTable::load: warning, lookup entry at line " << line << " lacks [ at start" << std::endl;
			key = temp;
			myString = new OneStringToken();

			// read all strings
			for (int n=0; n<languageCount; n++)
			{
				if (fgets(temp, 1024, fp)==NULL)
				{
					delete myString;
					goto doublebreak;
				}
				line++;
				terminateLine(temp, 1024);
				if (temp==NULL)
					myString->data.push_back("");
				else
					myString->data.push_back(temp);
			}
			stringAccess[key] = strings.size();
			strings.push_back(myString);
		}
		doublebreak:
		fclose(fp);

		for (std::map<std::string, size_t>::iterator it=stringAccess.begin(); it!=stringAccess.end(); ++it)
		{
			bool lcwp=false;
			int baseCountS=0;
			int baseCountD=0;
			const std::string &s = it->first;
			for (size_t j=0; j<s.length(); j++)
			{
				char c = s[j];
				if (lcwp && c!=' ' && c!='%')
				{
					if (c=='s')
						baseCountS++;
					else if (c=='d')
						baseCountD++;
					else
					{
						std::cerr << "text=(" << s << "), Only %d and %s are supported in translations !" << std::endl;
						assert(false);
						return false;
					}
				}
				lcwp=(c=='%');
			}
			for (size_t i=0; i<strings[it->second]->data.size(); i++)
			{
				const std::string &s = strings[it->second]->data[i];
				bool lcwp=false;
				int countS=0;
				int countD=0;
				for (size_t j=0; j<s.length(); j++)
				{
					char c=s[j];
					if (lcwp && c!=' ' && c!='%')
					{
						if (c=='s')
							countS++;
						else if (c=='d')
							countD++;
						else
						{
							std::cerr << "translation=(" << s << "Only %s and %d are supported in translations !" << std::endl;
							assert(false);
							return false;
						}
					}
					lcwp=(c=='%');
				}
				if (baseCountS!=countS ||baseCountD!=countD)
				{
					std::cerr << "text=]" << baseCountS << ":" << baseCountD << "](" << it->first << "), translation=[" << countS << ":" << countD << "](" << s << "), doesn't match !" << std::endl;
					assert(false);
					return false;
				}
			}
		}
		
		return true;
	}
}


void StringTable::print()
{
	for (std::map<std::string, size_t>::iterator it=stringAccess.begin(); it!=stringAccess.end(); ++it)
	{
		std::cout << "name=" << it->first << "\n";
		for (size_t i=0; i<strings[it->second]->data.size(); i++)
			std::cout<< "trad[" << i << "]=" << strings[it->second]->data[i];
		std::cout << std::endl;
	}
}

const char *StringTable::getString(const char *stringname, int index) const
{
	if ((actLang < languageCount) && (actLang >= 0))
	{
		std::string key(stringname);
		std::map<std::string, size_t>::const_iterator accessIt = stringAccess.find(key);
		if (accessIt == stringAccess.end())
		{
			return "ERROR : NO STRING";
		}
		else
		{
			int dec = index >= 0 ? index + 1 : 0;
			if (accessIt->second+dec >= strings.size())
				return "ERROR : INDEX OUT OF BOUND";
			std::string &s = strings[accessIt->second+dec]->data[actLang];
			if (s.length() == 0)
				return strings[accessIt->second+dec]->data[defaultLang].c_str();
			else
				return s.c_str();
		}
	}
	else
	{
		return "ERROR, BAD LANG";
	}
}

const char *StringTable::getStringInLang(const char *stringname, int lang) const
{
	if ((lang < languageCount) && (lang >= 0))
	{
		std::string key(stringname);
		std::map<std::string, size_t>::const_iterator accessIt = stringAccess.find(key);
		if (accessIt == stringAccess.end())
		{
			return "ERROR : NO STRING";
		}
		else
		{
			return strings[accessIt->second]->data[lang].c_str();
		}
	}
	else
	{
		return "ERROR, BAD LANG ID";
	}
}

