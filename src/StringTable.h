/*
 * Globulation 2 String Table support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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
	bool load(char *filename);
	char *getString(const char *stringname);
	char *getString(const char *stringname, int index);
	void print();

private:
	std::vector<OneStringToken *> strings;
	int actlang;
	int numberoflanguages;
};

#endif

