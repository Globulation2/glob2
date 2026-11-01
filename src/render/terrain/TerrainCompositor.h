// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMaterials.h"
#include "TerrainType.h"
#include "render/MapAssetSprites.h"
#include <SDLGraphicContext.h>
#include <memory>
#include <span>

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
	}
	// Retire the renderer allocation while keeping expensive composed CPU pixels.
	SDL_Surface *takePixels()
	{
		auto *pixels = sdlsurface;
		sdlsurface = nullptr;
		return pixels;
	}
};

// Renderer-owned, never accessed from the simulation. Sources are immutable
// copies refreshed on sprite publication/content changes, including HD toggles.
class Compositor
{
  public:
	explicit Compositor(Catalog catalog, std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty());
	const Catalog &catalog() const { return definitions; }
	bool contextualBorders() const { return hasContextualProfiles; }
	std::pair<GAGCore::Sprite *, unsigned> editorIcon(TerrainType type) const;
	void prepare(bool hd, int time);
    // Retain only requested variants; immutable CPU snapshots remain usable.
    void prepareForRecipes(bool hd, int time, std::span<const Recipe> recipes);
    // Inclusive world-cell bounds, wrapped to the map. HD streams asynchronously
    // with native sources as fallback until each requested frame is available.
    void prepareVisible(bool hd, int time, const SceneMap &, int left, int top, int right, int bottom);
	Recipe describe(const SceneMap &, int x, int y) const;
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
	// A uniform recipe ignores the mask; a mixed one composes from it when given.
	void compose(const Recipe &, SDL_Surface *target, int x, int y, int scale,
				 const CellMask *mask = nullptr) const;
	// Animated textures or surf: either changes this material's composed pixels over time.
	bool animated(MaterialId id) const
	{
		const auto &m = definitions.materials[id];
		return m.animationFrames > 1 || m.surf.period > 0;
	}
	unsigned surfTime() const { return preparedTime; }
	// Subtile palette samples share the detailed renderer's material partition.
	static constexpr int OverviewSamples = 4;
	// Per corner (Recipe::corners order), an overview colour replacing the
	// material's preview, or null; saved custom terrain carries its own.
	using CornerColors = std::array<const std::array<unsigned char, 3> *, 4>;
	void composeOverview(const Recipe &, SDL_Surface *target, int x, int y,
						 const CornerColors *cornerColors = nullptr) const;
	// The shared decor sprite (every decor block names the same one), or null
	// when the catalog has no decor; the renderer batches decor like resources.
	GAGCore::Sprite *decorSprite() const { return sharedDecorSprite; }
    GAGCore::Sprite *decorSprite(MaterialId id) const { return decorSprites.at(id); }
    GAGCore::Sprite *decorSprite(const SceneMap &map, int x, int y) const;
	// Decor frame drawn over cell (x, y), or -1. Cells with a neighbour of a
	// different appearance use the smaller edge frames.
	int decorFrame(const SceneMap &, int x, int y) const;
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
    struct CpuScratch
    {
        CellMask mask;
        std::vector<PreparedMaterialView> sources;
    };
    struct CpuMaterials
    {
        struct Texture
        {
            int size = 0;
            std::shared_ptr<const MaterialPixels> pixels;
        };
        std::shared_ptr<const Catalog> definitions;
        std::vector<std::vector<Texture>> textures;
        std::vector<char> included;
        std::size_t bytes = 0;
        unsigned surfTime = 0; // Animation time of the surf phase, as prepared.
        // Every recipe corner must be included in this immutable snapshot.
        void compose(const Recipe &, SDL_Surface *, int scale, const CellMask * = nullptr) const;
        void compose(const Recipe &, SDL_Surface *, int scale, CpuScratch &,
                     const CellMask * = nullptr) const;
        CellMask mask(const Recipe &, int scale) const;
    };
    std::shared_ptr<const CpuMaterials> cpuMaterials(std::span<const MaterialId> needed = {});
    std::size_t cpuMaterialBytes(std::span<const MaterialId> needed = {}) const;
    std::shared_ptr<const CpuMaterials> cpuMaterialsForRecipes(std::span<const Recipe> recipes);
    std::size_t cpuMaterialBytesForRecipes(std::span<const Recipe> recipes) const;
	std::uint64_t materialRevision(MaterialId id) const { return materialRevisions[id]; }
	int scale() const { return resolution; }
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
      static void composePixels(const Catalog &, const Recipe &, SDL_Surface *, int, int, int,
                                std::span<const PreparedMaterialView>, const CellMask *,
                                unsigned surfTime);
      static void buildMask(const Catalog &, const Recipe &, int, CellMask &);
	struct Texture
	{
		GAGCore::DrawableSurface *source = nullptr;
		std::uint64_t identity = 0, revision = 0;
		int size = 0;
		std::vector<std::array<unsigned char, 4>> pixels;
	};
	Catalog definitions;
	bool hasContextualProfiles = false;
    MapAssetSprites customSprites;
    MaterialId materialFor(const SceneMap&, TerrainType type) const;
    void describeShoreCalm(const SceneMap&, Recipe &) const;
    std::pair<MaterialId, unsigned> decorMaterial(const SceneMap&, int x, int y) const;
	std::shared_ptr<CompiledPack> pack;
	std::map<std::uint64_t, std::uint64_t> cleanSources;
	std::array<MaterialId, TERRAIN_COUNT> terrainBindings{};
	std::vector<GAGCore::Sprite *> sprites;
	std::vector<GAGCore::Sprite *> decorSprites;
    GAGCore::Sprite *sharedDecorSprite = nullptr;
    bool mixedDecorSprites = false;
	static void readTexture(Texture &, GAGCore::DrawableSurface *);
	std::vector<std::vector<Texture>> textures;
	struct AnimationTextures
	{
		std::vector<Texture> current, next;
		unsigned blend = ~0u;
	};
	std::vector<AnimationTextures> animationTextures;
	using Usage = std::vector<std::vector<unsigned char>>;
    void prepareSelected(bool hd, int time, const Usage *usage);
    Usage recipeUsage(std::span<const Recipe> recipes) const;
    std::size_t cpuMaterialBytesSelected(std::span<const MaterialId>, const Usage *) const;
    std::shared_ptr<const CpuMaterials> cpuMaterialsSelected(std::span<const MaterialId>, const Usage *);
    bool publishedSubset = false;
    bool prepareMaterial(unsigned id, bool hd, int phase, std::vector<Texture> &target,
                         const std::vector<unsigned char> *wanted);
	std::vector<std::uint64_t> materialRevisions;
	int resolution = 1;
	unsigned preparedTime = 0;
};
} // namespace TerrainVisual
