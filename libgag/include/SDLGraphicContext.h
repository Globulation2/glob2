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

#ifndef INCLUDED_SDL_GRAPHICCONTEXT_H
#define INCLUDED_SDL_GRAPHICCONTEXT_H

#include "GAGSys.h"
#include "CursorManager.h"
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <valarray>

#include <set>
#include <boost/tuple/tuple.hpp>


namespace GAGCore
{
	//! Color is 4 bytes big but provides easy access to components
	struct Color
	{
		//! Typical usefull alpha values pre-defined
		enum Alpha
		{
			ALPHA_TRANSPARENT = 0, //!< constant for transparent alpha
			ALPHA_OPAQUE = 255 //!< constant for opaque alpha
		};
		
		Uint8 r, g, b, a; //!< component of the color
		
		//! Constructor. Default color is opaque black
		Color() { r = g = b = 0; a = ALPHA_OPAQUE; }
		//! Constructor from components
		Color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = ALPHA_OPAQUE) { this->r = r; this->g = g; this->b = b; this->a = a; }
		
		//! Return HSV values in pointers
		void getHSV(float *hue, float *sat, float *lum);
		//! Set color from HLS, alpha unctouched
		void setHSV(float hue, float sat, float lum);
		
		//! pack components in a 32 bits int given SDL screen values
		Uint32 pack() const;
		//! unpack from a 32 bits int given SDL screen values
		void unpack(const Uint32 packedValue);
		
		//! comparaison for inequality
		bool operator<(const Color &o) const { return pack() < o.pack(); }
		//! comparaison for equality
		bool operator==(const Color &o) const { return pack() == o.pack(); }
		
		//! return a new color with a different alpha value
		Color applyAlpha(Uint8 _a) const { Color c = *this; c.a = _a; return c; }
		//! return a new color resulting of the multiplication by alpha
		Color applyMultiplyAlpha(Uint8 _a) const ;
		
