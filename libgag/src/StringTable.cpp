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

#include "StringTable.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "assert.h"

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

	if ((fp=Toolkit::getFileManager()->openFP(filename, "r"))==NULL)
	{
		return false;
	}
	else
	{
		actlang=0;
		// get length
		if (fgets(temp, 1024, fp)==NULL)
		{
			fclose(fp);
			return true;
		}
		line++;
		terminateLine(temp, 1024);
		numberoflanguages=atoi(temp);
		while (!feof(fp))
		{
			OneStringToken *myString;

			if (fgets(temp, 1024, fp)==NULL)
				break;
			line++;
			terminateLine(temp, 1024);
			if (temp[0]!='[')
				fprintf(stderr, "StringTable::load: warning, lookup entry at line %d lacks [ at start\n", line);
			myString=new OneStringToken(temp);

			for (int n=0; n<numberoflanguages; n++)
			{
				if (fgets(temp, 1024, fp)==NULL)
				{
					delete myString;
					goto doublebreak;
				}
				line++;
				terminateLine(temp, 1024);
				if (temp==NULL)
					myString->addData("");
				else
					myString->addData(temp);
			}
			strings.push_back(myString);
		}
		doublebreak:
		fclose(fp);

		for (std::vector<OneStringToken *>::iterator it=strings.begin(); it!=strings.end(); it++)
		{
			char *s=(*it)->name;
			int l=strlen(s);
			bool lcwp=false;
			int baseCountS=0;
			int baseCountD=0;
			for (int j=0; j<l; j++)
			{
				char c=s[j];
				if (lcwp && c!=' ' && c!='%')
				{
					if (c=='s')
						baseCountS++;
					else if (c=='d')
						baseCountD++;
					else
					{
						printf("text=(%s), %s\n", s, "Only %d and %s are supported in translations!");
						assert(false);
						return false;
					}
				}
				lcwp=(c=='%');
			}
			for (int i=0; i<(int)((*it)->data.size()); i++)
			{
				char *s=(*it)->data[i];
				int l=strlen(s);
				bool lcwp=false;
				int countS=0;
				int countD=0;
				for (int j=0; j<l; j++)
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
							printf("translation=(%s), %s\n", s, "Only %s and %d are supported in translations!");
							assert(false);
							return false;
						}
					}
					lcwp=(c=='%');
				}
				if (baseCountS!=countS ||baseCountD!=countD)
				{
					printf("text=[%d:%d](%s), translation=[%d:%d](%s), doesn\'t match!\n", baseCountS, baseCountD, (*it)->name, countS, countD, s);
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
	for (std::vector<OneStringToken *>::iterator it=strings.begin(); it!=strings.end(); it++)
	{
		printf("name=%s\n", (*it)->name);
		for (int i=0; i<(int)((*it)->data.size()); i++)
		{
			printf("trad[%d]=%s\n", i, (*it)->data[i]);
		}
	}
}

const char *StringTable::getString(const char *stringname) const
{
	if (actlang<numberoflanguages)
	{
		for (std::vector<OneStringToken *>::const_iterator it=strings.begin(); it!=strings.end(); it++)
		{
			if (strcmp(stringname, (*it)->name)==0)
			{
				const char *s=(*it)->data[actlang];
				assert(s);
				if (s[0]==0)
					s=(*it)->data[0];
				
				assert(s);
				return s;
			}
		}
		return "ERROR : NO STRING";
	}
	else
	{
		return "ERROR, BAD LANG";
	}
}

const char *StringTable::getStringInLang(const char *stringname, int lang) const
{
	if ((lang<numberoflanguages) && (lang>=0))
	{
		for (std::vector<OneStringToken *>::const_iterator it=strings.begin(); it!=strings.end(); it++)
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

const char *StringTable::getString(const char *stringname, int index) const
{
	if (actlang<numberoflanguages)
	{
		for (int i=0; i<(int)(strings.size()); i++)
		{
			if (strcmp(stringname, strings[i]->name)==0)
			{
				OneStringToken *token=strings[i+index+1];
				assert(token);
				
				const char *s=token->data[actlang];
				assert(s);
				
				if (s[0]==0)
					s=token->data[0];
					
				assert(s);
				return s;
			}
		}
		return "ERROR : NO STRING";
	}
	else
	{
		return "ERROR, BAD LANG";
	}
}

