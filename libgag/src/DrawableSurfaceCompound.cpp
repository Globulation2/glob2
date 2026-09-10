// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <algorithm>
#include <assert.h>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <valarray>
#include <cstring>
#include <vector>

namespace GAGCore
{
	void DrawableSurface::drawCircle(int x, int y, int radius, const Color& _color)
	{
		// we want to modify the color
		Color color = _color;

		// clip
		if ((x+radius < clipRect.x) || (x-radius >= clipRect.x+clipRect.w) || (y+radius < clipRect.y) || (y-radius >= clipRect.y+clipRect.h))
			return;

		// draw
		int dx, dy, d;
		int rdx, rdy;
		int i;
		color.a >>= 2;
		for (i=0; i<3; i++)
		{
			dx = 0;
			dy = (radius<<1) + i;
			d = 0;

			do
			{
				rdx = (dx>>1);
				rdy = (dy>>1);
				drawPixel(x+rdx, y+rdy, color);
				drawPixel(x+rdx, y-rdy, color);
				drawPixel(x-rdx, y+rdy, color);
				drawPixel(x-rdx, y-rdy, color);
				drawPixel(x+rdy, y+rdx, color);
				drawPixel(x+rdy, y-rdx, color);
				drawPixel(x-rdy, y+rdx, color);
				drawPixel(x-rdy, y-rdx, color);
				dx++;
				if (d >= 0)
				{
					dy--;
					d += ((dx-dy)<<1)+2;
				}
				else
				{
					d += (dx<<1) +1;
				}
			}
			while (dx <= dy);
		}
	}

