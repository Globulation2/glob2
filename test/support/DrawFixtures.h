// SPDX-License-Identifier: GPL-3.0-or-later
// Scene extraction and draw passes for tests: MapDrawer::drawMap and GameGUI::drawAll
// over a scene extracted the way the client publishes one, and a SceneMap bound to a map.
#pragma once

#include "Glob2Test.h"

#include "render/MapDrawer.h"
#include "render/scene/Scene.h"

#include <tuple>
#include <utility>

class Game;
class GameGUI;
class SceneMap;

namespace glob2test
{
	// Extract the scene a draw pass reads, as MapDrawer::drawMap does for callers without a
	// published scene. Without `into`, the result lives in shared storage that the next
	// call overwrites; use it within the statement.
	const PresentationFrame &sceneOf(const Game &game, const ViewState &view, int localTeam = 0, PresentationFrame *into = nullptr);
	const PresentationFrame &sceneOf(const Game &game);
    void observeMap(const Map& map,SceneMap& view);
    void drawGUI(GameGUI& gui,int team);

    template<class... Args> void drawMap(Game& game,Args&&... args)
    {
        auto arguments=std::forward_as_tuple(args...);
        auto& view=std::get<9>(arguments);
        const auto* previous=view.scene;
        PresentationFrame frame;
        if (!previous) {
            sceneOf(game,view,std::get<8>(arguments),&frame);
            view.scene=&frame;
        }
        struct Restore { ViewState& view;const PresentationFrame* previous;
            ~Restore(){view.scene=previous;} } restore{view,previous};
        MapDrawer::drawMap(std::forward<Args>(args)...);
    }
}
