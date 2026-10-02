// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <SpriteDrawBatch.h>
#include <RenderBackend.h>
#ifdef HAVE_OPENGL
#include <AlphaMapRender.h>
#endif
#include <assert.h>
#include <valarray>
#include <vector>
#include <algorithm>
#include <exception>
#include <stdexcept>

namespace GAGCore
{
namespace
{
struct SpriteBatchState
{
	static constexpr size_t maxQuads = 4096;
	static constexpr size_t maxRuns = 32;
	struct Bounds
	{
		float left, top, right, bottom;
		bool overlaps(const Bounds &other) const
		{
			return left < other.right && right > other.left && top < other.bottom &&
				   bottom > other.top;
		}
		void include(const Bounds &other)
		{
			left = std::min(left, other.left);
			top = std::min(top, other.top);
			right = std::max(right, other.right);
			bottom = std::max(bottom, other.bottom);
		}
	};
	struct Quad
	{
		float x, y, w, h, u0, v0, u1, v1;
		int next = -1;
	};
	struct Texture
	{
		const void *key;
		unsigned int id;
		SDL_Surface *pixels;
		std::uint64_t revision;
	};
	struct Run
	{
		Texture texture;
		Uint8 alpha;
		Bounds bounds;
		int first, last;
	};
	GraphicContext *owner = nullptr;
	RenderBackend *backend = nullptr;
	Run runs[maxRuns];
	size_t runCount = 0;
	std::vector<Quad> quads;
	std::vector<float> vertices, coordinates;
	std::vector<SDL_Vertex> portableVertices;

	void clear()
	{
		quads.clear();
		vertices.clear();
		coordinates.clear();
		portableVertices.clear();
		runCount = 0;
	}
	void discard()
	{
		clear();
		owner = nullptr;
		backend = nullptr;
	}

	void flush()
	{
		if (quads.empty())
			return;
		if (backend)
		{
			for (size_t i = 0; i < runCount; ++i)
			{
				const Run &run = runs[i];
				portableVertices.clear();
				const SDL_Color color{255, 255, 255, run.alpha};
				for (int index = run.first; index != -1; index = quads[index].next)
				{
					const Quad &q = quads[index];
					const SDL_Vertex a{{q.x, q.y}, color, {q.u0, q.v0}};
					const SDL_Vertex b{{q.x + q.w, q.y}, color, {q.u1, q.v0}};
					const SDL_Vertex c{{q.x + q.w, q.y + q.h}, color, {q.u1, q.v1}};
					const SDL_Vertex d{{q.x, q.y + q.h}, color, {q.u0, q.v1}};
					portableVertices.insert(portableVertices.end(), {a, b, c, a, c, d});
				}
				backend->triangles(portableVertices, run.texture.key, run.texture.pixels,
								   run.texture.revision);
			}
			clear();
			return;
		}
#ifdef HAVE_OPENGL
		glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glState.doBlend(true);
		glState.doTexture(true);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		for (size_t i = 0; i < runCount; ++i)
		{
			const Run &run = runs[i];
			vertices.clear();
			coordinates.clear();
			for (int index = run.first; index != -1; index = quads[index].next)
			{
				const Quad &q = quads[index];
				vertices.insert(vertices.end(),
								{q.x, q.y, q.x + q.w, q.y, q.x + q.w, q.y + q.h, q.x, q.y + q.h});
				coordinates.insert(coordinates.end(),
								   {q.u0, q.v0, q.u1, q.v0, q.u1, q.v1, q.u0, q.v1});
			}
			glState.setTexture(run.texture.id);
			glColor4ub(255, 255, 255, run.alpha);
			glVertexPointer(2, GL_FLOAT, 0, vertices.data());
			glTexCoordPointer(2, GL_FLOAT, 0, coordinates.data());
			glDrawArrays(GL_QUADS, 0, vertices.size() / 2);
		}
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
		clear();
	}

