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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LIBGL

#include "SDLGraphicContext.h"
#include "GLGraphicContext.h"
#include "SDLFont.h"
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <sstream>

// Internal support functions
int roundToNextPowerOfTwo(int v)
{
	int i = 1;
	while(i)
	{
		if (v<=i)
			return i;
		i<<=1;
	}
	return 0;
}

void GLGraphicContext::setClipRect(int x, int y, int w, int h)
{
	glScissor(x, y, w, h);
}

void GLGraphicContext::setClipRect(void)
{
	glScissor(0, 0, screen->w, screen->h);
}

void GLGraphicContext::drawSprite(int x, int y, Sprite *sprite, int index)
{
/*	if (!surface)
		return;
	((SDLSprite *)sprite)->draw(surface, &clipRect, x, y, index);*/
}

void GLGraphicContext::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
	glColor4ub(r, g, b, a);
	glBegin(GL_POINTS);
	glVertex2f(x+0.5, y+0.5);
	glEnd();
}

void GLGraphicContext::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	glDisable(GL_LINE_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glColor4ub(r, g, b, a);
	glBegin(GL_POLYGON);
	glVertex2f(x+0.49, y+0.49);
	glVertex2f(x+0.49, y+h-0.49);
	glVertex2f(x+w-0.49, y+h-0.49);
	glVertex2f(x+w-0.49, y+0.49);
	glEnd();
}

void GLGraphicContext::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	glDisable(GL_LINE_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColor4ub(r, g, b, a);
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x, y+h);
	glVertex2f(x+w, y+h);
	glVertex2f(x+w, y);
	glEnd();
}

bool disableSmooth=false;

void GLGraphicContext::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	glDisable(GL_LINE_SMOOTH);
	disableSmooth=true;
	drawLine(x, y, x, y+l-1, r, g, b, a);
	disableSmooth=false;
}

void GLGraphicContext::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	glDisable(GL_LINE_SMOOTH);
	disableSmooth=true;
	drawLine(x, y, x+l-1, y, r, g, b, a);
	disableSmooth=false;
}

void GLGraphicContext::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (!disableSmooth)
		glEnable(GL_LINE_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glColor4ub(r, g, b, a);
	glBegin(GL_LINES);
	glVertex2f(x1+0.5, y1+0.5);
	glVertex2f(x2+0.5, y2+0.5);
	glEnd();
}

void GLGraphicContext::drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	unsigned i;
	unsigned tot=ray;
	
	double fx=(double)x+0.5;
	double fy=(double)y+0.5;
	double fr=(double)ray;
	
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glColor4ub(r, g, b, a);
	glBegin(GL_POLYGON);
	for (i=0; i<tot; i++)
	{
		double angle = (2*M_PI*(double)i)/((double)tot);
		glVertex2d(fx+fr*sin(angle), fy+fr*cos(angle));
	}
	glEnd();
}

// usefull macro to replace some char (like newline) with \0 in string
#define FILTER_OUT_CHAR(s, c) { char *_c; if ( (_c=(strchr(s, c)))!=NULL) *_c=0; }

void GLGraphicContext::drawString(int x, int y, const Font *font, int i)
{
	std::stringstream str;
	str << i;
	return this->drawString(x, y, 0, font, str.str().c_str());
}

void GLGraphicContext::drawString(int x, int y, const Font *font, const char *msg)
{
	return this->drawString(x, y, 0, font, msg);
}

void GLGraphicContext::drawString(int x, int y, int w, const Font *font, const char *msg)
{
	/*if (!surface)
		return;

	std::string output(msg);

	FILTER_OUT_CHAR(output.c_str(), '\n');
	FILTER_OUT_CHAR(output.c_str(), '\r');
	((const SDLFont *)font)->drawString(surface, x, y, w, output.c_str(), &clipRect);*/
}

void GLGraphicContext::drawSurface(int x, int y, DrawableSurface *surface)
{
	/*if (!surface)
		return;

	SDLDrawableSurface *sdlsurface=dynamic_cast<SDLDrawableSurface *>(surface);
	if ((sdlsurface) && (sdlsurface->surface))
	{
		SDL_Rect r;

		r.x=x;
		r.y=y;
		r.w=surface->getW();
		r.h=surface->getH();

		SDL_BlitSurface(sdlsurface->surface, NULL, this->surface, &r);
	}*/
}

