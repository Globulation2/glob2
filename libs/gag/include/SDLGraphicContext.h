// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GAGSys.h"
#include "GAGColor.h"
#include "CursorManager.h"
#include "RenderFramePacer.h"
#include "RendererSelection.h"
#include "RenderResources.h"
#include "GameplayRecording.h"
#include "SkinAtlasCache.h"
#include "SkinShadow.h"
#include "RadialFade.h"
#include "CloudCover.h"
#include "SpriteRenderAtlas.h"
#include <AssetLoader.h>
#include <array>
#include <functional>
#include <map>
#include <span>
#include <utility>
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <optional>
#include <cstdint>

#include <span>
#include <set>
#include <tuple>
#include <list>
#include <unordered_map>

#include <SDL3/SDL.h>


namespace GAGCore
{
    class DrawableSurface;
    class RenderBatch;
    class SpriteLoad;
    struct SkinMesh;
    struct SkinMeshRequest;

    class RenderBackend;
    struct DrawPacket;
    struct DeviceSkinState;
    class RenderDevice;
    class RenderResources;
    class SoftwareFramePresenter;
    struct RenderOperations;
	
	class Sprite;
	namespace RenderMemory { struct SpriteUsage; }
	
	//! Font with a given foundry, shape and color
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

		//! Whether every character of this UTF-8 string has a glyph in this font
		virtual bool hasGlyphsFor(const std::string &utf8Text) { return true; }

		//! Positive UTF-8 byte ends of indivisible shaping clusters, in logical order.
		//! Legacy fonts default to code points; shaped fonts override this contract.
		virtual std::vector<std::size_t> getStringClusterEnds(const std::string &text);

		//! Changes whenever measurements of the same text and style may change
		//! (reload, script language). Callers memoize layout by this value.
		virtual std::uint64_t metricsRevision() const { return 0; }

		// width and height
		virtual int getStringWidth(const std::string string) = 0;
		virtual int getStringWidth(const std::string string, int len);
		virtual int getStringWidth(const int i);
		virtual int getStringHeight(const std::string string) = 0;
		virtual int getStringHeight(const std::string string, int len);
		virtual int getStringHeight(const int i);
		//! Owned text bitmap at explicit pixels per authored unit, for transformed labels.
		//! Layout metrics and the current screen transform are unaffected.
		virtual std::unique_ptr<DrawableSurface> rasterizeString(const std::string &text, float scale) = 0;
	
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

	// Texture dimensions
	struct TextureInfo
	{
		//! which animation or texture atlas this surface is part of.
		Sprite* sprite = nullptr;
		//! sprite sheet coordinates
		int texX = 0;
		int texY = 0;
		//! width and height of this tile if using atlas
		int w = 0;
		int h = 0;
	};
	
	//! A surface on which we can draw
	class DrawableSurface
	{
	protected:
		friend struct Color;
		friend class GraphicContext;
		friend class Sprite;
        friend class SpriteLoad;
		//! the underlying software SDL surface
		SDL_Surface *sdlsurface;
		// Texture dimensions
		std::optional<TextureInfo> textureInfo;
        Sprite *spriteSource = nullptr;
        std::uint32_t spriteFrame = 0;
		//! The clipping rect, we do not draw outside it
		SDL_Rect clipRect;
		// Content revisions are never consumed by drawing. Each backend remembers
		// its own uploaded revision; raw pixel writes must call markPixelsChanged().
		std::vector<unsigned char> preparedUploadPixels;
        std::vector<AssetImage::Mip> preparedMips;
        AssetImage::BlockCompression preparedCompression = AssetImage::BlockCompression::Uncompressed;
        std::uint64_t preparedUploadRevision = 0;
		std::uint64_t glUploadedRevision = 0; // Revision uploaded to this surface's legacy GL texture.
        std::uint64_t pixelRevision = 1, opacityRevision = 0;
        static std::uint64_t nextSurfaceIdentity();
        const std::uint64_t surfaceIdentity = nextSurfaceIdentity();
        bool opaquePixels = false;
		bool highResolutionSampling=false;
		//! The device budget class this surface's texture counts against.
		//! Terrain imagery uses TerrainPage, which the page caches size for.
		TextureDescriptor::BudgetClass deviceBudgetClass = TextureDescriptor::BudgetClass::General;
		//! texture index if GPU (GL) is used
		unsigned int texture=0;
		size_t gpuBytes=0;
		//! The size of the pixels releasePixels() freed.
		int releasedW = 0, releasedH = 0;
		bool releasedOnPortable = false;
		bool releasedOnDevice = false;
		//! Prepared mip levels left out of the legacy GL upload (a detail band).
		std::uint8_t uploadSkipLevels = 0;
		//! An HD sprite frame: every renderer samples only the one level the
		//! HD sprite policy names (legacy GL its band's first level, devices
		//! the first prepared level), linearly, as the portable renderer does.
		bool singleLevelSampling = false;
		//! texture divisor
		float texMultX, texMultY;
		
	protected:
		//! draw a vertical line. This function is private because it is only a helper one
		void _drawVertLine(int x, int y, int l, const Color& color);
		//! draw a horizontal line. This function is private because it is only a helper one
		void _drawHorzLine(int x, int y, int l, const Color& color);
		
	protected:
		//! Protected constructor, only called by GraphicContext
		DrawableSurface() { sdlsurface = NULL; }
		//! allocate a GPU texture object, optionally initializing legacy padded storage
		void allocateTexture(bool initializeStorage = true);
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
        //! Adopt CPU-prepared ARGB8888 pixels on the renderer thread.
        struct AdoptPixels {};
        DrawableSurface(SDL_Surface *prepared, AdoptPixels, bool allocateGPU = true);
        //! Create an owned renderer surface from prepared CPU data. Exclusive
        //! callers must own the image payload; other callers receive a pixel copy.
        static std::unique_ptr<DrawableSurface> fromAssetImage(const AssetImage& image,
            bool exclusive = false, bool allocateGPU = true);
        //! A surface of the top-left `width` by `height` pixels of the image's
        //! prepared mip `level`: the reduced HD frame renderers without mip
        //! chains draw (see HighResolutionPolicy). It keeps no upload preparation.
        static std::unique_ptr<DrawableSurface> fromReducedLevel(const AssetImage& image, unsigned level,
            int width, int height);
        void adoptUploadPreparation(const AssetImage& image, bool exclusive = false);
        void prepareTexture();
		DrawableSurface *clone(void);
        DrawableSurface(const DrawableSurface&) = delete;
        DrawableSurface& operator=(const DrawableSurface&) = delete;
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
		virtual int getW(void) { if (textureInfo) return textureInfo->w; return sdlsurface ? sdlsurface->w : releasedW; }
		virtual int getH(void) { if (textureInfo) return textureInfo->h; return sdlsurface ? sdlsurface->h : releasedH; }
		//! The raw software surface, e.g. to hand off to an SDL API that wants one directly.
		//! Null once releasePixels() succeeded.
		SDL_Surface *getSDLSurface(void) { return sdlsurface; }
		//! Whether the decoded pixels are still in memory.
		bool hasPixels() const { return sdlsurface != nullptr; }
		//! Frees the decoded pixels of a surface whose desktop legacy OpenGL texture
		//! holds its current content: the texture is then the only copy, which a
		//! context rebuild loses, so the owner must be able to load the surface
		//! again. Only drawing it and reading its size and opacity stay valid.
		//! Returns false and changes nothing where the pixels are still needed.
		bool releasePixels();
		//! The portable renderer's counterpart: frees the CPU pixels once its
		//! texture holds this revision; draws then use that texture. Owners
		//! recompose or reload such a surface after a renderer reset.
		bool releasePortablePixels();
		//! Pixels released by releasePortablePixels, drawn from the texture.
		bool portableReleased() const { return releasedOnPortable; }
		//! Released, and the portable renderer no longer holds its texture (a
		//! reset or a backend change took it): nothing can draw it any more.
		bool portableTextureLost() const;
		//! The presentation devices' counterpart (SDL GPU, programmable GL,
		//! WebGPU): frees the CPU pixels and upload staging once the device
		//! adapter holds a restoration snapshot of this revision, which it then
		//! keeps (device resets and context recovery restore from it). Draws use
		//! that snapshot. A few per frame; false where the pixels stay.
		bool releaseDevicePixels();
		//! Pixels released by releaseDevicePixels.
		bool deviceReleased() const { return releasedOnDevice; }
		//! Released on a device whose adapter still holds the snapshot: false
		//! after a backend switch, so the owner must reload the surface.
		bool deviceBacked() const;
		//! On a presentation device, whether the adapter holds this revision's
		//! texture, so drawing it uploads nothing. True on other renderers.
		bool deviceHoldsPixels() const;
		//! Takes one of the few new device textures a frame may upload (true
		//! on other renderers). A caller with a stand-in draws that instead,
		//! or keeps drawing a held older revision, when this is false: SDL GPU
		//! submits a command buffer for every texture it creates.
		bool takeDeviceUpload();
		//! Admits this revision to the presentation device adapter ahead of its
		//! first draw, within takeDeviceUpload's allowance: the adapter keeps
		//! its texture snapshot (a copy of the pixels), so a later frame draws it
		//! without taking an allowance; the device copies it to the GPU in the
		//! command buffer of the first frame that draws it, as for any draw.
		//! True once the adapter holds it; false on other renderers (which upload
		//! when they draw), for linear or mipmapped surfaces, and while the
		//! allowance is spent.
		bool prepareDeviceTexture();
        std::uint64_t lifetimeIdentity() const { return surfaceIdentity; }
        std::uint64_t contentRevision() const { return pixelRevision; }
        virtual void prepareDraw() {}
        void markPixelsChanged() { ++pixelRevision; }
        //! Marks raw writes confined to `dirty` (surface pixel rectangles). The
        //! portable renderer then updates only those rectangles of a texture
        //! holding the previous revision; other backends upload as usual.
        void markPixelsChanged(std::span<const SDL_Rect> dirty);
        bool hasOpaquePixels();
		static size_t allocatedTextureBytes();
		//! Decoded pixels and upload staging that live surfaces hold in memory.
		struct ResidentPixels { size_t surfaces = 0, surfaceBytes = 0, preparedBytes = 0; };
		static ResidentPixels residentPixels();

