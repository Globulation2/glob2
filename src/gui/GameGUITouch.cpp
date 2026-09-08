// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "TeamStat.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
#if defined(__IPHONEOS__)
#include "mobile/ios/SafeArea.h"
#endif
using namespace GAGCore;

GameGUITouch::GameGUITouch(GameGUI& gui) : gui(gui) {
#if defined(__ANDROID__) || defined(__IPHONEOS__)
    touchActive=true;
#endif
}
bool GameGUITouch::usesHUD() const
{
    auto* gfx=globalContainer->gfx;
    return touchActive && gfx->hasPortableRenderer() && !globalContainer->replaying &&
        (SDL_getenv("GLOB2_TOUCH_HUD") || std::min(gfx->getW(),gfx->getH())/gfx->logicalUnitsPerPoint()<600);
}
MobileLayout GameGUITouch::layout() const
{
    auto* gfx=globalContainer->gfx;
    const double unit=gfx->logicalUnitsPerPoint();
    SafeInsets insets;
#if defined(__IPHONEOS__)
    insets=iosGameSafeInsets(SDL_GetWindowFromID(gfx->windowID()));
#endif
    auto result=MobileLayout::calculate(gfx->getW()/unit,gfx->getH()/unit,insets,0,1,panelOpen);
    for (auto* rect : {&result.safe,&result.status,&result.world,&result.actions,&result.panel}) {
        rect->x*=unit; rect->y*=unit; rect->w*=unit; rect->h*=unit;
    }
    return result;
}
double GameGUITouch::panelScale() const { return 1.75*globalContainer->gfx->logicalUnitsPerPoint(); }
ViewPoint GameGUITouch::panelOrigin() const
{
    const auto panel=layout().panel;
    return {panel.x+(panel.w-RIGHT_MENU_WIDTH*panelScale())/2, panel.y-panelScroll*panelScale()};
}
void GameGUITouch::clampScroll()
{
    panelScroll=std::clamp(panelScroll,0.0,std::max(0.0,globalContainer->gfx->getH()-layout().panel.h/panelScale()));
}
GameGUITouch::~GameGUITouch() = default;
ViewRect GameGUITouch::world() const
{
    if (usesHUD()) return layout().world;
    return {0,16,double(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH),double(globalContainer->gfx->getH()-16)};
}
ViewRect GameGUITouch::controls() const
{
    if (usesHUD()) return layout().actions;
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
        return point.x<controls().x+controls().w/2 ? 1 : 2;
    if (usesHUD()) {
        if (layout().actions.contains(point)) return 10+std::min(5,int((point.x-layout().actions.x)/(layout().actions.w/6)));
        if (layout().panel.contains(point)) return 3;
        if (tutorialRect().contains(point)) return 7;
        return world().contains(point) ? 0 : 6;
    }
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
            if (usesHUD() && ownerRegion==7 && action.kind==TouchActionKind::Pan) {
                const double unit=globalContainer->gfx->logicalUnitsPerPoint();
                tutorialScroll=std::clamp(tutorialScroll-point.y/unit,0.0,
                    std::max(0.0,tutorialLines.size()*24.0-tutorialRect().h/unit+64));
            }
            if (usesHUD() && ownerRegion==3 && action.kind==TouchActionKind::Pan) {
                panelScroll-=point.y/panelScale(); clampScroll();
            }
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
        if (point.x<controls().x+controls().w/2) {
            if (preview && previewType==gui.toolManager.getBuildingName() && !globalContainer->replaying) {
                const auto cursor=previewCursor();
                if (gui.toolManager.confirmBuilding(int(cursor.x),int(cursor.y),gui.localTeamNo,gui.viewportX,gui.viewportY)) {
                    preview.reset(); gui.clearSelection();
                }
            }
        } else { preview.reset(); gui.clearSelection(); }
        return;
    }
    if (usesHUD() && !gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText && tutorialRect().contains(point)
        && !layout().panel.contains(point)) {
        if (gui.swallowSpaceKey) { SDL_Keysym key{}; key.sym=SDLK_SPACE; gui.handleKey(key,true); }
        return;
    }
    const bool hudInput=usesHUD() && !gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText;
    if (hudInput && layout().actions.contains(point)) {
        const int button=std::min(5,int((point.x-layout().actions.x)/(layout().actions.w/6)));
        if (button<3) {
            if ((button==0 && (gui.hiddenGUIElements & GameGUI::HIDABLE_BUILDINGS_LIST)) ||
                (button==1 && (gui.hiddenGUIElements & GameGUI::HIDABLE_FLAGS_LIST))) return;
            if (button<2) {
                const auto mode=button==0 ? GameGUI::CONSTRUCTION_VIEW : GameGUI::FLAG_VIEW;
                panelOpen=!(panelOpen && gui.displayMode==mode && gui.selectionMode==GameGUI::NO_SELECTION);
                gui.clearSelection(); gui.displayMode=mode;
            } else {
                panelOpen=!panelOpen || (gui.selectionMode==GameGUI::NO_SELECTION && gui.displayMode!=GameGUI::STAT_TEXT_VIEW);
                if (gui.selectionMode==GameGUI::NO_SELECTION) gui.displayMode=GameGUI::STAT_TEXT_VIEW;
            }
            panelScroll=144; clampScroll(); return;
        }
        // Route the visible Menu/Objectives/Alliance buttons through existing actions.
        point={double(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH),
               double(button==3 ? IGM_OBJECTIVES_ICON_Y+10 : button==4 ? IGM_ALLIANCE_ICON_Y+10 : 10)};
    } else if (hudInput && layout().panel.contains(point)) {
        const auto origin=panelOrigin();
        point={globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+(point.x-origin.x)/panelScale(),
               (point.y-origin.y)/panelScale()};
        if (point.x<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH || point.x>=globalContainer->gfx->getW()) return;
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
    if (usesHUD() && (gui.selectionMode==GameGUI::TOOL_SELECTION || gui.selectionMode==GameGUI::BRUSH_SELECTION)) panelOpen=false;
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
    if (usesHUD() && gui.selectionMode!=GameGUI::NO_SELECTION) { panelOpen=true; panelScroll=144; }
}

