// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>
namespace MapGeneration
{
// Real geography, baked into the client: one small categorical raster per continent, drawn by
// tools/world_atlas.py from Natural Earth (coasts, lakes, rivers, glaciers, named deserts and
// ranges) and the Köppen-Geiger climate classification (which land is forest, farmland, steppe,
// desert, tundra or ice), and compiled in as WorldAtlasData.cpp. A generator fits a region onto
// its map with Raster.h and reads each tile's kind of land from the result; it never opens a file,
// so a map is a function of the request and the seed alone, the same on every platform.
//
// The atlas describes the world, not the game: it says where the Sahara and the Rockies are, and
// the generator decides that a desert is sand and a range is stone. Keep that translation in the
// generator (or a shared stage) so the same data can serve another design.

/// The kinds of land the atlas tells apart, in the order the data file encodes them. Ocean is 0,
/// so a cell nothing was drawn on is sea; Plain, Forest and Steppe are the buildable, farmable
/// kinds, Desert and Ice hold nothing, Mountain is rugged ground and Tundra grass that grows
/// nothing.
enum class LandClass : unsigned char
{
	Ocean = 0,
	Lake = 1,
	Plain = 2,    // temperate and continental farmland: wheat country
	Forest = 3,   // rainforest and taiga: wood country
	Steppe = 4,   // savanna and steppe: dry grassland with sparse crops
	Desert = 5,   // true desert: sand
	Mountain = 6, // a named range: stone
	Tundra = 7,   // grass that grows nothing
	Ice = 8,      // ice cap: sand
	Count = 9
};
/// A cell's class lives in its low nibble; this bit above it marks a great river running through
/// the cell.
constexpr unsigned char kAtlasRiverFlag = 0x10;
constexpr unsigned char kAtlasClassMask = 0x0F;

inline LandClass atlasClass(unsigned char cell)
{
	return LandClass(cell & kAtlasClassMask);
}
inline bool atlasRiver(unsigned char cell)
{
	return (cell & kAtlasRiverFlag) != 0;
}
/// Land, as opposed to sea or lake.
inline bool atlasLand(unsigned char cell)
{
	return atlasClass(cell) != LandClass::Ocean && atlasClass(cell) != LandClass::Lake;
}

/// One region of the atlas: its stable id ("africa"), its size in cells, and its cells run-length
/// encoded as (value, run) byte pairs, row-major.
struct AtlasRegion
{
	const char *id;
	int width, height;
	const unsigned char *encoded;
	unsigned encodedBytes;
};

/// The regions in the data file, in its order (the six continents).
extern const AtlasRegion kAtlasRegions[];
extern const int kAtlasRegionCount;

/// The region with this id, or nullptr.
const AtlasRegion *atlasRegion(const char *id);

/// A region's cells decoded, width * height bytes row-major: atlasClass and atlasRiver read them.
std::vector<unsigned char> decodeAtlas(const AtlasRegion &);
} // namespace MapGeneration