		virtual int getTexX(void) { if (textureInfo) { return textureInfo->texX; } return 0; }
		virtual int getTexY(void) { if (textureInfo) { return textureInfo->texY; } return 0; }
		
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
		//! Draws a frame's base and team layers as one team-colour shader quad; false when it cannot.
		bool drawTeamShaderSprite(float x, float y, float w, float h, Sprite *sprite, unsigned index, Uint8 alpha);
		// An on-demand renderer (every one but legacy GL) where the HD sprite
		// policy shows HD: the frame's HD layers at its logical size, when they
		// can draw now (Sprite::highResolutionDrawable); false draws native.
		bool drawHighResolution(float x, float y, float w, float h, Sprite *sprite, unsigned index, Uint8 alpha);
		// A device renderer with a team-colour shader: the frame's team layer
		// (HD when `experiment` and it has pixels, as prepareDrawSurface picks)
		// recoloured on the GPU, with no CPU recolour. A negative w draws it at
		// its own size. False when the caller must draw a CPU-recoloured copy.
		bool drawDeviceTeamLayer(float x, float y, float w, float h, Sprite *sprite, unsigned index, bool experiment, Uint8 alpha);
		void drawSprite(float x, float y, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		void drawSprite(int x, int y, int w, int h, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		void drawSprite(float x, float y, float w, float h, Sprite *sprite, unsigned index = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		
		void drawString(int x, int y, Font *font, const std::string &msg, int w = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		void drawString(float x, float y, Font *font, const std::string &msg, float w = 0, Uint8 alpha = Color::ALPHA_OPAQUE);
		

		//! Draw an alpha map of size mapW, mapH using a specific color at coordinates x, y using cells of size cellW, cellH.
		//! map views at least mapW * mapH row-major values; it is read, never kept.
		virtual void drawAlphaMap(std::span<const float> map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		virtual void drawAlphaMap(std::span<const unsigned char> map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		
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
		
		// This is for translation text shot code, it works by trapping calls to the getString function in the translation StringTables,
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
		static std::vector<std::tuple<SRectangle, std::string, GAGCore::DrawableSurface*> > drawSquares;
		// This holds the directory the pictures will be stored in. The system is disabled if this string is empty.
		static std::string translationPicturesDirectory;
		// This flushes all of the detected texts, making bmp pictures
		static void flushTextPictures();
		// This prints out information on all of the texts that where requested but not detected
		static void printFinishingText();
	};

	//! The description of a video mode
	typedef std::vector<SDL_DisplayMode> VideoModes;
	
	//! Why presentation memory is trimmed: the system is short of memory while
	//! the game runs, or the game is going to the background (nothing is drawn).
	enum class PresentationTrim { LowMemory, Background };
	//! A presentation cache outside libgag (terrain pages) frees what it can
	//! rebuild while subscribed. Handlers run on the presentation thread, from
	//! GraphicContext::trimPresentationMemory; a low-memory handler must spread
	//! its work over its later frames rather than free everything at once.
	class PresentationTrimSubscription
	{
	  public:
		using Handler = std::function<void(PresentationTrim)>;
		explicit PresentationTrimSubscription(Handler handler);
		~PresentationTrimSubscription();
		PresentationTrimSubscription(const PresentationTrimSubscription &) = delete;
		PresentationTrimSubscription &operator=(const PresentationTrimSubscription &) = delete;
		static void notify(PresentationTrim);
	  private:
		Handler handler;
	};

	//! A GraphicContext is a DrawableSurface that represent the main screen of the application.
	class GraphicContext:public DrawableSurface
	{
        RendererSwitchStatus backendStatus;
        std::optional<RendererBackend> nextBackend;
        bool changingBackend = false, nextBackendPersists = true, switchPersists = true, recoveringBackend = false;
        RendererCapabilities retiringCapabilities;
        Uint64 switchStarted = 0;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> transitionPixels{nullptr, SDL_DestroySurface};
        std::function<void(RendererBackend)> backendPreferenceCallback;
        static std::optional<RendererBackend> initialBackend;
        RendererBackend automaticBackend = RendererBackend::Software;
        //! Families Auto tries in order at startup; a lost device falls to the next.
        std::vector<RendererBackend> automaticOrder;
        RendererBackend fallbackAfter(RendererBackend failed) const;
        bool awaitingBrowserSurface = false;
        bool awaitingDevicePreparation = false, awaitingSwitchCapture = false;
        RendererBackend switchingFrom = RendererBackend::Software, switchingTo = RendererBackend::Software;
        int switchWindowX = SDL_WINDOWPOS_UNDEFINED, switchWindowY = SDL_WINDOWPOS_UNDEFINED;
        void retirePresentationSurface();
        void applyQueuedBackend();
        void captureBackendFrame();
        void completeBackendFrame();
        void recoverPresentationDevice();
        void handleDeviceFailure(const std::string &);
        void resetDeviceSkinState() { deviceSkinState.reset(); }
        void invalidateDeviceSkinTiles();
        void prepareDeviceSkinMeshes(const std::vector<SkinMeshRequest> &);
        bool drawDeviceSkinMesh(const SkinMeshRequest &, float, float, float, float, SkinShadow, Uint8, Color = Color(), Uint8 = 0);
        bool readDeviceSkinMesh(const SkinMeshRequest &, std::vector<std::uint8_t> &);
        void captureScreenshot();
        bool saveScreenshot(SDL_Surface &, const std::string &);
        static std::string rendererShaderDirectory();
    public:
        RenderDevice *renderDevice() const { return renderer == deviceRenderer.get() ? presentationDevice.get() : nullptr; }
        RenderResources *renderResources() const { return deviceResources.get(); }
        //! Whether the device renderer has room to keep another HD sprite frame
        //! as the only copy of its pixels (its retained General textures under
        //! three eighths of that class, a quarter below the cap retainDeviceTexture
        //! enforces); true on other renderers.
        bool deviceHighResolutionRoom() const;
        //! Snapshots the device adapter retains for surfaces whose pixels it released.
        std::size_t retainedDeviceSnapshotBytes() const;
        //! Frees presentation memory that can be rebuilt, without a renderer
        //! reset: completed GPU copies, CPU restoration snapshots whose owners
        //! keep their pixels, terrain pages off the view and retired HD frames.
        //! Backgrounded nothing is drawn, so it all goes at once; otherwise the
        //! snapshots and frames go in bounded steps over the next frames.
        void trimPresentationMemory(PresentationTrim);
        //! One bounded step of a low-memory trim; Toolkit::pollAssets calls it a frame.
        void servicePresentationTrim();
        bool presentationTrimPending() const { return snapshotTrimPending; }
        std::shared_ptr<RenderResources> renderResourceOwner() const { return deviceResources; }
        bool beginDeviceTarget(TextureSnapshot target, float logicalWidth, float logicalHeight);
        void endDeviceTarget();
        bool submitDevicePacket(DrawPacket packet);
        // Bounded offscreen producers publish only completed logical captures.
        bool flushDeviceFrame();
        bool presentedDeviceFrame() const;
        bool deferDeviceFrame();
        // See GAGCore::allowDeviceFrameDeferral.
        void allowDeviceFrameDeferral();
        bool setDeviceTargetScissor(const SDL_Rect *pixels);
    private:
        bool activateBackend(RendererBackend backend, SDL_Surface *previousFrame);
		RenderFramePacer renderPacer;
		static const bool verbose = false;
	public:
        static void setInitialRendererBackend(RendererBackend backend) { initialBackend = backend; }
        bool requestBackendSwitch(RendererBackend backend);
        const RendererSwitchStatus &backendSwitchStatus() const { return backendStatus; }
        RendererCapabilities rendererCapabilities() const;
        std::vector<RendererAvailability> rendererAvailability() const;
        //! The families Auto tries, best first, for a context created with \p flags.
        //! Unavailable entries are skipped and Software always ends the list.
        static std::vector<RendererBackend> automaticRendererOrder(Uint32 flags);
        //! What Auto resolves to in this context (the family it started on when Auto was requested).
        RendererBackend automaticRenderer() const { return automaticBackend; }
        void setBackendPreferenceCallback(std::function<void(RendererBackend)> callback) { backendPreferenceCallback = std::move(callback); }
		// Interactive hosts reserve drawing before painting; exports and benchmarks
		// leave the context's default uncapped. Zero means Unlimited.
		void setTargetRenderFps(int fps) { renderPacer.configure(fps); }
		void resetRenderPacing() { renderPacer.reset(); }
        bool beginRenderFrame();
		Uint32 renderFrameWait() const { return renderPacer.waitMilliseconds(SDL_GetTicksNS()); }
		bool renderFrameCapped() const { return renderPacer.capped(); }

		//! The cursor manager, public to be able to set custom cursors
		CursorManager cursorManager;
		
		//! Flags that define the characteristic of the graphic context
		enum OptionFlags
		{
			DEFAULT = 0,
			USEGPU = 1,
			FULLSCREEN = 2,
			//! Allow windowed logical dimensions to follow the window size
			RESIZABLE = 8,
			CUSTOMCURSOR = 16,
			PORTABLEGPU = 32,
            NOAUDIO = 128, // Offline artwork export creates only the video context.
            //! Opt out of high-density backing pixels for fixed-pixel profiling.
            LOWPIXELDENSITY = 64,
            //! Let Auto follow the game's measured per-platform renderer order
            //! (automaticRendererOrder). The game sets it at startup; it is never
            //! persisted. Without it Auto keeps the historical mapping of USEGPU
            //! and PORTABLEGPU for tools and tests.
            PREFERDEVICE = 256,
            //! Open the desktop window maximized, and track whether the player
            //! leaves it so. The windowed size remembered beside it is the
            //! restored size, never the maximized one.
            MAXIMIZED = 512,
		};
		
	protected:
		//! the minimum acceptable resolution
		int minW, minH;
		//! window size in window points, as SDL reports mouse coordinates; differs from the logical resolution when fullscreen scaling is active
		int windowW = 0, windowH = 0;
#if !defined(GLOB2_MOBILE) && !defined(__EMSCRIPTEN__)
		bool nativeDesktop = true;
#else
		bool nativeDesktop = false;
#endif
		bool nativeSoftware = false;
		int desktopLogicalW = 0, desktopLogicalH = 0;
		float preferredUiScale = 0, desktopSystemScale = 1;
		int desktopDisplay = -1;
		void refreshDesktopScale();
		bool refreshNativeWindow();
		//! Mirror the window's maximized state into MAXIMIZED, unless it is
		//! fullscreen; returns whether the windowed state is maximized.
		bool trackMaximized()
		{
			if (!window) return bool(optionFlags & MAXIMIZED);
			const SDL_WindowFlags state = SDL_GetWindowFlags(window);
			if (state & SDL_WINDOW_FULLSCREEN) return bool(optionFlags & MAXIMIZED);
			if (state & SDL_WINDOW_MAXIMIZED) optionFlags |= MAXIMIZED; else optionFlags &= ~MAXIMIZED;
			return bool(state & SDL_WINDOW_MAXIMIZED);
		}
		std::function<void(int, int, bool)> displayPreferenceCallback;
        int fixedLogicalW=0, fixedLogicalH=0;
        bool responsiveViewport=false, compactWindowAllowed=false;
        bool uiTransformActive=false;
        SDL_Rect uiSavedClip{}, uiBounds{};
        float uiTransformScale=1, uiTransformX=0, uiTransformY=0;
        int responsiveMinW=0, responsiveMinH=0;
		//! Output drawable size in pixels; exceeds window points on HiDPI displays
		int drawableW = 0, drawableH = 0;
		//! resolution asked of setRes(), before the interface scale divides it
		int requestedW = 0, requestedH = 0;
		//! Window points per logical layout unit; native rasterization applies backing density separately
		float uiScale = 1.0f;
		//! the scale setRes() was asked for, before the window floor reduced it
		float wantedUiScale = 1.0f;
		//! interface scale for the next setRes(); 0 follows the desktop
		static float requestedUiScale;
		//! push minW/minH to SDL scaled by uiScale, so the floor applies to the
		//! logical surface rather than to the window it is stretched into
		void applyWindowMinimumSize(void);
		//! ratio of GL drawable pixels to logical pixels
		float drawableScale(void);
		//! target pixels per logical pixel for the rasteriser: the offscreen target while one is set, the window otherwise
		float rasterScale(void);
		//! target pixels per logical pixel while drawing into an offscreen target; 0 while drawing into the window
		float renderTargetScale = 0.0f;
		bool offscreenPass = false;
		bool mapTransformActive=false;
		//! A CPU surface's map pass at 1:1 draws directly, without a
		//! transform, but still clips to the map's rectangle.
		bool cpuMapClip=false;
        bool periodicCopy=false;
		//! The portable renderer biases nearest-sampling ties (every pixel
		//! centre at half zoom) toward the lower texel, except while a
		//! team-coloured building frame draws (see drawSurface).
		bool nearestTiesUnbiased=false;
		float mapScale=1, mapTranslateX=0, mapTranslateY=0;
		void drawMapSnappedRect(int x1, int y1, int x2, int y2, const Color& color, bool stroked, float maxStrokePoints);
		float overlayScale=1;
        float mapCopyTranslateX=0, mapCopyTranslateY=0;
		unsigned long drawCalls=0;
		int mapClipX=0,mapClipY=0,mapClipW=0,mapClipH=0;
		//! the GL viewport that fits the logical resolution into the drawable without distorting its aspect ratio, letterboxed/pillarboxed as needed
		void glLetterbox(float &scale, int &offX, int &offY);
		//! apply glLetterbox() as the current GL viewport
		void applyGLViewport(void);
		//! true when sdlsurface was allocated here rather than fetched from SDL
		bool ownsSurface = false;
		//! release sdlsurface when this context allocated it
		void freeOwnedSurface(void);
		//! refresh the window and drawable sizes after the window was resized
		void updateWindowSize(void);
		SDL_Window *window = nullptr;
		unsigned glContextGeneration = 0;
        std::uint64_t targetContentRevision = 0;
		SDL_GLContext context = nullptr;
		SDL_ThreadID eventThread = 0;
		bool pollingEvents = false;
		bool presenting = false;
		bool watchingEvents = false;
		// Owned here and released explicitly while the GL context is still current.
		struct FrameCache
		{
			SDL_Surface *surface = nullptr;
			unsigned texture = 0;
			int width = 0, height = 0;
			int textureWidth = 0, textureHeight = 0;
			int maximumTextureSize = 0;
			bool valid = false;
			bool failureReported = false;
		} frameCache;
		void reportFrameCacheFailure(const char *reason);
		void releaseFrameCache();
		void captureRecordingFrame();
		void cacheFrame();
        std::unique_ptr<SoftwareFramePresenter> softwarePresenter;
        void prepareDraw() override;
		void presentLastFrame();
		// An HD layer's alpha-weighted mip chain for device uploads, prepared
		// once per content revision and borrowed by the returned levels. Levels
		// carry worker-prepared blocks wherever the device samples them.
		std::vector<RenderMipLevel> deviceMipLevels(DrawableSurface *surface, SDL_Surface *pixels);
		// After a draw of deviceMipLevels: once blocks reached the device, the
		// RGBA levels are only needed again by a device that cannot sample them.
		static void releaseDeviceRgbaLevels(DrawableSurface *surface, std::span<const RenderMipLevel> levels);
		// GLSL 1.20 program that recolors a sprite's unrotated team layer on the
		// GPU (same HSV hue shift as DrawableSurface::shiftHSV) and combines it
		// with the base layer, so no team-coloured or composite texture is ever
		// created while it is active. Created after the context above exists and
		// destroyed before it is torn down. Plain unsigned/int fields, not GL
		// types, to keep GL headers out of this public header (see Sprite::vbo).
		std::unique_ptr<RenderBatch> renderBatch;
        bool renderBatchEnabled=true;
		// Experimental live mesh renderer, owned by the GL context.
		struct SkinResources
		{
			unsigned program = 0, rigProgram = 0, fragment = 0, framebuffer = 0, depth = 0;
			struct RigBuffers
			{
				unsigned vertices = 0, indices = 0, vao = 0;
			};
			struct Uniforms
			{
				int region = -1, bones = -1, view = -1, normalView = -1, shell = -1;
				void initialize(unsigned program);
			} uniforms, rigUniforms;
			void prepareRigShader();
			const RigBuffers &restBuffers(const SkinMesh &mesh);
			static void bindRigGeometry(const RigBuffers &buffers);
			static void finishRigGeometry();
			void uploadRigPalette(const SkinMesh &mesh, unsigned sample);
			void bindCpuGeometry(const SkinMesh &mesh, unsigned sample);
			// Geometry cache is bounded separately from the pose atlas. Rebuilt
			// from immutable models after context restoration.
			std::map<std::uint64_t, RigBuffers> rigs;
			std::vector<float> cpuPose, uploadScratch;
			std::vector<unsigned> colors;
			using Key = SkinAtlasCache::Key;
			SkinAtlasCache slots;
			unsigned poses = 0, uv = 0, indices = 0, vao = 0;
			std::uint64_t meshIdentity = 0;
			unsigned frame = ~0u;
			// Uniform values survive program switches and atlas batches.
			std::uint64_t paletteIdentity = 0;
			unsigned paletteFrame = ~0u;
			bool attempted = false, rigAttempted = false;
			// Fur support is cached by material-map identity and revision.
			std::map<std::pair<std::uint64_t, std::uint64_t>, std::array<bool, 4>> shellRegions;
		} skinResources;
		void destroySkinRenderer();
        static void releaseSurfaceTextures();
        static void restoreSurfaceTextures();
        unsigned unitShaderProgram = 0;
		RadialFade radialFade;
		int unitShaderLocBase = -1, unitShaderLocTeam = -1;
		int unitShaderLocHasBase = -1, unitShaderLocHasTeam = -1;
		bool unitShaderFailureLogged = false;
		void createUnitShader();
		void destroyUnitShader();
		// The opening cloud bank's program and lattice texture, made on first use.
		unsigned cloudCoverProgram = 0, cloudCoverTexture = 0;
		int cloudCoverTextureW = 0, cloudCoverTextureH = 0;
		bool cloudCoverAttempted = false;
		void destroyCloudCover();
		// Central presentation boundary, also used by render-validation contexts.
		virtual void swapBuffers();
		static bool SDLCALL watchWindow(void *userdata, SDL_Event *event);
		std::unique_ptr<RenderBackend> portableRenderer;
        std::unique_ptr<RenderDevice> presentationDevice;
        std::shared_ptr<RenderResources> deviceResources;
        std::unique_ptr<RenderBackend> deviceRenderer;
        //! DrawableSurface::releaseDevicePixels calls this frame (reset in nextFrame).
        unsigned deviceReleasesThisFrame = 0;
        //! DrawableSurface::takeDeviceUpload calls this frame (reset in nextFrame).
        unsigned deviceUploadsThisFrame = 0;
        //! Level-zero bytes whose mip chains drawSurface built this frame (reset in nextFrame).
        std::size_t deviceMipBuildBytesThisFrame = 0;
        bool snapshotTrimPending = false;
        RendererBackend configuredBackend = RendererBackend::Software;
        std::shared_ptr<DeviceSkinState> deviceSkinState;
        struct DeviceTargetState { SDL_Rect clip; float scale; bool offscreen, map, ui; };
        std::vector<DeviceTargetState> deviceTargetStates;
        std::map<std::uint64_t, Recording::CaptureTicket> recordingCaptures;
		std::uint64_t captureFrameIdentity = 0;
		unsigned presentationHoldFrames = 0; // holdPresentation
		Uint64 heldPresentationStart = 0;
		std::uint64_t screenshotIdentity = UINT64_MAX / 2;
		std::map<std::uint64_t, std::string> screenshotCaptures;
		struct MemoryCapture
		{
			std::uint64_t frameIdentity = 0;
			std::function<void(std::shared_ptr<SDL_Surface>, std::uint64_t, std::string)> complete;
		};
		std::map<std::uint64_t, MemoryCapture> pendingMemoryCaptures, memoryCaptures;
		std::uint64_t memoryCaptureIdentity = UINT64_MAX / 4;
		void captureMemoryFrame();
		void pollCaptures();
		void cancelCaptures();
		RenderBackend *renderer =
			nullptr; // Borrowed active backend; ownership stays in the two unique_ptrs.
		// Rasterizes transformed passes into the existing software framebuffer.
		std::unique_ptr<RenderBackend> softwareRasterizer;
        bool softwareTransform=false;
        void beginSoftwareTransform();
        void endSoftwareTransform();
		std::string pendingScreenshot;
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
		
        // Fences active CPU drawing before borrowing its pixels. Base-class
        // aliases and already-borrowed pointers still require an owner fence.
        SDL_Surface *getSDLSurface();

		//! Bounded software pass; borrows pixels and restores facade/backend state on failure.
		void drawToSurface(SDL_Surface* surface, float scale, const std::function<void()>& draw);
		unsigned getGLContextGeneration() const { return glContextGeneration; }
        bool isLegacyGLScreen() const
        { return backendStatus.active == RendererBackend::LegacyGL && !offscreenPass && !nativeSoftware; }
        // Failed bounded admission invalidates composed pixels, not logical leases.
        std::uint64_t renderTargetContentRevision() const { return targetContentRevision; }

		// modifiers
		virtual bool setRes(int w, int h, Uint32 flags);
        // Resize a software render target without replacing its window or assets.
        bool resizeViewport(int w, int h);
        bool setResponsiveViewport(bool enabled, int minimumWidth=0, int minimumHeight=0);
        bool refreshPresentation();
        void setCompactWindowAllowed(bool allowed) {
            if (compactWindowAllowed==allowed) return;
            compactWindowAllowed=allowed;applyWindowMinimumSize();
        }
        bool isResponsiveViewport() const { return responsiveViewport; }
        bool hasPortableRenderer() const { return bool(renderer) && !nativeSoftware && !offscreenPass; }
        //! Inside drawToSurface: a one-shot draw (export, preview) that must
        //! finish everything it shows rather than spread work over frames.
        bool drawingOffscreen() const { return offscreenPass; }
        double logicalUnitsPerPoint() const;
        //! Logical pixels per authored font pixel for text a touch painter sizes in
        //! points: logicalUnitsPerPoint() times the player's text-size preference.
        double textUnitsPerPoint() const;
        void setUITransform(float scale=1, float x=0, float y=0, const SDL_Rect* bounds=nullptr);
        Uint32 windowID() const { return SDL_GetWindowID(window); }
#ifdef GLOB2_WEBGL2
        static void restoreBrowserContext();
#endif
		virtual void setRes(int w, int h) { setRes(w, h, optionFlags); }
		//! the resolution asked of setRes(), which the interface scale then divides
		int getRequestedW(void) const { return requestedW; }
		int getRequestedH(void) const { return requestedH; }
		//! interface scale to apply on the next setRes(); 0 follows the desktop
		static void setRequestedUiScale(float scale) { requestedUiScale = scale; }
		//! the interface scale the player chose; 0 follows the desktop
		static float getRequestedUiScale(void) { return requestedUiScale; }
		//! the interface scale in use
		float getUiScale(void) const { return uiScale; }
		//! the scale the last setRes() was asked for, before the window floor reduced it
		float getWantedUiScale(void) const { return wantedUiScale; }
		//! drawable pixels per logical pixel for text drawn on this context, 1 when unscaled
		float textRenderScale(void);
		//! the interface scale the desktop asks for, or 0 when nothing reports one
		static float querySystemUiScale(void);
		//! the scale actually used for a preference; 0 follows the desktop, GLOB2_UI_SCALE wins
		static float effectiveUiScale(float preferred);
		//! true when the window pixel size differs from the logical resolution, so output is scaled
		bool isScalingActive(void);
		int getW() override { return nativeDesktop && desktopLogicalW ? desktopLogicalW : DrawableSurface::getW(); }
		int getH() override { return nativeDesktop && desktopLogicalH ? desktopLogicalH : DrawableSurface::getH(); }
		bool isNativeDesktop() const { return nativeDesktop; }
		// UI-thread interface. Buffers are independently owned and may be encoded on a worker.
		using CaptureCompletion =
			std::function<void(std::shared_ptr<SDL_Surface>, std::uint64_t, std::string)>;
		std::uint64_t requestMemoryCapture(CaptureCompletion complete);
		void cancelMemoryCapture(std::uint64_t identity);
		std::uint64_t frameIdentity() const { return captureFrameIdentity; }
		SDL_Window *getWindow() const { return window; }
		void outputToWindow(float &x, float &y) const
		{
			if (drawableW > 0 && drawableH > 0)
			{
				x *= float(windowW) / drawableW;
				y *= float(windowH) / drawableH;
			}
		}
		int getDrawableW() const { return drawableW; }
		int getDrawableH() const { return drawableH; }
		bool setFullscreen(bool fullscreen);
		bool setUiScale(float scale);
		void setDisplayPreferenceCallback(std::function<void(int, int, bool)> callback)
		{ displayPreferenceCallback = std::move(callback); }
		bool toggleFullscreen();
		void beginMapTransform(float zoom,float x,float y,int clipX,int clipY,int clipW,int clipH);
		void endMapTransform();
        //! Axis-aligned world boundary, snapped to target pixels with a minimum one-pixel stroke.
        //! Unlike ordinary UI lines, this remains visible when the map is zoomed out.
        //! maxStrokePoints, when positive, stops the stroke thickening past that many screen points.
        void drawMapBoundary(int x1, int y1, int x2, int y2, const Color& color, float maxStrokePoints = 0);
        //! Axis-aligned world rectangle with edges snapped to target pixels, so
        //! translucent neighbours tile without seams or doubled edges at any zoom.
        void drawMapFill(int x1, int y1, int x2, int y2, const Color& color);
        //! A whole-tile fill that must meet neighbouring sprites exactly. Both
        //! software backends snap shared edges to target pixels; native software
        //! draws there directly to avoid DPI geometry rounding. Accelerated
        //! renderers retain exact fractions to match their neighbouring sprites.
        void drawMapTileFill(int x1, int y1, int x2, int y2, const Color& color);
        //! A sprite frame covering the `size`-pixel map square at (x, y), meeting
        //! its neighbours and drawMapTileFill exactly. Both software backends snap
        //! its edges in target pixels; elsewhere it is drawSprite.
        void drawMapTileSprite(int x, int y, int size, Sprite *sprite, unsigned index, Uint8 alpha = Color::ALPHA_OPAQUE);
        // Repeat a presentation-only pass. Its primary invocation advances visual
        // state once; subsequent invocations must only draw.
        void drawMapCopies(int periodW,int periodH,int viewW,int viewH,const std::function<void()> &draw, bool advance = true);
        bool isPeriodicCopy() const {return periodicCopy;}
		//! Sets nearestTiesUnbiased and returns its previous value.
		bool exchangeNearestTiesUnbiased(bool unbiased) { std::swap(unbiased, nearestTiesUnbiased); return unbiased; }
		void beginScreenOverlay(int &x,int &y,int &sx,int &sy,int &sw,int &sh);
		//! Scale of the active map transform; 1 outside a map pass.
		float mapTransformScale() const { return mapTransformActive ? mapScale : 1.f; }
		//! Where a map pixel lands in the screen overlay's logical units,
		//! including the periodic copy being drawn.
		void mapToScreen(int x, int y, float &screenX, float &screenY) const;
		void endScreenOverlay();
		unsigned long getDrawCallCount() const {return drawCalls;}
		void resetDrawCallCount(){drawCalls=0;}
		//! convert window pixel coordinates (as delivered by SDL) to logical coordinates
		void windowToLogical(float &x, float &y);
		//! declare that drawing now goes to an offscreen target with this many of its pixels per logical pixel; 0 restores the window
		void setRenderTargetScale(float pixelsPerLogicalPixel) {renderTargetScale = pixelsPerLogicalPixel;}
		//! target pixels per logical pixel the rasteriser is drawing at right now
		float getRasterScale(void) {return rasterScale();}
		//! translate SDL_GetMouseState coordinates through the active context's scaling
		static void translateMouseCoordinates(int &x, int &y);
        static void translateMouseCoordinates(float &x, float &y);
		//! rewrite a polled event's mouse coordinates from window pixels to logical coordinates
		static void translateMouseEvent(SDL_Event *event);
		//! Counts render device/target resets seen by translateMouseEvent, so caches of
		//! composed surfaces (editor swatches) can drop themselves without an event hook.
		static std::uint64_t renderResetGeneration();
		//! Pump events at a frame boundary; modal expose callbacks only present a cached frame.
		static int pollEvent(SDL_Event *event);
		virtual void setClipRect(int x, int y, int w, int h);
		virtual void setClipRect(void);
		enum class FrameMode { FullRedraw, PreserveContent };
        void beginFrame(FrameMode mode = FrameMode::PreserveContent);
        void discardFrame() noexcept;
        RenderOperations backendOperations() const;
        //! The portable SDL renderer's texture bytes, resident and peak, in constant time.
        std::pair<std::uint64_t, std::uint64_t> portableTextureMemory() const;
        SDL_Surface* completedFrame() const;
        virtual void nextFrame(void);
        //! Draws the next `frames` frames in full (uploads, streaming, device
        //! submission and completion) without presenting them: the window keeps
        //! showing the last presented frame meanwhile. Startup holds the main
        //! menu's first frames so every renderer settles before it shows.
        void holdPresentation(unsigned frames);
        unsigned heldPresentationFrames() const { return presentationHoldFrames; }
        //! Starts the per-frame device upload, release and mip-build allowances
        //! again. nextFrame does this; tools that present frames through the
        //! backend directly call it once a frame.
        void resetFrameAllowances();
		//! This function does not work for GraphicContext
		virtual bool loadImage(const std::string name) { return false; }
		//! This function does not work for GraphicContext
		virtual bool loadImage(const std::string &name) { return false; }
		//! This function does not work for GraphicContext
		virtual void shiftHSV(float hue, float sat, float lum) { }
		
		// reimplemented drawing commands for HW (GPU / GL) accelerated version
		virtual bool canDrawStretchedSprite(void) { return hasPortableRenderer() || (optionFlags & USEGPU) != 0; }
		
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

		void finishDrawingSprite(Sprite* sprite, Uint8 alpha);

		//! Draw a unit pose combining a base and team-colour layer with the
		//! unit shader, at logical position (x,y) and explicit destination size
		//! (independent of the sampled textures' own size, so an HD source
		//! layer still draws at the sprite's logical dimensions), alpha applied
		//! to the combined result. Returns false, drawing nothing, when the
		//! shader is unavailable so the caller can fall back to two ordinary
		//! drawSurface calls (base, then the CPU-recoloured team layer).
		bool drawTeamColoredQuad(DrawableSurface *base, DrawableSurface *team, float x, float y, float w, float h, Uint8 alpha, float hueShift);
		//! Draw all of a team layer stretched to (x, y, w, h), recoloured by
		//! hueShift degrees on the GPU exactly as Sprite::applyTeamHueShift
		//! recolours a copy. Device renderers whose shader set has the
		//! team-colour material only; false, drawing nothing, elsewhere, so the
		//! caller draws the CPU-recoloured copy.
		bool drawTeamColorSurface(float x, float y, float w, float h, DrawableSurface *team, float hueShift, Uint8 alpha = Color::ALPHA_OPAQUE);
		//! Whether drawTeamColorSurface can draw now.
		bool hasDeviceTeamColor() const;
		//! Draw all of `surface` stretched to (x, y, w, h), adding its light (its
		//! colour times alpha) to what is already drawn instead of blending over
		//! it, so it brightens rather than tints. Accelerated renderers only; the
		//! software renderer draws it normally. Earlier batched draws go first.
		void drawSurfaceAdditive(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha = Color::ALPHA_OPAQUE);
		//! Draw the opening cloud bank over the whole screen, computed per pixel
		//! from its lattice. Returns false, drawing nothing, where that is not
		//! supported (only desktop OpenGL draws it), so the caller can draw
		//! its own texture instead. Earlier batched draws go first.
		bool drawCloudCover(const CloudCoverFrame &frame);
		//! Make drawCloudCover's program now, outside a frame.
		void prepareCloudCover();
		//! Maximum supported texture dimension, or zero when no limit is reported.
		int maximumTextureSize() const;
		RenderBatch* getRenderBatch() const { return renderBatchEnabled?renderBatch.get():nullptr; }
        void countRenderBatchDraw() { ++drawCalls; }
        // Diagnostic comparison switches use the same context and assets.
        void setRenderBatchEnabled(bool enabled);
        bool hasUnitShader() const { return unitShaderProgram != 0; }
        //! Fade sprite, surface and skin draws with distance from a point until
        //! replaced; an inactive fade draws normally. Prefer RadialFadeScope
        //! (RenderStateScope.h). The native software surface, used without a
        //! renderer backend, draws unfaded.
        void setRadialFade(const RadialFade &fade) { radialFade = fade; }
        const RadialFade &getRadialFade() const { return radialFade; }
        // Render distinct visible poses before painting the map; composites retain painter order.
        void drawSkinSprite(float x,float y,float w,float h,DrawableSurface *surface,int sx,int sy,int sw,int sh,Uint8 alpha);
        // Production FBO tile, top-down straight RGBA; no shadows or window readback.
        bool readSkinMesh(const SkinMeshRequest &request, std::vector<std::uint8_t> &rgba);
        void prepareSkinMeshes(const std::vector<SkinMeshRequest> &requests);
        // Returns false without drawing when the backend or assets are unavailable.
        // texture is the team's colour atlas and material its material-id map;
        // region (SkinRegion) selects the model's quadrant of both. A non-zero
        // glowAlpha then adds the mesh's own image again, tinted by glow, to
        // brighten it in that colour.
        bool drawSkinMesh(const SkinMesh &mesh, unsigned frame, DrawableSurface &texture,
                          DrawableSurface &material, std::uint8_t region,
                          float x, float y, float w, float h, SkinShadow underlay = {}, Uint8 alpha = Color::ALPHA_OPAQUE,
                          Color glow = Color(), Uint8 glowAlpha = 0);
		
		virtual void drawAlphaMap(std::span<const float> map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		virtual void drawAlphaMap(std::span<const unsigned char> map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color);
		
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
		// Report-only identity. GL strings are available only on this context's
		// presentation thread while the context is current; never switches it.
		struct RenderingIdentity {
			std::string backend;
			std::string glVendor, glRenderer;
			bool glIdentityAvailable = false;
		};
		RenderingIdentity renderingIdentity() const;
	};
	
    class HighResolutionInputs;
	//! A sprite is a collection of images (frames) that can be displayed one after another to make an animation
	class Sprite
	{
        friend class SpriteLoad;
        void registerLoaded();
        static HighResolutionInputs prefetchHighResolution(const std::string&, size_t frames);
        void adoptLoaded(Sprite& prepared);
	protected:
		struct RotatedImage
		{
			DrawableSurface *orig;
			typedef std::map<Color32, DrawableSurface *> RotationMap;
			RotationMap rotationMap;
	
			RotatedImage(DrawableSurface *s) { orig = s; }
			~RotatedImage();
		};

		friend class GraphicContext;
	
		std::string fileName;
//#define DEBUG_SPRITE_NOT_DRAWN
#ifdef DEBUG_SPRITE_NOT_DRAWN
		static std::vector <Sprite*> sprites;
#endif
		std::vector <DrawableSurface *> images;
		std::vector <RotatedImage *> rotated;
		std::unique_ptr<Sprite> highResolutionAtlas;
		void createHighResolutionAtlas(HighResolutionInputs& inputs);
		bool highResolutionDemanded = false;
		std::vector <DrawableSurface *> experimentImages;
		std::vector <RotatedImage *> experimentRotated;

		// Sprite sheet stuff to efficiently draw terrain/water/units.
#ifdef HAVE_OPENGL
		std::vector <float> vertices;
		std::vector <float> texCoords;
		unsigned int vbo = 0; // Vertex buffer object
		unsigned int texCoordBuffer = 0;
        unsigned bufferGeneration = 0;
#endif
        std::shared_ptr<const SpriteAtlasMemory> atlasMemory;
        std::unique_ptr<const DrawableSurface> atlas = nullptr;
        std::vector<SpriteAtlasStamp> atlasStamps;
        std::vector<SDL_Rect> atlasRegions;
        bool atlasVariableSizes = false;
        //! Frame (GraphicContext::frameIdentity) before which a sheet whose
        //! device atlas could not be made does not try again: every draw tried.
        std::uint64_t atlasRetryFrame = 0;
		static void checkAllSpritesDrawn();
		Color actColor;

		// Team-colour recoloring for sprites marked dynamicTeamColor (currently
		// only the unit sprite, see load()). GPU rendering recolors in a shader
		// (GraphicContext::drawTeamColoredQuad) and never creates a CPU/GPU
		// recolor surface at all; this bounded cache backs the software renderer
		// and GL-without-a-working-shader fallback, shared by world units and
		// unit UI previews (portraits, editor previews, indicators, credits) since
		// they all route through the same DrawableSurface::drawSprite.
		bool dynamicTeamColor = false;
		//! Every dynamicTeamColor sprite's HD layer renders onto this fixed
		//! pixel canvas regardless of its own native/logical size (unlike other
		//! sprite categories' HD layers, which are always exactly 4x native).
		//! Matches UNIT_HD_PIXEL_SIZE in tools/unit-animation/render.py.
		static constexpr int highResolutionTextureSize = 128;
		struct TeamColorKey
		{
			int index; bool experiment; Uint8 r, g, b;
			bool operator==(const TeamColorKey &o) const
			{ return index == o.index && experiment == o.experiment && r == o.r && g == o.g && b == o.b; }
		};
		struct TeamColorKeyHash
		{
			size_t operator()(const TeamColorKey &k) const
			{
				size_t h = static_cast<size_t>(k.index) * 2 + (k.experiment ? 1 : 0);
				h = h * 257 + k.r; h = h * 257 + k.g; h = h * 257 + k.b;
				return h;
			}
		};
		struct TeamColorNode { TeamColorKey key; std::unique_ptr<DrawableSurface> surface; size_t bytes; };
		// Front = most recently used. Byte-accounted LRU: CPU pixels plus any GPU
		// texture allocation, evicted oldest-first before a new entry is admitted.
		std::list<TeamColorNode> teamColorList;
		std::unordered_map<TeamColorKey, std::list<TeamColorNode>::iterator, TeamColorKeyHash> teamColorIndex;
		size_t teamColorBytes = 0;
		static const size_t teamColorCacheCap = 64u * 1024u * 1024u;
		DrawableSurface *lookupTeamColor(const TeamColorKey &key);
		void insertTeamColor(const TeamColorKey &key, std::unique_ptr<DrawableSurface> surface, size_t bytes);
		void clearTeamColorCache();
		// Silhouettes reuse the key: `experiment` holds silhouette()'s teamLayerOnly.
		std::list<TeamColorNode> silhouetteList;
		std::unordered_map<TeamColorKey, std::list<TeamColorNode>::iterator, TeamColorKeyHash> silhouetteIndex;
		size_t silhouetteBytes = 0;
		static const size_t silhouetteCacheCap = 8u * 1024u * 1024u;

		friend class DrawableSurface;
		// Support functions

		//! Check if index is within bound and return true, assert false and return false otherwise
		bool checkBound(int index);
		//! Return a rotated drawable surface for actColor, create it if necessary
    public:
        // The uncolored layer may be retained beneath replacement geometry
        // (unit sheets store their ground shadows here). The caller sizes it
        // to the native logical canvas, even when an HD surface is returned.
        // Asking for a base frame demands the sprite's HD layers (see
        // demandHighResolution), so the first answers can be native frames.
        DrawableSurface *baseFrame(unsigned index)
        {
            if (!checkBound(index)) return nullptr;
            return sampleBaseFrame(index);
        }
        //! The HD base layer (or the native frame) to draw, never read on the
        //! CPU: unlike baseFrame, it leaves released pixels released.
        DrawableSurface *drawFrame(unsigned index)
        {
            if (!checkBound(index)) return nullptr;
            if (!highResolutionDemanded) demandHighResolution();
            return prepareDrawSurface(index, false, true);
        }
        //! After an on-demand renderer's draw: frees frame `index`'s
        //! HD base pixels, which only its texture (portable) or the device's
        //! retained snapshot then holds, unless the CPU may sample them.
        void releaseDrawnHighResolution(unsigned index);
        //! After a portable draw of frame `index`'s recoloured HD team layer
        //! (in the current colour): frees the cached copy's pixels, which its
        //! texture then holds alone; a lost texture recolours it again.
        void releaseDrawnTeamColor(unsigned index);
        //! Legacy OpenGL loads every sprite's HD layers with it; other backends
        //! load them on demand: what their draws reach while the HD sprite
        //! policy shows HD (highResolutionDrawable) and what baseFrame samples
        //! (terrain composition, skin shadows, editor swatches). Returns true
        //! when this queued a reload.
        bool demandHighResolution();
        //! On those backends, drops the HD layers a demand loaded (or cancels
        //! their reload) until the next baseFrame demands them again. Deferred
        //! banks retire frames through their windows instead; elsewhere this
        //! does nothing.
        void releaseHighResolutionDemand();
        //! The HD sprite policy's level for the current view (a
        //! HighResolutionPolicy::Level): FullHD, HalfHD or Native; Native
        //! whenever the HD setting is off. Every renderer draws by it.
        static unsigned highResolutionLevel();
        //! Whether the policy draws sprite frames from their HD layers now.
        static bool highResolutionShown();
        //! Grows whenever highResolutionShown() may have changed: caches that
        //! recorded which layer a frame drew add it to their artwork revision.
        static std::uint64_t highResolutionPolicyRevision();
        //! Whether an on-demand renderer (every one but legacy OpenGL) draws
        //! sprite frames from their HD layers now: the policy shows HD and the
        //! renderer can load them.
        static bool drawsHighResolutionOnDemand();
        //! Puts the policy at the level of `density` at once, with no pending
        //! change (tests, and views that must not wait out the hysteresis).
        static void resetHighResolutionPolicy(double density);
        //! On an on-demand renderer showing HD: demands this sprite's HD layers
        //! (team layers included) and answers whether frame `index` can draw from
        //! them now. False, so the native frame draws, until every layer the
        //! native frame has is HD, for a unit whose action is not complete in
        //! HD, for a first draw beyond this frame's upload budget, and on a
        //! device whose budget for HD frames is full.
        bool highResolutionDrawable(unsigned index);
    private:
        DrawableSurface *sampleBaseFrame(unsigned index);
        bool sampledByBaseFrame = false; // Its HD base pixels stay for CPU sampling.
    public:

        //! The frame's shape filled with one colour, for effects drawn over the
        //! sprite at its logical size: opaque wherever either layer is, or only
        //! where its team-colour layer is. Built from the native layers on first
        //! use and kept in a small LRU cache; null when there is no such layer.
        DrawableSurface *silhouette(unsigned index, const Color &color, bool teamLayerOnly = false);

        // Immutable native image access for terrain cache preparation. Team layers
        // need separate composition and therefore are not cacheable here.
        DrawableSurface* nativeFrame(unsigned index) const
        {
            return index < images.size() && index < rotated.size() && !rotated[index] ? images[index] : nullptr;
        }

        //! A frame's optional shadow layer (`<frame>s`, sheet layer `shadow`):
        //! black, its alpha the ground shadow's coverage at full strength. Its
        //! top-left corner is the frame's; it may be larger, to reach past the
        //! frame to the lower right. It is never drawn on its own: the world
        //! view composes every shadow into one mask (src/render/ShadowMask.h).
        //! The native layer, with CPU pixels; null when the frame has none.
        DrawableSurface *shadowFrame(unsigned index) const
        {
            return index < shadowImages.size() ? shadowImages[index].get() : nullptr;
        }
        //! The HD shadow layer (4x the native one, from the HD index's
        //! `<frame>s` row), with CPU pixels. Null while it loads (the first ask
        //! queues it), when the frame has none or HD artwork is off, and for a
        //! layer not yet asked for while HD admission is held. Never block
        //! compressed (BC3, BC7 or ASTC): the mask reads its alpha on the CPU,
        //! so it stays the lossless RGBA it was packed as.
        DrawableSurface *highResolutionShadowFrame(unsigned index);
        //! Grows when an HD shadow layer becomes available: composed shadows
        //! that used a native layer instead recompose.
        static std::uint64_t shadowLayerRevision();
        const std::string &getFileName() const { return fileName; }

    protected:
        std::vector<std::unique_ptr<DrawableSurface>> shadowImages;
        std::vector<std::unique_ptr<DrawableSurface>> highResolutionShadows;
        std::vector<std::uint8_t> highResolutionShadowState; // 0 unasked, 1 loading, 2 done

    protected:
		virtual DrawableSurface *getRotatedSurface(int index);
		//! One bit per 32-phase block, recomputed whenever the HD layer arrays
		//! change (load and atomic HD publication); backs blockHasCompleteHD.
		std::vector<bool> blockCompleteHD;
		//! The HD frame clock (one tick per setHighResolutionDensity) when each
		//! 32-frame block last drew HD on an on-demand renderer: the oldest idle
		//! blocks go first when a device runs out of room for HD frames.
		std::vector<std::uint64_t> highResolutionBlockDrawn;
		//! Drops one block's HD layers (they draw native until drawing reaches
		//! the block again, which reloads it); returns whether it held any.
		bool evictHighResolutionBlock(unsigned block);
		//! Evicts idle blocks, least recently drawn first, until the device
		//! has room for new HD frames again (setHighResolutionDensity).
		static void evictIdleHighResolution();
		void recomputeBlockCompleteHD();
		//! Revisions of the HD layers that became drawable: one counter for the
		//! sprite and the value it had when each frame last gained or replaced
		//! a layer. Only additions count: deleting a standalone layer frees its
		//! texture, which invalidates the geometry that recorded it, and an
		//! atlas page lives until the atlas is replaced, which does the same.
		std::uint64_t highResolutionLayerRevision = 0;
		std::vector<std::uint64_t> highResolutionFrameRevisions;
		void noteHighResolutionFrame(size_t frame);
		void noteHighResolutionReplaced();
		void applyTeamHueShift(DrawableSurface &surface);
		DrawableSurface *getColoredSurface(int index, bool experiment);
		DrawableSurface *prepareDrawSurface(unsigned index, bool teamColor, bool experiment);
        void appendHighResolutionFrame(size_t index, Sprite& target,
            HighResolutionInputs& inputs);
        void releaseHighResolution();
        void queueHighResolutionReload(bool retainPublishedOnFailure);
        // Loads the HD layers of these 32-frame blocks only, keeping the rest.
        void queueHighResolutionBlocks(const std::set<unsigned> &blocks);

	public:
		//! Opt into batching variable-size frames; callers must finishDrawingSprite.
		bool createTextureAtlas(bool allowVariableSizes = false);
        bool resolveDeviceAtlas(DrawableSurface &, SDL_Rect &, const DrawableSurface *&);
        bool createDeviceTextureAtlas(int maximumTextureSize, bool variableSizes);
		struct HighResolutionStats {size_t cpuBytes=0, coloredFrames=0;};
		static HighResolutionStats highResolutionStats();
		//! CPU team recolours of every loaded sprite, native and HD: rotation
		//! maps and the recolour cache, and the bytes of those still holding
		//! pixels. Zero where a device shader recolours.
		struct TeamColorStats {size_t cpuBytes=0, frames=0;};
		static TeamColorStats teamColorStats();
		//! Resident pixel and texture bytes of each loaded sprite, for RenderMemory::sprites().
		static void memoryUsage(std::vector<RenderMemory::SpriteUsage> &usage);
		static void setHighResolution(bool enabled);
        static void requestHighResolution(bool enabled);
        static bool highResolutionRequested();
        // An empty name refreshes sprites from the shared HD package.
        static void refreshHighResolutionSprite(const std::string &name);
        // Large terrain banks retain native frames and stream only the caller's HD window.
        // Leave a sprite's HD frames standalone where nothing draws them in
        // bulk, so no atlas is decoded, kept in memory or uploaded for it.
        static void skipHighResolutionAtlas(const std::string& name);
        static void deferHighResolution(const std::string& name);
        // The caller reads this sprite's HD frames as pixels (terrain materials
        // feed CPU sampling), so they are never released after upload.
        static void keepHighResolutionPixels(const std::string& name);
        // A sprite loaded later, when HD is already enabled, does not decode
        // its HD layers with its native frames; its deferred windows or a
        // demand load them (lazily loaded terrain material sheets).
        static void loadHighResolutionOnDemand(const std::string& name);
        static bool highResolutionDeferred(const std::string& name);
        void setHighResolutionFrames(std::span<const unsigned> indices);
        //! Deferred frames that left their window stay decoded, most recently
        //! retired first, so a view that pans or zooms back reuses them without
        //! reading the file. The oldest go when they exceed this shared byte
        //! budget or have been unused for the lifetime.
        struct RetiredHighResolutionBudget { size_t bytes; std::int64_t lifetimeNanoseconds; };
        static RetiredHighResolutionBudget retiredHighResolutionBudget();
        static void setRetiredHighResolutionBudget(RetiredHighResolutionBudget budget);
        static size_t retiredHighResolutionBytes();
        static void refreshHighResolutionForRenderer();
        //! Feeds the HD sprite policy (HighResolutionPolicy) the view's screen
        //! pixels a logical pixel at the current zoom (or the display's scale,
        //! for interface drawing, if larger). More detail is taken at once;
        //! less only after the view has wanted it for ten seconds. A change
        //! reloads what needs another level in the background (legacy GL's
        //! bands, the reduced frames of renderers without mip chains), and the
        //! current frames draw until the new ones are published; back at
        //! Native, on-demand renderers release what drawing loaded. Call once
        //! a frame.
        static void setHighResolutionDensity(double screenPixelsPerLogicalPixel);
        //! On-demand HD draws since the process started (highResolutionDrawable):
        //! drawn in HD, or native because the frame's block had not loaded, the
        //! frame's first draws this frame were spent, or the device had no room
        //! left for retained HD frames (GraphicContext::deviceHighResolutionRoom),
        //! and the blocks evicted to make that room (evictIdleHighResolution);
        //! `held` counts frames left native by holdHighResolutionAdmission.
        struct HighResolutionDrawCounts { std::size_t drawn = 0, notLoaded = 0, firstDrawCap = 0, room = 0, evicted = 0, held = 0; };
        static HighResolutionDrawCounts highResolutionDrawCounts();
        //! While held, on-demand renderers keep drawing the HD frames they
        //! already uploaded and draw every other frame native: nothing more
        //! loads, uploads or is evicted. The session's response to hitches
        //! while HD frames were short of device room, before HD goes off.
        static void holdHighResolutionAdmission(bool held);
        static bool highResolutionAdmissionHolding();
        //! Levels the current band leaves out: 0 for full detail, up to 2.
        static unsigned highResolutionDetailSkip();
        //! How long the view must want less detail before the band drops (tests shorten it).
        static void setHighResolutionDetailDelay(std::int64_t nanoseconds);
        //! The same delay, for other views that lower their detail on its clock
        //! (the portable renderer's terrain pages).
        static std::int64_t highResolutionDetailDelay();
        //! Called when legacy OpenGL textures are freed: HD frames whose pixels
        //! were released have nothing left, so their sprites drop back to
        //! native artwork and reload HD in the background at the next poll.
        static void discardUnbackedHighResolution();
        //! Frees the retired frames of streamed banks (see retiredHighResolutionBudget):
        //! at once, or 64 a poll until none are left.
        static void trimRetiredHighResolution(bool immediately);
        static bool pollHighResolution(unsigned budgetMs = 2);
        static bool pollHighResolutionUntil(std::int64_t deadlineNanoseconds); // GAGCore::steadyClockNanoseconds() clock
        // Blocks until every requested HD frame, deferred windows included, is
        // published; with a sprite, only until that sprite's reload is.
        static void finishHighResolution(const Sprite *only = nullptr);
		static void flushBatches(GraphicContext *gc);
    private:
        //! Forget this sprite's retired frames; destroy also deletes them.
        void releaseRetiredHighResolution(bool destroy);
        static void evictRetiredHighResolution();
        static void advanceBandReload();
        void releaseCoveredNativeTextures();
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

		bool isDynamicTeamColor() const { return dynamicTeamColor; }
		size_t getTeamColorCacheBytes() const { return teamColorBytes; }
		size_t getTeamColorCacheEntries() const { return teamColorList.size(); }
		//! True when every frame in index's 32-phase action/direction block has a
		//! matching HD layer wherever the native one exists. An incomplete block
		//! falls back to native throughout the action rather than switching
		//! resolutions from pose to pose. DrawSprite calls this per pose, so it is cached
		//! (recomputeBlockCompleteHD) rather than rescanned every time.
		bool blockHasCompleteHD(int index) const;
		//! Draws choose a frame's HD or native layer when they emit it, so for
		//! sheets drawn frame by frame (not units, whose HD choice covers a whole
		//! action block) retained geometry is current while no frame it drew has
		//! gained an HD layer since. The sprite revision moves whenever any
		//! frame's does; the frame revision says which (0 if it never has).
		std::uint64_t highResolutionRevision() const { return highResolutionLayerRevision; }
		std::uint64_t highResolutionRevision(unsigned frame) const
		{
			return frame < highResolutionFrameRevisions.size() ? highResolutionFrameRevisions[frame] : 0;
		}
		//! Hue shift, in degrees, from this sprite's base team colour to actColor;
		//! shared by the CPU recolor path and the uHueShift uniform of the shader.
		//! Not const: Color::getHSV isn't const-qualified.
		float teamHueShiftDegrees();
		//! Return the width of index frame of the sprite
		virtual int getW(int index);
		//! Return the height of index frame of the sprite
		virtual int getH(int index);
		//! Return the number of frame in this sprite
		virtual int getFrameCount(void);
	};
}
