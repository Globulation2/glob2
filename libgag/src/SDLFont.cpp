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

#include "SDLFont.h"
#include <Toolkit.h>
#include <SupportFunctions.h>
#include <FileManager.h>
#include <assert.h>
#include <SDL/SDL_image.h>

SDLBitmapFont::SDLBitmapFont()
{
	picture=NULL;
	CharPos=NULL;
}

SDLBitmapFont::SDLBitmapFont(const char *filename)
{
	load(filename);
}

SDLBitmapFont::~SDLBitmapFont()
{
	if (picture)
		SDL_FreeSurface(picture);
	if (CharPos)
		delete[] CharPos;
}

int SDLBitmapFont::getStringWidth(const char *string) const
{
	return textWidth(string);
}


int SDLBitmapFont::getStringHeight(const char *string) const
{
	return height;
}

bool SDLBitmapFont::load(const char *filename)
{
	init();

	SDL_Surface *temp, *sprite;
	printf("%p\n", Toolkit::getFileManager());
	SDL_RWops *stream=Toolkit::getFileManager()->open(filename, "rb");
	if (!stream)
		return false;
	temp=IMG_Load_RW(stream, 0);
	SDL_RWclose(stream);
	sprite=SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	return load(sprite);
}

void SDLBitmapFont::drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip) const
{
	if ((!picture) || (!Surface) || (!text))
		return;
	int ofs, i=0, bx=x;
	SDL_Rect srcrect, dstrect;
	while ((text[i]!='\0') && ((w==0) || (x-bx<w)))
	{
		if (text[i]==' ')
		{
			x+=spacew;
			i++;
		}
		else if ((text[i]>=startChar)&&(text[i]<=lastChar))
		{
			ofs=2*(text[i]-startChar);
			srcrect.w = dstrect.w = (Uint16) ( (CharPos[ofs+3]+CharPos[ofs+2])/2-(CharPos[ofs+1]+CharPos[ofs+0])/2 );
			srcrect.h = dstrect.h = (Uint16) height;
			srcrect.x = (Sint16) ( (CharPos[ofs+1]+CharPos[ofs+0])/2 );
			srcrect.y = 1;
			dstrect.x = (Sint16) ( x-(CharPos[ofs+1]-CharPos[ofs+0])/2 );
			dstrect.y = (Sint16) y;
			x+=CharPos[ofs+2]-CharPos[ofs+1];
			if ((w!=0) && (x-bx>=w))
				return;
			if (clip)
				GAG::sdcRects(&srcrect, &dstrect, *clip);
			SDL_BlitSurface( picture, &srcrect, Surface, &dstrect);
			i++;
		}
		else
			i++;// other chars are ignored
	}
}

void SDLBitmapFont::init()
{
	picture=NULL;
	CharPos=NULL;
	lastChar=startChar-1;
	height=0;
	spacew=0;
	backgroundR=0;
	backgroundG=0;
	backgroundB=0;
}

void SDLBitmapFont::getPixel(Sint32 x, Sint32 y, Uint8 *r, Uint8 *g, Uint8 *b)
{
	if ((!picture)||(x<0)||(x>=picture->w))
	{
    	fprintf(stderr, "GAG : SDLBitmapFontGetPixel recieved a bad parameter.\n");
    	assert(false);
    	return;
	}

	int bpp = picture->format->BytesPerPixel;

	SDL_LockSurface(picture);

	Uint8 *row = (Uint8 *)picture->pixels + y*picture->pitch + x*picture->format->BytesPerPixel;

	Uint32 pixel=0;

	switch (bpp)
	{
		case 1:
		{
			Uint8 *cp = (Uint8 *)row;
			pixel = (Uint32)*cp;
		}
		break;

		case 2:
		{
			Uint16 *cp = (Uint16 *)row;
			pixel = (Uint32)*cp;
		}
		break;

		case 3:
		{
			Uint32 *cp = (Uint32 *)row;
			pixel = (Uint32)*cp;
			if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
				pixel >>= 8;
		}
		break;

		case 4:
		{
			Uint32 *cp = (Uint32 *)row;
			pixel = (Uint32)*cp;
		}
		break;
	}
	SDL_UnlockSurface(picture);

	SDL_GetRGB(pixel, picture->format, r, g, b);
}
void SDLBitmapFont::setBackGround(Sint32 x, Sint32 y)
{
	getPixel(x, y, &backgroundR, &backgroundG, &backgroundB);
	Uint32 background=SDL_MapRGB(picture->format, backgroundR, backgroundG, backgroundB);
	SDL_SetColorKey(picture, SDL_SRCCOLORKEY, background);
}
bool SDLBitmapFont::isBackGround(Sint32 x, Sint32 y)
{
	Uint8 r, g, b;
	getPixel(x, y, &r, &g, &b);
	return ((r==backgroundR)&&(g==backgroundG)&&(b==backgroundB));
}

bool SDLBitmapFont::doStartNewChar(int x)
{
	if (!picture)
		return false;

	Uint8 r, g, b;
	getPixel(x, 0, &r, &g, &b);

	return ((r==255)&&(g==0)&&(b=255));
}

int SDLBitmapFont::shorteringChar(int x)
{
	//This do count the number of (255,255,0) pixels, starting from x.
	if (!picture)
		return false;

	int s=0;

	Uint8 r, g, b;
	getPixel((x+s), 0, &r, &g, &b);


	while ((r==255)&&(g==255)&&(b==0))
	{
		s++;
		getPixel((x+s), 0, &r, &g, &b);
	}
	return s;
}


