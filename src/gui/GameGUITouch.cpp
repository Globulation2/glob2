// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
using namespace GAGCore;

GameGUITouch::GameGUITouch(GameGUI& gui) : gui(gui) {}
GameGUITouch::~GameGUITouch() = default;
ViewRect GameGUITouch::world() const
{
    return {0,16,double(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH),double(globalContainer->gfx->getH()-16)};
}
ViewRect GameGUITouch::controls() const
{
    auto rect=world();
    rect.h=std::min(rect.h,48*globalContainer->gfx->logicalUnitsPerPoint());
    rect.y=globalContainer->gfx->getH()-rect.h;
    return rect;
}
ViewPoint GameGUITouch::previewCursor() const
{
    const auto& map=gui.game.map;
    auto wrap=[](double v,double n) { return v-std::floor(v/n)*n; };
    return {wrap(preview->x-gui.viewportX*32,map.getW()*32),wrap(preview->y-gui.viewportY*32,map.getH()*32)};
}
void GameGUITouch::cancel()
{
    gui.toolManager.cancelDrag(gui.localTeamNo);
    gesture.cancel(); fingers.clear(); preview.reset(); previewType.clear(); panX=panY=0;
}

int GameGUITouch::interfaceRegion(ViewPoint point) const
{
    if (gui.inGameMenu || gui.typingInputScreen || gui.scrollableText) return 4;
    if (gui.selectionMode==GameGUI::TOOL_SELECTION && controls().contains(point))
        return point.x<controls().w/2 ? 1 : 2;
    if (point.x>=world().w) return 3;
    if (globalContainer->replaying && point.y>=REPLAY_BAR_Y) return 5;
    return world().contains(point) ? 0 : 6;
}

bool GameGUITouch::process(SDL_Event& event)
{
    if (dispatching || !globalContainer->gfx->hasPortableRenderer()) return false;
    if ((event.type==SDL_MOUSEMOTION && event.motion.which==SDL_TOUCH_MOUSEID) ||
        ((event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP) && event.button.which==SDL_TOUCH_MOUSEID)) return true;
    if (event.type==SDL_MOUSEMOTION || event.type==SDL_MOUSEBUTTONDOWN) {
        if (touchActive) cancel();
        touchActive=false;
        return false;
    }
    if (event.type!=SDL_FINGERDOWN && event.type!=SDL_FINGERUP && event.type!=SDL_FINGERMOTION) return false;
    if (!gui.inputState.hasFocus()) return true;
    if (!fingers.empty() && (ownerSelection!=gui.selectionMode || ownerMenu!=gui.inGameMenu ||
        ownerTool!=gui.toolManager.getBuildingName() || ownerOverlay!=bool(gui.typingInputScreen || gui.scrollableText))) { cancel(); return true; }
    ViewPoint point{event.tfinger.x*globalContainer->gfx->getW(),event.tfinger.y*globalContainer->gfx->getH()};
    const auto key=std::make_pair(event.tfinger.touchId,event.tfinger.fingerId);
    if (event.type==SDL_FINGERDOWN) {
        if (fingers.empty()) {
            touchActive=true;
            gui.viewportSpeedX=gui.viewportSpeedY=0;
            gui.lastMouseButtonState=0; gui.selectionPushed=gui.panPushed=gui.miniMapPushed=false;
            scale=globalContainer->gfx->logicalUnitsPerPoint();
            ownerRegion=interfaceRegion(point);
            interfaceGesture=ownerRegion!=0;
            ownerSelection=gui.selectionMode; ownerMenu=gui.inGameMenu;
            ownerOverlay=gui.typingInputScreen || gui.scrollableText;
            ownerTool=gui.toolManager.getBuildingName();
            gesture.setMode(interfaceGesture ? TouchMode::Navigate :
                gui.selectionMode==GameGUI::TOOL_SELECTION ? TouchMode::Placement :
                gui.selectionMode==GameGUI::BRUSH_SELECTION ? TouchMode::Paint : TouchMode::Navigate);
        }
        if (std::find(fingers.begin(),fingers.end(),key)==fingers.end()) fingers.push_back(key);
        actions(gesture.down(key.first,key.second,{point.x/scale,point.y/scale}));
    } else if (event.type==SDL_FINGERMOTION) {
        actions(gesture.move(key.first,key.second,{point.x/scale,point.y/scale}));
    } else {
        actions(gesture.up(key.first,key.second,{point.x/scale,point.y/scale}));
        std::erase(fingers,key);
    }
    return true;
}

