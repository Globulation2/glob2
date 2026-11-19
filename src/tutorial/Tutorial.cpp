// SPDX-License-Identifier: GPL-3.0-or-later
#include "TutorialGuidance.h"
#include "GameGUI.h"
#include "GameGUITouch.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Order.h"
#include "GameGUIDialog.h"
#include "render/scene/Scene.h"
#include <cmath>

// Lesson destinations name a building by its origin tile; aim at the footprint's
// centre so markers and the camera land on the building rather than its corner.
std::pair<double,double> Tutorial::destinationCentre(const PresentationFrame &scene, const Tutorial::Destination &target)
{
    double x=target.x,y=target.y;
    const int tx=int(std::floor(x)),ty=int(std::floor(y));
    if (const auto *b=scene.entities.building(scene.map.getBuilding(tx,ty));
        b && b->posX==tx && b->posY==ty) {
        const auto *type=scene.entities.type(*b);
        x+=type->width/2.0; y+=type->height/2.0;
    }
    return {x,y};
}

void GameGUI::updateTutorial()
{
    const auto &s=drawnScene().panels.hud.state();
    auto lesson=Tutorial::view(s);
    if (!lesson || globalContainer->replaying || networkMatch.active) {
        tutorialGuidance.reset();
        for (int id : tutorialHighlights) highlights.erase(id);
        tutorialHighlights.clear();
        return;
    }
    if (!tutorialGuidance && !globalContainer->runNoX) {
        tutorialGuidance=std::make_unique<Tutorial::Guidance>([this](int destination) {
            if (destination==Tutorial::ShowDestination) {
                const auto target=Tutorial::view(drawnScene().panels.hud.state());
                if (!target || !target->destination) return;
                const int oldX=viewportX,oldY=viewportY;
                const auto [x,y]=Tutorial::destinationCentre(drawnScene(),*target->destination);
                camera.originX=x*32-camera.visibleW()/2;
                camera.originY=y*32-camera.visibleH()*.7;
                camera.normalize(); viewportX=camera.tileX(); viewportY=camera.tileY();
                viewportChanged(oldX,viewportX,oldY,viewportY);
            } else if (destination==0) {
                openDialog(IGM_TUTORIAL_RESTART, std::make_unique<InGameConfirmScreen>(
                    Glob2UI::tr("[tutorial restart]"),Glob2UI::tr("[tutorial restart body]"),
                    Glob2UI::tr("[tutorial restart]"),Glob2UI::tr("[Cancel]")));
            } else {
                tutorialDestination=destination;
                enqueueOrder(std::make_shared<PlayerQuitsGameOrder>(localPlayer));
                flushOutgoingAndExit=true;
            }
        });
        tutorialGuidance->attach(*globalContainer->gfx);
    }
    for (int id : tutorialHighlights) highlights.erase(id);
    tutorialHighlights.clear();
    for (const auto &anchor:lesson->highlights) {
        if (anchor=="palette") tutorialHighlights.insert(HighlightUnderMinimapIcon);
        else if (anchor=="workers") tutorialHighlights.insert(HighlightUnitsAssignedBar);
        else if (anchor=="production") tutorialHighlights.insert(HighlightRatioBar);
        else if(anchor=="upgrade") tutorialHighlights.insert(HighlightUpgradeButton);
        else if(anchor=="zone:guard") tutorialHighlights.insert(HighlightGuardZoneOnPanel);
        else if(anchor=="zone:forbidden") tutorialHighlights.insert(HighlightForbiddenZoneOnPanel);
        else if(anchor=="zone:clear") tutorialHighlights.insert(HighlightClearingZoneOnPanel);
        else if(anchor=="brush") tutorialHighlights.insert(HighlightBrushSelector);
        else {
            std::string name;
            if(anchor.starts_with("build:")) name=anchor.substr(6);
            else if(anchor=="flag:exploration") name="explorationflag";
            else if(anchor=="flag:war") name="warflag";
            else if(anchor=="flag:clearing") name="clearingflag";
            if(!name.empty()) tutorialHighlights.insert(int(HighlightBuildingOnPanel)+IntBuildingType::shortNumberFromType(name));
        }
    }
    highlights.insert(tutorialHighlights.begin(),tutorialHighlights.end());
    if (tutorialGuidance) {
        if (touch->usesHUD()) {
            const auto notice=touch->notificationBounds();
            const auto hud=touch->hudLayout();
            tutorialGuidance->mobileBounds({int(notice.x),int(notice.y),int(notice.w),
                std::max(0,int(hud.actions.y-notice.y-6*globalContainer->gfx->logicalUnitsPerPoint()))});
        }
        tutorialGuidance->model(lesson);
        // The mobile tools temporarily own the foreground. Cancel
        // pointer/focus ownership without changing the lesson or collapsed state.
        if (touch->obscuresTutorial())
            tutorialGuidance->cancelInteraction();
        tutorialGuidance->update(Uint32(SDL_GetTicks()));
    }
}

void Tutorial::drawDestination(GameGUI &gui)
{
    if (!gui.tutorialGuidance) return;
    const auto lesson=Tutorial::view(gui.drawnScene().panels.hud.state());
    if (!lesson || !lesson->destination) return;
    const auto &camera=gui.camera;
    const auto &target=*lesson->destination;
    const auto [x,y]=Tutorial::destinationCentre(gui.drawnScene(),target);
    const auto point=camera.worldToScreen(camera.originX+MapCamera::wrap(x*32-camera.originX,camera.mapWidth),
        camera.originY+MapCamera::wrap(y*32-camera.originY,camera.mapHeight));
    if (point.first<camera.offsetX+20 || point.first>camera.offsetX+camera.width-20 ||
        point.second<camera.offsetY+20 || point.second>camera.offsetY+camera.height-20) return;
    auto *gfx=globalContainer->gfx;
    gfx->setUITransform(); gfx->setClipRect();
    const int px=int(point.first),py=int(point.second);
    gfx->drawCircle(px,py,18,255,220,80);
    gfx->drawCircle(px,py,20,255,220,80);
    const auto label=Glob2UI::tr(target.label);
    const int labelWidth=globalContainer->standardFont->getStringWidth(label.c_str());
    // Centre the label under the ring and keep it inside the map view, clear of the sidebar.
    const int left=int(camera.offsetX)+8,right=int(camera.offsetX+camera.width)-labelWidth-8;
    gfx->drawString(std::clamp(px-labelWidth/2,left,std::max(left,right)),py+24,
        globalContainer->standardFont,label);
}
