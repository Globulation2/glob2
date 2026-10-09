// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapPictures.h"

#include "GenerationRequest.h"
#include "GeneratorRegistry.h"
#include "LandscapePreviewer.h"
#include "MapCache.h"
#include "MapThumbnail.h"
#include "OnlineServices.h"

#include <GraphicContext.h>

namespace Glob2UI
{
namespace
{
// The size drawn: enough for a card thumbnail at 3x without upscaling.
constexpr int PICTURE_PIXELS = 192;

std::unique_ptr<GAGCore::DrawableSurface> pictureOf(const MapThumbnail &thumbnail)
{
	if (!thumbnail.isLoaded())
		return nullptr;
	auto surface = std::make_unique<GAGCore::DrawableSurface>(PICTURE_PIXELS, PICTURE_PIXELS);
	thumbnail.loadIntoSurface(surface.get());
	return surface;
}
} // namespace

MapPictures::MapPictures() = default;
// Each previewer joins its worker threads when destroyed.
MapPictures::~MapPictures() = default;

GAGCore::DrawableSurface *MapPictures::generator(const std::string &generatorId)
{
	auto found = landscapes.find(generatorId);
	if (found == landscapes.end())
	{
		Landscape landscape;
		try
		{
			GenerationRequest request;
			request.setMethodDefaults(GeneratorRegistry::active().idOf(generatorId));
			request.wDec = request.hDec = 7;
			request.nbTeams = 2;
			request.seed = 1;
			landscape.previewer = std::make_unique<LandscapePreviewer>(std::vector<GenerationRequest>{request}, 1);
		}
		catch (const std::exception &)
		{
			// A generator this build does not know: a placeholder, not a crash.
			landscape.failed = true;
		}
		found = landscapes.emplace(generatorId, std::move(landscape)).first;
	}
	return found->second.surface.get();
}

GAGCore::DrawableSurface *MapPictures::cached(const std::string &hash)
{
	if (hash.empty())
		return nullptr;
	auto found = maps.find(hash);
	if (found == maps.end())
	{
		std::unique_ptr<GAGCore::DrawableSurface> surface;
		if (const auto path = Online::services().maps.path(hash))
		{
			MapThumbnail thumbnail;
			thumbnail.loadFromMap(*path);
			surface = pictureOf(thumbnail);
		}
		found = maps.emplace(hash, std::move(surface)).first;
	}
	return found->second.get();
}

bool MapPictures::update()
{
	bool changed = false;
	for (auto &[id, landscape] : landscapes)
	{
		if (!landscape.previewer || landscape.surface || landscape.failed)
			continue;
		if (landscape.previewer->threadCount() == 0)
			landscape.previewer->poll();
		const unsigned revision = landscape.previewer->revision(0);
		if (revision == landscape.revision)
			continue;
		landscape.revision = revision;
		const auto preview = landscape.previewer->preview(0);
		if (preview.state == LandscapePreviewer::State::Ready)
		{
			landscape.surface = pictureOf(preview.thumbnail);
			changed = true;
		}
		else if (preview.state == LandscapePreviewer::State::Failed)
			landscape.failed = true;
	}
	return changed;
}
} // namespace Glob2UI
