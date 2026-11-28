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
#include <optional>
#include <cstdlib>
#include <cstring>
#include <SpriteLoad.h>
#include <SteadyClock.h>
#include <DetailHysteresis.h>
#include <HighResolutionPolicy.h>
#include <chrono>
#include <AssetLoader.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <RenderMemory.h>
#ifdef __EMSCRIPTEN__
#include <ApplicationHost.h>
#endif
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
    static size_t packGeneration = 0;
    struct PackEntry {
        int width, height, scale;
        std::string base, team;
    };
    using PackEntries = std::unordered_map<std::string, PackEntry>;
    static std::unordered_map<std::string, std::optional<PackEntries>> packIndexes;
    struct HighResolutionReload {
        HighResolutionInputs inputs;
        std::unique_ptr<Sprite> prepared;
        size_t frame = 0;
        bool retainPublishedOnFailure = false;
        // Blocks of 32 frames to load; empty loads every frame. Frames outside
        // them keep what the sprite has already published.
        std::set<unsigned> blocks;
    };
    constexpr unsigned HighResolutionBlock = 32; // As blockHasCompleteHD.
    HighResolutionInputs prefetchHighResolutionBlocks(const std::string& name, size_t frames,
                                                      const std::set<unsigned>& blocks);
    static std::map<Sprite*, HighResolutionReload> pendingHighResolution;
    // Sprites whose HD frames were discarded with their textures; see
    // discardUnbackedHighResolution().
    static std::set<Sprite*> lostHighResolution;
    // The HD sprite policy every renderer follows: whether the view draws HD
    // frames and at which level, which is also the detail band (mip levels
    // left out) legacy GL loads frames at. A view takes a sharper level at
    // once and a coarser one only after wanting it for lowerDetailDelay, so
    // zooming across a threshold does not re-decode.
    static HighResolutionPolicy highResolutionPolicy;
    static bool highResolutionSkipChosen = false;
    static std::int64_t lowerDetailDelay = 10'000'000'000;
    // Before any map view chooses a level, the display's scale does: the
    // interface draws sprites at their logical size.
    static const HighResolutionPolicy &currentHighResolutionPolicy()
    {
        if (!highResolutionSkipChosen && _gc) {
            highResolutionPolicy.reset(HighResolutionPolicy::levelFor(_gc->getRasterScale()));
            highResolutionSkipChosen = true;
        }
        return highResolutionPolicy;
    }
    // A band change re-decodes every banded sprite on the asset workers; once
    // the last is published, their freed buffers go back to the system.
    static bool trimAfterBandReload = false;
    // Band reloads run one sprite at a time, each with the ordinary eight-frame
    // decode window: all at once they hold hundreds of MiB of decoded levels.
    static std::deque<Sprite*> bandReloadQueue;
    static Sprite *bandReloading = nullptr;
    static std::set<std::string> deferredHighResolutionNames;
    // Names whose HD layers something sampled through baseFrame; on-demand
    // backends load only these (see Sprite::demandHighResolution).
    static std::set<std::string> demandedHighResolutionNames;
    static bool requestedHighResolutionOnDemand = false;
    static std::set<std::string> atlasSkippedHighResolutionNames, pixelSampledHighResolutionNames;
    // Whether legacy OpenGL packs this sprite's HD frames into one atlas.
    static bool highResolutionAtlasWanted(const std::string& name)
    {
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        return _gc && (_gc->getOptionFlags() & GraphicContext::USEGPU) &&
            (prefix == "terrain" || prefix == "ressource") &&
            !atlasSkippedHighResolutionNames.contains(name);
    }
    struct RetiredHighResolution { Sprite *sprite; unsigned frame; size_t bytes; std::int64_t retiredAt; };
    // Front = most recently retired.
    static std::list<RetiredHighResolution> retiredHighResolution;
    static size_t retiredHighResolutionTotal = 0;
    // Room for the material variants of a view near the zoom where HD frames
    // start to matter, so crossing it back and forth decodes nothing.
    static Sprite::RetiredHighResolutionBudget retiredBudget{
#if defined(GLOB2_MOBILE) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        32u * 1024 * 1024,
#else
        192u * 1024 * 1024,
#endif
        20'000'000'000};
    struct DeferredHighResolution {
        std::set<unsigned> wanted, completed, active;
        std::map<unsigned, HighResolutionInputs> pending;
        std::deque<unsigned> queued;
        std::vector<unsigned> order;
        std::map<unsigned, std::list<RetiredHighResolution>::iterator> retired;
    };
    static std::map<Sprite*, DeferredHighResolution> deferredHighResolution;
    static Sprite *deferredHighResolutionCursor = nullptr;
    static bool validHighResolutionFile(const std::string& file);

    // Source decoding and standalone frame preparation work with every
    // filtering backend. Only packing those frames into the legacy atlas
    // requires GL. The portable renderer decodes the base image alone at full
    // HD, and the chain only for its half-resolution level. The software
    // renderer draws native artwork only.
    static bool highResolutionFramesSupported()
    {
        return _gc && _gc->rendererCapabilities().textureFiltering;
    }
    // Legacy OpenGL loads every sprite's HD layers with the sprite. Every other
    // backend loads them on demand: the 32-frame blocks its draws reach while
    // the policy shows HD (Sprite::highResolutionDrawable), and what
    // Sprite::baseFrame samples.
    static bool highResolutionOnDemand()
    {
        return !_gc || !(_gc->getOptionFlags() & GraphicContext::USEGPU);
    }
    // Sprites an on-demand renderer draws from their HD layers while the policy
    // shows HD (Sprite::highResolutionDrawable), and their names: those load
    // team layers too.
    static std::set<Sprite*> drawDemandedSprites;
    static std::set<std::string> drawDemandedNames;
    // On-demand draws load only the blocks they reach: those requested so far
    // per sprite, and those wanted since its last reload began.
    static std::map<const Sprite*, std::set<unsigned>> portableBlocks, portableWaitingBlocks;
    // Frames whose HD textures were first drawn, and how many first draws this
    // frame admitted: each one uploads (and may recolour) a frame.
    static std::set<std::pair<const Sprite*, unsigned>> portableDrawnFrames;
    static unsigned portableFirstDraws = 0;
    static Sprite::HighResolutionDrawCounts drawCounts;
    static constexpr unsigned PortableFirstDrawsPerFrame = 8;
    // A device holds uploaded HD frames as the only copy of their pixels, up to
    // deviceHighResolutionRoom. When a new frame finds no room, the next frame
    // evicts the blocks drawn longest ago, among those idle for at least this
    // many frames (about two seconds), so what is on screen never thrashes.
    static std::uint64_t highResolutionFrameClock = 0;
    static bool highResolutionRoomShort = false;
    static constexpr std::uint64_t HighResolutionIdleFrames = 120;
    static constexpr unsigned HighResolutionEvictionsPerFrame = 16;
    // holdHighResolutionAdmission: frames already uploaded keep drawing in HD,
    // new ones stay native, and nothing more loads or is evicted.
    static bool highResolutionAdmissionHeld = false;
    // On-demand backends sample only uncolored base layers (baseFrame), so
    // they neither decode nor retain team-colour layers, unless the portable
    // renderer draws that sprite's HD frames.
    static std::vector<std::string> wantedLayers(const PackEntry& entry, const std::string& name)
    {
        if (highResolutionOnDemand() && !drawDemandedNames.contains(name)) return {entry.base};
        return {entry.base, entry.team};
    }
    static bool highResolutionPackageReady(const std::string& name, const std::string& directory)
    {
#ifdef __EMSCRIPTEN__
        if (directory == "data/highres/v1") {
            const auto package = name.starts_with("data/gfx/terrain-") ? "hd-" + name.substr(9) : "hd";
            return ApplicationHost::assetPackageReady(package.c_str());
        }
#else
        (void)name;
        (void)directory;
#endif
        return true;
    }
    static const PackEntries* readPackIndex(const std::string& path)
    {
        const auto generation = Toolkit::assets().sourceGeneration();
        if (packGeneration != generation) {
            packGeneration = generation;
            packIndexes.clear();
        }
        auto [found, inserted] = packIndexes.try_emplace(path);
        if (!inserted) return found->second ? &*found->second : nullptr;
        auto bytes = Toolkit::assets().wait(Toolkit::assets().requestBytes(path));
        if (!bytes || bytes->empty() || bytes->size() > 8 * 1024 * 1024) return nullptr;
        std::string text(bytes->begin(), bytes->end());
        std::istringstream index(text);
        std::string line, magic;
        int version = 0;
        if (!std::getline(index, line)) return nullptr;
        std::istringstream header(line);
        if (!(header >> magic >> version) || magic != "GLOB2_HIGHRES" ||
            (version != 1 && version != 2) || (header >> line)) return nullptr;
        PackEntries entries;
        while (std::getline(index, line)) {
            std::istringstream row(line);
            std::string id, extra;
            PackEntry entry;
            if (!(row >> id)) continue;
            if (!(row >> entry.width >> entry.height >> entry.scale >> entry.base >> entry.team) ||
                (row >> extra) || entry.width <= 0 || entry.height <= 0) return nullptr;
            for (const auto& token : {entry.base, entry.team}) {
                PackedSpriteFrame location;
                if (token.starts_with('@') &&
                    (version != 2 || !PackedSpriteFrame::parse(token, location))) return nullptr;
            }
            if (!entries.emplace(id, std::move(entry)).second) return nullptr;
        }
        if (packGeneration != generation) return nullptr;
        auto& parsed = packIndexes.at(path);
        parsed.emplace(std::move(entries));
        return &*parsed;
    }
    static const PackEntries* readPack(const std::string& directory, const std::string& name)
    {
        if (!highResolutionPackageReady(name, directory)) return nullptr;
#ifdef __EMSCRIPTEN__
        if (directory == "data/highres/v1" && name.starts_with("data/gfx/terrain-")) {
            if (const auto* family = readPackIndex(directory + '/' + name.substr(9) + ".frames"))
                return family;
        }
#endif
        return readPackIndex(directory + "/frames.txt");
    }

    static std::set<std::string> demandLoadedHighResolutionNames;
    void Sprite::loadHighResolutionOnDemand(const std::string& name)
    {
        demandLoadedHighResolutionNames.insert(name);
    }
    bool highResolutionWantedAtLoad(const std::string& name)
    {
        // On-demand renderers load HD layers when a draw or baseFrame first
        // asks for them, not with the sprite.
        return !demandLoadedHighResolutionNames.contains(name) &&
            (!requestedHighResolutionOnDemand || demandedHighResolutionNames.contains(name));
    }
    bool highResolutionNeedsAsyncLoad(const std::string& name, size_t frames)
    {
#ifdef __EMSCRIPTEN__
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported() ||
            Sprite::highResolutionDeferred(name))
            return false;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        const auto* packEntries = readPack(directory, name);
        if (!packEntries) return false;
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        for (size_t frame = 0; frame < frames; ++frame) {
            auto entry = packEntries->find(prefix + std::to_string(frame));
            if (entry != packEntries->end() &&
                (entry->second.base.find(".h264,") != std::string::npos ||
                 entry->second.team.find(".h264,") != std::string::npos))
                return true;
        }
#else
        (void)name;
        (void)frames;
#endif
        return false;
    }

	Sprite::RotatedImage::~RotatedImage()
	{
		delete orig;
		for (RotationMap::iterator it = rotationMap.begin(); it != rotationMap.end(); ++it)
		{
			delete it->second;
		}
	}
	
    // HD shadow layers load on first ask, one decode per frame, from the HD
    // index's `<frame>s` row (a plain file or a packed frame token).
    static std::map<std::pair<const Sprite*, unsigned>, AssetLoader::Handle<AssetImage>> pendingShadows;
    static std::uint64_t shadowRevision = 1;
    std::uint64_t Sprite::shadowLayerRevision() { return shadowRevision; }
    DrawableSurface *Sprite::highResolutionShadowFrame(unsigned index)
    {
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported() ||
            !shadowFrame(index)) return nullptr;
        if (highResolutionShadows.size() < shadowImages.size()) {
            highResolutionShadows.resize(shadowImages.size());
            highResolutionShadowState.resize(shadowImages.size(), 0);
        }
        if (highResolutionShadows[index]) return highResolutionShadows[index].get();
        auto &state = highResolutionShadowState[index];
        if (state == 2) return nullptr;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        // While HD admission is held (holdHighResolutionAdmission), layers
        // already loaded stay and no new one loads; it is asked again once
        // the hold lifts, and the mask keeps the native layer meanwhile.
        if (state == 0 && highResolutionAdmissionHeld) return nullptr;
        // The web HD package may still be downloading: ask again later
        // rather than settling on the native layer for the session.
        if (state == 0 && !highResolutionPackageReady(fileName, directory)) return nullptr;
        if (state == 0) {
            state = 2;
            const auto* packEntries = readPack(directory, fileName);
            if (!packEntries) return nullptr;
            const auto found = packEntries->find(fileName.substr(fileName.find_last_of('/') + 1) +
                std::to_string(index) + "s");
            if (found == packEntries->end()) return nullptr;
            const auto &entry = found->second;
            auto *native = shadowFrame(index);
            if (entry.base == "-" || entry.scale != 4 || entry.width != native->getW() || entry.height != native->getH() ||
                !validHighResolutionFile(entry.base)) return nullptr;
            const auto path = directory + '/' + entry.base;
            AssetLoader::Handle<AssetImage> handle;
            if (entry.base.starts_with('@')) {
                PackedSpriteFrame location;
                if (!PackedSpriteFrame::parse(entry.base, location)) return nullptr;
                handle = location.request(Toolkit::assets(), path, false);
            } else handle = Toolkit::assets().requestImage(path, AssetLoader::Priority::Required, false);
            pendingShadows[{this, index}] = std::move(handle);
            state = 1;
        }
        auto pending = pendingShadows.find({this, index});
        if (pending == pendingShadows.end()) { state = 2; return nullptr; }
        if (pending->second.pending()) return nullptr;
        const auto image = pending->second.get();
        pendingShadows.erase(pending);
        state = 2;
        auto *native = shadowFrame(index);
        if (!image || image->surface->w != native->getW() * 4 || image->surface->h != native->getH() * 4) return nullptr;
        highResolutionShadows[index] = DrawableSurface::fromAssetImage(*image, false, false);
        ++shadowRevision;
        return highResolutionShadows[index].get();
    }

    void Sprite::registerLoaded() {
        loadedSprites.insert(this);
        if ((!highResolutionOnDemand() || demandedHighResolutionNames.contains(fileName)) &&
            highResolutionNeedsAsyncLoad(fileName, images.size())) {
            queueHighResolutionReload(true);
        }
        for (std::size_t index=0;index<images.size();++index) if(images[index]) {
            images[index]->spriteSource=this; images[index]->spriteFrame=index;
        }
        // HD frames appended while loading already cover their native frames.
        releaseCoveredNativeTextures();
    }
    void Sprite::adoptLoaded(Sprite& prepared)
    {
        assert(images.empty() && rotated.empty());
        fileName = std::move(prepared.fileName);
        dynamicTeamColor = prepared.dynamicTeamColor;
        images.swap(prepared.images); rotated.swap(prepared.rotated);
        shadowImages.swap(prepared.shadowImages);
        experimentImages.swap(prepared.experimentImages); experimentRotated.swap(prepared.experimentRotated);
        highResolutionAtlas = std::move(prepared.highResolutionAtlas);
        blockCompleteHD = std::move(prepared.blockCompleteHD);
        highResolutionLayerRevision = prepared.highResolutionLayerRevision;
        highResolutionFrameRevisions = std::move(prepared.highResolutionFrameRevisions);
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
        // A sheet that cannot have an atlas (team-coloured or rotated frames,
        // frames without pixels, too large, no atlas memory) tries again only
        // after two seconds of frames, not on every draw of every frame.
        const std::uint64_t frame = Toolkit::gc ? Toolkit::gc->frameIdentity() : 0;
        if (!atlas && frame && frame < atlasRetryFrame) return false;
        const auto refuse = [&] { atlasRetryFrame = frame + 120; return false; };
        if (images.empty() || dynamicTeamColor || std::any_of(rotated.begin(),rotated.end(),[](auto value){return value!=nullptr;})) return refuse();
        std::vector<SpriteAtlasSource> sources; sources.reserve(images.size());
        for (std::size_t index=0;index<images.size();++index) {
            auto *image=images[index]; if(!image || image->highResolutionSampling || !image->sdlsurface)return refuse();
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
        if(!page)return refuse();
        AssetImage image(page->pixels.release());
        auto replacement=DrawableSurface::fromAssetImage(image,true,false);
        atlas=std::move(replacement);atlasMemory=std::move(page->memory);
        atlasStamps=std::move(page->stamps);atlasRegions=std::move(page->regions);return true;
    }
    bool Sprite::resolveDeviceAtlas(DrawableSurface &surface,SDL_Rect &source,const DrawableSurface *&upload) {
        if(surface.spriteSource!=this || surface.spriteFrame>=images.size() || images[surface.spriteFrame]!=&surface ||
            !Toolkit::gc || !Toolkit::gc->renderDevice() || surface.highResolutionSampling)return false;
        // Every sprite draw comes here: when the atlas already holds this
        // frame's current pixels, use it without revisiting every frame. A
        // changed frame rebuilds the atlas when that frame is drawn.
        const auto frame=surface.spriteFrame;
        if(atlas && atlasMemory && atlasStamps.size()==images.size() && atlasRegions.size()==images.size() && surface.sdlsurface &&
            atlasStamps[frame]==SpriteAtlasStamp{surface.lifetimeIdentity(),surface.contentRevision(),surface.sdlsurface->w,surface.sdlsurface->h} &&
            std::max(atlas->sdlsurface->w,atlas->sdlsurface->h)<=std::min(2048,Toolkit::gc->renderDevice()->capabilities().maximumTextureSize)) {
            const auto &region=atlasRegions[frame];
            if(source.x<0 || source.y<0 || source.w<=0 || source.h<=0 || source.x>region.w-source.w || source.y>region.h-source.h)return false;
            source.x+=region.x;source.y+=region.y;upload=atlas.get();return true;
        }
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
	
	void Sprite::noteHighResolutionFrame(size_t frame)
	{
		if (highResolutionFrameRevisions.size() < images.size()) highResolutionFrameRevisions.resize(images.size(), 0);
		if (frame < highResolutionFrameRevisions.size())
			highResolutionFrameRevisions[frame] = ++highResolutionLayerRevision;
	}
	void Sprite::noteHighResolutionReplaced()
	{
		highResolutionFrameRevisions.assign(images.size(), ++highResolutionLayerRevision);
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

	DrawableSurface *Sprite::silhouette(unsigned index, const Color &color, bool teamLayerOnly)
	{
		if (!checkBound(index))
			return nullptr;
		DrawableSurface *team = rotated[index] ? rotated[index]->orig : nullptr;
		DrawableSurface *base = teamLayerOnly ? nullptr : images[index];
		DrawableSurface *shape = base ? base : team;
		if (!shape)
			return nullptr;
		const TeamColorKey key{int(index), teamLayerOnly, color.r, color.g, color.b};
		if (auto found = silhouetteIndex.find(key); found != silhouetteIndex.end())
		{
			silhouetteList.splice(silhouetteList.begin(), silhouetteList, found->second);
			return silhouetteList.front().surface.get();
		}
		std::unique_ptr<DrawableSurface> ds(shape->clone());
		SDL_Surface *pixels = ds->getSDLSurface();
		// The team layer adds its coverage to the base layer's: 1-(1-a)(1-b).
		SDL_Surface *over = base && team && team->getSDLSurface()->w == pixels->w
			&& team->getSDLSurface()->h == pixels->h ? team->getSDLSurface() : nullptr;
		const auto *format = SDL_GetPixelFormatDetails(pixels->format);
		const auto *overFormat = over ? SDL_GetPixelFormatDetails(over->format) : nullptr;
		for (int y = 0; y < pixels->h; ++y)
		{
			Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + y * pixels->pitch);
			const Uint32 *overRow = over ? reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(over->pixels) + y * over->pitch) : nullptr;
			for (int x = 0; x < pixels->w; ++x)
			{
				Uint8 r, g, b, a;
				SDL_GetRGBA(row[x], format, SDL_GetSurfacePalette(pixels), &r, &g, &b, &a);
				if (overRow)
				{
					Uint8 ignored, coverage;
					SDL_GetRGBA(overRow[x], overFormat, SDL_GetSurfacePalette(over), &ignored, &ignored, &ignored, &coverage);
					a = Uint8(255 - (255 - a) * (255 - coverage) / 255);
				}
				row[x] = SDL_MapSurfaceRGBA(pixels, color.r, color.g, color.b, a);
			}
		}
		const size_t bytes = static_cast<size_t>(pixels->w) * pixels->h * 4;
		while (!silhouetteList.empty() && silhouetteBytes + bytes > silhouetteCacheCap)
		{
			silhouetteBytes -= silhouetteList.back().bytes;
			silhouetteIndex.erase(silhouetteList.back().key);
			silhouetteList.pop_back();
		}
		silhouetteList.push_front(TeamColorNode{key, std::move(ds), bytes});
		silhouetteIndex[key] = silhouetteList.begin();
		silhouetteBytes += bytes;
		return silhouetteList.front().surface.get();
	}

	DrawableSurface *Sprite::getColoredSurface(int index, bool experiment)
	{
		RotatedImage *image = experiment ? experimentRotated[index] : rotated[index];
		assert(image);
		// HD recolours are sixteen times a native one: buildings use the capped
		// cache for those too, rather than one retained clone per colour.
		if (dynamicTeamColor || experiment)
		{
			// Bounded software/shader-unavailable fallback: keyed by source frame,
			// resolution (native/HD) and team color, never by the composited result.
			TeamColorKey key{index, experiment, actColor.r, actColor.g, actColor.b};
			if (DrawableSurface *hit = lookupTeamColor(key))
			{
				if (!hit->portableTextureLost())
					return hit;
				// Its texture held the only copy: recolour it again.
				teamColorBytes -= teamColorList.front().bytes;
				teamColorIndex.erase(key);
				teamColorList.pop_front();
			}
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

    void Sprite::memoryUsage(std::vector<RenderMemory::SpriteUsage> &usage)
    {
        for (const auto *sprite : loadedSprites)
        {
            RenderMemory::SpriteUsage entry;
            entry.name = sprite->fileName;
            const auto add = [&entry](const DrawableSurface *surface, bool highResolution)
            {
                if (!surface) return;
                ++entry.frames;
                entry.gpuBytes += surface->gpuBytes;
                if (highResolution) entry.highResolutionGpuBytes += surface->gpuBytes;
                if (!surface->sdlsurface || !surface->sdlsurface->pixels) return;
                const auto bytes = size_t(surface->sdlsurface->pitch) * surface->sdlsurface->h;
                (highResolution ? entry.highResolutionCpuBytes : entry.nativeCpuBytes) += bytes;
            };
            const auto addRotated = [&add](const RotatedImage *rotated, bool highResolution)
            {
                if (!rotated) return;
                add(rotated->orig, highResolution);
                for (const auto &colored : rotated->rotationMap) add(colored.second, highResolution);
            };
            const auto addSprite = [&](const Sprite &source, bool highResolution)
            {
                for (const auto *image : source.images) add(image, highResolution);
                for (const auto *rotated : source.rotated) addRotated(rotated, highResolution);
                for (const auto *image : source.experimentImages) add(image, true);
                for (const auto *rotated : source.experimentRotated) addRotated(rotated, true);
                for (const auto &node : source.teamColorList) add(node.surface.get(), node.key.experiment);
                add(source.atlas.get(), highResolution);
            };
            addSprite(*sprite, false);
            if (sprite->highResolutionAtlas) addSprite(*sprite->highResolutionAtlas, true);
            usage.push_back(std::move(entry));
        }
    }

    Sprite::HighResolutionStats Sprite::highResolutionStats()
    {
        HighResolutionStats stats;
        for(auto sprite:loadedSprites)
        {
            // Frames whose pixels were released after upload hold no CPU bytes.
            for(auto s:sprite->experimentImages)if(s&&s->hasPixels())stats.cpuBytes+=s->getW()*s->getH()*4;
            for(auto r:sprite->experimentRotated)if(r)
            {
                if(r->orig->hasPixels())stats.cpuBytes+=r->orig->getW()*r->orig->getH()*4;
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

    Sprite::TeamColorStats Sprite::teamColorStats()
    {
        TeamColorStats stats;
        const auto add=[&stats](DrawableSurface *surface)
        {
            // A drawn portable recolour may keep only its texture.
            if(surface->hasPixels())stats.cpuBytes+=static_cast<size_t>(surface->getW())*surface->getH()*4;
            ++stats.frames;
        };
        const auto addSprite=[&add](const Sprite &sprite)
        {
            for(const auto *images:{&sprite.rotated,&sprite.experimentRotated})
                for(const auto *rotated:*images)if(rotated)for(const auto &entry:rotated->rotationMap)add(entry.second);
            for(const auto &node:sprite.teamColorList)add(node.surface.get());
        };
        for(const auto *sprite:loadedSprites)
        {
            addSprite(*sprite);
            if(sprite->highResolutionAtlas)addSprite(*sprite->highResolutionAtlas);
        }
        return stats;
    }

    bool Sprite::highResolutionDeferred(const std::string& name)
    {
        return deferredHighResolutionNames.contains(name);
    }
    void Sprite::skipHighResolutionAtlas(const std::string& name)
    {
        atlasSkippedHighResolutionNames.insert(name);
    }
    // Bands apply where standalone frames upload their prepared levels and draw
    // only through their textures: desktop legacy GL, never device renderers,
    // which restore from full levels, or atlases and CPU-sampled banks.
    static bool highResolutionBanded(const std::string& name)
    {
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
        return _gc && (_gc->getOptionFlags() & GraphicContext::USEGPU) && !_gc->renderDevice() &&
            _gc->backendSwitchStatus().active == RendererBackend::LegacyGL &&
            !Sprite::highResolutionDeferred(name) && !highResolutionAtlasWanted(name) &&
            !pixelSampledHighResolutionNames.contains(name);
#else
        (void)name;
        return false;
#endif
    }
    unsigned Sprite::highResolutionDetailSkip() { return currentHighResolutionPolicy().reduction(); }
    void Sprite::setHighResolutionDetailDelay(std::int64_t nanoseconds) { lowerDetailDelay = nanoseconds; }
    std::int64_t Sprite::highResolutionDetailDelay() { return lowerDetailDelay; }
    static bool highResolutionSettingOn()
    {
        return highResolutionEnabled || std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    }
    unsigned Sprite::highResolutionLevel()
    {
        return currentHighResolutionPolicy().level(highResolutionSettingOn());
    }
    bool Sprite::highResolutionShown() { return highResolutionLevel() != HighResolutionPolicy::Native; }
    static std::uint64_t policyRevision = 0;
    std::uint64_t Sprite::highResolutionPolicyRevision() { return policyRevision; }
    bool Sprite::drawsHighResolutionOnDemand()
    {
        return highResolutionShown() && _gc && !(_gc->getOptionFlags() & GraphicContext::USEGPU) &&
            highResolutionFramesSupported();
    }
    void Sprite::resetHighResolutionPolicy(double density)
    {
        ++policyRevision;
        highResolutionPolicy.reset(HighResolutionPolicy::levelFor(density));
        highResolutionSkipChosen = true;
    }
    // On-demand renderers (every filtering one but legacy GL) draw the HD level the
    // policy names as the frame itself: below full HD, the frame is replaced
    // by its half-resolution level, the second of the CPU chain the asset
    // workers build. Legacy GL reaches the same level through its bands.
    static bool reducedHighResolutionBackend()
    {
        return _gc && !(_gc->getOptionFlags() & GraphicContext::USEGPU);
    }
    static bool reducedHighResolutionWanted(const std::string &name)
    {
        return reducedHighResolutionBackend() && !Sprite::highResolutionDeferred(name) &&
            !pixelSampledHighResolutionNames.contains(name) &&
            currentHighResolutionPolicy().reduction() == HighResolutionPolicy::HalfHD;
    }
    // Whether HD inputs are decoded with their CPU mip chain: for renderers
    // that sample mip chains, and for a reduced level, which is the chain's
    // second level.
    static bool highResolutionInputMipmaps(const std::string &name)
    {
        if (Sprite::highResolutionDeferred(name)) return false;
        return _gc->rendererCapabilities().textureMipmaps || reducedHighResolutionWanted(name);
    }
    Sprite::HighResolutionDrawCounts Sprite::highResolutionDrawCounts() { return drawCounts; }
    void Sprite::holdHighResolutionAdmission(bool held)
    {
        highResolutionAdmissionHeld = held;
        // Blocks already queued would upload frames that cannot draw.
        if (held) portableWaitingBlocks.clear();
    }
    bool Sprite::highResolutionAdmissionHolding() { return highResolutionAdmissionHeld; }
    void Sprite::evictIdleHighResolution()
    {
        if (!_gc || _gc->deviceHighResolutionRoom()) return;
        struct Candidate { std::uint64_t drawn; Sprite *sprite; unsigned block; };
        std::vector<Candidate> candidates;
        for (auto *sprite : drawDemandedSprites) {
            // Deferred banks retire their own frames; a reload in flight
            // publishes into these blocks.
            if (!loadedSprites.contains(sprite) || Sprite::highResolutionDeferred(sprite->fileName) ||
                pendingHighResolution.contains(sprite)) continue;
            for (unsigned block = 0; block < sprite->highResolutionBlockDrawn.size(); ++block) {
                const auto drawn = sprite->highResolutionBlockDrawn[block];
                if (drawn && drawn + HighResolutionIdleFrames <= highResolutionFrameClock)
                    candidates.push_back({drawn, sprite, block});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) { return a.drawn < b.drawn; });
        unsigned evicted = 0;
        for (const auto &candidate : candidates) {
            if (evicted >= HighResolutionEvictionsPerFrame || _gc->deviceHighResolutionRoom()) break;
            if (candidate.sprite->evictHighResolutionBlock(candidate.block)) ++evicted;
        }
        drawCounts.evicted += evicted;
    }
    bool Sprite::evictHighResolutionBlock(unsigned block)
    {
        if (block < highResolutionBlockDrawn.size()) highResolutionBlockDrawn[block] = 0;
        // A sheet a base frame samples keeps its pixels, and an atlas sheet its
        // pages: neither holds retained textures to give back.
        if (sampledByBaseFrame || highResolutionAtlas || highResolutionAtlasWanted(fileName)) return false;
        bool held = false;
        const size_t end = std::min<size_t>(size_t(block + 1) * HighResolutionBlock, experimentImages.size());
        for (size_t frame = size_t(block) * HighResolutionBlock; frame < end; ++frame) {
            auto *&image = experimentImages[frame];
            held = held || image || (frame < experimentRotated.size() && experimentRotated[frame]);
            delete image; image = nullptr;
            if (frame < experimentRotated.size()) { delete experimentRotated[frame]; experimentRotated[frame] = nullptr; }
            portableDrawnFrames.erase({this, unsigned(frame)});
        }
        if (!held) return false;
        // Drawing it again queues the block for a reload (highResolutionDrawable).
        if (auto found = portableBlocks.find(this); found != portableBlocks.end()) found->second.erase(block);
        clearTeamColorCache();
        recomputeBlockCompleteHD();
        return true;
    }
    void Sprite::setHighResolutionDensity(double density)
    {
        portableFirstDraws = 0;
        ++highResolutionFrameClock;
        if (std::exchange(highResolutionRoomShort, false)) evictIdleHighResolution();
        // The blocks last frame's draws reached load together, one reload a sprite.
        for (auto it = portableWaitingBlocks.begin(); it != portableWaitingBlocks.end();) {
            auto *sprite = const_cast<Sprite*>(it->first);
            if (!loadedSprites.contains(sprite)) { it = portableWaitingBlocks.erase(it); continue; }
            if (pendingHighResolution.contains(sprite)) { ++it; continue; }
            portableBlocks[sprite].insert(it->second.begin(), it->second.end());
            sprite->queueHighResolutionBlocks(it->second);
            it = portableWaitingBlocks.erase(it);
        }
        currentHighResolutionPolicy();
        const auto previous = HighResolutionPolicy::Level(highResolutionPolicy.reduction());
        if (!highResolutionPolicy.update(density, GAGCore::steadyClockNanoseconds(), lowerDetailDelay)) return;
        const auto level = HighResolutionPolicy::Level(highResolutionPolicy.reduction());
        if ((level == HighResolutionPolicy::Native) != (previous == HighResolutionPolicy::Native)) ++policyRevision;
        if (level == HighResolutionPolicy::Native)
        {
            // Back on native frames: what drawing demanded goes again.
            const auto demanded = std::exchange(drawDemandedSprites, {});
            drawDemandedNames.clear();
            portableDrawnFrames.clear();
            for (auto *sprite : demanded)
                if (loadedSprites.contains(sprite)) sprite->releaseHighResolutionDemand();
        }
        else if (previous != HighResolutionPolicy::Native && reducedHighResolutionBackend())
        {
            // Between full and half resolution: the blocks drawing reached
            // load again at the new level; the current frames draw meanwhile.
            for (auto *sprite : drawDemandedSprites)
                if (loadedSprites.contains(sprite) && !highResolutionDeferred(sprite->fileName) &&
                    portableBlocks.contains(sprite) && !portableBlocks[sprite].empty())
                    sprite->queueHighResolutionBlocks(portableBlocks[sprite]);
        }
        const bool moreDetail = level < previous;
        if (!highResolutionEnabled || !highResolutionFramesSupported()) return;
        trimAfterBandReload = true;
        bandReloadQueue.clear();
        for (auto *sprite : loadedSprites) {
            if (!highResolutionBanded(sprite->fileName)) continue;
            const bool loaded = std::any_of(sprite->experimentImages.begin(), sprite->experimentImages.end(),
                [](const DrawableSurface *image) { return image != nullptr; });
            // A reload in flight was prepared for the previous band.
            if (loaded || pendingHighResolution.contains(sprite)) bandReloadQueue.push_back(sprite);
        }
        // Zooming in, the smallest first: the unit sheet alone takes seconds,
        // and buildings, flags and effects queued behind it stayed coarse for
        // that long while every other renderer already drew them sharp.
        // Zooming out, the largest first: the unit sheet frees the most.
        std::stable_sort(bandReloadQueue.begin(), bandReloadQueue.end(),
            [moreDetail](const Sprite *a, const Sprite *b) {
                return moreDetail ? a->images.size() < b->images.size() : a->images.size() > b->images.size(); });
        if (bandReloading && pendingHighResolution.contains(bandReloading)) bandReloadQueue.push_front(bandReloading);
        bandReloading = nullptr;
    }
    // Starts the next queued band reload once the previous one is published.
    void Sprite::advanceBandReload()
    {
        if (bandReloading && pendingHighResolution.contains(bandReloading)) return;
        if (bandReloading) {
            // Each sprite's decodes leave freed buffers in the workers' arenas:
            // hand them back before the next sprite's decodes, not at the end.
#ifdef __GLIBC__
            malloc_trim(0);
#endif
        }
        bandReloading = nullptr;
        while (!bandReloadQueue.empty()) {
            auto *sprite = bandReloadQueue.front();
            bandReloadQueue.pop_front();
            if (!loadedSprites.contains(sprite)) continue;
            sprite->queueHighResolutionReload(true);
            bandReloading = sprite;
            return;
        }
    }
    void Sprite::releaseCoveredNativeTextures()
    {
        // Native frames are the floor drawn while HD loads. Once a frame's HD
        // layers are published its native textures are no longer drawn there:
        // free them, keeping the pixels, and a draw that falls back (an
        // incomplete unit action, a renderer without the team shader) uploads
        // them again. Only where HD layers draw from textures alone.
        if (!highResolutionBanded(fileName)) return;
        for (size_t i = 0; i < images.size(); ++i) {
            const bool hd = i < experimentImages.size() && experimentImages[i];
            const bool hdTeam = i < experimentRotated.size() && experimentRotated[i];
            if (dynamicTeamColor && !blockHasCompleteHD(unsigned(i))) continue;
            if (hd && images[i] && images[i]->hasPixels()) images[i]->freeGPUTexture();
            if (hdTeam && i < rotated.size() && rotated[i] && rotated[i]->orig->hasPixels())
                rotated[i]->orig->freeGPUTexture();
        }
    }
    void Sprite::keepHighResolutionPixels(const std::string& name)
    {
        pixelSampledHighResolutionNames.insert(name);
    }
    void Sprite::deferHighResolution(const std::string& name)
    {
        deferredHighResolutionNames.insert(name);
    }
    Sprite::RetiredHighResolutionBudget Sprite::retiredHighResolutionBudget() { return retiredBudget; }
    void Sprite::setRetiredHighResolutionBudget(RetiredHighResolutionBudget budget)
    {
        retiredBudget = budget;
        evictRetiredHighResolution();
    }
    size_t Sprite::retiredHighResolutionBytes() { return retiredHighResolutionTotal; }
    // Set by trimRetiredHighResolution: evict retired frames regardless of
    // budget and age, a bounded number a poll, until none are left.
    static bool trimmingRetiredHighResolution = false;
    void Sprite::trimRetiredHighResolution(bool immediately)
    {
        trimmingRetiredHighResolution = true;
        if (immediately)
            while (trimmingRetiredHighResolution) evictRetiredHighResolution();
    }
    void Sprite::evictRetiredHighResolution()
    {
        constexpr std::size_t TrimmedPerPoll = 64;
        std::set<Sprite *> changed;
        std::size_t trimmed = 0;
        const auto expired = GAGCore::steadyClockNanoseconds() - retiredBudget.lifetimeNanoseconds;
        while (!retiredHighResolution.empty() && ((trimmingRetiredHighResolution && trimmed++ < TrimmedPerPoll) ||
                                                 retiredHighResolutionTotal > retiredBudget.bytes ||
                                                 retiredHighResolution.back().retiredAt <= expired)) {
            const auto oldest = retiredHighResolution.back();
            retiredHighResolution.pop_back();
            retiredHighResolutionTotal -= oldest.bytes;
            deferredHighResolution[oldest.sprite].retired.erase(oldest.frame);
            auto &sprite = *oldest.sprite;
            delete sprite.experimentImages[oldest.frame]; sprite.experimentImages[oldest.frame] = nullptr;
            delete sprite.experimentRotated[oldest.frame]; sprite.experimentRotated[oldest.frame] = nullptr;
            changed.insert(&sprite);
        }
        for (auto *sprite : changed) sprite->recomputeBlockCompleteHD();
        if (retiredHighResolution.empty()) trimmingRetiredHighResolution = false;
    }
    void Sprite::releaseRetiredHighResolution(bool destroy)
    {
        const auto found = deferredHighResolution.find(this);
        if (found == deferredHighResolution.end()) return;
        for (const auto &[frame, entry] : found->second.retired) {
            retiredHighResolutionTotal -= entry->bytes;
            retiredHighResolution.erase(entry);
            if (!destroy) continue;
            delete experimentImages[frame]; experimentImages[frame] = nullptr;
            delete experimentRotated[frame]; experimentRotated[frame] = nullptr;
        }
        found->second.retired.clear();
    }
    void Sprite::setHighResolutionFrames(std::span<const unsigned> indices)
    {
        if (!highResolutionDeferred(fileName)) {
            // Declaring a window over ordinary frames is a request for them.
            if (!indices.empty() && !highResolutionDemanded) demandHighResolution();
            return;
        }
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
        // Frames leaving the window are retired, not deleted, and a frame that
        // returns is simply wanted again. Readers own copied pixels, never a
        // borrowed pointer across this declaration: eviction below may delete
        // any sprite's retired frames.
        // Released frames hold only their texture (gpuBytes is legacy GL's).
        const auto surfaceBytes = [](DrawableSurface *surface) {
            if (!surface) return size_t(0);
            return (surface->hasPixels() ? size_t(surface->getW()) * surface->getH() * 4 : size_t(0)) +
                surface->gpuBytes;
        };
        for (unsigned frame = 0; frame < images.size(); ++frame) {
            const auto retired = stream.retired.find(frame);
            if (stream.wanted.contains(frame)) {
                if (retired == stream.retired.end()) continue;
                retiredHighResolutionTotal -= retired->second->bytes;
                retiredHighResolution.erase(retired->second);
                stream.retired.erase(retired);
            } else if (retired == stream.retired.end() && (experimentImages[frame] || experimentRotated[frame])) {
                const auto bytes = surfaceBytes(experimentImages[frame]) +
                    (experimentRotated[frame] ? surfaceBytes(experimentRotated[frame]->orig) : 0);
                retiredHighResolution.push_front({this, frame, bytes, GAGCore::steadyClockNanoseconds()});
                stream.retired[frame] = retiredHighResolution.begin();
                retiredHighResolutionTotal += bytes;
            }
        }
        evictRetiredHighResolution();
        recomputeBlockCompleteHD();
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        const auto* packEntries = readPack(directory, fileName);
        if (!packEntries) return;
        const auto prefix = fileName.substr(fileName.find_last_of('/') + 1);
        const bool mipmaps = false; // Deferred terrain is sampled by CPU page composition.
        // Preserve caller order, which interleaves current/next animation endpoints.
        for (unsigned frame : indices) {
            if (!stream.wanted.contains(frame)) continue;
            if (experimentImages[frame] || stream.pending.contains(frame) || stream.completed.contains(frame)) continue;
            auto entry = packEntries->find(prefix + std::to_string(frame));
            if (entry == packEntries->end()) continue;
            auto &inputs = stream.pending[frame];
            stream.queued.push_back(frame);
            for (const auto &file : wantedLayers(entry->second, fileName))
                if (validHighResolutionFile(file)) inputs.add(directory + '/' + file, mipmaps);
        }
    }

    bool Sprite::highResolutionRequested()
    {
        return highResolutionEnabled && highResolutionFramesSupported();
    }

    void Sprite::refreshHighResolutionSprite(const std::string &name)
    {
        if (!highResolutionEnabled || !highResolutionFramesSupported()) return;
#ifdef __EMSCRIPTEN__
        // Package installation keeps the same source roots. Retain parsed
        // indexes for other families when the loader invalidates file requests.
        packGeneration = Toolkit::assets().sourceGeneration();
        packIndexes.erase(name.empty() ? "data/highres/v1/frames.txt"
                                        : "data/highres/v1/" + name.substr(name.find_last_of('/') + 1) + ".frames");
#endif
        for (auto *sprite : loadedSprites) {
            const auto& spriteName = sprite->fileName;
            if (name.empty() ? spriteName.starts_with("data/gfx/terrain-") : spriteName != name) continue;
            if (highResolutionDeferred(spriteName)) {
                auto &stream = deferredHighResolution[sprite];
                auto wanted = stream.order;
                stream.pending.clear();
                stream.active.clear();
                stream.queued.clear();
                stream.completed.clear();
                stream.wanted.clear();
                sprite->releaseRetiredHighResolution(true);
                sprite->setHighResolutionFrames(wanted);
            } else if (!requestedHighResolutionOnDemand || demandedHighResolutionNames.contains(spriteName)) {
                sprite->queueHighResolutionReload(true);
            }
        }
        // The application invalidated the file cache for this installation.
        // Subsequent renderer polls should not turn it into a global HD reload.
        requestedHighResolutionGeneration = Toolkit::assets().sourceGeneration();
    }

    void Sprite::requestHighResolution(bool enabled)
    {
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        const bool mipmaps = _gc && _gc->rendererCapabilities().textureMipmaps;
        requestedHighResolutionCPUChain = mipmaps && _gc && _gc->renderDevice();
        const bool onDemand = highResolutionOnDemand();
        const bool sameSource = highResolutionEnabled == enabled &&
            requestedHighResolutionGeneration == Toolkit::assets().sourceGeneration() &&
            requestedHighResolutionDirectory == directory;
#ifdef __EMSCRIPTEN__
        if (enabled && !overrideDir && highResolutionFramesSupported() &&
            !ApplicationHost::assetPackageReady("hd"))
            ApplicationHost::requestAssetPackage("hd");
#endif
        if (sameSource && requestedHighResolutionMipmaps == mipmaps &&
            requestedHighResolutionOnDemand == onDemand) return;
        const bool retainPublishedOnFailure = sameSource;
        requestedHighResolutionMipmaps = mipmaps;
        requestedHighResolutionOnDemand = onDemand;
        if (highResolutionEnabled != enabled) ++policyRevision;
        highResolutionEnabled = enabled;
        packIndexes.clear();
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
                sprite->releaseRetiredHighResolution(false);
                for (auto *&image : sprite->experimentImages) { delete image; image = nullptr; }
                for (auto *&image : sprite->experimentRotated) { delete image; image = nullptr; }
                sprite->highResolutionAtlas.reset();
                sprite->recomputeBlockCompleteHD();
                sprite->setHighResolutionFrames(wanted);
                continue;
            }
            if (onDemand && !demandedHighResolutionNames.contains(sprite->fileName)) {
                // Nothing on this backend samples these layers; a later
                // baseFrame request loads them again.
                sprite->releaseHighResolution();
                continue;
            }
            sprite->queueHighResolutionReload(retainPublishedOnFailure);
        }
    }
    bool Sprite::demandHighResolution()
    {
        if (highResolutionDemanded) return false;
        highResolutionDemanded = true;
        demandedHighResolutionNames.insert(fileName);
        // Eager backends, deferred banks and queued reloads need nothing more.
        if (!requestedHighResolutionOnDemand || !loadedSprites.contains(this) ||
            highResolutionDeferred(fileName) || pendingHighResolution.contains(this)) return false;
        if (std::any_of(experimentImages.begin(), experimentImages.end(), [](auto *image) { return image; }) ||
            std::any_of(experimentRotated.begin(), experimentRotated.end(), [](auto *image) { return image; }))
            return false;
        const char *overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return false;
        queueHighResolutionReload(true);
        return true;
    }
    // Every reload, whatever started it, decodes into a fresh sprite that is
    // published frame by frame. Replaces a reload already queued for it.
    void Sprite::queueHighResolutionReload(bool retainPublishedOnFailure)
    {
        auto prepared = std::make_unique<Sprite>();
        prepared->fileName = fileName;
        prepared->dynamicTeamColor = dynamicTeamColor;
        // A sprite portable draws loads only the blocks it has reached.
        if (auto found = portableBlocks.find(this); found != portableBlocks.end())
        {
            queueHighResolutionBlocks(found->second);
            return;
        }
        pendingHighResolution.insert_or_assign(this, HighResolutionReload{
            prefetchHighResolutionIncremental(fileName, images.size()), std::move(prepared), 0,
            retainPublishedOnFailure});
    }
    void Sprite::queueHighResolutionBlocks(const std::set<unsigned>& blocks)
    {
        auto prepared = std::make_unique<Sprite>();
        prepared->fileName = fileName;
        prepared->dynamicTeamColor = dynamicTeamColor;
        HighResolutionReload reload{prefetchHighResolutionBlocks(fileName, images.size(), blocks),
                                    std::move(prepared), 0, true};
        reload.blocks = blocks;
        pendingHighResolution.insert_or_assign(this, std::move(reload));
    }
    bool Sprite::highResolutionDrawable(unsigned index)
    {
        if (!drawsHighResolutionOnDemand()) return false;
        const bool deferred = highResolutionDeferred(fileName);
        if (!drawDemandedNames.contains(fileName))
        {
            // Load the team layers with the base ones: a frame never mixes resolutions.
            drawDemandedNames.insert(fileName);
            if (deferred) {
                // Decor streams the frames its windows declare.
                if (!highResolutionDemanded) {
                    drawDemandedSprites.insert(this);
                    demandHighResolution();
                }
            } else {
                // Blocks load as drawing reaches them, not the whole sheet.
                portableBlocks.try_emplace(this);
                if (!highResolutionDemanded) {
                    drawDemandedSprites.insert(this);
                    highResolutionDemanded = true;
                    demandedHighResolutionNames.insert(fileName);
                }
            }
        }
        const auto want = [&] {
            ++drawCounts.notLoaded;
            const unsigned block = index / HighResolutionBlock;
            if (!deferred && !highResolutionAdmissionHeld && !portableBlocks[this].contains(block))
                portableWaitingBlocks[this].insert(block);
            return false;
        };
        // Every layer the native frame has, in HD; a frame may be team layer only.
        if (index >= experimentImages.size()) return want();
        if (!HighResolutionPolicy::frameDrawsHighResolution(true, images[index], rotated[index],
                experimentImages[index], experimentRotated[index] && experimentRotated[index]->orig->hasPixels()))
            return want();
        if (dynamicTeamColor && !blockHasCompleteHD(index)) return want();
        // A first draw uploads the frame; a few per frame keep zooming in smooth.
        const auto key = std::make_pair(static_cast<const Sprite*>(this), index);
        const auto noteDrawn = [&] {
            const unsigned block = index / HighResolutionBlock;
            if (highResolutionBlockDrawn.size() <= block) highResolutionBlockDrawn.resize(block + 1);
            highResolutionBlockDrawn[block] = highResolutionFrameClock;
            ++drawCounts.drawn;
            return true;
        };
        if (portableDrawnFrames.contains(key)) return noteDrawn();
        if (highResolutionAdmissionHeld) { ++drawCounts.held; return false; }
        if (portableFirstDraws >= PortableFirstDrawsPerFrame) { ++drawCounts.firstDrawCap; return false; }
        // A device keeps an uploaded HD frame as the only copy of its pixels,
        // within 3/8 of its General budget; past that, new frames stay native
        // and the next frame evicts idle blocks to make room.
        if (!_gc->deviceHighResolutionRoom()) { ++drawCounts.room; highResolutionRoomShort = true; return false; }
        ++portableFirstDraws;
        portableDrawnFrames.insert(key);
        return noteDrawn();
    }
    DrawableSurface *Sprite::sampleBaseFrame(unsigned index)
    {
        if (!highResolutionDemanded) demandHighResolution();
        sampledByBaseFrame = true;
        auto *surface = prepareDrawSurface(index, false, true);
        if (surface && !surface->hasPixels())
        {
            // A portable draw released it: sample native until its block
            // reloads, keeping pixels from now on.
            if (portableBlocks.contains(this))
                portableWaitingBlocks[this].insert(index / HighResolutionBlock);
            // A device draw released it: reload the sheet in the background
            // with pixels; the released layers keep drawing until it publishes.
            else if (surface->deviceReleased() && !pendingHighResolution.contains(this) &&
                     !highResolutionDeferred(fileName))
                queueHighResolutionReload(true);
            return images[index];
        }
        return surface;
    }
    void Sprite::releaseDrawnHighResolution(unsigned index)
    {
        // Cheap checks first: drawing calls this for every frame it draws.
        // Deferred banks release too (decor): a reset that loses the textures
        // asks their windows for the frames again (discardUnbackedHighResolution).
        // Banks the compositor samples on the CPU keep their pixels.
        if (sampledByBaseFrame || index >= experimentImages.size() || !experimentImages[index] ||
            !experimentImages[index]->hasPixels() || highResolutionAtlasWanted(fileName) ||
            pixelSampledHighResolutionNames.contains(fileName))
            return;
        if (!experimentImages[index]->releasePortablePixels())
            experimentImages[index]->releaseDevicePixels();
    }
    void Sprite::releaseDrawnTeamColor(unsigned index)
    {
        const auto found = teamColorIndex.find(TeamColorKey{int(index), true, actColor.r, actColor.g, actColor.b});
        if (found != teamColorIndex.end())
            found->second->surface->releasePortablePixels();
    }
    void Sprite::releaseHighResolutionDemand()
    {
        if (!highResolutionDemanded || !requestedHighResolutionOnDemand || highResolutionDeferred(fileName)) return;
        highResolutionDemanded = false;
        demandedHighResolutionNames.erase(fileName);
        // Cancel every route that would load the layers again: a queued or
        // running reload, a band change and a lost-texture reload.
        pendingHighResolution.erase(this);
        lostHighResolution.erase(this);
        std::erase(bandReloadQueue, this);
        if (bandReloading == this) bandReloading = nullptr;
        releaseHighResolution();
    }
    void Sprite::releaseHighResolution()
    {
        std::erase_if(portableDrawnFrames, [this](const auto &key) { return key.first == this; });
        drawDemandedSprites.erase(this);
        portableBlocks.erase(this);
        portableWaitingBlocks.erase(this);
        highResolutionBlockDrawn.clear();
        clearTeamColorCache();
        for (auto *&image : experimentImages) { delete image; image = nullptr; }
        for (auto *&image : experimentRotated) { delete image; image = nullptr; }
        highResolutionAtlas.reset();
        recomputeBlockCompleteHD();
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
    void Sprite::discardUnbackedHighResolution()
    {
        // A layer released on a device adapter that still holds its snapshot
        // survives resets and recovery; one released elsewhere does not.
        const auto lost = [](const DrawableSurface *image) {
            return image && !image->hasPixels() && !image->deviceBacked();
        };
        const auto unbacked = [&](const Sprite &sprite) {
            for (const auto *image : sprite.experimentImages)
                if (lost(image)) return true;
            for (const auto *rotated : sprite.experimentRotated)
                if (rotated && lost(rotated->orig)) return true;
            return false;
        };
        for (auto *sprite : loadedSprites) {
            if (highResolutionDeferred(sprite->fileName)) {
                // A deferred bank keeps the frames that still have pixels and
                // forgets only those its window must decode again.
                auto &stream = deferredHighResolution[sprite];
                bool lost = false;
                for (unsigned frame = 0; frame < sprite->experimentImages.size(); ++frame) {
                    auto *&image = sprite->experimentImages[frame];
                    auto *&rotated = sprite->experimentRotated[frame];
                    if (!(image && !image->hasPixels()) && !(rotated && !rotated->orig->hasPixels())) continue;
                    if (const auto retired = stream.retired.find(frame); retired != stream.retired.end()) {
                        retiredHighResolutionTotal -= retired->second->bytes;
                        retiredHighResolution.erase(retired->second);
                        stream.retired.erase(retired);
                    }
                    delete image; image = nullptr;
                    delete rotated; rotated = nullptr;
                    stream.completed.erase(frame);
                    portableDrawnFrames.erase({sprite, frame});
                    lost = true;
                }
                if (!lost) continue;
                sprite->clearTeamColorCache();
                sprite->recomputeBlockCompleteHD();
                lostHighResolution.insert(sprite);
                continue;
            }
            const auto pending = pendingHighResolution.find(sprite);
            const bool staged = pending != pendingHighResolution.end() && pending->second.prepared &&
                unbacked(*pending->second.prepared);
            if (!staged && !unbacked(*sprite)) continue;
            // A half-built replacement lost its textures too: start it again.
            if (pending != pendingHighResolution.end()) pendingHighResolution.erase(pending);
            for (auto *&image : sprite->experimentImages) { delete image; image = nullptr; }
            for (auto *&image : sprite->experimentRotated) { delete image; image = nullptr; }
            sprite->clearTeamColorCache();
            sprite->recomputeBlockCompleteHD();
            lostHighResolution.insert(sprite);
        }
    }
    bool Sprite::pollHighResolution(unsigned budgetMs)
    {
        return pollHighResolutionUntil(GAGCore::steadyClockNanoseconds() + std::int64_t(budgetMs) * 1000000);
    }
    bool Sprite::pollHighResolutionUntil(std::int64_t deadlineNanoseconds)
    {
        evictRetiredHighResolution();
        advanceBandReload();
        // Reload what a context rebuild discarded, under the renderer now active.
        for (auto *sprite : lostHighResolution) {
            if (!loadedSprites.contains(sprite)) continue;
            if (highResolutionDeferred(sprite->fileName)) {
                // Declare the window again: its lost frames are no longer
                // completed, so they queue; in-flight and resident ones stay.
                auto &stream = deferredHighResolution[sprite];
                const auto window = stream.order;
                stream.wanted.clear();
                sprite->setHighResolutionFrames(window);
                continue;
            }
            if (pendingHighResolution.contains(sprite)) continue;
            sprite->queueHighResolutionReload(true);
        }
        lostHighResolution.clear();
        const std::chrono::steady_clock::time_point deadline(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(deadlineNanoseconds)));
        for (auto it = pendingHighResolution.begin(); it != pendingHighResolution.end();) {
            if (std::chrono::steady_clock::now() >= deadline) break;
            auto *sprite = it->first;
            auto &reload = it->second;
            // Read native dimensions from the live sprite, but build every HD
            // layer and its atlas in a private target until publication.
            while (reload.frame < sprite->images.size()) {
                if (!reload.blocks.empty() && !reload.blocks.contains(unsigned(reload.frame / HighResolutionBlock))) {
                    // Not loaded by this reload: publication keeps the sprite's own.
                    reload.prepared->experimentImages.push_back(nullptr);
                    reload.prepared->experimentRotated.push_back(nullptr);
                    ++reload.frame;
                    continue;
                }
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
                    if (!reload.prepared->experimentImages[frame] && !reload.prepared->experimentRotated[frame]) {
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
            sprite->noteHighResolutionReplaced();
            sprite->releaseCoveredNativeTextures();
            it = pendingHighResolution.erase(it);
        }
        if (trimAfterBandReload && pendingHighResolution.empty() && bandReloadQueue.empty()) {
            trimAfterBandReload = false;
            // glibc keeps memory the worker threads freed in their own arenas;
            // thousands of decodes otherwise leave hundreds of MiB resident.
#ifdef __GLIBC__
            malloc_trim(0);
#endif
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
                    if (sprite->experimentImages[found->first] || sprite->experimentRotated[found->first])
                        sprite->noteHighResolutionFrame(found->first);
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
        finishHighResolution();
    }
    void Sprite::finishHighResolution(const Sprite *only)
    {
        while (!pollHighResolution(4) && (!only || pendingHighResolution.contains(const_cast<Sprite*>(only)))) {
            Toolkit::assets().poll();
#ifdef __EMSCRIPTEN__
            if (Toolkit::assets().browserVideoPending()) break;
#else
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
        if (!highResolutionAtlasWanted(fileName)) return;
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
        const std::string& name, size_t frames, bool incremental, const std::set<unsigned>* blocks = nullptr)
    {
        HighResolutionInputs inputs;
        const char* overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return inputs;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        const auto* packEntries = readPack(directory, name);
        if (!packEntries) return inputs;
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        const bool mipmaps = highResolutionInputMipmaps(name);
        size_t firstFrame = frames;
        auto add = [&](const std::string& path, bool mipmaps) {
            inputs.add(path, mipmaps);
            if (!incremental) inputs.request(Toolkit::assets(), path, mipmaps);
        };
        for (size_t i = 0; i < frames; ++i) {
            if (blocks && !blocks->contains(unsigned(i / HighResolutionBlock))) continue;
            auto found = packEntries->find(prefix + std::to_string(i));
            if (found == packEntries->end()) continue;
            for (const auto& file : wantedLayers(found->second, name)) {
                if (!validHighResolutionFile(file)) continue;
                firstFrame = std::min(firstFrame, i);
                add(directory + '/' + file, mipmaps);
            }
        }
        if (highResolutionAtlasWanted(name))
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
    HighResolutionInputs prefetchHighResolutionBlocks(const std::string& name, size_t frames,
                                                      const std::set<unsigned>& blocks)
    {
        return highResolutionInputs(name, frames, true, &blocks);
    }
    bool highResolutionFrameReady(const std::string& name, size_t index, HighResolutionInputs& inputs)
    {
        const char* overrideDir = std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        if ((!highResolutionEnabled && !overrideDir) || !highResolutionFramesSupported()) return true;
        const std::string directory = overrideDir ? overrideDir : "data/highres/v1";
        const auto* packEntries = readPack(directory, name);
        if (!packEntries) return true;
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        const bool mipmaps = highResolutionInputMipmaps(name);
        // Deferred sparse inputs own one frame; numeric lookahead only adds
        // repeated failed lookups there. Ordinary sprite prefetch keeps eight.
        const size_t lookahead = Sprite::highResolutionDeferred(name) ? 1 : 8;
        for (size_t frame = index; frame < index + lookahead; ++frame) {
            auto entry = packEntries->find(prefix + std::to_string(frame));
            if (entry == packEntries->end()) continue;
            for (const auto& file : wantedLayers(entry->second, name))
                if (validHighResolutionFile(file)) inputs.request(Toolkit::assets(), directory + '/' + file, mipmaps);
        }
        auto found = packEntries->find(prefix + std::to_string(index));
        if (found == packEntries->end()) return true;
        for (const auto& file : wantedLayers(found->second, name))
            if (validHighResolutionFile(file) && inputs.pending(directory + '/' + file, mipmaps)) return false;
        return true;
    }
    bool highResolutionAtlasReady(const std::string& name, HighResolutionInputs& inputs)
    {
        const auto prefix = name.substr(name.find_last_of('/') + 1);
        if (!highResolutionAtlasWanted(name)) return true;
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
        const auto* packEntries = readPack(directory, fileName);
        if (!packEntries) return;
        const auto wanted = fileName.substr(fileName.find_last_of('/') + 1) + std::to_string(index);
        auto found = packEntries->find(wanted); if (found == packEntries->end()) return;
        const auto &entry = found->second;
        if (entry.width != getW(index) || entry.height != getH(index) || entry.scale != (dynamicTeamColor ? 0 : 4)) return;
        const bool deferred = highResolutionDeferred(fileName);
        const bool mipmaps = highResolutionInputMipmaps(fileName);
        // Legacy OpenGL (desktop and the browser's WebGL2) draws a standalone HD
        // layer from its texture alone, so its pixels go once uploaded
        // (releasePixels refuses on every other renderer). The exceptions read pixels later: frames an atlas is
        // built from, sprites a caller samples, and, without the team-colour
        // shader, team layers, which a CPU recolour then clones.
        const bool releaseBase = !deferred && !highResolutionAtlasWanted(fileName) &&
            !pixelSampledHighResolutionNames.contains(fileName);
        const bool releaseTeam = releaseBase && _gc->hasUnitShader();
        const bool banded = highResolutionBanded(fileName);
        const auto &policy = currentHighResolutionPolicy();
        // Renderers without mip chains draw the policy's level as the frame:
        // its half-resolution level is the second of the chain the workers built.
        const bool reduce = reducedHighResolutionWanted(fileName) && !sampledByBaseFrame;
        auto load = [&](const std::string& name, DrawableSurface *original, bool release) -> DrawableSurface* {
            if (!validHighResolutionFile(name)) return nullptr;
            // A partial pack may have no usable prepacked atlas. Every accepted
            // layer needs a ready standalone texture for that fallback.
            auto image = inputs.consume(directory + '/' + name, mipmaps);
            if (!image.value) return nullptr;
            auto *surface = image.value->surface;
            const int width = dynamicTeamColor ? highResolutionTextureSize : (original ? original->getW() : entry.width) * entry.scale;
            const int height = dynamicTeamColor ? highResolutionTextureSize : (original ? original->getH() : entry.height) * entry.scale;
            if (surface->w != width || surface->h != height) return nullptr;
            std::unique_ptr<DrawableSurface> result;
            const auto &mips = image.value->mips;
            if (reduce && mips.size() > 1 && (!mips[1].pixels.empty()))
                result = DrawableSurface::fromReducedLevel(*image.value, 1, width / 2, height / 2);
            else
                result = DrawableSurface::fromAssetImage(*image.value, image.exclusive, !deferred);
            result->highResolutionSampling = true;
            // Every renderer samples a drawn HD frame's one level linearly, so
            // all show the same pixels (devices draw it with blitLinear).
            if (!deferred && (banded || highResolutionOnDemand())) result->singleLevelSampling = true;
            if (banded) result->uploadSkipLevels = std::uint8_t(policy.reduction());
            // Deferred terrain feeds CPU composition; only composed pages need
            // GPU textures. On-demand backends reach HD layers through baseFrame,
            // mostly for CPU composition too. A direct draw uploads lazily.
            if (!deferred && !highResolutionOnDemand()) result->prepareTexture();
            if (release) result->releasePixels();
            return result.release();
        };
        const bool team = !highResolutionOnDemand() || drawDemandedNames.contains(fileName);
        // A unit's base layer is recoloured with its team layer without the shader.
        auto *normal = load(entry.base, images[index], dynamicTeamColor ? releaseTeam : releaseBase);
        auto *colored = team ? load(entry.team, rotated[index] ? rotated[index]->orig : nullptr, releaseTeam) : nullptr;
        if ((entry.base != "-" && !normal) || (team && entry.team != "-" && !colored) ||
            (images[index] && entry.base == "-") || (team && rotated[index] && entry.team == "-")) {
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
			// A recolour clones the layer's pixels; a released HD layer has none.
			if (experiment && experimentRotated[index] && experimentRotated[index]->orig->hasPixels())
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
        releaseRetiredHighResolution(false);
        lostHighResolution.erase(this);
        std::erase(bandReloadQueue, this);
        if (bandReloading == this) bandReloading = nullptr;
        deferredHighResolution.erase(this);
        drawDemandedSprites.erase(this);
        portableBlocks.erase(this);
        portableWaitingBlocks.erase(this);
        std::erase_if(portableDrawnFrames, [this](const auto &key) { return key.first == this; });
        if (deferredHighResolutionCursor == this) deferredHighResolutionCursor = nullptr;
        if (loadedSprites.erase(this)) pendingHighResolution.erase(this);
        std::erase_if(pendingShadows, [this](const auto &entry) { return entry.first.first == this; });
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
