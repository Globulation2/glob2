// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include "SpriteHighResolution.h"
#include <math.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <SpriteLoad.h>
#include <AssetLoader.h>
#include <RenderDevice.h>

#include <memory>
#include <deque>
using std::make_unique;

#define GL_GLEXT_PROTOTYPES
#ifdef HAVE_OPENGL
#if defined(GLOB2_WEBGL2)
#include <GL/gl.h>
#include <GL/glext.h>
#elif defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/glu.h>
#define GL_TEXTURE_RECTANGLE_NV GL_TEXTURE_RECTANGLE_EXT
#else
#include <epoxy/gl.h>
#ifdef _WIN32
#include <epoxy/wgl.h>
#else
#include <epoxy/glx.h>
#endif // _WIN32
#endif // defined(__APPLE__)
#endif // ifdef HAVE_OPENGL


namespace GAGCore
{
	static std::set<Sprite*> loadedSprites;
	static bool highResolutionEnabled = false;
    static size_t requestedHighResolutionGeneration = 0;
    static bool requestedHighResolutionMipmaps = false;
    static bool requestedHighResolutionCPUChain = false;
    static std::string requestedHighResolutionDirectory;
    static std::string packDirectory, packText;
    static bool packRead=false;
    static size_t packGeneration = 0;
    struct PackEntry {
        int width, height, scale;
        std::string base, team;
    };
    static std::unordered_map<std::string, PackEntry> packEntries;
    static AssetLoader::Handle<AssetLoader::Bytes> packSource;
    struct HighResolutionReload {
        HighResolutionInputs inputs;
        std::unique_ptr<Sprite> prepared;
        size_t frame = 0;
        bool retainPublishedOnFailure = false;
    };
    static std::map<Sprite*, HighResolutionReload> pendingHighResolution;
    static std::set<std::string> deferredHighResolutionNames;
    struct DeferredHighResolution {
        std::set<unsigned> wanted, completed, active;
        std::map<unsigned, HighResolutionInputs> pending;
        std::deque<unsigned> queued;
        std::vector<unsigned> order;
    };
    static std::map<Sprite*, DeferredHighResolution> deferredHighResolution;
    static Sprite *deferredHighResolutionCursor = nullptr;
    static bool validHighResolutionFile(const std::string& file);

    // Source decoding and standalone frame preparation work with either texture
    // backend. Only packing those frames into the legacy atlas requires GL.
    // SDL's portable renderer consumes only the base image; requesting a mip
    // chain there duplicates work and prevents adopting the base-only prefetch.
    static bool highResolutionFramesSupported()
    {
        return _gc && _gc->rendererCapabilities().textureFiltering;
    }
    static bool readPack(const std::string &directory)
    {
        const auto generation = Toolkit::assets().sourceGeneration();
        if(packRead && packDirectory==directory && packGeneration==generation)return !packText.empty();
        packGeneration= generation;
        packRead=true;packDirectory=directory;packText.clear();
        packEntries.clear();
        packSource = Toolkit::assets().requestBytes(directory + "/frames.txt");
        auto bytes = Toolkit::assets().wait(packSource);
        if (!bytes || bytes->empty() || bytes->size() > 8 * 1024 * 1024) return false;
        std::string text(bytes->begin(), bytes->end());
        std::istringstream header(text);std::string magic;int version=0;header>>magic>>version;
        if(magic!="GLOB2_HIGHRES"||version!=1){std::cerr<<"Unsupported high-resolution pack"<<std::endl;return false;}
        std::string id; PackEntry entry;
        while (header >> id >> entry.width >> entry.height >> entry.scale >> entry.base >> entry.team)
            packEntries.emplace(id, entry);
        packText=std::move(text);return true;
    }

	Sprite::RotatedImage::~RotatedImage()
	{
		delete orig;
		for (RotationMap::iterator it = rotationMap.begin(); it != rotationMap.end(); ++it)
		{
			delete it->second;
		}
	}
	
    void Sprite::registerLoaded() {
        loadedSprites.insert(this);
        for (std::size_t index=0;index<images.size();++index) if(images[index]) {
            images[index]->spriteSource=this; images[index]->spriteFrame=index;
        }
    }
    void Sprite::adoptLoaded(Sprite& prepared)
    {
        assert(images.empty() && rotated.empty());
        fileName = std::move(prepared.fileName);
        dynamicTeamColor = prepared.dynamicTeamColor;
        images.swap(prepared.images); rotated.swap(prepared.rotated);
        experimentImages.swap(prepared.experimentImages); experimentRotated.swap(prepared.experimentRotated);
        highResolutionAtlas = std::move(prepared.highResolutionAtlas);
        blockCompleteHD = std::move(prepared.blockCompleteHD);
        atlas = std::move(prepared.atlas);
        atlasStamps = std::move(prepared.atlasStamps); atlasRegions = std::move(prepared.atlasRegions);
        atlasMemory = std::move(prepared.atlasMemory); atlasVariableSizes = prepared.atlasVariableSizes;
        for (auto *image : images) if (image && image->textureInfo) image->textureInfo->sprite = this;
#ifdef HAVE_OPENGL
        vbo = std::exchange(prepared.vbo, 0); texCoordBuffer = std::exchange(prepared.texCoordBuffer, 0);
        bufferGeneration = prepared.bufferGeneration;

#endif
        registerLoaded();
    }
    bool Sprite::load(const std::string filename)
    {
        SpriteLoad loading(filename);
        while (!loading.poll()) {
#ifndef __EMSCRIPTEN__
            SDL_Delay(1);
#endif
        }
        auto result = loading.take();
        if (!result) return false;
        adoptLoaded(*result);
        return getFrameCount() > 0;
    }

#ifdef DEBUG_SPRITE_NOT_DRAWN
	std::vector<Sprite*> Sprite::sprites;
#endif

