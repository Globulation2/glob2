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

#include "SDLGraphicContext.h"
#include <math.h>
#include <Toolkit.h>
#include <SupportFunctions.h>
#include <FileManager.h>
#include <assert.h>
#include <SDL_image.h>
#include <algorithm>
#include <iostream>

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

extern SDLGraphicContext *screen;

Sprite::Surface::Surface(SDL_Surface *source)
{
	assert(source);
	s = SDL_DisplayFormatAlpha(source);
	assert(s);
	SDL_FreeSurface(source);

	this->t = -1;
	#ifdef HAVE_LIBGL
	// TODO : if we use GL (check on graphicContext, allocate GL texture)
	#endif
}

Sprite::Surface::~Surface()
{
	SDL_FreeSurface(s);
	#ifdef HAVE_LIBGL
	if (t>=0)
	{
		// TODO : GL free texture code
	}
	#endif
}

Sprite::RotatedImage::~RotatedImage()
{
	SDL_FreeSurface(orig);
	for (RotationMap::iterator it = rotationMap.begin(); it != rotationMap.end(); ++it)
	{
		delete it->second;
	}
}


void Sprite::drawSDL(SDL_Surface *dest, const SDL_Rect *clip, int x, int y, int index)
{
	checkBound(index);
	assert(index>=0);
	assert(index<(int)images.size());

	SDL_Rect oldr, r;
	SDL_Rect newr=*clip;
	SDL_Rect src;
	int w, h;
	int diff;

	w=getW(index);
	h=getH(index);

	src.x=0;
	src.y=0;
	if (x<newr.x)
	{
		diff=newr.x-x;
		w-=diff;
		src.x+=static_cast<Sint16>(diff);
		x=newr.x;
	}
	if (y<newr.y)
	{
		diff=newr.y-y;
		h-=diff;
		src.y+=static_cast<Sint16>(diff);
		y=newr.y;
	}

	if (x+w>newr.x+newr.w)
	{
		diff=(x+w)-(newr.x+newr.w);
		w-=diff;
	}
	if (y+h>newr.y+newr.h)
	{
		diff=(y+h)-(newr.y+newr.h);
		h-=diff;
	}

	if ((w<=0) || (h<=0))
		return;

	src.w=static_cast<Sint16>(w);
	src.h=static_cast<Sint16>(h);
	r.x=static_cast<Sint16>(x);
	r.y=static_cast<Sint16>(y);
	r.w=static_cast<Sint16>(w);
	r.h=static_cast<Sint16>(h);

	SDL_GetClipRect(dest, &oldr);
	SDL_SetClipRect(dest, &newr);

	if (images[index])
		SDL_BlitSurface(images[index]->s, &src, dest, &r);

	if (rotated[index])
	{
		RotatedImage::RotationMap::const_iterator it = rotated[index]->rotationMap.find(actColor);
		Surface *toBlit;
		if (it == rotated[index]->rotationMap.end())
		{
			float hue, lum, sat;
			float baseHue, hueDec;

			GAG::RGBtoHSV(51.0f/255.0f, 255.0f/255.0f, 153.0f/255.0f, &baseHue, &sat, &lum);
			GAG::RGBtoHSV( ((float)actColor.channel.r)/255, ((float)actColor.channel.g)/255, ((float)actColor.channel.b)/255, &hue, &sat, &lum);
			hueDec = hue-baseHue;
			int w = rotated[index]->orig->w;
			int h = rotated[index]->orig->h;

			SDL_Surface *newSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, 0xff, 0xff00, 0xff0000, 0xff000000);

			Uint32 *sPtr = (Uint32 *)rotated[index]->orig->pixels;
			Uint32 *dPtr = (Uint32 *)newSurface->pixels;
			for (int i=0; i<w*h; i++)
			{
				Uint8 sR, sG, sB, alpha;
				Uint8 dR, dG, dB;
				float nR, nG, nB;
				SDL_GetRGBA(*sPtr, rotated[index]->orig->format, &sR, &sG, &sB, &alpha);

				if (alpha != DrawableSurface::ALPHA_TRANSPARENT)
				{
					GAG::RGBtoHSV( ((float)sR)/255, ((float)sG)/255, ((float)sB)/255, &hue, &sat, &lum);

					float newHue = hue + hueDec;
					if (newHue >= 360)
						newHue -= 360;
					if (newHue < 0)
						newHue += 360;

					GAG::HSVtoRGB(&nR, &nG, &nB, newHue, sat, lum);

					dR = static_cast<Uint8>(255.0f*nR);
					dG = static_cast<Uint8>(255.0f*nG);
					dB = static_cast<Uint8>(255.0f*nB);

					*dPtr = SDL_MapRGBA(newSurface->format, dR, dG, dB, alpha);
				}
				else
					*dPtr = 0;


				sPtr++;
				dPtr++;
			}

			toBlit = new Surface(newSurface);
			rotated[index]->rotationMap[actColor] = toBlit;
		}
		else
		{
			toBlit = it->second;
		}
		SDL_BlitSurface(toBlit->s, &src, dest, &r);
	}

	SDL_SetClipRect(dest, &oldr);
}