	bool append(Texture texture, Uint8 alpha, float x, float y, float w, float h, float u0,
				float v0, float u1, float v1)
	{
		if (quads.size() == maxQuads)
			flush();
		const Bounds bounds{std::min(x, x + w), std::min(y, y + h), std::max(x, x + w),
							std::max(y, y + h)};
		size_t target = runCount;
		// Move a quad into an earlier texture run only if it cannot cover
		// any intervening run. Union bounds make this conservative and
		// bound the search to 32 comparisons, rather than all queued quads.
		for (size_t i = runCount; i > 0; --i)
		{
			const Run &run = runs[i - 1];
			if (run.texture.key == texture.key && run.alpha == alpha)
			{
				target = i - 1;
				break;
			}
			if (run.bounds.overlaps(bounds))
				break;
		}
		const bool newSubmission = target == runCount;
		if (newSubmission && runCount == maxRuns)
		{
			flush();
			target = 0;
		}
		const int index = static_cast<int>(quads.size());
		quads.push_back({x, y, w, h, u0, v0, u1, v1});
		if (newSubmission)
			runs[runCount++] = {texture, alpha, bounds, index, index};
		else
		{
			Run &run = runs[target];
			quads[run.last].next = index;
			run.last = index;
			run.bounds.include(bounds);
		}
		return newSubmission;
	}
};
SpriteBatchState spriteBatch;
} // namespace

SpriteDrawBatch::SpriteDrawBatch(GraphicContext *context, Sprite *sprite)
{
	bool enabled = context && context->hasPortableRenderer();
#ifdef HAVE_OPENGL
	enabled = enabled || (context && (context->getOptionFlags() & GraphicContext::USEGPU));
#endif
	// Dynamic team-color surfaces can be evicted while a frame is being
	// drawn. Keep their shader/cache path immediate, outside this batch.
	if (enabled && sprite && !sprite->isDynamicTeamColor())
	{
		if (spriteBatch.owner)
			throw std::logic_error("Sprite draw batches cannot nest");
		context->finishDrawingSprite(sprite, Color::ALPHA_OPAQUE);
		spriteBatch.quads.reserve(SpriteBatchState::maxQuads);
		if (context->hasPortableRenderer())
			spriteBatch.portableVertices.reserve(SpriteBatchState::maxQuads * 6);
		else
		{
			spriteBatch.vertices.reserve(SpriteBatchState::maxQuads * 8);
			spriteBatch.coordinates.reserve(SpriteBatchState::maxQuads * 8);
		}
		spriteBatch.owner = context;
		active = true;
	}
}

SpriteDrawBatch::~SpriteDrawBatch() noexcept(false)
{
	if (active)
	{
		try
		{
			if (std::uncaught_exceptions() == 0)
				spriteBatch.flush();
		}
		catch (...)
		{
			spriteBatch.discard();
			throw;
		}
		spriteBatch.discard();
	}
}

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
        if (renderer) { drawSurface(float(x),float(y),float(sw),float(sh),surface,sx,sy,sw,sh,alpha); return; }
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			drawSurface(x, y, sw, sh, surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(x, y, surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
        if (renderer) { drawSurface(float(x),float(y),float(sw),float(sh),surface,sx,sy,sw,sh,alpha); return; }
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			drawSurface(x, y, static_cast<float>(sw), static_cast<float>(sh), surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(static_cast<int>(x), static_cast<int>(y), surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha)
	{
        if (renderer) { drawSurface(float(x),float(y),float(w),float(h),surface,sx,sy,sw,sh,alpha); return; }
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawSurface(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(x, y, w, h, surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		if (renderer) prepareDraw();
        if (renderer) {
            if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
            auto* pixels=surface->getSDLSurface();
            if (!pixels || pixels->w <= 0 || pixels->h <= 0) return;
            if (spriteBatch.owner == this)
            {
                const float u0 = float(sx)/pixels->w, v0 = float(sy)/pixels->h;
                const float u1 = float(sx+sw)/pixels->w, v1 = float(sy+sh)/pixels->h;
                spriteBatch.backend = renderer;
                if (spriteBatch.append({surface, 0, pixels, surface->contentRevision()},
                                       alpha, x, y, w, h, u0, v0, u1, v1)) ++drawCalls;
                return;
            }
            renderer->blit(surface, pixels, surface->contentRevision(),
                           renderer == softwareRasterizer.get() && surface->hasOpaquePixels(),
                           SDL_Rect{sx,sy,sw,sh}, SDL_FRect{x,y,w,h}, alpha);
            return;
        }
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			// upload
			if (surface->glUploadedRevision != surface->contentRevision())
			{
				// Submit pending quads before the source texture is uploaded again.
				if (spriteBatch.owner == this && !surface->textureInfo)
					spriteBatch.flush();
				surface->uploadToTexture();
			}

			// Bias nearest-neighbour ties toward the same texel for standalone
			// sprites and atlas frames. A symmetric inset crosses texel boundaries
			// in opposite directions depending on floating-point atlas offsets.
			// sqrt(2)/1000 texels avoids alignment with common fractional HiDPI scales.
			const float biasX = 0.00141421356f * surface->texMultX;
			const float biasY = 0.00141421356f * surface->texMultY;
			const float u0 = static_cast<float>(sx) * surface->texMultX + biasX;
			const float u1 = static_cast<float>(sx + sw) * surface->texMultX + biasX;
			const float v0 = static_cast<float>(sy) * surface->texMultY + biasY;
			const float v1 = static_cast<float>(sy + sh) * surface->texMultY + biasY;

			// draw
			if (renderBatch && renderBatch->active())
			{
				const auto *atlas = surface->textureInfo ? surface->textureInfo->sprite : nullptr;
				unsigned tex = atlas ? atlas->atlas->texture : surface->texture;
				ArrayView array{};
				if (surface->highResolutionSampling)
					array = renderBatch->pack(tex);
				std::array<QueueVertex, 8> v{};
				v[0] = {x, y, u0, v0};
				v[1] = {x + w, y, u1, v0};
				v[2] = {x + w, y + h, u1, v1};
				v[3] = {x, y + h, u0, v1};
				for (int j = 0; j < 4; ++j)
				{
					v[j].color = Color(255, 255, 255, alpha);
					v[j].alpha = alpha / 255.f;
					v[j].baseLayer = array.layer;
				}
				renderBatch->append({array.texture ? QueueKey::ArrayTexture : QueueKey::Texture,
									 array.texture ? array.texture : tex, 0, false, false, true, 1},
									v, 4);
				return;
			}
			if (spriteBatch.owner == this)
			{
				const auto *batch = surface->textureInfo ? surface->textureInfo->sprite : nullptr;
				const GLuint texture = batch ? batch->atlas->texture : surface->texture;
				const void *key = batch ? static_cast<const void *>(batch->atlas.get()) : surface;
				if (spriteBatch.append({key, texture, nullptr, 0}, alpha, x, y, w, h, u0, v0,
									   u1, v1))
					++drawCalls;
				return;
			}
			if (!surface->textureInfo) glState.setTexture(surface->texture);
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
				// state change
				glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glState.doBlend(true);
				glState.doTexture(true);
				glColor4ub(255, 255, 255, alpha);

				glState.setTexture(surface->texture);
				++drawCalls;
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
			if(sprite->highResolutionAtlas)finishDrawingSprite(sprite->highResolutionAtlas.get(),alpha);
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
#ifdef GLOB2_WEBGL2
            // The compatibility renderer packs client arrays into its own GPU buffer.
            glVertexPointer(2, GL_FLOAT, 0, sprite->vertices.data());
            glTexCoordPointer(2, GL_FLOAT, 0, sprite->texCoords.data());
#else
			glBindBuffer(GL_ARRAY_BUFFER, sprite->vbo);
			glBufferData(GL_ARRAY_BUFFER, sprite->vertices.size() * sizeof(float), sprite->vertices.data(), GL_STREAM_DRAW);
			glVertexPointer(2, GL_FLOAT, 0, 0);
			glBindBuffer(GL_ARRAY_BUFFER, sprite->texCoordBuffer);
			glBufferData(GL_ARRAY_BUFFER, sprite->texCoords.size() * sizeof(float), sprite->texCoords.data(), GL_STREAM_DRAW);
			glTexCoordPointer(2, GL_FLOAT, 0, 0);
#endif
			++drawCalls;
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
		if (renderBatch)
			renderBatch->barrier();
		if (renderer) prepareDraw();
        if (renderer) {
            if (mapW < 2 || mapH < 2 || size_t(mapW)*size_t(mapH) > map.size()) return;
            for(int j=0;j<mapH-1;++j) for(int i=0;i<mapW-1;++i) {
                auto vertex = [&](float px,float py,float alpha) {
                    return SDL_Vertex{{px,py},{color.r,color.g,color.b,Uint8(std::clamp(alpha,0.0f,1.0f)*255)}, {0,0}};
                };
                float a=map[j*mapW+i]/1.0f, b=map[j*mapW+i+1]/1.0f;
                float c=map[(j+1)*mapW+i+1]/1.0f, d=map[(j+1)*mapW+i]/1.0f;
                float left=x+i*cellW, top=y+j*cellH, right=left+cellW, bottom=top+cellH;
                auto va=vertex(left,top,a), vb=vertex(right,top,b), vc=vertex(right,bottom,c), vd=vertex(left,bottom,d);
                auto center=vertex((left+right)/2,(top+bottom)/2,(a+b+c+d)/4);
                const SDL_Vertex vertices[]={center,va,vb,center,vb,vc,center,vc,vd,center,vd,va};
                renderer->triangles(vertices);
            }
            return;
        }
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
				++drawCalls;glBegin(GL_QUADS);
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
						++drawCalls;glBegin(GL_TRIANGLE_FAN);
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
		if (renderBatch)
			renderBatch->barrier();
		if (renderer) prepareDraw();
        if (renderer) {
            if (mapW < 2 || mapH < 2 || size_t(mapW)*size_t(mapH) > map.size()) return;
            for(int j=0;j<mapH-1;++j) for(int i=0;i<mapW-1;++i) {
                auto vertex = [&](float px,float py,float alpha) {
                    return SDL_Vertex{{px,py},{color.r,color.g,color.b,Uint8(std::clamp(alpha,0.0f,1.0f)*255)}, {0,0}};
                };
                float a=map[j*mapW+i]/255.0f, b=map[j*mapW+i+1]/255.0f;
                float c=map[(j+1)*mapW+i+1]/255.0f, d=map[(j+1)*mapW+i]/255.0f;
                float left=x+i*cellW, top=y+j*cellH, right=left+cellW, bottom=top+cellH;
                auto va=vertex(left,top,a), vb=vertex(right,top,b), vc=vertex(right,bottom,c), vd=vertex(left,bottom,d);
                auto center=vertex((left+right)/2,(top+bottom)/2,(a+b+c+d)/4);
                const SDL_Vertex vertices[]={center,va,vb,center,vb,vc,center,vc,vd,center,vd,va};
                renderer->triangles(vertices);
            }
            return;
        }
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
				++drawCalls;glBegin(GL_QUADS);
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
				++drawCalls;
				drawAlphaMapBatched(map, mapW, mapH, x, y, cellW, cellH, color.r, color.g, color.b);
			}
		}
		else
	#endif
			DrawableSurface::drawAlphaMap(map, mapW, mapH, x, y, cellW, cellH, color);
	}
}