void GameGUITouch::actions(const std::vector<TouchAction>& changes)
{
    for (const auto& action : changes) {
        const ViewPoint point{action.point.x*scale,action.point.y*scale};
        if (action.kind==TouchActionKind::Cancel) {
            preview.reset(); gui.toolManager.cancelDrag(gui.localTeamNo); continue;
        }
        if (interfaceGesture) {
            if (action.kind==TouchActionKind::Select && interfaceRegion(point)==ownerRegion) interfaceTap(point);
            continue;
        }
        if (action.kind==TouchActionKind::Pan) {
            panX-=point.x; panY-=point.y;
            const int dx=int(panX/32), dy=int(panY/32);
            panX-=dx*32; panY-=dy*32;
            const int oldX=gui.viewportX, oldY=gui.viewportY;
            gui.viewportX=(oldX+dx)&gui.game.map.getMaskW();
            gui.viewportY=(oldY+dy)&gui.game.map.getMaskH();
            gui.moveParticles(oldX,gui.viewportX,oldY,gui.viewportY);
        } else if (action.kind==TouchActionKind::Preview && world().contains(point) && !controls().contains(point)) {
            preview=ViewPoint{point.x+gui.viewportX*32,point.y+gui.viewportY*32};
            previewType=gui.toolManager.getBuildingName();
            prepareDraw();
        } else if (action.kind==TouchActionKind::Select && world().contains(point)) {
            select(point);
        } else if (!globalContainer->replaying && gui.selectionMode==GameGUI::BRUSH_SELECTION) {
            if (action.kind==TouchActionKind::BeginStroke && world().contains(point))
                gui.toolManager.handleMouseDown(int(point.x),int(point.y),gui.localTeamNo,gui.viewportX,gui.viewportY);
            else if (action.kind==TouchActionKind::Stroke && world().contains(point))
                gui.toolManager.handleMouseDrag(int(point.x),int(point.y),gui.localTeamNo,gui.viewportX,gui.viewportY);
            else if (action.kind==TouchActionKind::EndStroke)
                gui.toolManager.cancelDrag(gui.localTeamNo);
        }
    }
}

void GameGUITouch::interfaceTap(ViewPoint point)
{
    if (!gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText && gui.selectionMode==GameGUI::TOOL_SELECTION && controls().contains(point)) {
        if (point.x<controls().w/2) {
            if (preview && previewType==gui.toolManager.getBuildingName() && !globalContainer->replaying) {
                const auto cursor=previewCursor();
                if (gui.toolManager.confirmBuilding(int(cursor.x),int(cursor.y),gui.localTeamNo,gui.viewportX,gui.viewportY)) {
                    preview.reset(); gui.clearSelection();
                }
            }
        } else { preview.reset(); gui.clearSelection(); }
        return;
    }
    // Some shared panel hit tests read the current cursor instead of the event.
    gui.mouseX=gui.view.mouseX=gui.lastMouseX=int(point.x);
    gui.mouseY=gui.view.mouseY=gui.lastMouseY=int(point.y);
    dispatching=true;
    SDL_Event click{}; click.type=SDL_MOUSEBUTTONDOWN; click.button.button=SDL_BUTTON_LEFT;
    click.button.x=int(point.x); click.button.y=int(point.y);
    gui.processEvent(&click);
    click.type=SDL_MOUSEBUTTONUP; gui.processEvent(&click);
    dispatching=false;
    gui.lastMouseButtonState=0; gui.selectionPushed=gui.panPushed=gui.miniMapPushed=false;
}

