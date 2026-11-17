// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMaterials.h"
#include "TerrainBuiltins.h"
#include "TerrainStones.h"
#include "TerrainType.h"
#include "map/MapAssetBundle.h"
#include "render/MapAssetSprites.h"
#include <SDLGraphicContext.h>
#include <DetailHysteresis.h>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <tuple>
#include <unordered_map>

class SceneMap;
namespace TerrainVisual
{
class CompiledPack;

// Generated view pages use the same mip preparation as authored HD artwork.
class Surface : public GAGCore::DrawableSurface
{
  public:
	Surface(SDL_Surface *pixels, bool hd) : DrawableSurface(pixels, AdoptPixels{}, true)
	{
		highResolutionSampling = hd;
		// Pages, overview pages and animated tiles: the page caches size
		// themselves for the device's terrain page budget, not the general one.
		deviceBudgetClass = GAGCore::TextureDescriptor::BudgetClass::TerrainPage;
	}
	// Retire the renderer allocation while keeping expensive composed CPU pixels.
	SDL_Surface *takePixels()
	{
		auto *pixels = sdlsurface;
		sdlsurface = nullptr;
		return pixels;
	}
};

// The native sheets of the installed terrain materials (one sprite each). An
// on-demand compositor loads one when a map, the editor or composition first
// needs it, on the asset workers through Toolkit::requestSprite, and draws the
// material's flat preview colour until it is published. Sheets that no map
// retains go after the retired HD frame lifetime, sooner when they and the
// retired HD frames exceed that shared byte budget. Presentation thread only.
class MaterialSheets
{
  public:
	// The loaded sheet; null while it loads (the first ask queues it) or
	// after it failed.
	static GAGCore::Sprite *find(const std::string &name);
	// The loaded sheet, without queuing a load.
	static GAGCore::Sprite *loaded(const std::string &name);
	static bool failed(const std::string &name);
	// Blocks until each sheet is loaded or failed: complete draws only
	// (captures, exports, editor swatches), never an interactive frame.
	static void finish(std::span<const std::string> names);
	// Moves whenever a sheet is published or released. A holder of sheet
	// pointers resolves them again before using them once it moved.
	static std::uint64_t generation();
	// Publishes finished loads and releases sheets as described above. Cheap;
	// compositors call it once a frame.
	static void poll();
	// Marks a sheet used now: an unretained sheet in use is not released.
	static void touch(const std::string &name);
	// The sheets an owner's map holds, which stay loaded; replaces the last set.
	static void retain(const void *owner, std::vector<std::string> names);
	static void forget(const void *owner);
	// Pixel bytes of the sheets loaded on demand, and of those no owner retains.
	static std::size_t residentBytes();
	// Sheets queued and not yet published.
	static std::size_t loading();
	static std::size_t unretainedBytes();
};

// Renderer-owned, never accessed from the simulation. Sources are immutable
// copies refreshed on sprite publication/content changes, including HD toggles.
class Compositor
{
  public:
	// onDemand loads installed material sheets through MaterialSheets as they
	// are needed (the game). Otherwise every sheet is loaded and validated
	// here, as tools and tests expect.
	explicit Compositor(Catalog catalog, std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty(),
						bool onDemand = false);
	~Compositor();
	Compositor(const Compositor &) = delete;
	Compositor &operator=(const Compositor &) = delete;
	const Catalog &catalog() const { return definitions; }
	// Contextual and smooth profiles read one vertex of halo around each cell.
	bool neighborhoodBorders() const { return hasNeighborhoodProfiles; }
	// The sheet and frame of a terrain type's icon; a null sheet while it loads.
	std::pair<GAGCore::Sprite *, unsigned> editorIcon(TerrainType type);
	// Draws that icon at its 32x32 logical size, or the material's preview
	// colour while its sheet loads.
	void drawEditorIcon(GAGCore::GraphicContext &target, int x, int y, TerrainType type);
	// Whether every material this map holds has its sheet (or failed to load
	// one). Advances a bounded scan of the map's terrain, so a few frames
	// pass before a new map answers true; until then composition draws flat
	// preview colours for missing sheets. Retains those sheets.
	bool materialsReady(const SceneMap &);
	// Blocks until the map's (or these) material sheets are loaded: for
	// complete draws (captures, exports, swatches), never interactive frames.
	void finishMaterials(const SceneMap &);
	void finishMaterials(std::span<const MaterialId>);
	bool materialLoaded(MaterialId id) const { return sheet(id) != nullptr; }
	// Whether every material a recipe composes from has its sheet (or can
	// never have one); asks for the missing ones first. Interactive pages
	// leave a cell uncomposed until it answers true, so the fallback chain
	// beneath draws, never a material's flat preview colour.
	bool sheetsReady(const Recipe &) const;
	// Counters for holders of composed pixels: sheets published, and
	// preparations that drew a flat preview colour for a missing sheet.
	std::uint64_t materialArrivals() const { return sheetArrivals; }
	std::uint64_t materialFallbacks() const { return sheetFallbacks; }
	void prepare(bool hd, int time);
    // Retain only requested variants; immutable CPU snapshots remain usable.
    void prepareForRecipes(bool hd, int time, std::span<const Recipe> recipes);
    // Inclusive world-cell bounds, wrapped to the map. HD streams asynchronously
    // with native sources as fallback until each requested frame is available.
    // Without decor, only material frames stream: a renderer that draws decor
    // sprites from their native frames alone (the portable SDL renderer) has
    // no use for HD decor. `extraPages` lists more 16-cell pages (world tile
    // origins) whose materials composition needs this frame, such as the
    // portable renderer's resident base pages; their decor is not collected.
    void prepareVisible(bool hd, int time, const SceneMap &, int left, int top, int right, int bottom,
                        bool nativeComposition = false, bool decor = true,
                        std::span<const SDL_Point> extraPages = {});
    // Whether the portable renderer's pages compose HD material frames for a
    // view at `density` screen pixels per logical pixel (map scale times raster
    // scale). Native frames are the HD frames reduced by four, so at one or
    // fewer an HD page holds texels nothing shows. More detail is taken at
    // once; native frames only once the view has wanted them for
    // Sprite::highResolutionDetailDelay(), so zooming across the threshold and
    // back recomposes nothing. Call once a frame.
    bool highResolutionPages(double density);
    // The same view for a caller that filters its own tiles through
    // phasePixels(), as direct terrain does: only the HD stream window is
    // declared and no CPU textures are kept here. Without frames the view
    // draws native material and decor art and streams nothing: the native
    // frames are the HD frames reduced, so HD adds nothing at 32 pixels a cell
    // or fewer.
    void declareVisible(int time, const SceneMap &, int left, int top, int right, int bottom, bool frames);
	Recipe describe(const SceneMap &, int x, int y) const;
	Recipe describeOverview(const SceneMap &, int x, int y) const;
	// The colour the overview draws a vertex of this terrain with: a saved
	// custom terrain's own overview colour, or its material's preview.
	std::array<unsigned char, 3> overviewColor(const SceneMap &, TerrainType) const;
	// Coverage of one mixed cell at one sampling scale. It depends on the cell's
	// geometry, never on the animation phase, so a page can keep it and re-blend
	// only the textures when an animated material changes phase.
	struct CellMask
	{
		struct Sample
		{
			// Weights of the palette slots other than the largest, in slot order;
			// the largest takes the rest of 65536, so each fits sixteen bits.
			std::uint16_t others[3];
			std::uint16_t margin;
			std::uint8_t slots; // Largest, dominant and neighbor palette slots, two bits each.
		};
		Recipe recipe;
		int scale = 0;
		std::array<MaterialId, 4> palette{}; // Distinct corner materials.
		unsigned colors = 0;
		std::vector<Sample> samples;
		std::size_t bytes() const { return sizeof(CellMask) + samples.capacity() * sizeof(Sample); }
	};
	CellMask mask(const Recipe &, int scale) const;
	void mask(const Recipe &, int scale, CellMask &out) const; // Reuses out's storage.
	// What changed since a mixed cell was last composed into its target with the
	// same recipe and mask: the palette slots whose textures have new revisions,
	// and those whose surf advanced a step. Only samples those can affect are
	// recomposed; the rest keep the identical pixels already in the target.
	struct Refresh
	{
		unsigned textures = 0, surf = 0; // Palette slot bits.
		// Optional animatedSamples of the mask, valid when every changed slot
		// is animated: only those samples are visited.
		const std::vector<std::uint32_t> *samples = nullptr;
	};
	// The samples of a mixed cell that an animated material's textures or surf
	// can change, in sample order, with a flag for those an opaque animated
	// texel alone determines. Depends only on the mask, so a page keeps it.
	std::vector<std::uint32_t> animatedSamples(const CellMask &) const;
	// Palette slot bits of a mask's animated materials.
	unsigned animatedSlots(const CellMask &) const;
	// A uniform recipe ignores the mask; a mixed one composes from it when given.
	// A refresh requires the mask and is ignored by uniform recipes.
	// reduction > 1 (a mixed cell only, without a refresh) composes the cell
	// 32 * scale / reduction pixels square, as the direct pass draws it at
	// that density: each pixel blends its block of samples, toned by the
	// seam and surf of the block's centre sample.
	void compose(const Recipe &, SDL_Surface *target, int x, int y, int scale,
				 const CellMask *mask = nullptr, const Refresh *refresh = nullptr, int reduction = 1) const;
	// Animated textures or surf: either changes this material's composed pixels over time.
	bool animated(MaterialId id) const
	{
		const auto &m = definitions.materials[id];
		return m.animationFrames > 1 || m.surf.period > 0;
	}
	unsigned surfTime() const { return preparedTime; }
	// Per corner (Recipe::corners order), an overview colour replacing the
	// material's preview, or null; saved custom terrain carries its own.
	using CornerColors = std::array<const std::array<unsigned char, 3> *, 4>;
	// Writes `samples` x `samples` palette samples for one cell at (x, y). Subtile
	// samples share the detailed renderer's material partition.
	void composeOverview(const Recipe &, SDL_Surface *target, int x, int y, int samples,
						 const CornerColors *cornerColors = nullptr) const;
	// The same from a catalog snapshot (CpuMaterials::definitions), for workers.
	static void composeOverview(const Catalog &, const Recipe &, SDL_Surface *target, int x, int y,
								int samples, const CornerColors *cornerColors = nullptr);
	// The shared sprite supports cached row batching; mixed custom sprites use
	// the ordinary renderer. Material lookup also serves editor swatches.
	GAGCore::Sprite *decorSprite() const { return sharedDecorSprite; }
    GAGCore::Sprite *decorSprite(MaterialId id) const { return decorSprites.at(id); }
	// How far the largest decor frame reaches past its cell, for view culling.
	DecorReach decorReach() const { return reach; }
	// Terraces of tiered masses. TierField holds, per map vertex, how many
	// vertices deep it lies inside its mass (capped); it is presentation only,
	// built once per terrain revision and shared, so per-cell queries are a few
	// array reads with no locking. tierField returns null when no material is
	// tiered; callers fetch it once per draw and pass it to decorLayers.
	struct TierField
	{
		std::uint64_t revision = 0;
		const void *registry = nullptr;
		int w = 0, h = 0;
		std::uint32_t seed = 0;
		std::vector<std::uint8_t> depth;
	};
	std::shared_ptr<const TierField> tierField(const SceneMap &) const;
	// A cell's raised decor, one frame per slot, drawn in slot order: the
	// connected masses that stand on open ground (cliffs, icefall, glacier...),
	// connected foliage (hedge and thicket, which join one another), every other
	// obstacle, then the terraces of tiered masses. Masses take slots 0 and 1,
	// and a third and fourth the slots after them; foliage beside three masses
	// moves to slot 3, where no obstacle draws. Each slot draws only from
	// its own corners, so masses keep their walls against foliage and crags.
	// Where an open-edged mass meets another connected material it stands back
	// from it (connected_foreign), so the two show their own rims and walls
	// across a valley half a cell wide; the mass with more north corners draws
	// first. An undecorated corner suppresses the foliage and obstacle slots;
	// within those, mixed materials choose one by presentation hashing,
	// weighted by corner count. Terrace slot k draws decorSlotRise higher.
	static constexpr unsigned TierSlot = 4;
	static constexpr unsigned DecorSlots = TierSlot + MaxDecorTiers;
	static constexpr int decorSlotRise(unsigned slot)
	{
		return slot >= TierSlot ? int(slot - TierSlot + 1) * DecorTierRise : 0;
	}
	struct DecorLayer
	{
		GAGCore::Sprite *sprite = nullptr;
		int frame = -1;
	};
	std::array<DecorLayer, DecorSlots> decorLayers(const SceneMap &, int x, int y,
		const TierField *tiers = nullptr) const;
	// The frame of terrace `tier` (1..MaxDecorTiers) over a cell, or -1.
	int decorTierFrame(const SceneMap &, const TierField &, int x, int y, int tier) const;
	// Flat animated overlays (fertile glints), drawn under decor. A cell draws
	// one only when all four corners carry an overlay. `time` is the presentation
	// clock that animates lava and water (MapRenderState::animationTime), not the
	// simulation tick, so glints twinkle in the editor and pause with the game.
	GAGCore::Sprite *overlaySprite() const { return sharedOverlaySprite; }
	GAGCore::Sprite *overlaySprite(MaterialId id) const { return overlaySprites.at(id); }
	GAGCore::Sprite *overlaySprite(const SceneMap &map, int x, int y) const;
	int overlayFrame(const SceneMap &, int x, int y, int time, bool *animated = nullptr) const;
    struct PreparedMaterialView
    {
        unsigned variant = 0;
        std::uint64_t revision = 0;
        int size = 0;
        std::span<const std::array<unsigned char, 4>> pixels;
        int step = 0;
    };
    // Valid until prepare() changes this material. Uploaders copy the canonical
    // animation/HD pixels into owned revisions before retaining or queueing them.
    PreparedMaterialView preparedMaterial(const Recipe &, MaterialId) const;
    // Published material revisions own only composition data. A later prepare
    // can replace animated pixels without invalidating a running CPU batch.
    using MaterialPixels = std::vector<std::array<unsigned char, 4>>;
    // Prepared pixels shared, copy on write, with the CPU snapshots published
    // from them, so a snapshot holds no second copy of an unchanged texture.
    class SharedPixels
    {
        std::shared_ptr<MaterialPixels> pixels;
        static const MaterialPixels &none()
        {
            static const MaterialPixels empty;
            return empty;
        }
      public:
        SharedPixels() = default;
        SharedPixels &operator=(MaterialPixels &&replacement)
        {
            if (replacement.empty()) pixels.reset();
            else pixels = std::make_shared<MaterialPixels>(std::move(replacement));
            return *this;
        }
        const MaterialPixels &get() const { return pixels ? *pixels : none(); }
        bool empty() const { return get().empty(); }
        std::size_t size() const { return get().size(); }
        std::size_t capacity() const { return get().capacity(); }
        const MaterialPixels::value_type &operator[](std::size_t i) const { return get()[i]; }
        const MaterialPixels::value_type *data() const { return get().data(); }
        const MaterialPixels::value_type &front() const { return get().front(); }
        auto begin() const { return get().begin(); }
        auto end() const { return get().end(); }
        // Writable pixels, first copied if a published snapshot shares them.
        MaterialPixels &mutate()
        {
            if (!pixels) pixels = std::make_shared<MaterialPixels>();
            else if (pixels.use_count() > 1) pixels = std::make_shared<MaterialPixels>(*pixels);
            return *pixels;
        }
        // `count` writable pixels whose old values do not matter: fresh storage
        // if a snapshot shares the current ones.
        MaterialPixels &overwrite(std::size_t count)
        {
            if (!pixels || pixels.use_count() > 1) pixels = std::make_shared<MaterialPixels>(count);
            else pixels->resize(count);
            return *pixels;
        }
        std::shared_ptr<const MaterialPixels> share() const { return pixels; }
        // The pixels by value, moved when nothing else shares them.
        MaterialPixels take()
        {
            if (!pixels) return {};
            MaterialPixels result = pixels.use_count() > 1 ? *pixels : std::move(*pixels);
            pixels.reset();
            return result;
        }
    };
    // A single immutable animation phase, prepared with the reference variant edges.
    MaterialPixels nativeMaterialPixels(MaterialId id, unsigned variant, unsigned phase);
    // Individual source endpoints for GPU crossfades. Stamps include deferred HD
    // publication and edits; phasePixels applies the same shared variant edges
    // as the CPU compositor without blending the two animation endpoints.
    struct PhaseSource
    {
        std::uint64_t identity, revision;
        int size;
        std::uint64_t edgeIdentity, edgeRevision;
        bool operator==(const PhaseSource &) const = default;
    };
    // native: sample the native frame even while an HD frame is resident, as a
    // view at 32 pixels a cell or fewer does.
    PhaseSource phaseSource(MaterialId id, unsigned variant, unsigned phase, bool native = false) const;
    MaterialPixels phasePixels(MaterialId id, unsigned variant, unsigned phase, bool native = false);
    // The frame's own pixels (square, 32-bit ARGB), when they need no shared
    // variant edges applied and so can be sampled in place of phasePixels();
    // null otherwise. Valid until the next stream window declaration.
    const SDL_Surface *phaseSurface(MaterialId id, unsigned variant, unsigned phase, bool native = false) const;
    struct CpuScratch
    {
        CellMask mask;
        std::vector<PreparedMaterialView> sources;
        std::array<MaterialPixels, 4> filtered; // Box-filtered HD sources, one per palette slot.
    };
    // An HD source composed below its own size: each output texel is the
    // premultiplied box average of the step-by-step block it covers, as the
    // streamed direct pass filters its art tiles (DirectTerrain), so every
    // renderer draws the same half-resolution level.
    static void boxFilter(const PreparedMaterialView &, int size, MaterialPixels &out);
    struct CpuMaterials
    {
        struct Texture
        {
            int size = 0;
            std::shared_ptr<const MaterialPixels> pixels;
        };
        std::shared_ptr<const Catalog> definitions;
        std::shared_ptr<const StoneMaps> stones;
        std::vector<std::vector<Texture>> textures;
        std::vector<char> included;
        std::size_t bytes = 0;
        unsigned surfTime = 0; // Animation time of the surf phase, as prepared.
        // Every recipe corner must be included in this immutable snapshot.
        void compose(const Recipe &, SDL_Surface *, int scale, const CellMask * = nullptr) const;
        void compose(const Recipe &, SDL_Surface *, int scale, CpuScratch &,
                     const CellMask * = nullptr, const Refresh * = nullptr, int reduction = 1) const;
        CellMask mask(const Recipe &, int scale) const;
    };
    std::shared_ptr<const CpuMaterials> cpuMaterials(std::span<const MaterialId> needed = {});
    std::size_t cpuMaterialBytes(std::span<const MaterialId> needed = {}) const;
    std::shared_ptr<const CpuMaterials> cpuMaterialsForRecipes(std::span<const Recipe> recipes);
    std::size_t cpuMaterialBytesForRecipes(std::span<const Recipe> recipes) const;
	std::uint64_t textureRevision(MaterialId id) const { return textureRevisions[id]; }
	int scale() const { return resolution; }
    // Diagnostic: HD preparations, and those that drew a native source for a
    // material whose HD frames had already appeared (streaming fallback).
    struct HighResolutionCoverage { std::uint64_t prepares = 0, fallbacks = 0; };
    HighResolutionCoverage highResolutionCoverage() const { return coverage; }
    // Look-ahead phases kept resident for streamed animated HD materials, beyond
    // the current and next phase that every animation requires.
    static constexpr std::size_t AnimationLookaheadBytes = 64u * 1024 * 1024;
	std::size_t sourceBytes() const;

