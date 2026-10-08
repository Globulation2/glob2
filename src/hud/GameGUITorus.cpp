// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameGUITouch.h"
#include "GlobalContainer.h"
#include "render/UnitMotion.h"
#include "Unit.h"

// Adapt the rendered surface to the normal tool coordinate system, retaining
// sub-tile precision for even-sized buildings and wrapping at the map seams.
bool GameGUI::torusMapPointer(int x, int y, int &mx, int &my) const
{
    int px, py;
    if (!torusView.pick(x, y, px, py))
        return false;
    mx = (px - viewportX * 32) & (drawnScene().map.getW() * 32 - 1);
    my = (py - viewportY * 32) & (drawnScene().map.getH() * 32 - 1);
    return true;
}

bool GameGUI::handleTorusPointer(const SDL_Event &event)
{
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP)
        return false;
    if (event.button.button != SDL_BUTTON_LEFT)
        return false;
    bool onMap = event.button.x >= 0 && event.button.y >= 16 &&
        event.button.x < globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;
    if (!onMap && !torusPointerDown)
        return false;
    int mx, my;
    bool hit = onMap && torusMapPointer(event.button.x, event.button.y, mx, my);
    mouseX = event.button.x;
    mouseY = event.button.y;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        torusPointerDown = hit;
        torusView.setPointerHeld(hit);
        if (hit)
        {
            // The atlas renderer's mouse hit refers to a previous capture.
            const auto& scene=drawnScene();
            view.mouseUnit = UnitRef();
            const int x=((mx>>5)+viewportX)&scene.map.getMaskW();
            const int y=((my>>5)+viewportY)&scene.map.getMaskH();
            auto gid=scene.map.getAirUnit(x,y);
            if (gid==NOGUID) gid=scene.map.getGroundUnit(x,y);
            if (const auto* unit=scene.entities.unit(gid); unit &&
                (unit->team==localTeamNo || scene.map.isFOWDiscovered(x,y,Team::teamNumberToMask(localTeamNo)) || globalContainer->replaying))
                view.mouseUnit=unit->identity;
            handleMapClick(mx, my, SDL_BUTTON_LEFT);
        }
    }
    else
    {
        if (torusPointerDown)
        {
            const auto* ref=std::get_if<BuildingRef>(&selection);
            const auto* building=ref ? inputBuilding(*ref) : nullptr;
            if (hit && selectionMode == BUILDING_SELECTION && selectionPushed &&
                building && drawnScene().entities.type(*building)->semantics.relocatable)
                moveFlag(mx, my, true);
            else if (selectionMode == BRUSH_SELECTION || selectionMode == TOOL_SELECTION)
            {
                if (hit)
                    toolManager.handleMouseUp(mx, my, localTeamNo, viewportX, viewportY, inputState.modifiers());
                else
                    toolManager.finishPointerGesture(localTeamNo);
            }
        }
        torusPointerDown = false;
        miniMapPushed = selectionPushed = panPushed = false;
    }
    return true;
}

void GameGUI::drawTorusMap(int originX, int originY, int width, int height, int team, unsigned options, int cloudGridLimit, bool advancePreviews)
{
    Game::drawMap(0, 0, width, height, 0, 0,
                 originX, originY, team, view, options, nullptr, &buildingGuiState, gamePaused, cloudGridLimit, true);
    if (globalContainer->replaying)
        return;
    ghostManager.drawAll(drawnScene(), originX, originY, localTeamNo, width, height);
    globalContainer->gfx->drawMapCopies(drawnScene().map.getW() * 32, drawnScene().map.getH() * 32, width, height, [&]() {

        int px, py;
        if ((selectionMode == TOOL_SELECTION || (selectionMode == BRUSH_SELECTION && !touch->usesHUD())) &&
            torusView.pick(mouseX, mouseY, px, py))
        {
            int mx = (px - originX * 32) & (drawnScene().map.getW() * 32 - 1);
            int my = (py - originY * 32) & (drawnScene().map.getH() * 32 - 1);
            toolManager.drawTool(mx, my, localTeamNo, originX, originY, inputState.modifiers());
        }
        // The ring replaces the 2D map transform, so the selection markers the flat
        // view paints over the map belong on the surface itself, anchored to it.
        const PresentationFrame &scene = drawnScene();
        if (selectionMode == BUILDING_SELECTION && scene.panels.building.valid)
        {
            const SceneBuildingPanel &b = scene.panels.building;
            int x, y;
            drawnScene().map.buildingPosToCursor(displayedPosX(b), displayedPosY(b), b.type->width, b.type->height, &x, &y,
                                         originX, originY);
            if (b.owner().number == localTeamNo)
                globalContainer->gfx->drawCircle(x, y, b.type->width * 16, 0, 0, 190);
            else if (scene.panels.local.state().allies & b.owner().mask)
                globalContainer->gfx->drawCircle(x, y, b.type->width * 16, 255, 196, 0);
            else if (!b.type->isVirtual)
                globalContainer->gfx->drawCircle(x, y, b.type->width * 16, 190, 0, 0);

            // draw a white circle around units that are working at building
            if (showUnitWorkingToBuilding && (b.owner().allies & (Team::teamNumberToMask(localTeamNo))))
                for (UnitRef worker : scene.entities.selectedBuilding.unitsWorking)
                {
                    const SnapshotUnit *unit = scene.entities.unit(worker.gid);
                    if (!unit || unit->identity!=worker)
                        continue;
                    int ux, uy;
                    scene.map.mapCaseToDisplayable(unit->posX, unit->posY, &ux, &uy, originX, originY);
                    int deltaLeft = 255 - drawnUnitDelta(*unit, view.render.unitMotion);
                    if (unit->action < BUILD)
                    {
                        ux -= (unit->dx * deltaLeft) >> 3;
                        uy -= (unit->dy * deltaLeft) >> 3;
                    }
                    globalContainer->gfx->drawCircle(ux + 16, uy + 16, 16, 255, 255, 255, 180);
                }
        }
        else if (selectionMode == RESOURCE_SELECTION)
        {
            int resource = selectionResource();
            int rx = resource & drawnScene().map.getMaskW();
            int ry = resource >> drawnScene().map.getShiftW();
            int px, py;
            drawnScene().map.mapCaseToDisplayable(rx, ry, &px, &py, originX, originY);
            globalContainer->gfx->drawCircle(px + 16, py + 16, 16, 0, 0, 190);
        }
    }, advancePreviews);
}
