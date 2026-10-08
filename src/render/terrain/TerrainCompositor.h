// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMaterials.h"
#include "TerrainType.h"
#include "render/MapAssetSprites.h"
#include <SDLGraphicContext.h>
#include <memory>

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
};

// Renderer-owned, never accessed from the simulation. Sources are immutable
// copies refreshed on sprite publication/content changes, including HD toggles.
class Compositor
{
  public:
	explicit Compositor(Catalog catalog, std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty());
	const Catalog &catalog() const { return definitions; }
	std::pair<GAGCore::Sprite *, unsigned> editorIcon(TerrainType type) const;
	void prepare(bool hd, int time);
	Recipe describe(const SceneMap &, int x, int y) const;
	void compose(const Recipe &, SDL_Surface *target, int x, int y, int scale) const;
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
	std::uint64_t materialRevision(MaterialId id) const { return materialRevisions[id]; }
	int scale() const { return resolution; }
	std::size_t sourceBytes() const;

  private:
	struct Texture
	{
		GAGCore::DrawableSurface *source = nullptr;
		std::uint64_t identity = 0, revision = 0;
		int size = 0;
		std::vector<std::array<unsigned char, 4>> pixels;
	};
	Catalog definitions;
    MapAssetSprites customSprites;
    MaterialId materialFor(const SceneMap&, TerrainType type) const;
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
	std::vector<std::uint64_t> materialRevisions;
	int resolution = 1;
};
} // namespace TerrainVisual