	void Sprite::checkAllSpritesDrawn()
	{
#ifdef DEBUG_SPRITE_NOT_DRAWN
		for (const Sprite* sprite : sprites)
			if (sprite->vertices.size() || sprite->texCoords.size())
			{
				std::cout << "Warning: Sprite " << sprite->fileName << " has not been drawn" << std::endl;
			}
#endif
	}

	// Create texture atlas for images array
	// Using a sprite sheet lets us efficiently drawn terrain and water with a few calls
	// to glDrawArrays, rather than 272 individual calls to glBegin...glEnd.
    bool Sprite::createDeviceTextureAtlas(int maximumTextureSize, bool variableSizes) {
        if (images.empty() || dynamicTeamColor || std::any_of(rotated.begin(),rotated.end(),[](auto value){return value!=nullptr;})) return false;
        std::vector<SpriteAtlasSource> sources; sources.reserve(images.size());
        for (std::size_t index=0;index<images.size();++index) {
            auto *image=images[index]; if(!image || image->highResolutionSampling || !image->sdlsurface)return false;
            image->spriteSource=this;image->spriteFrame=index;
            sources.push_back({image->sdlsurface,{image->lifetimeIdentity(),image->contentRevision(),image->sdlsurface->w,image->sdlsurface->h}});
        }
        const bool unchanged=atlas && atlasStamps.size()==sources.size() && atlasRegions.size()==sources.size() &&
            std::equal(atlasStamps.begin(),atlasStamps.end(),sources.begin(),[](const auto &a,const auto &b){return a==b.stamp;}) &&
            atlas->sdlsurface->w<=std::min(2048,maximumTextureSize) && atlas->sdlsurface->h<=std::min(2048,maximumTextureSize);
        if(unchanged) {
            if(!atlasMemory)atlasMemory=reserveSpriteAtlasMemory(std::size_t(atlas->sdlsurface->pitch)*atlas->sdlsurface->h);
            return bool(atlasMemory);
        }
        // Device packets already own old pixel revisions. Retire the source page
        // before admission so a full CPU cache can still replace a changed page.
        atlas.reset();atlasMemory.reset();atlasStamps.clear();atlasRegions.clear();
        for(auto *image:images)image->textureInfo.reset();
        auto page=prepareSpriteRenderAtlas(sources,maximumTextureSize,variableSizes);
        if(!page)return false;
        AssetImage image(page->pixels.release());
        auto replacement=DrawableSurface::fromAssetImage(image,true,false);
        atlas=std::move(replacement);atlasMemory=std::move(page->memory);
        atlasStamps=std::move(page->stamps);atlasRegions=std::move(page->regions);return true;
    }
    bool Sprite::resolveDeviceAtlas(DrawableSurface &surface,SDL_Rect &source,const DrawableSurface *&upload) {
        if(surface.spriteSource!=this || surface.spriteFrame>=images.size() || images[surface.spriteFrame]!=&surface ||
            !Toolkit::gc || !Toolkit::gc->renderDevice() || surface.highResolutionSampling)return false;
        if(!createDeviceTextureAtlas(Toolkit::gc->renderDevice()->capabilities().maximumTextureSize,atlasVariableSizes))return false;
        const auto &region=atlasRegions[surface.spriteFrame];
        if(source.x<0 || source.y<0 || source.w<=0 || source.h<=0 || source.x>region.w-source.w || source.y>region.h-source.h)return false;
        source.x+=region.x;source.y+=region.y;upload=atlas.get();return true;
    }
	bool Sprite::createTextureAtlas(bool allowVariableSizes)
	{
        atlasVariableSizes=allowVariableSizes;
        if (Toolkit::gc && Toolkit::gc->renderDevice())
            return createDeviceTextureAtlas(Toolkit::gc->renderDevice()->capabilities().maximumTextureSize, allowVariableSizes);

#ifdef HAVE_OPENGL
		if (!Toolkit::gc || !(Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU))
			return false;
        if (atlas && atlasMemory) {
            atlas.reset(); atlasMemory.reset(); atlasStamps.clear(); atlasRegions.clear();
            for (auto *image:images) if(image)image->textureInfo.reset();
        }
		if (atlas)
			return true;
		if (images.empty())
			return false;
#ifdef DEBUG_SPRITE_NOT_DRAWN
		sprites.push_back(this);
#endif
		size_t numImages = images.size();
		int tileWidth = 0, tileHeight = 0;
		static int maxTextureSize = 0;
		if (!maxTextureSize)
		{
			glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
			assert(maxTextureSize);
		}
		// The normal automatic path retains its equal-size requirement. Resource
		// rendering explicitly opts into padded cells for variable-size frames.
		for (auto image : images)
		{
			if (!image)
				return false; // One of the images is null
			if (!tileWidth || !tileHeight)
			{
				tileWidth = image->getW();
				tileHeight = image->getH();
			}
			if (!allowVariableSizes && (image->getW() != tileWidth || image->getH() != tileHeight))
				return false;
			tileWidth = std::max(tileWidth, image->getW());
			tileHeight = std::max(tileHeight, image->getH());
		}
		// Even nearest sampling can reach just outside a frame at fractional
		// zoom because of the texel tie bias. Isolate every frame, not only
		// variable-size resource sprites.
		const int padding = 1;
		tileWidth += 2 * padding;
		tileHeight += 2 * padding;
		// Use only the cells needed. An extra empty column can make a padded
		// power-of-two frame (notably water) double the GPU texture dimensions.
		const int columns = static_cast<int>(std::ceil(std::sqrt(numImages)));
		const int rows = (static_cast<int>(numImages) + columns - 1) / columns;
		int sheetWidth = tileWidth * columns;
		int sheetHeight = tileHeight * rows;
		if (sheetWidth > maxTextureSize || sheetHeight > maxTextureSize)
		{
			std::cerr << "Warning: Sprite sheet " << fileName << " with size " << sheetWidth << "x" << sheetHeight
				<< " exceeds your graphics card's maximum texture size of " << maxTextureSize << std::endl;
			return false; // We can't continue, falling back to glBegin/glEnd rendering.
		}
		std::unique_ptr<DrawableSurface> atlas = make_unique<DrawableSurface>(sheetWidth, sheetHeight);
		int x = padding, y = padding;
		for (auto image: images)
		{
			const int width = image->getW(), height = image->getH();
			{
				// Copy straight RGBA and extrude the border so linear filtering
				// matches each standalone texture, without bleeding adjacent frames.
				SDL_BlendMode blend;
				SDL_GetSurfaceBlendMode(image->sdlsurface, &blend);
				SDL_SetSurfaceBlendMode(image->sdlsurface, SDL_BLENDMODE_NONE);
				int sx[3] = {0, 0, width - 1}, sy[3] = {0, 0, height - 1};
				int dx[3] = {x - 1, x, x + width}, dy[3] = {y - 1, y, y + height};
				int widths[3] = {1, width, 1}, heights[3] = {1, height, 1};
				for (int row = 0; row < 3; ++row)
					for (int col = 0; col < 3; ++col)
					{
						SDL_Rect src = {sx[col], sy[row], widths[col], heights[row]};
						SDL_Rect dest = {dx[col], dy[row], widths[col], heights[row]};
						SDL_BlitSurface(image->sdlsurface, &src, atlas->sdlsurface, &dest);
					}
				SDL_SetSurfaceBlendMode(image->sdlsurface, blend);
			}
			TextureInfo info = { this, x, y, width, height };
			image->textureInfo = info;
			image->texMultX = atlas->texMultX;
			image->texMultY = atlas->texMultY;
			x += tileWidth;
			if (tileWidth + x - padding > sheetWidth) {
				x = padding;
				y += tileHeight;
			}
		}
		atlas->uploadToTexture();
        atlasStamps.clear(); atlasRegions.clear();
        for (auto *image:images) {
            image->spriteSource=this; image->spriteFrame=atlasStamps.size();
            atlasStamps.push_back({image->lifetimeIdentity(),image->contentRevision(),image->sdlsurface->w,image->sdlsurface->h});
            atlasRegions.push_back({image->textureInfo->texX,image->textureInfo->texY,image->textureInfo->w,image->textureInfo->h});
        }
		this->atlas = std::move(atlas);
#ifndef GLOB2_WEBGL2
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &texCoordBuffer);
        bufferGeneration = Toolkit::gc->getGLContextGeneration();
#endif
		return true; // Success
#else
		return false;
#endif
	}
	
	void Sprite::recomputeBlockCompleteHD()
	{
		const size_t blockCount = (images.size() + 31) / 32;
		blockCompleteHD.assign(blockCount, true);
		for (size_t block = 0; block < blockCount; ++block)
		{
			const int blockStart = static_cast<int>(block * 32);
			const int blockEnd = std::min(blockStart + 32, static_cast<int>(images.size()));
			for (int i = blockStart; i < blockEnd; ++i)
				if ((images[i] && !experimentImages[i]) || (rotated[i] && !experimentRotated[i]))
				{
					blockCompleteHD[block] = false;
					break;
				}
		}
	}

	bool Sprite::blockHasCompleteHD(int index) const
	{
		const size_t block = static_cast<size_t>(index) / 32;
		return block < blockCompleteHD.size() && blockCompleteHD[block];
	}

	DrawableSurface *Sprite::getRotatedSurface(int index)
	{
		return getColoredSurface(index, false);
	}

	float Sprite::teamHueShiftDegrees()
	{
		float baseHue, actHue, lum, sat;
		Color(51, 255, 153).getHSV(&baseHue, &sat, &lum);
		actColor.getHSV(&actHue, &sat, &lum);
		return actHue - baseHue;
	}

	void Sprite::applyTeamHueShift(DrawableSurface &surface)
	{
		surface.shiftHSV(teamHueShiftDegrees(), 0.0f, 0.0f);
	}

	DrawableSurface *Sprite::lookupTeamColor(const TeamColorKey &key)
	{
		auto it = teamColorIndex.find(key);
		if (it == teamColorIndex.end())
			return nullptr;
		// Touch: splice to the front (most-recently-used end) in O(1), no copy.
		teamColorList.splice(teamColorList.begin(), teamColorList, it->second);
		return teamColorList.front().surface.get();
	}

	void Sprite::insertTeamColor(const TeamColorKey &key, std::unique_ptr<DrawableSurface> surface, size_t bytes)
	{
		// Evict least-recently-used entries first, so at most one active,
		// oversized entry (bigger than the whole cap by itself) is ever retained,
		// and only until a distinct entry next needs the room.
		while (!teamColorList.empty() && teamColorBytes + bytes > teamColorCacheCap)
		{
			teamColorBytes -= teamColorList.back().bytes;
			teamColorIndex.erase(teamColorList.back().key);
			teamColorList.pop_back();
		}
		teamColorList.push_front(TeamColorNode{key, std::move(surface), bytes});
		teamColorIndex[key] = teamColorList.begin();
		teamColorBytes += bytes;
	}

	void Sprite::clearTeamColorCache()
	{
		teamColorList.clear();
		teamColorIndex.clear();
		teamColorBytes = 0;
	}

	DrawableSurface *Sprite::getColoredSurface(int index, bool experiment)
	{
		RotatedImage *image = experiment ? experimentRotated[index] : rotated[index];
		assert(image);
		if (dynamicTeamColor)
		{
			// Bounded software/shader-unavailable fallback: keyed by source frame,
			// resolution (native/HD) and team color, never by the composited result.
			TeamColorKey key{index, experiment, actColor.r, actColor.g, actColor.b};
			if (DrawableSurface *hit = lookupTeamColor(key))
				return hit;
			std::unique_ptr<DrawableSurface> ds(image->orig->clone());
			applyTeamHueShift(*ds);
			// Count the first GPU upload toward the budget, matching every other
			// GPU-backed entry rather than only discovering its cost on next draw.
			if (Toolkit::gc && (Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU))
				ds->uploadToTexture();
			const size_t bytes = static_cast<size_t>(ds->getW()) * ds->getH() * 4 + ds->gpuBytes;
			DrawableSurface *raw = ds.get();
			insertTeamColor(key, std::move(ds), bytes);
			return raw;
		}
		RotatedImage::RotationMap::const_iterator it = image->rotationMap.find(actColor);
		DrawableSurface *ds;
		if (it == image->rotationMap.end())
		{
			ds = image->orig->clone();
			applyTeamHueShift(*ds);
			image->rotationMap[actColor] = ds;
		}
		else
		{
			ds = it->second;
		}
		return ds;
	}

    Sprite::HighResolutionStats Sprite::highResolutionStats()
    {
        HighResolutionStats stats;
        for(auto sprite:loadedSprites)
        {
            for(auto s:sprite->experimentImages)if(s)stats.cpuBytes+=s->getW()*s->getH()*4;
            for(auto r:sprite->experimentRotated)if(r)
            {
                stats.cpuBytes+=r->orig->getW()*r->orig->getH()*4;
                for(auto entry:r->rotationMap){stats.cpuBytes+=entry.second->getW()*entry.second->getH()*4;++stats.coloredFrames;}
            }
            // dynamicTeamColor sprites (the unit sprite) never populate the
            // rotationMap above; count their bounded cache's CPU pixels instead,
            // native and HD entries alike. It clears on the same reload as the
            // maps above, so this stays consistent with the cpuBytes==0 checks.
            if(sprite->dynamicTeamColor)
                for(auto &node:sprite->teamColorList)
                {stats.cpuBytes+=static_cast<size_t>(node.surface->getW())*node.surface->getH()*4;++stats.coloredFrames;}
#ifdef HAVE_OPENGL
            if(sprite->highResolutionAtlas)stats.cpuBytes+=sprite->highResolutionAtlas->atlas->sdlsurface->w*sprite->highResolutionAtlas->atlas->sdlsurface->h*4;
#endif
        }
        return stats;
    }

    bool Sprite::highResolutionDeferred(const std::string& name)
    {
        return deferredHighResolutionNames.contains(name);
    }
    void Sprite::deferHighResolution(const std::string& name)
    {
        deferredHighResolutionNames.insert(name);
    }
    void Sprite::setHighResolutionFrames(std::span<const unsigned> indices)
    {
        if (!highResolutionDeferred(fileName)) return;
        std::set<unsigned> wanted;
        for (unsigned frame : indices) if (frame < images.size()) wanted.insert(frame);
        auto &stream = deferredHighResolution[this];
        if (wanted == stream.wanted) return;
        stream.wanted = std::move(wanted);
        stream.order.assign(indices.begin(), indices.end());
        // Preserve overlapping in-flight subscriptions as animation advances.
        // Cancelling the whole window here can prevent later variants decoding.
        std::erase_if(stream.pending, [&](const auto &item) { return !stream.wanted.contains(item.first); });
        std::erase_if(stream.completed, [&](unsigned frame) { return !stream.wanted.contains(frame); });
        std::erase_if(stream.active, [&](unsigned frame) { return !stream.wanted.contains(frame); });
        std::erase_if(stream.queued, [&](unsigned frame) { return !stream.wanted.contains(frame); });
        experimentImages.resize(images.size(), nullptr);
        experimentRotated.resize(images.size(), nullptr);
        // Readers own copied pixels, never a borrowed pointer across this declaration.
        for (unsigned frame = 0; frame < images.size(); ++frame) {
            if (!stream.wanted.contains(frame)) {
                delete experimentImages[frame]; experimentImages[frame] = nullptr;
                delete experimentRotated[frame]; experimentRotated[frame] = nullptr;
            }
        }
        recomputeBlockCompleteHD();
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        if (!readPack(directory)) return;
        const auto prefix = fileName.substr(fileName.find_last_of('/') + 1);
        const bool mipmaps = false; // Deferred terrain is sampled by CPU page composition.
        // Preserve caller order, which interleaves current/next animation endpoints.
        for (unsigned frame : indices) {
            if (!stream.wanted.contains(frame)) continue;
            if (experimentImages[frame] || stream.pending.contains(frame) || stream.completed.contains(frame)) continue;
            auto entry = packEntries.find(prefix + std::to_string(frame));
            if (entry == packEntries.end()) continue;
            auto &inputs = stream.pending[frame];
            stream.queued.push_back(frame);
            for (const auto &file : {entry->second.base, entry->second.team})
                if (validHighResolutionFile(file)) inputs.add(directory + '/' + file, mipmaps);
        }
    }

    void Sprite::requestHighResolution(bool enabled)
    {
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        const bool mipmaps = _gc && _gc->rendererCapabilities().textureMipmaps;
        requestedHighResolutionCPUChain = mipmaps && _gc && _gc->renderDevice();
        const bool sameSource = highResolutionEnabled == enabled &&
            requestedHighResolutionGeneration == Toolkit::assets().sourceGeneration() &&
            requestedHighResolutionDirectory == directory;
        if (sameSource && requestedHighResolutionMipmaps == mipmaps) return;
        const bool retainPublishedOnFailure = sameSource;
        requestedHighResolutionMipmaps = mipmaps;
        highResolutionEnabled = enabled;
        packRead = false; packText.clear(); packEntries.clear(); packSource = {};
        pendingHighResolution.clear();
        Toolkit::assets().invalidate();
        requestedHighResolutionGeneration = Toolkit::assets().sourceGeneration();
        requestedHighResolutionDirectory = directory;
        for (auto *sprite : loadedSprites) {
            if (highResolutionDeferred(sprite->fileName)) {
                auto &stream = deferredHighResolution[sprite];
                auto wanted = stream.order;
                stream.pending.clear(); stream.completed.clear(); stream.wanted.clear();
                stream.active.clear(); stream.queued.clear();
                for (auto *&image : sprite->experimentImages) { delete image; image = nullptr; }
                for (auto *&image : sprite->experimentRotated) { delete image; image = nullptr; }
                sprite->highResolutionAtlas.reset();
                sprite->recomputeBlockCompleteHD();
                sprite->setHighResolutionFrames(wanted);
                continue;
            }
            auto prepared = std::make_unique<Sprite>();
            prepared->fileName = sprite->fileName;
            prepared->dynamicTeamColor = sprite->dynamicTeamColor;
            pendingHighResolution.emplace(sprite, HighResolutionReload{
                prefetchHighResolutionIncremental(sprite->fileName, sprite->images.size()), std::move(prepared), 0, retainPublishedOnFailure});
        }
    }
    void Sprite::refreshHighResolutionForRenderer()
    {
        // Standalone graphics contexts can switch without an asset toolkit.
        // Artwork policy is initialized when the application loads its assets.
        if (!Toolkit::getFileManager()) return;
        // Backend changes preserve the artwork preference established by the
        // application or caller; only its preparation policy is refreshed.
        const bool needsCPUChain = _gc && _gc->renderDevice() && _gc->rendererCapabilities().textureMipmaps;
        if (needsCPUChain && !requestedHighResolutionCPUChain) {
            for (const auto *sprite : loadedSprites) {
                for (auto *image : sprite->experimentImages) {
                    if (image && (image->getW() > 1 || image->getH() > 1) && image->preparedMips.empty()) {
                        // Legacy GL may have retired its CPU chain after upload.
                        // Force one reload when entering a device which restores
                        // from owned levels; repeated failed refreshes stay terminal.
                        requestedHighResolutionMipmaps = false;
                        break;
                    }
                }
            }
        }
        requestHighResolution(highResolutionEnabled);
    }
    bool Sprite::pollHighResolution(unsigned budgetMs)
    {
        return pollHighResolutionUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs));
    }
    bool Sprite::pollHighResolutionUntil(std::chrono::steady_clock::time_point deadline)
    {
        for (auto it = pendingHighResolution.begin(); it != pendingHighResolution.end();) {
            if (std::chrono::steady_clock::now() >= deadline) break;
            auto *sprite = it->first;
            auto &reload = it->second;
            // Read native dimensions from the live sprite, but build every HD
            // layer and its atlas in a private target until publication.
            while (reload.frame < sprite->images.size()) {
                if (!highResolutionFrameReady(sprite->fileName, reload.frame, reload.inputs)) break;
                sprite->appendHighResolutionFrame(reload.frame++, *reload.prepared, reload.inputs);
                if (std::chrono::steady_clock::now() >= deadline) return false;
            }
            if (reload.frame < sprite->images.size() ||
                !highResolutionAtlasReady(sprite->fileName, reload.inputs) ||
                reload.inputs.pending()) {
                ++it; continue;
            }
            reload.prepared->createHighResolutionAtlas(reload.inputs);
            if (reload.retainPublishedOnFailure) {
                for (size_t frame = 0; frame < sprite->experimentImages.size(); ++frame) {
                    if (!reload.prepared->experimentImages[frame]) {
                        std::swap(reload.prepared->experimentImages[frame], sprite->experimentImages[frame]);
                        std::swap(reload.prepared->experimentRotated[frame], sprite->experimentRotated[frame]);
                    }
                }
            }
            sprite->clearTeamColorCache();
            sprite->highResolutionAtlas.swap(reload.prepared->highResolutionAtlas);
            sprite->experimentImages.swap(reload.prepared->experimentImages);
            sprite->experimentRotated.swap(reload.prepared->experimentRotated);
            sprite->recomputeBlockCompleteHD();
            it = pendingHighResolution.erase(it);
        }
        bool streamed = true;
        auto streamIt = deferredHighResolution.upper_bound(deferredHighResolutionCursor);
        if (streamIt == deferredHighResolution.end()) streamIt = deferredHighResolution.begin();
        for (size_t bank = 0; bank < deferredHighResolution.size(); ++bank) {
            auto &[sprite, stream] = *streamIt;
            if (++streamIt == deferredHighResolution.end()) streamIt = deferredHighResolution.begin();
            // Rotate banks when a short presentation budget expires, so one
            // animated bank cannot monopolize every poll.
            deferredHighResolutionCursor = sprite;
            bool changed = false;
            const auto expired = [&] {
                if (std::chrono::steady_clock::now() < deadline) return false;
                if (changed) sprite->recomputeBlockCompleteHD();
                return true;
            };
            // Publish before refilling so completed decodes immediately release
            // slots. Refill within this same deadline instead of leaving workers
            // idle until the next presentation turn.
            for (;;) {
                bool progress = false;
                for (auto it = stream.active.begin(); it != stream.active.end();) {
                    if (expired()) return false;
                    auto found = stream.pending.find(*it);
                    if (found->second.pending()) { ++it; continue; }
                    sprite->appendHighResolutionFrame(found->first, *sprite, found->second);
                    stream.completed.insert(found->first);
                    stream.pending.erase(found);
                    it = stream.active.erase(it);
                    changed = progress = true;
                }
                while (stream.active.size() < 64 && !stream.queued.empty()) {
                    if (expired()) return false;
                    const unsigned frame = stream.queued.front(); stream.queued.pop_front();
                    auto found = stream.pending.find(frame);
                    highResolutionFrameReady(sprite->fileName, frame, found->second);
                    stream.active.insert(frame);
                    progress = true;
                }
                if (!progress) break;
            }
            if (changed) sprite->recomputeBlockCompleteHD();
            streamed &= stream.pending.empty();
        }
        return pendingHighResolution.empty() && streamed;
    }
    void Sprite::setHighResolution(bool enabled)
    {
        // Explicit synchronous reloads refresh assets even when the preference is unchanged.
        requestedHighResolutionGeneration = 0;
        requestHighResolution(enabled);
        while (!pollHighResolution(4)) {
            Toolkit::assets().poll();
#ifndef __EMSCRIPTEN__
            SDL_Delay(1);
#endif
        }
    }

    void Sprite::flushBatches(GraphicContext *gc)
    {
        for(auto sprite:loadedSprites)gc->finishDrawingSprite(sprite,255);
    }
    void Sprite::createHighResolutionAtlas(HighResolutionInputs& inputs)
    {
#ifdef HAVE_OPENGL
        // Portable renderers retain standalone HD sources. Never query GL or
        // allocate its atlas without a current OpenGL rendering context.
        if (!_gc || !(_gc->getOptionFlags() & GraphicContext::USEGPU)) return;
        const bool resources=fileName=="data/gfx/ressource";
        if(!resources && fileName!="data/gfx/terrain")return;
        int count=0;
        while(count<static_cast<int>(experimentImages.size()) && experimentImages[count])++count;
        if (!count) return;
        // Legacy packs use a fixed cell pitch, but frame count and page extent
        // come from the supplied image, not a list of recognized terrain counts.
        // New terrain materials compose into independently budgeted view pages.
        const int border=resources?32:64;
        int columns=0, atlasW=0, atlasH=0;
        const std::string prefix=resources?"ressource":"terrain";
        if(experimentImages.size()<static_cast<size_t>(count))return;
        if(std::none_of(experimentImages.begin(),experimentImages.begin()+count,[](auto p){return p!=nullptr;}))return;
        auto reject=[&]()
        {
            for(int i=0;i<count;++i){delete experimentImages[i];experimentImages[i]=nullptr;}
            std::cerr<<"High-resolution atlas rejected; using original frames"<<std::endl;
        };
        for(int i=0;i<count;++i)if(!experimentImages[i]){reject();return;}
        GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
        const char *overrideDir=std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        std::string directory=overrideDir?overrideDir:"data/highres/v1";
        // Keep every supplied mip in CPU staging through dimension and alpha validation.
        std::vector<std::unique_ptr<DrawableSurface>> levels;
        for(int mip=0;mip<4;++mip)
        {
            auto image = inputs.consume(directory+"/"+prefix+"-atlas-mip"+std::to_string(mip)+".webp", false);
            auto* s = image.value ? image.value->surface : nullptr;
            // A missing optional atlas keeps valid individual HD sources.
            if(!s) return;
            if (!mip) {
                atlasW=s->w; atlasH=s->h; columns=atlasW/256;
                if (!columns || atlasW%256 || atlasH%256 || count>columns*(atlasH/256) ||
                    atlasW>maxSize || atlasH>maxSize) return;
                for(int i=0;i<count;++i)
                    if (experimentImages[i]->getW()+border>256 || experimentImages[i]->getH()+border>256) {
                        return;
                    }
            }
            if(s->w!=(atlasW>>mip)||s->h!=(atlasH>>mip)){reject();return;}
            levels.push_back(DrawableSurface::fromAssetImage(*image.value, image.exclusive, false));
        }
        // The atlas must correspond to this pack's validated frame layers.
        for(int i=0;i<count;++i)for(int y=0;y<experimentImages[i]->getH();++y)
        {
            auto source=static_cast<unsigned char*>(experimentImages[i]->sdlsurface->pixels)+y*experimentImages[i]->sdlsurface->pitch;
            auto packed=static_cast<unsigned char*>(levels[0]->sdlsurface->pixels)+((i/columns)*256+border+y)*levels[0]->sdlsurface->pitch+((i%columns)*256+border)*4;
            for(int x=0;x<experimentImages[i]->getW();++x)
                if(source[x*4+3]!=packed[x*4+3]){reject();return;}
        }
        auto batch=std::make_unique<Sprite>();
        auto atlas=std::move(levels[0]);atlas->allocateTexture(false);
        glState.setTexture(atlas->texture);
        // Supplied packed mip levels define exact storage directly, without
        // temporary power-of-two textures for the CPU staging surfaces.
        // Browser GL has no BGRA upload. Convert each packed mip to RGBA at
        // the upload boundary, as DrawableSurface::uploadToTexture does.
#ifdef GLOB2_WEBGL2
        auto uploadAtlasMip = [&](int mip, DrawableSurface &surface) {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
                SDL_ConvertSurface(surface.sdlsurface, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            if (!rgba) return false;
            glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA, rgba->w, rgba->h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
            return true;
        };
        if (!uploadAtlasMip(0, *atlas)) { reject(); return; }
#else
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,atlasW,atlasH,0,GL_BGRA,GL_UNSIGNED_BYTE,atlas->sdlsurface->pixels);
#endif
        glState.allocatedTextureBytes-=atlas->gpuBytes;
        atlas->gpuBytes=atlasW*atlasH*4;glState.allocatedTextureBytes+=atlas->gpuBytes;
        atlas->texMultX=1.f/atlasW;atlas->texMultY=1.f/atlasH;

        for(int mip=1;mip<4;++mip)
#ifdef GLOB2_WEBGL2
            if (!uploadAtlasMip(mip, *levels[mip])) { reject(); return; }
#else
            glTexImage2D(GL_TEXTURE_2D,mip,GL_RGBA,atlasW>>mip,atlasH>>mip,0,GL_BGRA,GL_UNSIGNED_BYTE,levels[mip]->sdlsurface->pixels);
#endif
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);
        const size_t mipBytes=((atlasW/2)*(atlasH/2)+(atlasW/4)*(atlasH/4)+(atlasW/8)*(atlasH/8))*4;
        atlas->gpuBytes+=mipBytes;glState.allocatedTextureBytes+=mipBytes;
        atlas->glUploadedRevision = atlas->pixelRevision;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenBuffers(1,&batch->vbo);glGenBuffers(1,&batch->texCoordBuffer);
        batch->bufferGeneration = _gc->getGLContextGeneration();
        for(int i=0;i<count;++i)
        {
            auto surface=experimentImages[i];surface->freeGPUTexture();
            surface->textureInfo=TextureInfo{batch.get(),(i%columns)*256+border,(i/columns)*256+border,surface->getW(),surface->getH()};
            surface->texMultX=1.f/atlasW;surface->texMultY=1.f/atlasH;
        }
        batch->atlas=std::move(atlas);highResolutionAtlas=std::move(batch);