void GameGUITouch::prepareDraw()
{
    if (!touchActive) return;
    if (usesHUD()) { clampScroll(); prepareTutorial(); }
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
    gfx->drawFilledRect(int(rect.x),int(rect.y),half,int(rect.h),Color(preview ? 35 : 55,preview ? 90 : 55,45,245));
    gfx->drawFilledRect(int(rect.x)+half,int(rect.y),int(rect.w)-half,int(rect.h),Color(100,35,35,245));
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
        gfx->drawSurface(int(rect.x)+index*half+(half-width)/2,int(rect.y)+(int(rect.h)-height)/2,width,height,label);
    }
    gfx->setClipRect();
}

void GameGUITouch::drawPanel()
{
    if (!usesHUD()) return;
    const auto panel=layout().panel;
    if (panel.w<=0 || panel.h<=0) return;
    auto* gfx=globalContainer->gfx;
    gfx->setClipRect();
    gfx->drawFilledRect(int(panel.x),int(panel.y),int(panel.w),int(panel.h),Color(12,18,26,245));
    const auto origin=panelOrigin();
    SDL_Rect clip{int(panel.x),int(panel.y),int(panel.w),int(panel.h)};
    gfx->setUITransform(panelScale(),origin.x-(gfx->getW()-RIGHT_MENU_WIDTH)*panelScale(),origin.y,&clip);
    const size_t arrows=gui.arrowPositions.size();
    gui.drawPanel(); gfx->setClipRect();
    for (size_t i=arrows;i<gui.arrowPositions.size();++i) {
        const auto& arrow=gui.arrowPositions[i];
        gfx->drawSprite(arrow.x,arrow.y,globalContainer->gamegui,arrow.sprite);
    }
    gui.arrowPositions.erase(gui.arrowPositions.begin()+arrows,gui.arrowPositions.end());
    gui.minimap.draw(gui.localTeamNo,gui.viewportX+int(world().x/32),gui.viewportY+int(world().y/32),int(world().w/32),int(world().h/32));
    gfx->setUITransform(); gfx->setClipRect();
    // Visible scrollbar communicates that lower controls remain available.
    const double content=gfx->getH()*panelScale(), thumb=panel.h*panel.h/content;
    gfx->drawFilledRect(int(panel.x+panel.w-4),int(panel.y+panelScroll*panelScale()*panel.h/content),3,int(thumb),Color(170,185,190));
}

void GameGUITouch::drawHUD()
{
    if (!usesHUD() || gui.inGameMenu || gui.typingInputScreen || gui.scrollableText) return;
    auto* gfx=globalContainer->gfx;
    const auto ui=layout(); const double unit=gfx->logicalUnitsPerPoint();
    gfx->setClipRect();
    gfx->drawFilledRect(int(ui.status.x),int(ui.status.y),int(ui.status.w),int(ui.status.h),Color(12,18,26,240));
    // Existing unit artwork with readable free/total counts, in window-point sizing.
    globalContainer->unitmini->setBaseColor(gui.localTeam->color);
    for (int i=0;i<3;++i) {
        const double x=ui.status.x+i*ui.status.w/3;
        SDL_Rect clip{int(x),int(ui.status.y),int(ui.status.w/3),int(ui.status.h)};
        gfx->setUITransform(1.5*unit,x+4*unit,ui.status.y+14*unit,&clip);
        gfx->drawSprite(0,0,globalContainer->unitmini,i);
        const int free=gui.teamStats->getFreeUnits(i)-(i==0 ? gui.teamStats->getWorkersNeeded() : 0);
        gfx->drawString(22,0,globalContainer->littleFont,std::to_string(free)+"/"+std::to_string(gui.teamStats->getTotalUnits(i)));
        gfx->setUITransform(); gfx->setClipRect();
    }
    drawTutorial();
    drawPanel();
    if (gui.selectionMode==GameGUI::TOOL_SELECTION) return; // Confirm/Cancel owns this strip.
    const int icons[]={1,29,3,47,45,6};
    for (int i=0;i<6;++i) {
        const double x=ui.actions.x+i*ui.actions.w/6, width=ui.actions.w/6;
        gfx->drawFilledRect(int(x),int(ui.actions.y),int(width)-1,int(ui.actions.h),Color(24,34,44,245));
        SDL_Rect clip{int(x),int(ui.actions.y),int(width),int(ui.actions.h)};
        gfx->setUITransform(unit,x+(width-32*unit)/2,ui.actions.y+8*unit,&clip);
        gfx->drawSprite(0,0,globalContainer->gamegui,icons[i]);
        gfx->setUITransform(); gfx->setClipRect();
    }
}