		static Color black; //!< black color (0,0,0)
		static Color white; //!< black color (255,255,255)
	};
	
	//! Deprecated, for compatibility only. Eventually, all Color32 should be removed or changed to Color
	typedef Color Color32;
	
	class Sprite;
	
	//! Font with a given foundery, shape and color
	class Font
	{
	public:
		//! Shape of the font
		enum Shape
		{
			STYLE_NORMAL = 0x00, //!< normal fond
			STYLE_BOLD = 0x01, //!< bold font
			STYLE_ITALIC = 0x02, //!< italic font
			STYLE_UNDERLINE = 0x04, //!< underlined font
		};
		
		//! Style of the font, i.e. a shape and a color
		struct Style
		{
			Shape shape; //!< shape of this style
			Color color; //!< color of this style
			
			//! Constructor. Default is normal with white opaque color
			Style() { shape = STYLE_NORMAL; color = Color::white; }
			
			//! Constructor from shape and color
			Style(Shape _shape, Color _color) :
				shape(_shape),
				color(_color)
			{
			}
			
			//! Constructor from shape and color component
			Style(Shape _shape, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE) :
				shape(_shape),
				color(r, g, b, a)
			{
			}
			
			//! inequality comparaison
			bool operator<(const Style &o) const
			{
				if (color == o.color)
					return shape < o.shape;
				else
					return color < o.color;
			}
		};
	
	public:
		//! Destructor
		virtual ~Font() { }
	
		// width and height
		virtual int getStringWidth(const std::string string) = 0;
		virtual int getStringWidth(const std::string string, int len);
		virtual int getStringWidth(const int i);
		virtual int getStringHeight(const std::string string) = 0;
		virtual int getStringHeight(const std::string string, int len);
		virtual int getStringHeight(const int i);
	
		// Style and color
		virtual void setStyle(Style style) = 0;
		virtual Style getStyle(void) const = 0;
		virtual void pushStyle(Style style) = 0;
		virtual void popStyle(void) = 0;
		
	protected:
		friend class DrawableSurface;
		virtual void drawString(DrawableSurface *Surface, int x, int y, int w, const std::string text, Uint8 alpha) = 0;
		virtual void drawString(DrawableSurface *Surface, float x, float y, float w, const std::string text, Uint8 alpha) = 0;
	};
	
	//! A surface on which we can draw
	class DrawableSurface
	{
	protected:
		friend struct Color;
		friend class GraphicContext;
		//! the underlying software SDL surface
		SDL_Surface *sdlsurface;
		//! The clipping rect, we do not draw outside it
		SDL_Rect clipRect;
		//! this surface has been modified since latest blit
		bool dirty;
		//! texture index if GPU (GL) is used
		unsigned int texture;
		//! texture divisor
		float texMultX, texMultY;
		
	protected:
		//! draw a vertical line. This function is private because it is only a helper one
		void _drawVertLine(int x, int y, int l, const Color& color);
		//! draw a horizontal line. This function is private because it is only a helper one
		void _drawHorzLine(int x, int y, int l, const Color& color);
		
	protected:
		//! Protectedconstructor, only called by GraphicContext
		DrawableSurface() { sdlsurface = NULL; }
		//! allocate textre in GPU for this surface
		void allocateTexture(void);
		//! reset the texture size upon changes
		void initTextureSize(void);
		//! upload surface to GPU
		void uploadToTexture(void);
		//! free texture in GPU for this surface
		void freeGPUTexture(void);
		//! transform any SDL Surface to a GL uploadable one
		SDL_Surface *convertForUpload(SDL_Surface *source);
		
	public:
		// New API
		
		// constructors and destructor
		DrawableSurface(const std::string &imageFileName);
		DrawableSurface(int w, int h);
		DrawableSurface(const SDL_Surface *sourceSurface);
		DrawableSurface *clone(void);
		virtual ~DrawableSurface(void);
		
		// modifiers
		virtual void setRes(int w, int h);
		virtual void getClipRect(int *x, int *y, int *w, int *h);
		virtual void setClipRect(int x, int y, int w, int h);
		virtual void setClipRect(void);
		virtual void nextFrame(void) { flushTextPictures(); }
		virtual bool loadImage(const std::string name);
		virtual void shiftHSV(float hue, float sat, float lum);
		
		// accessors
		virtual int getW(void) { return sdlsurface->w; } 
		virtual int getH(void) { return sdlsurface->h; }
		
		// capability querying
		virtual bool canDrawStretchedSprite(void) { return false; }
		
		// drawing commands
		virtual void drawPixel(int x, int y, const Color& color);
		virtual void drawPixel(float x, float y, const Color& color);
		
		virtual void drawRect(int x, int y, int w, int h, const Color& color);
		virtual void drawRect(float x, float y, float w, float h, const Color& color);
		
		virtual void drawFilledRect(int x, int y, int w, int h, const Color& color);
		virtual void drawFilledRect(float x, float y, float w, float h, const Color& color);
		
		virtual void drawLine(int x1, int y1, int x2, int y2, const Color& color);
		virtual void drawLine(float x1, float y1, float x2, float y2, const Color& color);
		
		virtual void drawCircle(int x, int y, int radius, const Color& color);
		virtual void drawCircle(float x, float y, float radius, const Color& color);
		
		virtual void drawSurface(int x, int y, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawSurface(int x, int y, int w, int h, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawSurface(int x, int y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		void drawSprite(int x, int y, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		void drawSprite(float x, float y, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		void drawSprite(int x, int y, int w, int h, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		void drawSprite(float x, float y, float w, float h, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		void drawString(int x, int y, Font *font, const std::string &msg, int w = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		void drawString(float x, float y, Font *font, const std::string &msg, float w = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		

		//! Draw an alpha map of size mapW, mapH using a specific color at coordinantes x, y using cells of size cellW, cellH
		virtual void drawAlphaMap(const std::valarray<float> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		virtual void drawAlphaMap(const std::valarray<unsigned char> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		
		// old API, deprecated, do not use. It is only there for compatibility with existing code
		virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawVertLine(int x, int y, int l, const Color& color);
		virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawHorzLine(int x, int y, int l, const Color& color);
		virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawString(int x, int y, Font *font, int i);
		
		// This is for translation textshot code, it works by trapping calls to the getString function in the translation StringTables,
		// then later in drawString, if we are drawing one of the found strings returned by StringTable, it will add it to the list of
		// rectangles that represent found texts. Just before the next frame begins to draw, all of the rectangle pictures are flushed
		// into bmp's. This is done because we want the translation pictures to be done when *all* of the screen is already drawn (when
		// we flushed into a bmp directly from drawString, sometimes the screen was only partially drawn, and the resulting pictures
		// would be hard to interpret)
		class SRectangle
		{
		public:
			SRectangle(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}
			int x, y, w, h;
		};
		// This holds texts requested by the toolkit, but not yet drawn to the screen
		static std::map<std::string, std::string> texts;
		// This holds texts already drawn to the screen and detected, so that pictures aren't taken twice, which would lag the game badly
		static std::set<std::string> wroteTexts;
		// This holds detected texts that will be printed on the next flush
		static std::vector<boost::tuple<SRectangle, std::string, GAGCore::DrawableSurface*> > drawSquares;
		// This holds the directory the pictures will be stored in. The system is disabled if this string is empty.
		static std::string translationPicturesDirectory;
		// This flushes all of the detected texts, making bmp pictures
		static void flushTextPictures();
		// This prints out information on all of the texts that where requested but not detected
		static void printFinishingText();
	};

	//! The description of a video mode
	typedef std::vector<SDL_DisplayMode> VideoModes;
	
	//! A GraphicContext is a DrawableSurface that represent the main screen of the application.
	class GraphicContext:public DrawableSurface
	{
		static const bool verbose = false;
	public:
		//! The cursor manager, public to be able to set custom cursors
		CursorManager cursorManager;
		
		//! Flags that define the characteristic of the graphic context
		enum OptionFlags
		{
			DEFAULT = 0,
			USEGPU = 1,
			FULLSCREEN = 2,
			//TODO: either implement "resizable" as a resizable gui or explain what this does
			RESIZABLE = 8,
			CUSTOMCURSOR = 16,
		};
		
	protected:
		//! the minimum acceptable resolution
		int minW, minH;
		int prevW, prevH;
		SDL_Window *window = nullptr;
		SDL_GLContext context = nullptr;
		friend class DrawableSurface;
		//! option flags
		Uint32 optionFlags;
		std::string windowTitle;
		std::string appIcon;
		
	public:
		//! Constructor. Create a new window of size (w,h). If useGPU is true, use GPU for accelerated 2D (OpenGL or DX)
		GraphicContext(int w, int h, Uint32 flags, const std::string title = "", const std::string icon = "");
		//! Destructor
		virtual ~GraphicContext(void);
		
		// modifiers
		virtual bool setRes(int w, int h, Uint32 flags);
		virtual void setRes(int w, int h) { setRes(w, h, optionFlags); }
		virtual SDL_Rect getRes();
		virtual bool resChanged();
		virtual void createGLContext();
		virtual void setClipRect(int x, int y, int w, int h);
		virtual void setClipRect(void);
		virtual void nextFrame(void);
		//! This function does not work for GraphicContext
		virtual bool loadImage(const std::string name) { return false; }
		//! This function does not work for GraphicContext
		virtual bool loadImage(const std::string &name) { return false; }
		//! This function does not work for GraphicContext
		virtual void shiftHSV(float hue, float sat, float lum) { }
		
		// reimplemented drawing commands for HW (GPU / GL) accelerated version
		virtual bool canDrawStretchedSprite(void) { return (optionFlags & USEGPU) != 0; }
		
		virtual void drawPixel(int x, int y, const Color& color);
		virtual void drawPixel(float x, float y, const Color& color);
		
		virtual void drawRect(int x, int y, int w, int h, const Color& color);
		virtual void drawRect(float x, float y, float w, float h, const Color& color);
		
		virtual void drawFilledRect(int x, int y, int w, int h, const Color& color);
		virtual void drawFilledRect(float x, float y, float w, float h, const Color& color);
		
		virtual void drawLine(int x1, int y1, int x2, int y2, const Color& color);
		virtual void drawLine(float x1, float y1, float x2, float y2, const Color& color);
		
		virtual void drawCircle(int x, int y, int radius, const Color& color);
		virtual void drawCircle(float x, float y, float radius, const Color& color);
		
		virtual void drawSurface(int x, int y, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawSurface(int x, int y, int w, int h, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawSurface(int x, int y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha = Color::ALPHA_OPAQUE);
		virtual void drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		virtual void drawAlphaMap(const std::valarray<float> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		virtual void drawAlphaMap(const std::valarray<unsigned char> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		
		// compat
		virtual void drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawVertLine(int x, int y, int l, const Color& color);
		virtual void drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawHorzLine(int x, int y, int l, const Color& color);
		virtual void drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		virtual void drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a = Color::ALPHA_OPAQUE);
		
		// GraphicContext specific methods
		//! Set the minimum acceptable resolution
		virtual void setMinRes(int w = 0, int h = 0);
		//! List all video modes
		VideoModes listVideoModes() const;

		//! Save a bmp of the screen to a file, bypass virtual filesystem
		virtual void printScreen(const std::string filename);
		
		//! Return the option flags
		Uint32 getOptionFlags(void) { return optionFlags; }
	};
	
	//! A sprite is a collection of images (frames) that can be displayed one after another to make an animation
	class Sprite
	{
	protected:
		struct RotatedImage
		{
			DrawableSurface *orig;
			typedef std::map<Color32, DrawableSurface *> RotationMap;
			RotationMap rotationMap;
	
			RotatedImage(DrawableSurface *s) { orig = s; }
			~RotatedImage();
		};
	
		std::string fileName;
		std::vector <DrawableSurface *> images;
		std::vector <RotatedImage *> rotated;
		Color actColor;
	
		friend class DrawableSurface;
		// Support functions
		//! Load a frame from two file pointers
		void loadFrame(SDL_RWops *frameStream, SDL_RWops *rotatedStream);
		//! Check if index is within bound and return true, assert false and return false otherwise
		bool checkBound(int index);
		//! Return a rotated drawable surface for actColor, create it if necessary
		virtual DrawableSurface *getRotatedSurface(int index);
	
	public:
		//! Constructor
		Sprite() : fileName("not loaded yet") { }
		//! Destructor
		virtual ~Sprite();
		
		//! Load a sprite from the file, return true if any frame have been loaded
		bool load(const std::string filename);
	
		//! Set the (r,g,b) color to a sprite's base color
		virtual void setBaseColor(Uint8 r, Uint8 g, Uint8 b) { actColor = Color(r, g, b); }
		//! Set the color to a sprite's base color
		virtual void setBaseColor(const Color& color) { actColor = color; }
		
		//! Return the width of index frame of the sprite
		virtual int getW(int index);
		//! Return the height of index frame of the sprite
		virtual int getH(int index);
		//! Return the number of frame in this sprite
		virtual int getFrameCount(void);
	};
}

#endif
