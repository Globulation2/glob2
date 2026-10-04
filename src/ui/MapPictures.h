// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include <memory>
#include <string>

namespace GAGCore
{
class DrawableSurface;
}
class LandscapePreviewer;
class MapThumbnail;

namespace Glob2UI
{
// Map pictures the online screens show without a server preview: the
// landscapes of a queue's map pool (generator ids, drawn once on background
// threads with a fixed seed, so the same pool always looks the same) and maps
// the player already has in the content-addressed map cache (by hash, such as
// the last match's map). Draw them with previewPicture(); null means not
// ready, unknown or unavailable, which previewPicture shows as a placeholder.
class MapPictures
{
  public:
	MapPictures();
	~MapPictures();
	MapPictures(const MapPictures &) = delete;
	MapPictures &operator=(const MapPictures &) = delete;

	// A two-colony 128 × 128 landscape of this generator; starts drawing it on first request.
	GAGCore::DrawableSurface *generator(const std::string &generatorId);
	// A cached map by its SHA-256; null when the map is not in the cache.
	GAGCore::DrawableSurface *cached(const std::string &hash);
	// Advances drawing on hosts without worker threads; true when a picture became ready.
	bool update();

  private:
	struct Landscape
	{
		std::unique_ptr<LandscapePreviewer> previewer;
		unsigned revision = ~0u;
		std::unique_ptr<GAGCore::DrawableSurface> surface;
		bool failed = false;
	};
	std::map<std::string, Landscape> landscapes;
	// A missing or unreadable map is remembered as null, so it is not read every frame.
	std::map<std::string, std::unique_ptr<GAGCore::DrawableSurface>> maps;
};
} // namespace Glob2UI
