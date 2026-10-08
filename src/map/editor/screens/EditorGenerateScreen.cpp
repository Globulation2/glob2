// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorGenerateScreen.h"
#include "MapEdit.h"
#include "GenerationService.h"
#include "BuildingLibrary.h"
#include "OnlineServices.h"
#include "OnlineStorage.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <AssetLoader.h>
#include <CooperativeTask.h>
namespace
{
GAGCore::CooperativeTask generate(MapEdit &editor, GenerationRequest descriptor, Uint32 seed)
{
	co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	auto storage = Online::makeUserDirectoryStorage();
	auto buildings =
		BuildingLibrary(Online::servicesCreated() ? Online::services().storage : *storage)
			.compose(globalContainer->buildingsTypes);
	editor.game.buildingsTypes = buildings.catalog;
	editor.game.gameHeader.setBuildingCatalogSnapshot(buildings.catalog.snapshotJson());
	editor.game.configureBuildingCatalog();
	GenerationService generator;
	// A chosen visual preview is reproduced exactly; unpreviewed drafts retain
	// the existing best-roll selection.
	if (!descriptor.seed)
		descriptor.seed = generator.bestSeed(descriptor, seed);
	if (!generator.generate(editor.game, descriptor))
		co_return false;
	editor.game.gameHeader.setBuildingArtwork(buildings.artwork ? buildings.artwork->bytes()
																: std::string{});
	if (!globalContainer->runNoX)
	{
		GAGCore::Toolkit::assets().setCommunityFiles(buildings.artwork
														 ? buildings.artwork->files()
														 : GAGCore::AssetLoader::CommunityFiles{});
		editor.game.buildingsTypes.loadSprites();
	}
	editor.mapHasBeenModified();
	editor.regenerateGameHeader();
	co_return true;
}
} // namespace
EditorGenerateScreen::EditorGenerateScreen(GenerationRequest descriptor, Uint32 seed,
										   GAGCore::CooperativeSlice slice)
	: EditorLoadScreen([descriptor, seed](MapEdit &editor)
					   { return generate(editor, descriptor, seed); }, "[Generating map]",
					   std::move(slice))
{
}

void EditorGenerateScreen::onTimer(Uint32 tick)
{
	// Present the progress screen and admit cancellation before starting a roll.
	if (!presented)
	{
		presented = true;
		return;
	}
	EditorLoadScreen::onTimer(tick);
}