void Sprite::drawGL(const SDL_Rect *clip, int x, int y, int index)
{
	#ifdef HAVE_LIBGL
	// TODO : GL drawing code
	/*glBegin(GL_POLYGON);
	glVertex2f(x+0.49, y+0.49);
	glVertex2f(x+0.49, y+h-0.49);
	glVertex2f(x+w-0.49, y+h-0.49);
	glVertex2f(x+w-0.49, y+0.49);
	glEnd();*/
	#endif
}

Sprite::~Sprite()
{
	for (std::vector <Surface *>::iterator imagesIt=images.begin(); imagesIt!=images.end(); ++imagesIt)
	{
		if (*imagesIt)
			delete (*imagesIt);
	}
	for (std::vector <RotatedImage *>::iterator rotatedIt=rotated.begin(); rotatedIt!=rotated.end(); ++rotatedIt)
	{
		if (*rotatedIt)
			delete (*rotatedIt);
	}
}

void Sprite::loadFrame(SDL_RWops *frameStream, SDL_RWops *rotatedStream)
{
	if (frameStream)
	{
		SDL_Surface *temp = IMG_Load_RW(frameStream, 0);
		images.push_back(new Surface(temp));
	}
	else
		images.push_back(NULL);

	if (rotatedStream)
	{
		SDL_Surface *sprite = IMG_Load_RW(rotatedStream, 0);
		assert(sprite);
		if (sprite->format->BitsPerPixel==32)
		{
			RotatedImage *image = new RotatedImage(sprite);
			rotated.push_back(image);
		}
		else
		{
			std::cerr << "GAG : Sprite::loadFrame(stream, stream) : warning, rotated image is in wrong depth (" << sprite->format->BitsPerPixel << " instead of 32)" << std::endl;
			rotated.push_back(NULL);
		}
	}
	else
		rotated.push_back(NULL);
}

int Sprite::getW(int index)
{
	checkBound(index);
	if (images[index])
		return images[index]->s->w;
	else if (rotated[index])
		return rotated[index]->orig->w;
	else
		return 0;
}

int Sprite::getH(int index)
{
	checkBound(index);
	if (images[index])
		return images[index]->s->h;
	else if (rotated[index])
		return rotated[index]->orig->h;
	else
		return 0;
}

int Sprite::getFrameCount(void)
{
	return std::max(images.size(), rotated.size());
}

void Sprite::checkBound(int index)
{
	if ((index < 0) || (index >= getFrameCount()))
	{
		Toolkit::SpriteMap::const_iterator i = Toolkit::spriteMap.begin();
		while (i != Toolkit::spriteMap.end())
		{
			if (i->second == this)
			{
				std::cerr << "GAG : Sprite::checkBound(" << index << ") : error : out of bound access for " << i->first << std::endl;
				assert(false);
				return;
			}
			++i;
		}
		std::cerr << "GAG : Sprite::checkBound(" << index << ") : error : sprite is not in the sprite server" << std::endl;
		assert(false);
	}
}