/*
 * Globulation 2 Global Artwork Container
 * (contains all static stuff that should be loaded on startup)
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GLOBALCONTAINER_H
#define __GLOBALCONTAINER_H

#include "GAG.h"
#include "StringTable.h"
#include "FileManager.h"

class GlobalContainer
{
public:
	GlobalContainer(void);

	void parseArgs(int argc, char *argv[]);
	
	char safer0[1024];
	Uint32 graphicFlags;
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
	Palette ShadedPal;
	char safer7[1024];

	SDLGraphicContext gfx;
	char safer8[1024];
	StringTable texts;
	FileManager fileManager;

	char safer9[1024];
	SDLBitmapFont menuFont;
};

extern GlobalContainer *globalContainer;

#endif 
