/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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
#include "GlobalContainer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef win32
#include <malloc.h>
#endif

OneStringToken::OneStringToken(const char *name)
{
	int len=strlen(name)+1;
	this->name=(char *)malloc(len);
	strncpy(this->name, name, len);
}

OneStringToken::~OneStringToken()
{
	free (name);
	for (std::vector<char *>::iterator it=data.begin(); it!=data.end(); it++)
		free (*it);
}

void OneStringToken::addData(char *data)
{
	int len=strlen(data)+1;
	char *temp=(char *)malloc(len);
	strncpy(temp, data, len);
	this->data.push_back(temp);
}


StringTable::StringTable()
{
	numberoflanguages=0;
}


StringTable::~StringTable()
{
	for (std::vector<OneStringToken *>::iterator it=strings.begin(); it!=strings.end(); it++)
		delete (*it);
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

bool StringTable::load(char *filename)
{
	FILE *fp;
	char temp[1024];
	char* tempp;
	int n;

	if ((fp=globalContainer->fileManager->openFP(filename,"r"))==NULL)
	{
		return false;
	}
	else
	{
		actlang=0;
		// get length
		fgets(temp,1024,fp);
		numberoflanguages=atoi(strtok(temp," \r\n"));
		while (!feof(fp))
		{
			OneStringToken *myString;
			
			if (fgets(temp,1024,fp)==NULL)
			{
				fclose(fp);
				return true;
			}
			tempp=strtok(temp,"\r\n");
			if (tempp==NULL)
			{
				fclose(fp);
				return true;
			}
			myString=new OneStringToken(tempp);
			
			for (n=0; n<numberoflanguages; n++)
			{
				if (fgets(temp,1024,fp)==NULL)
				{
					fclose(fp);
					delete myString;
					return true;
				}
				tempp=strtok(temp,"\r\n");
				if (tempp==NULL)
					myString->addData("");
				else
					myString->addData(tempp);
			}
			strings.push_back(myString);
		}
		fclose(fp);
		return true;
	}
}


void StringTable::print()
{
	for (std::vector<OneStringToken *>::iterator it=strings.begin(); it!=strings.end(); it++)
	{
		printf("name=%s\n", (*it)->name);
		for (int i=0; i<(int)((*it)->data.size()); i++)
		{
			printf("trad[%d]=%s\n", i, (*it)->data[i]);
		}
	}
}

char *StringTable::getString(const char *stringname)
{
	if (actlang<numberoflanguages)
	{
		for (std::vector<OneStringToken *>::iterator it=strings.begin(); it!=strings.end(); it++)
		{
			if (strcmp(stringname, (*it)->name)==0)
			{
				return ((*it)->data[actlang]);
			}
		}
		return "ERROR : NO STRING";
	}
	else
	{
		return "ERROR, BAD LANG";
	}
}

char *StringTable::getStringInLang(const char *stringname, int lang)
{
	if ((lang<numberoflanguages) && (lang>=0))
	{
		for (std::vector<OneStringToken *>::iterator it=strings.begin(); it!=strings.end(); it++)
		{
			if (strcmp(stringname, (*it)->name)==0)
			{
				return ((*it)->data[lang]);
			}
		}
		return "ERROR : NO STRING";
	}
	else
	{
		return "ERROR, BAD LANG ID";
	}
}

char *StringTable::getString(const char *stringname, int index)
{
	if (actlang<numberoflanguages)
	{
		for (int i=0; i<(int)(strings.size()); i++)
		{
			if (strcmp(stringname, strings[i]->name)==0)
			{
				return (strings[i+index+1]->data[actlang]);
			}
		}
		return "ERROR : NO STRING";
	}
	else
	{
		return "ERROR, BAD LANG";
	}
}

