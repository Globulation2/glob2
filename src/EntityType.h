/*
* Globulation 2 meta type
* (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
*/

#ifndef __ENTITYTYPE_H
#define __ENTITYTYPE_H

#include "GAG.h"

class EntityType
{
public:
	EntityType();
	EntityType(SDL_RWops *stream);
	virtual ~EntityType() { }
	virtual const char **getVars(int *size, Uint32 **data)=0;
	virtual void init(void);	
	virtual void load(SDL_RWops *stream);
	virtual bool loadText(SDL_RWops *stream);
	virtual void save(SDL_RWops *stream);
	virtual void dump(void);
private:
	char *gets(char *dest, int size, SDL_RWops *stream);
};

#endif
 