ViewRect GameGUITouch::tutorialRect() const
{
    if (tutorialLines.empty()) return {};
    const double unit=globalContainer->gfx->logicalUnitsPerPoint();
    auto rect=layout().world;
    rect.x+=8*unit; rect.y+=8*unit; rect.w-=16*unit;
    rect.h=std::min(rect.h*0.45,(tutorialLines.size()*24+64)*unit);
    return rect;
}
void GameGUITouch::prepareTutorial()
{
    std::string text=gui.game.sgslScript.isTextShown ? gui.game.sgslScript.textShown : "";
    if (!gui.scriptText.empty()) { if (!text.empty()) text+='\n'; text+=gui.scriptText; }
    const double width=layout().world.w/globalContainer->gfx->logicalUnitsPerPoint()-32;
    if (text==tutorialText && width==tutorialWidth) return;
    tutorialText=text; tutorialWidth=width; tutorialLines.clear(); tutorialScroll=0;
    std::string line;
    auto* font=globalContainer->standardFont;
    // Break at whitespace when possible, otherwise at UTF-8 character boundaries.
    for (size_t at=0;at<text.size();) {
        size_t end=at+1;
        while (end<text.size() && (static_cast<unsigned char>(text[end])&0xc0)==0x80) ++end;
        if (text[at]=='\n') { tutorialLines.push_back(line); line.clear(); at=end; continue; }
        const std::string next=text.substr(at,end-at);
        if (!line.empty() && font->getStringWidth(line+next)*1.2>width) {
            const auto space=line.find_last_of(' ');
            if (space!=std::string::npos) { tutorialLines.push_back(line.substr(0,space)); line.erase(0,space+1); }
            else { tutorialLines.push_back(line); line.clear(); }
        }
        line+=next; at=end;
    }
    if (!line.empty()) tutorialLines.push_back(line);
}
void GameGUITouch::drawTutorial()
{
    auto rect=tutorialRect(); if (rect.w<=0 || rect.h<=0) return;
    auto* gfx=globalContainer->gfx; const double unit=gfx->logicalUnitsPerPoint();
    gfx->drawFilledRect(int(rect.x),int(rect.y),int(rect.w),int(rect.h),Color(12,18,26,240));
    SDL_Rect clip{int(rect.x),int(rect.y),int(rect.w),int(std::max(0.0,rect.h-48*unit))};
    const size_t first=std::min(tutorialLines.size(),size_t(tutorialScroll/24));
    gfx->setUITransform(1.2*unit,rect.x+8*unit,rect.y+(8-tutorialScroll+first*24)*unit,&clip);
    const size_t end=std::min(tutorialLines.size(),first+size_t(rect.h/unit/24)+1);
    for (size_t i=first;i<end;++i)
        gfx->drawString(0,int((i-first)*20),globalContainer->standardFont,tutorialLines[i]);
    gfx->setUITransform(); gfx->setClipRect();
    const double textHeight=std::max(1.0,rect.h-48*unit), content=tutorialLines.size()*24*unit;
    if (content>textHeight) {
        gfx->drawFilledRect(int(rect.x+rect.w-3*unit),int(rect.y+tutorialScroll*unit*textHeight/content),
            std::max(1,int(2*unit)),int(textHeight*textHeight/content),Color(170,185,190));
    }
    if (gui.swallowSpaceKey) {
        gfx->drawFilledRect(int(rect.x),int(rect.y+rect.h-48*unit),int(rect.w),int(48*unit),Color(35,70,55));
        SDL_Rect footer{int(rect.x),int(rect.y+rect.h-48*unit),int(rect.w),int(48*unit)};
        gfx->setUITransform(1.2*unit,rect.x+12*unit,rect.y+rect.h-32*unit,&footer);
        gfx->drawString(0,0,globalContainer->standardFont,Toolkit::getStringTable()->getString("[ok]"));
        gfx->setUITransform(); gfx->setClipRect();
    }
}