void GameGUITouch::select(ViewPoint point)
{
    gui.view.mouseUnit=nullptr;
    const auto& map=gui.game.map;
    const int mx=int(point.x)/32+gui.viewportX, my=int(point.y)/32+gui.viewportY;
    const Uint32 visible=globalContainer->replaying ? globalContainer->replayVisibleTeams : gui.localTeam->me;
    const bool wholeMap=globalContainer->replaying && !globalContainer->replayShowFog;
    // Match draw order: ground units first, then flying units, using their interpolated rectangles.
    for (bool air : {false,true}) for (int y=my-1;y<=my+1;++y) for (int x=mx-1;x<=mx+1;++x) {
        const Uint16 gid=air ? map.getAirUnit(x,y) : map.getGroundUnit(x,y);
        if (gid==NOGUID) continue;
        auto* unit=gui.game.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
        if (!unit) continue;
        if (!wholeMap && !map.isFOWDiscovered(x,y,visible) && !map.isFOWDiscovered(x-unit->dx,y-unit->dy,visible)) continue;
        int px,py; map.mapCaseToDisplayable(unit->posX,unit->posY,&px,&py,gui.viewportX,gui.viewportY);
        if (unit->action<BUILD) { px-=(unit->dx*(255-unit->delta))>>3; py-=(unit->dy*(255-unit->delta))>>3; }
        if (point.x>px && point.x<px+32 && point.y>py && point.y<py+32 &&
            (wholeMap || map.isFOWDiscovered(x,y,visible) || Unit::GIDtoTeam(gid)==gui.localTeamNo)) gui.view.mouseUnit=unit;
    }
    gui.handleMapClick(int(point.x),int(point.y),SDL_BUTTON_LEFT);
    gui.selectionPushed=false;
}

void GameGUITouch::prepareDraw()
{
    if (!touchActive) return;
    if (gui.selectionMode!=GameGUI::TOOL_SELECTION || previewType!=gui.toolManager.getBuildingName()) preview.reset();
    if (preview) {
        const auto cursor=previewCursor();
        gui.mouseX=gui.view.mouseX=int(cursor.x); gui.mouseY=gui.view.mouseY=int(cursor.y);
    }
}

void GameGUITouch::drawControls()
{
    if (!touchActive || gui.selectionMode!=GameGUI::TOOL_SELECTION || gui.inGameMenu || gui.typingInputScreen || gui.scrollableText) return;
    auto* gfx=globalContainer->gfx;
    auto rect=controls(); const int half=int(rect.w/2);
    gfx->setClipRect(int(rect.x),int(rect.y),int(rect.w),int(rect.h));
    gfx->drawFilledRect(0,int(rect.y),half,int(rect.h),Color(preview ? 35 : 55,preview ? 90 : 55,45,245));
    gfx->drawFilledRect(half,int(rect.y),int(rect.w)-half,int(rect.h),Color(100,35,35,245));
    if (!confirmLabel) {
        auto makeLabel=[](const char* key) {
            const std::string text=Toolkit::getStringTable()->getString(key);
            auto* font=globalContainer->menuFont;
            auto label=std::make_unique<DrawableSurface>(font->getStringWidth(text),font->getStringHeight(text));
            label->drawFilledRect(0,0,label->getW(),label->getH(),Color(0,0,0,0));
            label->drawString(0,0,font,text);
            return label;
        };
        confirmLabel=makeLabel("[ok]"); cancelLabel=makeLabel("[Cancel]");
    }
    for (int index=0;index<2;++index) {
        auto* label=index ? cancelLabel.get() : confirmLabel.get();
        const double factor=std::min(rect.h*0.45/label->getH(),half*0.8/label->getW());
        const int width=int(label->getW()*factor),height=int(label->getH()*factor);
        gfx->drawSurface(index*half+(half-width)/2,int(rect.y)+(int(rect.h)-height)/2,width,height,label);
    }
    gfx->setClipRect();
}
