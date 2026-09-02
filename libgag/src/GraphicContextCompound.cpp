// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <assert.h>
#include <valarray>
#include <vector>

namespace GAGCore
{
	void GraphicContext::drawSurface(int x, int y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(float x, float y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, surface->getTexX(), surface->getTexY(), surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(int x, int y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			drawSurface(x, y, sw, sh, surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(x, y, surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			drawSurface(x, y, static_cast<float>(sw), static_cast<float>(sh), surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(static_cast<int>(x), static_cast<int>(y), surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawSurface(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(x, y, w, h, surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			// upload
			if (surface->dirty)
				surface->uploadToTexture();

			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(true);
			glColor4ub(255, 255, 255, alpha);

			// Sample texel centres: sprites share an atlas, so reaching the outer
			// texel edge blends in the neighbouring frame and seams every tile.
			const float insetX = 0.5f * surface->texMultX;
			const float insetY = 0.5f * surface->texMultY;
			const float u0 = static_cast<float>(sx) * surface->texMultX + insetX;
			const float u1 = static_cast<float>(sx + sw) * surface->texMultX - insetX;
			const float v0 = static_cast<float>(sy) * surface->texMultY + insetY;
			const float v1 = static_cast<float>(sy + sh) * surface->texMultY - insetY;

			// draw
			glState.setTexture(surface->texture);
			if (surface->textureInfo && surface->textureInfo->sprite)
			{
				Sprite* sprite = surface->textureInfo->sprite;
				std::vector<float> oldVertices, oldCoords;
				// If drawing with transparency, save vectors, draw immediately, then restore
				if (alpha != Color::ALPHA_OPAQUE)
				{
					// Fix bug #124 - School renders as white square when placing building and there is no room
					oldVertices = sprite->vertices;
					oldCoords = sprite->texCoords;
					sprite->vertices.clear();
					sprite->texCoords.clear();
				}
				// Queue this draw call until finishDrawingSprite is called.
				sprite->vertices.insert(sprite->vertices.end(), { x, y, x + w, y, x + w, y + h, x, y + h });
				sprite->texCoords.insert(sprite->texCoords.end(), {
					u0, v0,
					u1, v0,
					u1, v1,
					u0, v1
				});
				if (alpha != Color::ALPHA_OPAQUE)
				{
					finishDrawingSprite(sprite, alpha);
					sprite->vertices = oldVertices;
					sprite->texCoords = oldCoords;
				}
			}
			else
			{
				glBegin(GL_QUADS);
				glTexCoord2f(u0, v0);
				glVertex2f(x, y);
				glTexCoord2f(u1, v0);
				glVertex2f(x + w, y);
				glTexCoord2f(u1, v1);
				glVertex2f(x + w, y + h);
				glTexCoord2f(u0, v1);
				glVertex2f(x, y + h);
				glEnd();
			}
		}
		else
		#endif
			DrawableSurface::drawSurface(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), surface, sx, sy, sw, sh, alpha);
	}

	// Lets us efficiently draw terrain and water.
	void GraphicContext::finishDrawingSprite(Sprite* sprite, Uint8 alpha)
	{
#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			if (!sprite->atlas)
			{
				// No sprite sheet, so we have nothing to draw.
				assert(sprite->vertices.empty());
				assert(sprite->texCoords.empty());
				return;
			}
			if (sprite->vertices.empty() || sprite->texCoords.empty())
			{
				// No data.
				return;
			}
			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(true);
			glEnableClientState(GL_VERTEX_ARRAY);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glColor4ub(255, 255, 255, alpha);
			glState.setTexture(sprite->atlas->texture);
			glBindBuffer(GL_ARRAY_BUFFER, sprite->vbo);
			glBufferData(GL_ARRAY_BUFFER, sprite->vertices.size() * sizeof(float), sprite->vertices.data(), GL_STREAM_DRAW);
			glVertexPointer(2, GL_FLOAT, 0, 0);
			glBindBuffer(GL_ARRAY_BUFFER, sprite->texCoordBuffer);
			glBufferData(GL_ARRAY_BUFFER, sprite->texCoords.size() * sizeof(float), sprite->texCoords.data(), GL_STREAM_DRAW);
			glTexCoordPointer(2, GL_FLOAT, 0, 0);
			glDrawArrays(GL_QUADS, 0, sprite->vertices.size() / 2);

			sprite->vertices.clear();
			sprite->texCoords.clear();

			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glDisableClientState(GL_VERTEX_ARRAY);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
#endif
	}

	void GraphicContext::drawAlphaMap(const std::valarray<float> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
	#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			assert(mapW * mapH <= static_cast<int>(map.size()));
			float fr = 255.0f*(float)color.r;
			float fg = 255.0f*(float)color.g;
			float fb = 255.0f*(float)color.b;
			if (EXPERIMENTAL) {
				GLuint texture[1];
				GLboolean old_blend;                //var to store blend state
				glGetBooleanv(GL_BLEND,&old_blend); //store blend state
				glEnable(GL_BLEND);                 //enable blend
				GLboolean old_texture_2d;
				glGetBooleanv(GL_TEXTURE_2D,&old_texture_2d);
				glEnable(GL_TEXTURE_2D);
				std::valarray<GLfloat> image(mapW*mapH);
				for (int i=0; i<mapH; i++)
					for (int j=0; j<mapW;j++)
						image[i*mapW+j]=map[mapW*i+j];
				glColor4ub(fr, fg, fb, 255);
				glGenTextures(1, &texture[0]);
				glBindTexture(GL_TEXTURE_2D, texture[0]);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,mapW,mapH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, &image[0]);
				glBindTexture( GL_TEXTURE_2D, texture[0] );
				glBegin(GL_QUADS);
				glTexCoord2f( 1.0f, 0.0f ); glVertex2f(x+mapW*cellW,y+0);
				glTexCoord2f( 0.0f, 0.0f ); glVertex2f(x+0         ,y+0);
				glTexCoord2f( 0.0f, 1.0f ); glVertex2f(x+0         ,y+mapH*cellH);
				glTexCoord2f( 1.0f, 1.0f ); glVertex2f(x+mapW*cellW,y+mapH*cellH);
				glEnd( );
				if(!old_blend)
					glDisable(GL_BLEND);
				if(!old_texture_2d)
					glDisable(GL_TEXTURE_2D);
			} else {
				glState.doBlend(true);
				glState.doTexture(false);
				for (int dy=0; dy < mapH-1; dy++)
				{
					int midy = y + dy * cellH + cellH/2;
					for (int dx=0; dx < mapW-1; dx++)
					{
						glBegin(GL_TRIANGLE_FAN);
						//This interpolates to find the center color, then fans out to the four corners.
						int midx = x + dx * cellW + cellW/2;
						float mid_top_alpha = (map[mapW * dy + dx] + map[mapW * dy + dx + 1])/2;
						float mid_bottom_alpha = (map[mapW * (dy + 1) + dx] + map[mapW * (dy + 1) + dx + 1])/2;
						glColor4f(fr, color.g, color.b, (mid_top_alpha + mid_bottom_alpha) / 2);
						glVertex2f(midx, midy);
						//Touch each of the four corners
						glColor4f(fr, fg, fb, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glColor4f(fr, fg, fb, map[mapW * (dy + 1) + dx]);
						glVertex2f(x + dx * cellW, y + (dy + 1) * cellH);

						glColor4f(fr, fg, fb, map[mapW * (dy + 1) + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + (dy + 1) * cellH);
						glColor4f(fr, fg, fb, map[mapW * dy + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + dy * cellH);

						glColor4f(fr, fg, fb, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glEnd();
					}
				}
			}
		}
		else
	#endif
			DrawableSurface::drawAlphaMap(map, mapW, mapH, x, y, cellW, cellH, color);
	}

	void GraphicContext::drawAlphaMap(const std::valarray<unsigned char> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
	#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			assert(mapW * mapH <= static_cast<int>(map.size()));
			if(EXPERIMENTAL) {
				glPushMatrix();
				glEnable(GL_BLEND);
				glEnable(GL_TEXTURE_2D);
/*				glState.resetCache();
				bool oldBlend=glState.doBlend(true);
				bool oldTexture=glState.doTexture(true);*/
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				std::valarray<GLubyte> image(mapW*mapH);
				for (int i=0; i<mapH; i++)
					for (int j=0; j<mapW;j++)
						image[i*mapW+j]=map[mapW*i+j];
				glColor4ub(color.r, color.g, color.b, color.a);
				GLuint texture[1];
				glGenTextures(1, &texture[0]);
				glBindTexture(GL_TEXTURE_2D, texture[0]);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,mapW,mapH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, &image[0]);
				glBegin(GL_QUADS);
					glTexCoord2f( 1.0f, 0.0f ); glVertex2f(x+mapW*cellW,y+0);
					glTexCoord2f( 0.0f, 0.0f ); glVertex2f(x+0         ,y+0);
					glTexCoord2f( 0.0f, 1.0f ); glVertex2f(x+0         ,y+mapH*cellH);
					glTexCoord2f( 1.0f, 1.0f ); glVertex2f(x+mapW*cellW,y+mapH*cellH);
				glEnd( );
				glPopMatrix();
				//uploadToTexture();
//				glState.doBlend(oldBlend);
//				glState.doTexture(oldTexture);
			} else {
				glState.doBlend(true);
				glState.doTexture(false);
				for (int dy=0; dy < mapH-1; dy++)
				{
					int midy = y + dy * cellH + cellH/2;
					for (int dx=0; dx < mapW-1; dx++)
					{

						glBegin(GL_TRIANGLE_FAN);
						//This interpolates to find the center color, then fans out to the four corners.
						int midx = x + dx * cellW + cellW/2;
						int mid_top_alpha = (map[mapW * dy + dx] + map[mapW * dy + dx + 1])/2;
						int mid_bottom_alpha = (map[mapW * (dy + 1) + dx] + map[mapW * (dy + 1) + dx + 1])/2;
						glColor4ub(color.r, color.g, color.b, (mid_top_alpha + mid_bottom_alpha) / 2);
						glVertex2f(midx, midy);
						//Touch each of the four corners
						glColor4ub(color.r, color.g, color.b, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glColor4ub(color.r, color.g, color.b, map[mapW * (dy + 1) + dx]);
						glVertex2f(x + dx * cellW, y + (dy + 1) * cellH);

						glColor4ub(color.r, color.g, color.b, map[mapW * (dy + 1) + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + (dy + 1) * cellH);
						glColor4ub(color.r, color.g, color.b, map[mapW * dy + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + dy * cellH);

						glColor4ub(color.r, color.g, color.b, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glEnd();
					}
				}
			}
		}
		else
	#endif
			DrawableSurface::drawAlphaMap(map, mapW, mapH, x, y, cellW, cellH, color);
	}
}