GLGraphicContext::GLGraphicContext(void)
{
	screen=NULL;

	// Load the SDL library
	if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO)<0 )
	{
		fprintf(stderr, "GAG : Initialisation Error : %s\n", SDL_GetError());
		exit(1);
	}
	else
	{
		fprintf(stderr, "GAG : Initialized : Graphic Context created\n");
	}

	atexit(SDL_Quit);

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_EnableUNICODE(1);
	
	TTF_Init();
}

GLGraphicContext::~GLGraphicContext(void)
{
	fprintf(stderr, "GAG : Graphic Context destroyed\n");
	
	TTF_Quit();
}

bool GLGraphicContext::setRes(int w, int h, int depth, Uint32 flags)
{
	Uint32 sdlFlags=SDL_OPENGL;

	this->flags=flags;
	if (flags&FULLSCREEN)
		sdlFlags|=SDL_FULLSCREEN;
	if (flags&RESIZABLE)
		sdlFlags|=SDL_RESIZABLE;

	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	//SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	/*if (flags&DOUBLEBUF)
	{
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		sdlFlags|=SDL_DOUBLEBUF;
	}
	else
	{
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 0 );
	}*/

	screen = SDL_SetVideoMode(w, h, 32, SDL_OPENGL/*sdlFlags*/);

	if (!screen)
	{
		fprintf(stderr, "GAG : %s\n", SDL_GetError());
		return false;
	}
	else
	{
		//setClipRect();
		if (flags&FULLSCREEN)
			fprintf(stderr, "GAG : GL Screen set to %dx%d at %d bpp in fullscreen\n", w, h, depth);
		else
			fprintf(stderr, "GAG : GL Screen set to %dx%d at %d bpp in window\n", w, h, depth);
			
		printf("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
		printf("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
		printf("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
		printf("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
		
		gluOrtho2D(0, w, h, 0);
		glDisable(GL_DITHER);
		glEnable(GL_SCISSOR_TEST);
		return true;
	}
}

void GLGraphicContext::nextFrame(void)
{
	glFlush();
	SDL_GL_SwapBuffers();
}

void GLGraphicContext::updateRects(SDL_Rect *rects, int size)
{
	glFlush();
	SDL_GL_SwapBuffers();
};

void GLGraphicContext::updateRect(int x, int y, int w, int h)
{
	glFlush();
	SDL_GL_SwapBuffers();
}

void GLGraphicContext::loadImage(const char *name)
{
	/*if ((name) && (surface))
	{
		SDL_RWops *imageStream;
		if ((imageStream=GAG::fileManager->open(name, "rb", false))!=NULL)
		{
			SDL_Surface *temp;
			temp=IMG_Load_RW(imageStream, 0);
			if (temp)
			{
				SDL_Rect dRect;
				dRect.x=(surface->w-temp->w)>>1;
				dRect.y=(surface->h-temp->h)>>1;
				dRect.w=temp->w;
				dRect.h=temp->h;
				SDL_BlitSurface(temp, NULL, surface, &dRect);
				SDL_FreeSurface(temp);
			}
			SDL_RWclose(imageStream);
		}
	}*/
}

void GLGraphicContext::loadFont(const char *filename, unsigned size, const char *name)
{
	Font *rf = NULL;
	
	SDLTTFont *ttf=new SDLTTFont();
	if (ttf->load(filename, size))
		rf = ttf;
	else
		delete ttf;
	
	if (!rf)
	{
		SDLBitmapFont *font=new SDLBitmapFont();
		if (font->load(filename))
			rf = font;
		else
			delete font;
	}
	
	if (rf)
	{
		Toolkit::fontMap[std::string(name)] = rf;
	}
	else
	{
		fprintf(stderr, "GAG : Can't load font %s from %s\n", name, filename);
	}
}

DrawableSurface *GLGraphicContext::createDrawableSurface(const char *name)
{
	DrawableSurface *ds=new SDLDrawableSurface();
	ds->loadImage(name);
	return ds;
}

void GLGraphicContext::printScreen(const char *filename)
{

}

#endif
