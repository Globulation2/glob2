/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef INCLUDED_DX9_GRAPHICCONTEXT_H
#define INCLUDED_DX9_GRAPHICCONTEXT_H

#include "GAGSys.h"
#include "CursorManager.h"
#include <map>
#include <vector>
#include <string>

#include <d3d9.h>

namespace GAGCore
{
	class Sprite;
	
	class Font
	{
	public:
		enum Shape
		{
			STYLE_NORMAL = 0x00,
			STYLE_BOLD = 0x01,
			STYLE_ITALIC = 0x02,
			STYLE_UNDERLINE = 0x04,
		};
		
		struct Style
		{
			Shape shape;
			Uint8 r, g, b, a;
			
			Style() { shape = STYLE_NORMAL; r = 255; g = 255; b = 255; a = 255; }
			
			Style(Shape shape, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255)
			{
				this->shape = shape;
				this->r = r;
				this->g = g;
				this->b = b;
				this->a = a;
			}
			
			bool operator<(const Style &o) const
			{
				Uint32 w0 = (r<<24) | (g<<16) | (b<<8) | a;
				Uint32 w1 = (o.r<<24) | (o.g<<16) | (o.b<<8) | o.a;
				if (w0 == w1)
					return shape < o.shape;
				else
					return w0 < w1;
			}
		};
	
	public:
		virtual ~Font() { }
	
		// width and height
		virtual int getStringWidth(const std::string string) const = 0;
		virtual int getStringWidth(const std::string string, int len) const;
		virtual int getStringWidth(const int i) const;
		virtual int getStringHeight(const std::string string) const = 0;
		virtual int getStringHeight(const std::string string, int len) const;
		virtual int getStringHeight(const int i) const;
	
		// Style and color
		virtual void setStyle(Style style) = 0;
		virtual Style getStyle(void) const = 0;
		
	protected:
		friend class DrawableSurface;
		virtual void drawString(IDirect3DTexture9 *Surface, int x, int y, int w, const std::string text, RECT *clip=NULL) = 0;
		virtual void pushStyle(Style style) = 0;
		virtual void popStyle(void) = 0;
	};
	
	class DrawableSurface
	{
	protected:
		//! the underlying SDL or glSDL surface
		IDirect3DTexture9 *surface;
		//! The clipping rect, we do not draw outside it
		RECT clipRect;
		//! Flags, can be a combination of ResolutionFlags
		Uint32 flags;
		//! if true, surface is locked and suitable for direct pixel access
		bool locked;

		//! Surface width
		int width;
		//! Surface height
		int height;

		//! Pointer to pixels when locked
		void *pixels;
		
		//! lock only if necessary
		void lock(void) { /*if (!locked) { SDL_LockSurface(surface); locked = true; }*/ }
		//! unlock only if necessary
		void unlock(void) { /*if (locked) { SDL_UnlockSurface(surface); locked = false; }*/ }
		
	public:
		enum GraphicContextType
		{
			GC_SDL=0,
			GC_GL=1
		};
	
		enum Alpha
		{
			ALPHA_TRANSPARENT=0,
			ALPHA_OPAQUE=255
		};
	
		enum ResolutionFlags
		{
			DEFAULT=0,
			DOUBLEBUF=1,
			FULLSCREEN=2,
			HWACCELERATED=4,
			RESIZABLE=8,
		};
		
		enum Quality
		{
			HIGH_QUALITY=0,
			LOW_QUALITY=1
		};
	