  private:
      std::weak_ptr<const CpuMaterials> publishedCpu;
      std::vector<std::uint64_t> publishedRevisions;
      struct CpuPublication
      {
          std::uint64_t revision = 0;
          std::vector<std::weak_ptr<const MaterialPixels>> pixels;
      };
      // One weak record per prepared variant: older snapshots can keep an
      // unchanged revision reusable without the compositor retaining pixels.
      std::vector<CpuPublication> publishedPixels;
      std::weak_ptr<const Catalog> publishedCatalog;
      std::size_t cpuPublicationBytes() const;
      static void composePixels(const Catalog &, const StoneMaps &, const Recipe &, SDL_Surface *, int, int, int,
                                std::span<const PreparedMaterialView>, const CellMask *,
                                unsigned surfTime, const Refresh * = nullptr, int reduction = 1);
      static void buildMask(const Catalog &, const StoneMaps &, const Recipe &, int, CellMask &);
	struct Texture
	{
		GAGCore::DrawableSurface *source = nullptr;
		std::uint64_t identity = 0, revision = 0;
		int size = 0;
		bool opaque = false; // Every pixel has full alpha.
		SharedPixels pixels;
	};
	static void markOpaque(Texture &);
	Catalog definitions;
	bool hasNeighborhoodProfiles = false;
    MapAssetSprites customSprites;
    MaterialId materialFor(const SceneMap&, TerrainType type) const;
    // Detailed recipes add shore calm and whole-stone path edges.
    Recipe describe(const SceneMap &, int x, int y, bool detailed) const;
    Recipe describeCell(const SceneMap &, int x, int y, bool shoreCalm) const;
	void describeShoreCalm(const SceneMap&, Recipe &) const;
	void describeStones(const SceneMap &, Recipe &) const;
	void describeHalo(const SceneMap &, Recipe &) const;
	std::shared_ptr<const StoneMaps> stoneMaps;
	std::uint64_t instance = 0; // Distinguishes compositors in shared presentation caches.
    unsigned decorSlot(MaterialId) const;
	// Whether a connected open-ground mass vertex lies within `reach` vertices
	// of cell (x,y)'s corners (Decor::clearOfMasses).
	bool massWithin(const SceneMap &, int x, int y, int reach) const;
	MaterialId overlayMaterial(const SceneMap &, int x, int y, bool &found) const;
	std::shared_ptr<CompiledPack> pack;
	std::map<std::uint64_t, std::uint64_t> cleanSources;
	std::array<MaterialId, TERRAIN_COUNT> terrainBindings{};
	std::vector<GAGCore::Sprite *> sprites;
	// On demand: which materials' sheets load through MaterialSheets, and
	// those whose sheet failed validation.
	std::vector<char> lazySheets, invalidSheets;
	std::uint64_t sheetGeneration = 0, sheetArrivals = 0, sheetFallbacks = 0;
	// The adopted sheet of a material, or null while it loads.
	GAGCore::Sprite *sheet(unsigned id) const;
	// Asks for a missing sheet and marks a present one used.
	void wantSheet(unsigned id) const;
	// Adopts newly published sheets and forgets released ones.
	void refreshSheets();
	// Validates a sheet and derives what composition needs from it.
	void adoptSheet(unsigned id, GAGCore::Sprite *);
	void fallbackTexture(unsigned id, Texture &) const;
	// The materials each map holds, found a few rows a frame.
	struct MapScan
	{
		std::uint64_t identity = 0, revision = 0;
		int row = 0;
		bool scanning = false, complete = false;
		std::vector<char> present, seen;
	} scan;
	bool advanceScan(const SceneMap &, std::size_t vertexBudget);
	// The map's sheets not yet asked for: they load after those in view,
	// at most two at a time, so the view's own sheets come first.
	std::vector<std::string> prefetchSheets;
	std::vector<std::string> presentSheets() const;
	std::vector<GAGCore::Sprite *> decorSprites;
    GAGCore::Sprite *sharedDecorSprite = nullptr;
    bool mixedDecorSprites = false;
    bool tieredDecor = false;
    mutable std::mutex tierMutex;
    // A composition scale below an HD source's box-filters each step-by-step
    // block. Cells read one compact filtered copy per prepared variant
    // revision instead of filtering the whole source for every cell.
    struct Decimated
    {
        std::uint64_t revision = 0;
        const void *source = nullptr;
        int sourceSize = 0;
        std::shared_ptr<const MaterialPixels> pixels;
    };
    // The view of a compact copy; `hold` keeps it alive for the caller even if
    // a later call evicts or rebuilds the entry.
    PreparedMaterialView decimated(MaterialId, const PreparedMaterialView &, int scale,
                                   std::shared_ptr<const MaterialPixels> &hold) const;
    mutable std::mutex decimationMutex;
    mutable std::map<std::tuple<MaterialId, unsigned, int>, Decimated> decimations;
    mutable std::size_t decimationBytes = 0;
    mutable std::shared_ptr<const TierField> tierCache;
	DecorReach reach;
	std::vector<GAGCore::Sprite *> overlaySprites;
	GAGCore::Sprite *sharedOverlaySprite = nullptr;
	bool mixedOverlaySprites = false;
	static void readTexture(Texture &, GAGCore::DrawableSurface *);
	std::vector<std::vector<Texture>> textures;
	struct AnimationTextures
	{
		std::vector<Texture> current, next;
		unsigned blend = ~0u;
	};
	std::vector<AnimationTextures> animationTextures;
	using Usage = std::vector<std::vector<unsigned char>>;
#ifdef __EMSCRIPTEN__
    void requestMapTerrain(const SceneMap&);
    Uint64 requestedMapIdentity = 0;
    std::vector<std::string> requestedTerrainPackages;
#endif
    void prepareSelected(bool hd, int time, const Usage *usage, bool nativeComposition = false,
                         bool decor = true);
    Usage visibleUsage(const SceneMap &, int left, int top, int right, int bottom) const;
    // Material variants each 16x16 page of the map uses (material << 16 | variant),
    // kept until the terrain changes, so a frame unions short lists instead of
    // visiting every visible cell.
    const std::vector<std::uint32_t> &pageUsage(const SceneMap &, int wx, int wy) const;
    mutable std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> usagePages;
    // The decor frames each 16x16 page draws, kept like usagePages, so the HD
    // decor window holds only frames visible cells use, not every frame of
    // every material in view.
    const std::vector<DecorLayer> &pageDecor(const SceneMap &, int wx, int wy) const;
    mutable std::unordered_map<std::uint64_t, std::vector<DecorLayer>> decorPages;
    // The view's decor frames, set while its windows are declared.
    std::vector<DecorLayer> visibleDecor;
    bool visibleDecorSet = false;
    void collectVisibleDecor(const SceneMap &, int left, int top, int right, int bottom);
    mutable std::uint64_t usageIdentity = 0, usageRevision = 0;
    mutable std::uint32_t usageSeed = 0;
    // Nonperiodic custom art uses variant zero as its shared boundary source.
    void includeBoundarySources(Usage &usage) const;
    void declareWindows(bool materials, bool decor, int time, const Usage *requested);
    void releasePreparedTextures();
    // Whether prepareSelected may have left CPU textures to release.
    bool texturesPrepared = true;
    Usage recipeUsage(std::span<const Recipe> recipes) const;
    std::size_t cpuMaterialBytesSelected(std::span<const MaterialId>, const Usage *) const;
    std::shared_ptr<const CpuMaterials> cpuMaterialsSelected(std::span<const MaterialId>, const Usage *);
    bool publishedSubset = false;
    bool prepareMaterial(unsigned id, bool hd, int phase, std::vector<Texture> &target,
                         const std::vector<unsigned char> *wanted);
	std::vector<std::uint64_t> textureRevisions;
	std::vector<int> highResolutionSizes; // Largest source size seen per material.
	HighResolutionCoverage coverage;
    // Detail level of portable pages: 0 HD, 1 native. A view starts native.
    GAGCore::DetailHysteresis pageDetail{1};
	int resolution = 1;
	unsigned preparedTime = 0;
};
} // namespace TerrainVisual