#endif
    }

    static bool validHighResolutionFile(const std::string& file)
    {
        return file != "-" && file.find_first_of("/\\:") == std::string::npos &&
            file.find("..") == std::string::npos;
    }
    static HighResolutionInputs highResolutionInputs(
        const std::string& name, size_t frames, bool incremental)
    {
        HighResolutionInputs inputs;
        const char* overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return inputs;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        if (!readPack(directory)) return inputs;
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        const bool mipmaps = _gc->rendererCapabilities().textureMipmaps && !Sprite::highResolutionDeferred(name);
        size_t firstFrame = frames;
        auto add = [&](const std::string& path, bool mipmaps) {
            inputs.add(path, mipmaps);
            if (!incremental) inputs.request(Toolkit::assets(), path, mipmaps);
        };
        for (size_t i = 0; i < frames; ++i) {
            auto found = packEntries.find(prefix + std::to_string(i));
            if (found == packEntries.end()) continue;
            for (const auto& file : {found->second.base, found->second.team}) {
                if (!validHighResolutionFile(file)) continue;
                firstFrame = std::min(firstFrame, i);
                add(directory + '/' + file, mipmaps);
            }
        }
        if ((_gc->getOptionFlags() & GraphicContext::USEGPU) &&
            (prefix == "terrain" || prefix == "ressource"))
            for (int mip = 0; mip < 4; ++mip)
                add(directory + '/' + prefix + "-atlas-mip" + std::to_string(mip) + ".webp", false);
        if (incremental && firstFrame < frames) highResolutionFrameReady(name, firstFrame, inputs);
        return inputs;
    }
    HighResolutionInputs Sprite::prefetchHighResolution(const std::string& name, size_t frames)
    {
        return highResolutionInputs(name, frames, false);
    }
    HighResolutionInputs prefetchHighResolutionIncremental(const std::string& name, size_t frames)
    {
        return highResolutionInputs(name, frames, true);
    }
    bool highResolutionFrameReady(const std::string& name, size_t index, HighResolutionInputs& inputs)
    {
        const char* overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return true;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        if (!readPack(directory)) return true;
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        const bool mipmaps = _gc->rendererCapabilities().textureMipmaps && !Sprite::highResolutionDeferred(name);
        // Deferred sparse inputs own one frame; numeric lookahead only adds
        // repeated failed lookups there. Ordinary sprite prefetch keeps eight.
        const size_t lookahead = Sprite::highResolutionDeferred(name) ? 1 : 8;
        for (size_t frame = index; frame < index + lookahead; ++frame) {
            auto entry = packEntries.find(prefix + std::to_string(frame));
            if (entry == packEntries.end()) continue;
            for (const auto& file : {entry->second.base, entry->second.team})
                if (validHighResolutionFile(file)) inputs.request(Toolkit::assets(), directory + '/' + file, mipmaps);
        }
        auto found = packEntries.find(prefix + std::to_string(index));
        if (found == packEntries.end()) return true;
        for (const auto& file : {found->second.base, found->second.team})
            if (validHighResolutionFile(file) && inputs.pending(directory + '/' + file, mipmaps)) return false;
        return true;
    }
    bool highResolutionAtlasReady(const std::string& name, HighResolutionInputs& inputs)
    {
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        if (!_gc || !(_gc->getOptionFlags() & GraphicContext::USEGPU) ||
            (prefix != "terrain" && prefix != "ressource")) return true;
        const char* overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        bool ready = true;
        for (int mip = 0; mip < 4; ++mip) {
            const auto path = directory + '/' + prefix + "-atlas-mip" + std::to_string(mip) + ".webp";
            inputs.request(Toolkit::assets(), path, false);
            ready = ready && !inputs.pending(path, false);
        }
        return ready;
    }
    void Sprite::appendHighResolutionFrame(size_t index, Sprite& target,
        HighResolutionInputs& inputs)
    {
        if (target.experimentImages.size() == index) {
            target.experimentImages.push_back(nullptr); target.experimentRotated.push_back(nullptr);
        }
        assert(index < target.experimentImages.size() && index < target.experimentRotated.size());
        assert(!target.experimentImages[index] && !target.experimentRotated[index]);
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        if (!readPack(directory)) return;
        const auto wanted = fileName.substr(fileName.find_last_of('/') + 1) + std::to_string(index);
        auto found = packEntries.find(wanted); if (found == packEntries.end()) return;
        const auto &entry = found->second;
        if (entry.width != getW(index) || entry.height != getH(index) || entry.scale != (dynamicTeamColor ? 0 : 4)) return;
        const bool deferred = highResolutionDeferred(fileName);
        const bool mipmaps = _gc->rendererCapabilities().textureMipmaps && !deferred;
        auto load = [&](const std::string& name, DrawableSurface *original) -> DrawableSurface* {
            if (!validHighResolutionFile(name)) return nullptr;
            // A partial pack may have no usable prepacked atlas. Every accepted
            // layer needs a ready standalone texture for that fallback.
            auto image = inputs.consume(directory + '/' + name, mipmaps);
            if (!image.value) return nullptr;
            auto *surface = image.value->surface;
            const int width = dynamicTeamColor ? highResolutionTextureSize : (original ? original->getW() : entry.width) * entry.scale;
            const int height = dynamicTeamColor ? highResolutionTextureSize : (original ? original->getH() : entry.height) * entry.scale;
            if (surface->w != width || surface->h != height) return nullptr;
            auto result = DrawableSurface::fromAssetImage(*image.value, image.exclusive, !deferred);
            result->highResolutionSampling = true;
            // Deferred terrain feeds CPU composition; only composed pages need
            // GPU textures. A direct draw can still upload this source lazily.
            if (!deferred) result->prepareTexture();
            return result.release();
        };
        auto *normal = load(entry.base, images[index]);
        auto *colored = load(entry.team, rotated[index] ? rotated[index]->orig : nullptr);
        if ((entry.base != "-" && !normal) || (entry.team != "-" && !colored) ||
            (images[index] && entry.base == "-") || (rotated[index] && entry.team == "-")) {
            delete normal; delete colored; return;
        }
        target.experimentImages[index] = normal;
        if (colored) target.experimentRotated[index] = new RotatedImage(colored);
    }

	DrawableSurface *Sprite::prepareDrawSurface(unsigned index, bool teamColor, bool experiment)
	{
		DrawableSurface *surface;
		if (teamColor)
		{
			if (experiment && experimentRotated[index])
				surface = getColoredSurface(index, true);
			else if (rotated[index])
				surface = getColoredSurface(index, false);
			else
				surface = nullptr;
		}
		else
			surface = experiment && experimentImages[index] ? experimentImages[index] : images[index];
#ifdef HAVE_OPENGL
		if (surface && experiment)
		{
			// Preserve painter order when a pack mixes HD frames and original artwork.
			const Sprite *batch = surface->textureInfo ? surface->textureInfo->sprite : nullptr;
			if (batch != this && !vertices.empty())
				Toolkit::gc->finishDrawingSprite(this, 255);
			if (highResolutionAtlas && batch != highResolutionAtlas.get())
				Toolkit::gc->finishDrawingSprite(highResolutionAtlas.get(), 255);
		}
#endif
		return surface;
	}

	Sprite::~Sprite()
	{
        // Private reload staging sprites were never published. Their destruction
        // must not re-enter the pending map while it destroys its own entries.
        deferredHighResolution.erase(this);
        if (deferredHighResolutionCursor == this) deferredHighResolutionCursor = nullptr;
        if (loadedSprites.erase(this)) pendingHighResolution.erase(this);
#ifdef HAVE_OPENGL
        if (_gc && SDL_GL_GetCurrentContext() && bufferGeneration == _gc->getGLContextGeneration()) {
            if(vbo)glDeleteBuffers(1,&vbo);
            if(texCoordBuffer)glDeleteBuffers(1,&texCoordBuffer);
        }
#endif
		for (auto image : experimentImages)
			delete image;
		for (auto image : experimentRotated)
			delete image;
		for (std::vector <DrawableSurface *>::iterator imagesIt = images.begin(); imagesIt != images.end(); ++imagesIt)
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
	

	int Sprite::getW(int index)
	{
		if (!checkBound(index))
			return 0;
		if (images[index])
			return images[index]->getW();
		else if (rotated[index])
			return rotated[index]->orig->getW();
		else
			return 0;
	}
	
	int Sprite::getH(int index)
	{
		if (!checkBound(index))
			return 0;
		if (images[index])
			return images[index]->getH();
		else if (rotated[index])
			return rotated[index]->orig->getH();
		else
			return 0;
	}
	
	int Sprite::getFrameCount(void)
	{
		return std::max(images.size(), rotated.size());
	}
	
	bool Sprite::checkBound(int index)
	{
		if ((index < 0) || (index >= getFrameCount()))
		{
			Toolkit::SpriteMap::const_iterator it = Toolkit::spriteMap.begin();
			while (it != Toolkit::spriteMap.end())
			{
				if (it->second == this)
				{
					std::cerr << "GAG : Sprite " << fileName << " ::checkBound(" << index << ") : error : out of bound access for " << it->first << std::endl;
					assert(false);
					return false;
				}
				++it;
			}
			std::cerr << "GAG : Sprite " << fileName << " ::checkBound(" << index << ") : error : sprite is not in the sprite server" << std::endl;
			assert(false);
			return false;
		}
		else
			return true;
	}
}