	void DrawableSurface::drawCircle(float x, float y, float radius, const Color& color)
	{
		drawCircle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(radius), color);
	}

	// Uint8 (r, g, b, a) compat overloads
	void DrawableSurface::drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawCircle(x, y, radius, Color(r, g, b, a));
	}
	void DrawableSurface::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		 _drawVertLine(x, y, l, Color(r, g, b, a));
	}
	void DrawableSurface::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		_drawHorzLine(x, y, l, Color(r, g, b, a));
	}
	void DrawableSurface::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawLine(x1, y1, x2, y2, Color(r, g, b, a));
	}

	void DrawableSurface::drawSurface(int x, int y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(float x, float y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(int x, int y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		if (alpha == Color::ALPHA_OPAQUE)
		{
			#ifdef HAVE_OPENGL
			if ((surface == _gc) && (_gc->getOptionFlags() & GraphicContext::USEGPU))
			{
				if ((x == 0) && (y == 0) && (sdlsurface->w == sw) && (sdlsurface->h == sh))
				{
					// Capture only the game viewport: including letterbox bars would
					// squeeze screenshots and captured screen backgrounds. Average each
					// logical pixel's block of drawable pixels so thin lines survive.
					GLint viewport[4];
					glGetIntegerv(GL_VIEWPORT, viewport);
					const int fbW = viewport[2], fbH = viewport[3];
					std::valarray<unsigned char> tempPixels(4*fbW*fbH);
					#if SDL_BYTEORDER == SDL_BIG_ENDIAN
					glReadPixels(viewport[0], viewport[1], fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, &tempPixels[0]);
					#else
					glReadPixels(viewport[0], viewport[1], fbW, fbH, GL_BGRA, GL_UNSIGNED_BYTE, &tempPixels[0]);
					#endif
					if (fbW == sw && fbH == sh)
					{
						// same size: plain row copy, flipping GL's bottom-up rows
						for (int y = 0; y<sh; y++)
						{
							unsigned *destPtr = &(((unsigned *)sdlsurface->pixels)[y*sw]);
							const unsigned char *src = &tempPixels[4*(sh-1-y)*sw];
							#if SDL_BYTEORDER == SDL_BIG_ENDIAN
							for (int x = 0; x<sw; x++, src += 4)
							{
								unsigned char *dest = reinterpret_cast<unsigned char *>(destPtr++);
								dest[0] = src[3]; dest[1] = src[0]; dest[2] = src[1]; dest[3] = src[2];
							}
							#else
							memcpy(destPtr, src, 4*sw);
							#endif
						}
					}
					else
					for (int y = 0; y<sh; y++)
					{
						// GL rows run bottom-up, so logical row y covers window rows [y0, y1[ from the top
						const int y0 = std::min(fbH-1, fbH - ((y+1)*fbH)/sh);
						const int y1 = std::max(y0+1, fbH - (y*fbH)/sh);
						unsigned *destPtr = &(((unsigned *)sdlsurface->pixels)[y*sw]);
						for (int x = 0; x<sw; x++)
						{
							const int x0 = (x*fbW)/sw;
							const int x1 = std::max(x0+1, ((x+1)*fbW)/sw);
							unsigned sum[4] = {0, 0, 0, 0};
							for (int fy = y0; fy<y1; fy++)
								for (int fx = x0; fx<x1; fx++)
									for (int c = 0; c<4; c++)
										sum[c] += tempPixels[4*(fy*fbW+fx)+c];
							const unsigned n = (y1-y0)*(x1-x0);
							unsigned char *dest = reinterpret_cast<unsigned char *>(destPtr++);
							#if SDL_BYTEORDER == SDL_BIG_ENDIAN
							// RGBA bytes read back to ARGB
							dest[0] = sum[3]/n; dest[1] = sum[0]/n; dest[2] = sum[1]/n; dest[3] = sum[2]/n;
							#else
							for (int c = 0; c<4; c++)
								dest[c] = sum[c]/n;
							#endif
						}
					}
				}
				else
				{
					std::cerr << "Partial blitting to from framebuffer in GL is forbidden" << std::endl;
					assert(false);
				}
			}
			else
			{
			#endif // HAVE_OPENGL
				// well, we *hope* SDL is faster than a handmade code
				SDL_Rect sr, dr;
				sr.x = static_cast<Sint16>(sx);
				sr.y = static_cast<Sint16>(sy);
				sr.w = static_cast<Uint16>(sw);
				sr.h = static_cast<Uint16>(sh);
				dr.x = static_cast<Sint16>(x);
				dr.y = static_cast<Sint16>(y);
				dr.w = static_cast<Uint16>(sw);
				dr.h = static_cast<Uint16>(sh);
				SDL_BlitSurface(surface->sdlsurface, &sr, sdlsurface, &dr);
			#ifdef HAVE_OPENGL
			}
			#endif // HAVE_OPENGL
		}
		else
		{
			if ((surface == _gc) && (_gc->getOptionFlags() & GraphicContext::USEGPU))
			{
				std::cerr << "Blitting with alphablending from framebuffer in GL is forbidden" << std::endl;
				assert(false);
			}

			// check we assume the source rect is within the source surface
			assert((sx >= 0) && (sx < surface->getW()));
			assert((sy >= 0) && (sy < surface->getH()));
			assert((sw > 0) && (sx + sw <= surface->getW()));
			assert((sh > 0) && (sy + sh <= surface->getH()));

			// clip
			if (x < clipRect.x)
			{
				int diff = clipRect.x - x;
				sw -= diff;
				sx += diff;
				x = clipRect.x;
			}
			if (y < 0)
			{
				int diff = clipRect.y - y;
				sh -= diff;
				sy += diff;
				y = clipRect.y;
			}
			if (x + sw >= clipRect.x + clipRect.w)
			{
				sw = clipRect.x + clipRect.w - x;
			}
			if (y + sh >= clipRect.y + clipRect.h)
			{
				sh = clipRect.y + clipRect.h - y;
			}
			if ((sw <= 0) || (sh <= 0))
				return;

			// draw
			#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			Uint32 alphaShift = 0;
			#else
			Uint32 alphaShift = 24;
			#endif
			for (int dy = 0; dy < sh; dy++)
			{
				Uint32 *memSrc = ((Uint32 *)surface->sdlsurface->pixels) + (sy + dy)*(surface->sdlsurface->pitch>>2) + sx;
				Uint32 *memDest = ((Uint32 *)sdlsurface->pixels) + (y + dy)*(sdlsurface->pitch>>2) + x;
				int dw = sw;
				do
				{
					Uint32 srcValue = *memSrc++;
					Uint32 srcAlpha = (((srcValue >> alphaShift) & 0xFF) * alpha) >> 8;
					Uint32 destAlpha = 255 - srcAlpha;
					Uint32 srcPreMult0 =  (srcValue & 0x00FF00FF) * srcAlpha;
					Uint32 srcPreMult1 = ((srcValue >> 8) & 0x00FF00FF) * srcAlpha;

					Uint32 destValue = *memDest;
					Uint32 destPreMult0 =  (destValue & 0x00FF00FF) * destAlpha;
					Uint32 destPreMult1 = ((destValue >> 8) & 0x00FF00FF) * destAlpha;

					destPreMult0 += srcPreMult0;
					destPreMult1 += srcPreMult1;

					*memDest++ = ((destPreMult0 >> 8) & 0x00FF00FF) | (destPreMult1 & 0xFF00FF00);
				}
				while (--dw);
			}
		}
		dirty = true;
	}

	void DrawableSurface::drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		drawSurface(static_cast<int>(x), static_cast<int>(y), surface, sx, sy, sw, sh, alpha);
	}

	void DrawableSurface::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha)
	{
		// TODO : Implement
	}

	void DrawableSurface::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		drawSurface(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), surface, sx, sy, sw, sh, alpha);
	}

	void DrawableSurface::drawSprite(int x, int y, Sprite *sprite, unsigned index,  Uint8 alpha)
	{
		drawSprite(static_cast<float>(x), static_cast<float>(y), sprite, index, alpha);
	}

	void DrawableSurface::drawSprite(float x, float y, Sprite *sprite, unsigned index,  Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;
		const bool gpuActive = this == _gc && (_gc->getOptionFlags() & GraphicContext::USEGPU);

		// Team-colour shader: one quad combining base+team, no CPU/GPU recolor
		// surface. Shared by world units and unit UI previews since they all
		// reach here. Picks native or HD per blockHasCompleteHD (a shutter uses
		// one resolution throughout) but always draws at the sprite's logical
		// size -- HD only adds texture sampling density. Falls through when the
		// shader is unavailable (software renderer, or compile/link failure).
		if (gpuActive && sprite->dynamicTeamColor && _gc->hasUnitShader())
		{
			const bool useHD = sprite->blockHasCompleteHD(index)
				&& (sprite->experimentImages[index] || sprite->experimentRotated[index]);
			DrawableSurface *base = useHD ? sprite->experimentImages[index] : sprite->images[index];
			DrawableSurface *team = useHD
				? (sprite->experimentRotated[index] ? sprite->experimentRotated[index]->orig : nullptr)
				: (sprite->rotated[index] ? sprite->rotated[index]->orig : nullptr);
			if (_gc->drawTeamColoredQuad(base, team, x, y, static_cast<float>(sprite->getW(index)),
			                             static_cast<float>(sprite->getH(index)), alpha, sprite->teamHueShiftDegrees()))
				return;
		}

		const bool wantsHDRoute = sprite->highResolutionAtlas || sprite->experimentImages[index] || sprite->experimentRotated[index];
		// A dynamicTeamColor sprite (unit) with an incomplete HD block must not
		// take the per-frame HD routing below: that would let this frame render
		// HD while a different pose of the same motion-blur shutter, rejected
		// by blockHasCompleteHD above, renders native -- mixing resolutions
		// pose to pose. Falling through renders this frame native too.
		if (gpuActive && wantsHDRoute && (!sprite->dynamicTeamColor || sprite->blockHasCompleteHD(index)))
		{
			drawSprite(x, y, static_cast<float>(sprite->getW(index)), static_cast<float>(sprite->getH(index)), sprite, index, alpha);
			return;
		}

		// Plain fallback: native only. Reached by the software renderer, by a
		// shader-unavailable GPU without an HD counterpart to fall back to via
		// the sized overload above, and by every sprite with no team layer.
		if (sprite->images[index])
			drawSurface(x, y, sprite->images[index], alpha);
		if (sprite->rotated[index])
			drawSurface(x, y, sprite->getRotatedSurface(index), alpha);
	}

	void DrawableSurface::drawSprite(int x, int y, int w, int h, Sprite *sprite, unsigned index, Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;

		bool experiment = this == _gc && (_gc->getOptionFlags() & GraphicContext::USEGPU);
		if (auto surface = sprite->prepareDrawSurface(index, false, experiment))
			drawSurface(x, y, w, h, surface, alpha);
		if (auto surface = sprite->prepareDrawSurface(index, true, experiment))
		{
			float scale = experiment && sprite->experimentRotated[index] ? 4.0f : 1.0f;
			drawSurface(static_cast<float>(x), static_cast<float>(y), w * surface->getW() / (scale * sprite->getW(index)), h * surface->getH() / (scale * sprite->getH(index)), surface, alpha);
		}
	}

	void DrawableSurface::drawSprite(float x, float y, float w, float h, Sprite *sprite, unsigned index, Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;

		bool experiment = this == _gc && (_gc->getOptionFlags() & GraphicContext::USEGPU);
		if (auto surface = sprite->prepareDrawSurface(index, false, experiment))
			drawSurface(x, y, w, h, surface, alpha);
		if (auto surface = sprite->prepareDrawSurface(index, true, experiment))
		{
			float scale = experiment && sprite->experimentRotated[index] ? 4.0f : 1.0f;
			drawSurface(static_cast<float>(x), static_cast<float>(y), w * surface->getW() / (scale * sprite->getW(index)), h * surface->getH() / (scale * sprite->getH(index)), surface, alpha);
		}
	}

	void DrawableSurface::drawString(int x, int y, Font *font, const std::string &msg, int w, Uint8 alpha)
	{
		std::string output(msg);
		std::string::size_type pos = output.find('\n', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		pos = output.find('\r', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		font->drawString(this, x, y, w, output, alpha);

		///////////// The following code is for translation text shots ////////////
		if(!translationPicturesDirectory.empty())
		{
			for(std::map<std::string, std::string>::iterator i=texts.begin(); i!=texts.end(); ++i)
			{
				if(output.find(i->first)!=std::string::npos)
				{
					int width=font->getStringWidth(i->first.c_str());
					int height=font->getStringHeight(i->first.c_str());
					int startx=font->getStringWidth(output.substr(0, output.find(i->first)).c_str());
					drawSquares.push_back(std::make_tuple(SRectangle(x+startx, y, width, height), i->second, this));
					wroteTexts.insert(i->second);
					texts.erase(i);
					break;
				}
			}
		}
	}

	void DrawableSurface::drawString(float x, float y, Font *font, const std::string &msg, float w, Uint8 alpha)
	{
		std::string output(msg);
		std::string::size_type pos = output.find('\n', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		pos = output.find('\r', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		///////////// The following code is for translation text shots ////////////
		if(!translationPicturesDirectory.empty())
		{
			for(std::map<std::string, std::string>::iterator i=texts.begin(); i!=texts.end(); ++i)
			{
				if(output.find(i->first)!=std::string::npos)
				{
					int width=font->getStringWidth(i->first.c_str());
					int height=font->getStringHeight(i->first.c_str());
					int startx=font->getStringWidth(output.substr(0, output.find(i->first)).c_str());
					drawSquares.push_back(std::make_tuple(SRectangle(int(x+startx), int(y), width, height), i->second, this));
					wroteTexts.insert(i->second);
					texts.erase(i);
					break;
				}
			}
		}
		font->drawString(this, x, y, w, output, alpha);

	}

	void DrawableSurface::drawAlphaMap(const std::valarray<float> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
		assert(mapW * mapH <= static_cast<int>(map.size()));

		for (int dy=0; dy < mapH-1; dy++)
			for (int dx=0; dx < mapW-1; dx++)
				drawFilledRect(x + dx * cellW, y + dy * cellH, cellW, cellH, color.applyMultiplyAlpha((Uint8)(255.0f * map[mapW * dy + dx])));
	}

	void DrawableSurface::drawAlphaMap(const std::valarray<unsigned char> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
		assert(mapW * mapH <= static_cast<int>(map.size()));

		for (int dy=0; dy < mapH-1; dy++)
			for (int dx=0; dx < mapW-1; dx++)
				drawFilledRect(x + dx * cellW, y + dy * cellH, cellW, cellH, color.applyMultiplyAlpha(map[mapW * dy + dx]));
	}

	// compat
	void DrawableSurface::drawString(int x, int y, Font *font, int i)
	{
		std::stringstream str;
		str << i;
		this->drawString(x, y, font, str.str());
	}

	//This code is for the text shot code
	std::map<std::string, std::string> DrawableSurface::texts;
	std::set<std::string> DrawableSurface::wroteTexts;
	std::vector<std::tuple<DrawableSurface::SRectangle, std::string, GAGCore::DrawableSurface*> > DrawableSurface::drawSquares;
	std::string DrawableSurface::translationPicturesDirectory;

	void DrawableSurface::flushTextPictures()
	{
		using namespace GAGCore;
		for(std::vector<std::tuple<SRectangle, std::string, DrawableSurface*> >::iterator i=drawSquares.begin(); i!=drawSquares.end();)
		{
			DrawableSurface toPrint(std::get<2>(*i)->getW(), std::get<2>(*i)->getH());
			toPrint.drawSurface(0, 0, std::get<2>(*i));
			int x=std::get<0>(*i).x;
			int y=std::get<0>(*i).y;
			int width=std::get<0>(*i).w;
			int height=std::get<0>(*i).h;

			toPrint.drawRect(x-2, y-2, width+4, height+4, Color(255, 126, 21));
			toPrint.drawRect(x-3, y-3, width+6, height+6, Color(255, 126, 21));
			toPrint.drawCircle(x+width/2, y+height/2, std::max(width+4, height+4)/2+4, Color(255, 126, 21));
			toPrint.drawCircle(x+width/2, y+height/2, std::max(width+4, height+4)/2+5, Color(255, 126, 21));
			toPrint.drawCircle(x+width/2, y+height/2, std::max(width+4, height+4)/2+6, Color(255, 126, 21));

			// Print it using virtual filesystem
			for (size_t i2 = 0; i2 < Toolkit::getFileManager()->getDirCount(); i2++)
			{
				std::string fullFileName = translationPicturesDirectory + DIR_SEPARATOR_S + "text-" + std::get<1>(*i);
				if (SDL_SaveBMP(toPrint.sdlsurface, (fullFileName+".bmp").c_str()) == 0)
				{
					break;
				}
			}
			i=drawSquares.erase(i);
		}
	}

	void DrawableSurface::printFinishingText()
	{
		if(!texts.empty())
			std::cout<<"The following requested translation texts where never drawn to the screen, or too mangled to be detected:"<<std::endl;
		for(std::map<std::string, std::string>::iterator i=texts.begin(); i!=texts.end(); ++i)
		{
			std::cout<<"\t"<<i->second<<std::endl;
		}
	}

	// Font

	int Font::getStringWidth(const int i)
	{
		std::ostringstream temp;
		temp << i;
		return getStringWidth(temp.str());
	}

	int Font::getStringWidth(const std::string string, int len)
	{
		std::string temp;
		temp.append(string.c_str(), len);
		return getStringWidth(temp);
	}

	int Font::getStringHeight(const std::string string, int len)
	{
		std::string temp;
		temp.append(string.c_str(), len);
		return getStringHeight(temp);
	}

	int Font::getStringHeight(const int i)
	{
		std::ostringstream temp;
		temp << i;
		return getStringHeight(temp.str());
	}
}
