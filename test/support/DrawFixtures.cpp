// SPDX-License-Identifier: GPL-3.0-or-later
#include "DrawFixtures.h"

#include "Game.h"
#include "GameGUI.h"
#include "render/scene/SceneExtract.h"

namespace glob2test
{
	const PresentationFrame &sceneOf(const Game &game, const ViewState &view, int localTeam, PresentationFrame *into)
	{
		static PresentationFrame shared;
		PresentationFrame &scene = into ? *into : shared;
        SceneRequest request=game.gui ? game.gui->sceneRequest() : SceneRequest{};
		request.localTeam = localTeam;
        if (view.selectedBuilding) request.selectedBuilding=Game::refOf(view.selectedBuilding);
        if (view.selectedUnit) request.selectedUnit=Game::refOf(view.selectedUnit);
		game.snapshots().invalidateBoundary(); // Fixture helpers may directly edit public state.
		SceneExtractor().prepare(game.captureReadBoundary(SceneExtractor::captureOptions(request)),request,scene);
		return scene;
	}

	const PresentationFrame &sceneOf(const Game &game)
	{
		static const ViewState none;
		return sceneOf(game, none);
	}

    void drawGUI(GameGUI& gui,int team)
    {
        if (!gui.simulationThreaded) {
            gui.game.snapshots().invalidateBoundary(); // Test scripts can edit public records.
            gui.prepareLocalPresentation();
        }
        gui.drawAll(team);
    }

    void observeMap(const Map& map,SceneMap& view)
    {
        REQUIRE(map.game);
        SceneRequest request;request.includePanels=false;
        const auto world=map.game->captureReadBoundary(SceneExtractor::captureOptions(request));
        view.bindSnapshot(world,map.displayViewportW,map.displayViewportH,map.displayedTeam);
        view.prepareChunk(0,size_t(world.width)*world.height);
    }
}
