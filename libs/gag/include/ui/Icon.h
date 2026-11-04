// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>
namespace GAGCore
{
class DrawableSurface;
}
namespace GAGGUI::ui
{
// Immutable white alpha masks, owned with the element; raster variants sorted by width.
struct IconAsset
{
	std::string name;
	struct Raster
	{
		int pixels;
		std::shared_ptr<GAGCore::DrawableSurface> surface;
	};
	std::vector<Raster> rasters;
	// Renderer-owned bounded recolour cache. Keys include raster width and RGBA.
	mutable std::map<std::pair<int, std::uint32_t>, std::shared_ptr<GAGCore::DrawableSurface>>
		colours;
	bool available() const { return !rasters.empty(); }
};
using IconRef = std::shared_ptr<const IconAsset>;
} // namespace GAGGUI::ui
