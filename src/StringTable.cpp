/*
 * Globulation 2 String Table support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */


#include "StringTable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef win32
#include <malloc.h>
#endif

OneStringToken::OneStringToken(const char *name)
{
	this->name=(char *)malloc(strlen(name)+1);
	strcpy(this->name, name);
}

OneStringToken::~OneStringToken()
{
	free (name);
	for (std::vector<char *>::iterator it=data.begin(); it!=data.end(); it++)
		free (*it);
}

void OneStringToken::addData(char *data)
{
	char *temp=(char *)malloc(strlen(data)+1);
	strcpy(temp, data);
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

	if ((fp=fopen(filename,"r"))==NULL)
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
				{
					fclose(fp);
					delete myString;
					return true;
				}
				if (tempp[0]==' ')
					tempp++;
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

