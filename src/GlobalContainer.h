/*
 * Globulation 2 Global Artwork Container
 * (contains all static stuff that should be loaded on startup)
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GLOBALCONTAINER_H
#define __GLOBALCONTAINER_H

#include "GAG.h"
#include "StringTable.h"

class GlobalContainer
{
public:
	GlobalContainer(void);
	
	char safer0[1024];
	char safer1[1024];
	
	void load(void);
	bool safe(void);
	
	char safer2[1024];
	MacPalGraphicArchive terrain;
	char safer3[1024];
	MacPalGraphicArchive ressources;
	char safer4[1024];
	MacPalGraphicArchive units;
	char safer5[1024];
	MacPalGraphicArchive buildings;
	char safer6[1024];
	Palette macPal;
	char safer7[1024];

	SDLGraphicContext gfx;
	char safer8[1024];
	StringTable texts;
	
	char safer9[1024];
};

#endif 
