/*
 * GAG sprite support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __SPRITE_H
#define __SPRITE_H

#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include "Header.h"

// class for handling color lookup
class Palette
{
public:
	void load(SDL_RWops *input, SDL_PixelFormat *format);
	void decHue(float degree);
	Uint32 colors[256];
	Uint8 R[256];
	Uint8 G[256];
	Uint8 B[256];
public:
	static void RGBtoHSV( float r, float g, float b, float *h, float *s, float *v );
	static void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v );
private:
	static float fmin(float f1, float f2, float f3);
	static float fmax(float f1, float f2, float f3);
	SDL_PixelFormat *format;
};

// virtual class for handling Sprites
class Sprite
{
public:
	virtual ~Sprite() { }
	virtual void load(SDL_RWops *input)=0;
	virtual void save(SDL_RWops *input)=0;
	virtual void draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y)=0;
	virtual void enableColorKey(Uint8 key)=0;
	virtual void disableColorKey(void)=0;
	virtual int getW(void)=0;
	virtual int getH(void)=0;
};

// class for handling palettized Sprite (like Units)
class PalSprite: public Sprite
{
public:
	PalSprite() { isColorKey=false; data=NULL; }
	virtual ~PalSprite() { if (data) free(data); }
	void load(SDL_RWops *input);
	void save(SDL_RWops *input);
	void draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y);
	void enableColorKey(Uint8 key);
	void disableColorKey(void);
	int getW(void) { return w; }
	int getH(void) { return h; }
	void setPal(const Palette *pal) { this->pal=pal; }

protected:
	int w,h;
	Uint8 key;
	bool isColorKey;
	Uint8 *data;
	const Palette *pal;
};

// class for handling standard Sprite (like building, etc...)
class IMGSprite:public Sprite
{
public:
	IMGSprite() { sprite=NULL; }
	virtual ~IMGSprite() { if (sprite) SDL_FreeSurface(sprite); }
	void load(SDL_RWops *input);
	void save(SDL_RWops *input) { }
	void enableColorKey(Uint8 key);
	void disableColorKey(void);
	void draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y);
	int getW(void) { return sprite->w; }
	int getH(void) { return sprite->h; }

protected:
	SDL_Surface *sprite;
};

class GraphicArchive
{
public:
	GraphicArchive();
	virtual ~GraphicArchive() { freeMe(); }

	void load(const char *filename);
	void save(const char *filename);
	Sprite *getSprite(int n);
	int getSize(void) { return sprites.size(); }
	virtual void freeMe(void);
	void enableColorKey(Uint8 key);
	void disableColorKey(void);

	virtual Sprite *loadSprite(SDL_RWops *stream)=0;

protected:
	std::vector<Sprite *> sprites;
};

class MacPalGraphicArchive:public GraphicArchive
{
public:
	MacPalGraphicArchive() { }
	MacPalGraphicArchive(const char *filename) { load(filename); }
	virtual ~MacPalGraphicArchive() { }

	Sprite *loadSprite(SDL_RWops *stream);
	void setDefaultPal(const Palette *pal) { defaultPal=pal; }

protected:
	const Palette *defaultPal;
};

class IMGGraphicArchive:public GraphicArchive
{
public:
	IMGGraphicArchive(const char *filename) { load(filename); }
	virtual ~IMGGraphicArchive() { }

	Sprite *loadSprite(SDL_RWops *stream);
};

#endif
 