bool SDLBitmapFont::load(SDL_Surface *fontSurface)
{
	int x=0, i=0;

	int CharPos[256];

	if (!fontSurface)
	{
    	fprintf(stderr, "GAG : SDLBitmapFont received a NULL SDL_Surface\n");
    	assert(false);
    	return false;
    }
    picture=fontSurface;
    height=picture->h-1;
	while (x < picture->w)
	{
		if(doStartNewChar(x))
		{
			CharPos[i++]=x;
			while (( x < picture->w-1) && (doStartNewChar(x)))
				x++;

			int s=shorteringChar(x);
			CharPos[i++]=x-s;

			//printf("CharPos[%d]=%d, CharPos[%d]=%d\n", i-2, CharPos[i-2], i-1, CharPos[i-1]);
		}
		x++;
	}
	CharPos[i++]=picture->w;

	lastChar=startChar+(i/2)-1;
	setBackGround(0, height);


	this->CharPos=new int[i];
	memcpy(this->CharPos, CharPos, i*sizeof(int));

	//We search for a smart space width:
	spacew=0;
	if (!spacew)
		spacew=textWidth("a");
	if (!spacew)
		spacew=textWidth("A");
	if (!spacew)
		spacew=textWidth("0");
	if (!spacew)
		spacew=CharPos[1]-CharPos[0];

	return true;
}

int SDLBitmapFont::textWidth(const char *text, int min, int max) const
{
	if (!picture)
		return 0;
	int ofs, x=0,i=min;
	while ((text[i]!='\0')&&(i<max))
	{
		if (text[i]==' ')
		{
			x+=spacew;
			i++;
		}
		else if ((text[i]>=startChar)&&(text[i]<=lastChar))
		{
			ofs=2*(text[i]-startChar);
			x+=CharPos[ofs+2]-CharPos[ofs+1];
			i++;
		}
		else
			i++;
	}
	return x;
}

bool SDLBitmapFont::printable(char c) const
{
	//printf("startChar=%d, lastChar=%d, ' '=%d \n", startChar, lastChar, ' ');
	return (((c>=startChar)&&(c<=lastChar)) || (c==' '));
}


SDLTTFont::SDLTTFont()
{
	font = NULL;
	
}

SDLTTFont::SDLTTFont(const char *filename, unsigned size)
{
	load(filename, size);
}

SDLTTFont::~SDLTTFont()
{
	if (font)
		TTF_CloseFont(font);
		
}

bool SDLTTFont::load(const char *filename, unsigned size)
{
	SDL_RWops *fontStream = Toolkit::getFileManager()->open(filename, "rb", false);
	if (fontStream)
	{
		font = TTF_OpenFontRW(fontStream, 1, size);
		if (font)
		{
			setColor(0, 0, 0, DrawableSurface::ALPHA_OPAQUE);
			setStyle(Font::STYLE_NORMAL);
			return true;
		}
	}
	return false;
}

int SDLTTFont::getStringWidth(const char *string) const
{
	int w, h;
	TTF_SizeUTF8(font, string, &w, &h);
	return w;
}

int SDLTTFont::getStringHeight(const char *string) const
{
	if (string)
	{
		int w, h;
		TTF_SizeUTF8(font, string, &w, &h);
		return h;
	}
	else
	{
		return TTF_FontHeight(font);
	}
}

bool SDLTTFont::printable(char c) const
{
	return (c>=32);
}

void SDLTTFont::setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	assert(font);
	
	while (colorStack.size() > 0)
		colorStack.pop();
	
	pushColor(r, g, b, a);
}

void SDLTTFont::pushColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	assert(font);
	
	SDL_Color c;
	c.r = r;
	c.g = g;
	c.b = b;
	c.unused = a;
	colorStack.push(c);
}

void SDLTTFont::popColor(void)
{
	assert(font);
	
	if (colorStack.size() > 1)
		colorStack.pop();
}

void SDLTTFont::getColor(Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) const
{
	assert(font);
	
	SDL_Color c;
	c = colorStack.top();
	*r = c.r;
	*g = c.g;
	*b = c.b;
	*a = c.unused;
}

void SDLTTFont::setStyle(unsigned style)
{
	assert(font);
	
	while (styleStack.size() > 0)
		styleStack.pop();
		
	pushStyle(style);
}

void SDLTTFont::pushStyle(unsigned style)
{
	assert(font);
	
	styleStack.push(style);
	TTF_SetFontStyle(font, style);
}

void SDLTTFont::popStyle(void)
{
	assert(font);
	
	if (styleStack.size() > 1)
	{
		styleStack.pop();
		TTF_SetFontStyle(font, styleStack.top());
	}
}

unsigned SDLTTFont::getStyle(void) const
{
	assert(font);
	
	return styleStack.top();
}

void SDLTTFont::drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip) const
{
	assert(text);
	assert(font);
	assert(colorStack.size()>0);
	
	SDL_Color c = colorStack.top();
	SDL_Surface *s;
	s=TTF_RenderUTF8_Blended(font, text, c);
	if (s == NULL)
		return;

	SDL_Rect sr;
	sr.x=0;
	sr.y=0;
	sr.w=s->w;
	sr.h=s->h;

	SDL_Rect r;
	r.x=x;
	r.y=y;
	if (w)
		r.w=w;
	else
		r.w=s->w;
	r.h=s->h;

	SDL_Rect oc;
	if (clip)
	{
		SDL_GetClipRect(Surface, &oc);
		GAG::sdcRects(&sr, &r, *clip);
		SDL_SetClipRect(Surface, &r);
	}

	SDL_BlitSurface(s, &sr, Surface, &r);

	if (clip)
	{
		SDL_SetClipRect(Surface, &oc);
	}
	
	SDL_FreeSurface(s);
}