	public:
		DrawableSurface();
		virtual ~DrawableSurface(void) { if (surface) surface->Release(); surface=NULL; }
		virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT, Uint32 type=GC_SDL);
		virtual void setAlpha(bool usePerPixelAlpha=false, Uint8 alphaValue=ALPHA_OPAQUE);
		virtual int getW(void) { return width;  }
		virtual int getH(void) { return height; }
		virtual int getDepth(void) { return 32; }	// TODO: Well, that's what we start with...
		virtual int getFlags(void) { return flags; }
		virtual void setClipRect(int x, int y, int w, int h);
		virtual void setClipRect(void);
		virtual void loadImage(const std::string name);
		virtual void drawSprite(int x, int y, Sprite *sprite, int index=0);
		virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a=ALPHA_OPAQUE);
		virtual void drawString(int x, int y, Font *font, int i);
		virtual void drawString(int x, int y, Font *font, const char *msg);
		virtual void drawString(int x, int y, int w, Font *font, const char *msg);
		virtual void drawSurface(int x, int y, DrawableSurface *surface);
		virtual void pushFontStyle(Font *font, Font::Style style);
		virtual void popFontStyle(Font *font);
		virtual void nextFrame(void) { }
		virtual void *getPixelPointer(void)  { return pixels; }
	};
	
	class GraphicContext : public DrawableSurface
	{
		static const bool verbose = false;
	public:
		//! The cursor manager, public to be able to set custom cursors
		CursorManager cursorManager;
		
	private:
		//! Available full screen modes
		RECT **modes;
		//! Window handle
		HWND hWnd;

		
	protected:
		int minW, minH;
		//! draw all queued command due to the double buffering
		void drawQueudCommands(void);
		//! clear all queued commands
		void clearQueudCommands(void);
	
	public:
		GraphicContext();
		virtual ~GraphicContext(void);
	
		//! this must be called before any Drawable Surface method.
		virtual bool setRes(int w, int h, int depth=32, Uint32 flags=DEFAULT, Uint32 type=GC_SDL);
		virtual void setMinRes(int w=0, int h=0);
		virtual void setCaption(const char *title, const char *icon) { SetWindowText(hWnd,title);  } // TODO: Ignore icon
		virtual void beginVideoModeListing(void);
		virtual bool getNextVideoMode(int *w, int *h);
			
		virtual void loadImage(const std::string name);
	
		virtual void nextFrame(void);
		virtual void printScreen(const std::string filename);
	};
	
	union Color32
	{
		Uint32 id;
		struct
		{
			Uint8 r, g, b, a;
		} channel;
	
		Color32() { channel.r=channel.g=channel.b=0; channel.a=DrawableSurface::ALPHA_OPAQUE; }
		Color32(Uint8 r, Uint8 g, Uint8 b, Uint8 a=DrawableSurface::ALPHA_OPAQUE) { channel.r=r; channel.g=g; channel.b=b; channel.a=a; }
		Color32(Uint32 v) { id=v; }
		bool operator<(const Color32 &o) const { return id<o.id; }
	};
	
	
	class Sprite
	{
	protected:
		struct Surface
		{
			IDirect3DTexture9 *s;
			
			//! allocate the internal surface suitable for fast blit, free the source
			Surface(IDirect3DTexture9 *source);
			~Surface();
		};
	
		struct RotatedImage
		{
			IDirect3DTexture9 *orig;
			typedef std::map<Color32, Surface *> RotationMap;
			RotationMap rotationMap;
	
			RotatedImage(IDirect3DTexture9 *s) { orig=s; }
			~RotatedImage();
		};
	
		std::vector <Surface *> images;
		std::vector <RotatedImage *> rotated;
		Color32 actColor;
	
		friend class GraphicContext;
		// Support functions
		//! Load a frame from two file pointers
		void loadFrame(SDL_RWops *frameStream, SDL_RWops *rotatedStream);
		//! Check if index is within bound and return true, assert false and return false otherwise
		bool checkBound(int index);
	
	public:
		Sprite() { }
		virtual ~Sprite();
		
		//! Load a sprite from the file, return true if any frame have been loaded
		bool load(const std::string filename);
	
		//! Draw the sprite frame index at pos (x,y) on an SDL Surface with the clipping rect clip
		virtual void draw(IDirect3DTexture9 *dest, const RECT *clip, int x, int y, int index);
	
		//! Set the (r,g,b) color to a sprite's base color
		virtual void setBaseColor(Uint8 r, Uint8 g, Uint8 b) { actColor=Color32(r, g, b); }
		//! Set the alpha component of the sprite
		virtual void setAlpha(Uint8 alpha);
	
		//! Return the width of index frame of the sprite
		virtual int getW(int index);
		//! Return the height of index frame of the sprite
		virtual int getH(int index);
		//! Return the number of frame in this sprite
		virtual int getFrameCount(void);
	};

}

#endif
