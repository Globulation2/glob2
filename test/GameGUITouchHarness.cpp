// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <SDL_net.h>
#include <cstdio>
#include <stdexcept>

GlobalContainer* globalContainer=nullptr;
static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
class GameGUITouchHarness
{
public:
    static void run()
    {
        GameGUI gui;
        auto map=Engine::loadMapHeader("maps/balanced.map");
        GameHeader players; players.setNumberOfPlayers(1);
        players.getBasePlayer(0)=BasePlayer(0,"Touch",0,BasePlayer::P_LOCAL);
        require(gui.loadFromHeaders(map,players,true,true),"Fixture load failed");
        gui.localTeamNo=0; gui.localPlayer=0; gui.adjustLocalTeam();
        gui.viewportX=gui.viewportY=0;
        const auto checksum=gui.game.checkSum();
        auto finger=[&](Uint32 type,int id,float x,float y) {
            SDL_Event event{};event.type=type;event.tfinger.touchId=7;event.tfinger.fingerId=id;
            event.tfinger.x=x/globalContainer->gfx->getW();event.tfinger.y=y/globalContainer->gfx->getH();
            gui.processEvent(&event);
        };
        auto tap=[&](float x,float y) { finger(SDL_FINGERDOWN,1,x,y);finger(SDL_FINGERUP,1,x,y); };
        auto flag=[&] { gui.setSelection(GameGUI::TOOL_SELECTION,const_cast<char*>("warflag")); };
        auto noOrder=[&] { require(!gui.toolManager.getOrder(),"Navigation or preview emitted a tool order"); };
        tap(760,208);
        require(gui.selectionMode==GameGUI::TOOL_SELECTION && gui.toolManager.getBuildingName()=="inn",
            "A tool must be selectable from its real sidebar hit area without mouse hover");noOrder();
        gui.clearSelection();
        finger(SDL_FINGERDOWN,1,160,160);finger(SDL_FINGERMOTION,1,166,160);finger(SDL_FINGERUP,1,166,160);
        require(gui.viewportX==0,"Sub-threshold movement must not pan");
        finger(SDL_FINGERDOWN,1,160,160);finger(SDL_FINGERMOTION,1,224,160);finger(SDL_FINGERUP,1,224,160);
        require(gui.viewportX==gui.game.map.getW()-2,"Dragging must pan across the toroidal seam");
        flag(); tap(200,200); noOrder();
        require(gui.touch->hasPreview(),"Placement tap must retain a preview");
        const int before=gui.viewportX;
        finger(SDL_FINGERDOWN,1,200,200);finger(SDL_FINGERDOWN,2,300,200);
        finger(SDL_FINGERMOTION,1,264,200);finger(SDL_FINGERMOTION,2,364,200);
        finger(SDL_FINGERUP,2,364,200);finger(SDL_FINGERUP,1,264,200);
        require(gui.viewportX==((before-2)&gui.game.map.getMaskW()),"Two fingers must pan while placing"); noOrder();
        tap(100,576);
        auto order=std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
        require(bool(order),"Confirmation must emit the shared create order");
        require(gui.selectionMode==GameGUI::NO_SELECTION,"Confirmed placement must exit preview mode"); noOrder();
        flag();tap(200,200);tap(480,576);noOrder();
        require(gui.selectionMode==GameGUI::NO_SELECTION,"Cancel must exit placement");
        flag();tap(220,220);
        // The first point is in Confirm, the second just outside the strip.
        finger(SDL_FINGERDOWN,1,100,554);finger(SDL_FINGERUP,1,100,550);noOrder();
        require(gui.touch->hasPreview(),"Crossing a control boundary must not place or cancel");
        gui.suspendInput();tap(100,576);noOrder();
        require(!gui.touch->hasPreview(),"Suspension must invalidate placement confirmation");
        tap(220,220);gui.localTeam->noMoreBuildingSitesCountdown=1;tap(100,576);noOrder();
        require(gui.touch->hasPreview(),"Failed validation must retain the preview");
        gui.localTeam->noMoreBuildingSitesCountdown=0;
        gui.suspendInput();flag();finger(SDL_FINGERDOWN,1,200,200);
        gui.clearSelection();finger(SDL_FINGERUP,1,200,200);noOrder();
        require(!gui.touch->hasPreview(),"Mode changes must cancel an owned gesture");
        flag();tap(200,200);
        SDL_Event synthetic{};synthetic.type=SDL_MOUSEBUTTONUP;synthetic.button.which=SDL_TOUCH_MOUSEID;
        synthetic.button.button=SDL_BUTTON_LEFT;synthetic.button.x=200;synthetic.button.y=200;
        gui.step({synthetic},SDL_GetTicks64());
        require(gui.getOrder()->getOrderType()==ORDER_NULL,"Synthesized mouse release must not place");noOrder();
        gui.suspendInput();
        gui.setSelection(GameGUI::BRUSH_SELECTION);
        gui.toolManager.activateZoneTool(GameGUIToolManager::Forbidden); gui.brush.defaultSelection();
        finger(SDL_FINGERDOWN,1,200,200);finger(SDL_FINGERMOTION,1,232,200);
        finger(SDL_FINGERDOWN,2,300,200);
        require(bool(std::dynamic_pointer_cast<OrderAlterArea>(gui.toolManager.getOrder())),"Second finger must finish the current paint stroke");noOrder();
        finger(SDL_FINGERMOTION,1,296,200);finger(SDL_FINGERUP,2,300,200);finger(SDL_FINGERUP,1,296,200);noOrder();
        finger(SDL_FINGERDOWN,1,240,240);gui.suspendInput();
        require(bool(std::dynamic_pointer_cast<OrderAlterArea>(gui.toolManager.getOrder())),"Suspension must flush the existing brush stroke");
        finger(SDL_FINGERUP,1,240,240);noOrder();
        flag();tap(220,220);globalContainer->replaying=true;tap(100,576);noOrder();globalContainer->replaying=false;
        gui.suspendInput();flag();tap(200,200);
        gui.viewportResized(800,600,600,800);tap(100,576);noOrder();
        require(!gui.touch->hasPreview(),"Rotation must invalidate the preview");
        finger(SDL_FINGERDOWN,1,200,200);
        SDL_Event focus{};focus.type=SDL_WINDOWEVENT;focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
        gui.processEvent(&focus);finger(SDL_FINGERUP,1,200,200);noOrder();
        require(!gui.touch->hasPreview(),"Focus loss must clear owned pointers");
        focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;gui.processEvent(&focus);
        tap(200,200);
        gui.drawAll(0);
        globalContainer->gfx->printScreen("touch-placement.bmp");globalContainer->gfx->nextFrame();
        auto* capture=SDL_LoadBMP((std::string(SDL_getenv("GLOB2_USER_DATA_DIR"))+"/touch-placement.bmp").c_str());
        require(capture && capture->format->BytesPerPixel==4,"Placement screenshot must be captured");
        Uint8 r,g,b;
        const auto pixel=static_cast<Uint32*>(static_cast<void*>(static_cast<char*>(capture->pixels)+(capture->h-10)*capture->pitch))[10];
        SDL_GetRGB(pixel,capture->format,&r,&g,&b);
        SDL_FreeSurface(capture);
        require(g>r+20,"Confirm must be visibly drawn over the world");
        require(gui.game.checkSum()==checksum,"Touch navigation and queued orders must not mutate simulation state");
        gui.game.map.setMapDiscovered(); // Expose terrain for this rendering fixture only.
        const auto hudChecksum=gui.game.checkSum();
        gui.clearSelection(); gui.suspendInput();
        SDL_setenv("GLOB2_TOUCH_HUD","1",1);
        auto* gfx=globalContainer->gfx;
        SDL_setenv("GLOB2_RESPONSIVE_UI","1",1);
        gfx->setResponsiveViewport(true,800,600);
        for (auto [width,height] : {std::pair{320,568}, {568,320}}) {
            const int oldW=gfx->getW(),oldH=gfx->getH();
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resize{}; resize.type=SDL_WINDOWEVENT; resize.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resize);
            gui.viewportResized(oldW,oldH,gfx->getW(),gfx->getH());
            tap(100,200); // Activate touch after the resize cancellation.
            const float unit=gfx->logicalUnitsPerPoint();
            gui.clearSelection(); gui.displayMode=GameGUI::FLAG_VIEW;
            tap(gfx->getW()/12.0f,gfx->getH()-24*unit);
            require(gui.touch->usesHUD(),"Phone HUD must be active");
            require(gui.displayMode==GameGUI::CONSTRUCTION_VIEW,"Visible Build control routes to construction");
            gui.drawAll(0);
            gfx->printScreen(width<height ? "touch-hud-portrait.bmp" : "touch-hud-landscape.bmp");
            gfx->nextFrame();
            int targetX,targetY,centerX,centerY;
            gui.minimap.convertToMap(gfx->getW()-80,64,targetX,targetY);
            gui.minimapMouseToPos(gfx->getW()-80,64,&centerX,&centerY,true);
            const auto world=gui.touch->worldBounds();
            require(centerX==targetX-int((world.x+world.w/2)/32) &&
                    centerY==targetY-int((world.y+world.h/2)/32),
                    "Minimap navigation centers the visible world, excluding HUD margins");
            auto ui=GAGCore::MobileLayout::calculate(width,height,{},0,1,true);
            const float panelX=(ui.panel.x+(ui.panel.w-280)/2)*unit;
            const int cameraX=gui.viewportX,cameraY=gui.viewportY;
            finger(SDL_FINGERDOWN,1,(ui.panel.x+8)*unit,(ui.panel.y+70)*unit);
            finger(SDL_FINGERMOTION,1,(ui.panel.x+8)*unit,(ui.panel.y+30)*unit);
            finger(SDL_FINGERUP,1,(ui.panel.x+8)*unit,(ui.panel.y+30)*unit);
            require(gui.viewportX==cameraX && gui.viewportY==cameraY,"Panel scrolling must not pan the world"); noOrder();
            tap(panelX+120*1.75f*unit,(ui.panel.y+(208-144)*1.75f-40)*unit);
            require(gui.selectionMode==GameGUI::TOOL_SELECTION && gui.toolManager.getBuildingName()=="inn",
                    "Enlarged panel hit-testing must select the same building after rotation"); noOrder();
            gui.clearSelection();
            gui.scriptText="Build an inn to feed your workers. Drag the panel to find more buildings. "
                "Select a building, choose a location, and confirm when you are ready.\n"
                "This long instruction remains readable after rotating the phone.";
            gui.swallowSpaceKey=true; gui.setIsSpaceSet(false);
            gui.drawAll(0);
            gfx->printScreen(width<height ? "touch-tutorial-portrait.bmp" : "touch-tutorial-landscape.bmp");
            gfx->nextFrame();
            finger(SDL_FINGERDOWN,1,30*unit,80*unit);
            finger(SDL_FINGERMOTION,1,30*unit,60*unit);
            finger(SDL_FINGERUP,1,30*unit,60*unit);
            require(!gui.isSpaceSet(),"Scrolling tutorial text must not acknowledge it");
            tap(30*unit,80*unit);
            require(gui.isSpaceSet(),"Tutorial touch acknowledgment uses the shared Space action");
            gui.scriptText.clear(); gui.swallowSpaceKey=false;
        }
        require(gui.game.checkSum()==hudChecksum,"HUD interaction must not mutate the simulation");
        const int type=globalContainer->buildingsTypes.getTypeNum("inn",0,false);
        auto* building=new Building(0,0,2,type,gui.localTeam,&globalContainer->buildingsTypes,1,1);
        gui.localTeam->myBuildings[2]=building;
        require(building->type->maxUnitWorking>0,"Allocation fixture must accept workers");
        auto* rangeFlag=new Building(0,0,3,globalContainer->buildingsTypes.getTypeNum("warflag",0,false),
            gui.localTeam,&globalContainer->buildingsTypes,1,1);
        gui.localTeam->myBuildings[3]=rangeFlag;
        require(rangeFlag->type->defaultUnitStayRange && rangeFlag->type->maxUnitWorking,"Flag fixture needs range and workers");
        gui.orderQueue.clear();
        for (auto [width,height] : {std::pair{320,568}, {568,320}}) {
            const int oldW=gfx->getW(),oldH=gfx->getH();
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resized{};resized.type=SDL_WINDOWEVENT;resized.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resized);
            gui.viewportResized(oldW,oldH,gfx->getW(),gfx->getH());
            const float unit=gfx->logicalUnitsPerPoint();
            gui.setSelection(GameGUI::BUILDING_SELECTION,building);
            // Info opens the inspector for the selected entity.
            tap(gfx->getW()*2.5f/6,gfx->getH()-24*unit);
            const auto ui=GAGCore::MobileLayout::calculate(width,height,{},0,1,true);
            const float plusX=(ui.panel.x+ui.panel.w-24)*unit, rowY=(ui.panel.y+72)*unit;
            const int before=gui.displayedMaxUnitWorking(*building), authoritative=building->maxUnitWorking;
            const auto simulation=gui.game.checkSum();
            tap(plusX,rowY); tap(plusX,rowY);
            require(gui.orderQueue.size()==2,"Two allocation taps must queue exactly two orders");
            auto first=std::dynamic_pointer_cast<OrderModifyBuilding>(gui.orderQueue.front());gui.orderQueue.pop_front();
            auto second=std::dynamic_pointer_cast<OrderModifyBuilding>(gui.orderQueue.front());gui.orderQueue.pop_front();
            require(first && second && first->gid==building->gid && first->numberRequested==before+1 && second->numberRequested==before+2,
                    "Rapid allocation taps use pending values and the shared order format");
            require(building->maxUnitWorking==authoritative && gui.game.checkSum()==simulation,
                    "Allocation UI must not change authoritative simulation state");
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-allocation-portrait.bmp" : "touch-allocation-landscape.bmp");gfx->nextFrame();
            gui.requestWorkerAllocation(*building,MAX_UNIT_WORKING);gui.orderQueue.clear();
            tap(plusX,rowY);require(gui.orderQueue.empty(),"Allocation at maximum must not queue duplicates");
            finger(SDL_FINGERDOWN,1,plusX,rowY);
            gui.clearSelection();finger(SDL_FINGERUP,1,plusX,rowY);
            require(gui.orderQueue.empty(),"Selection changes cancel held allocation gestures");
            gui.setSelection(GameGUI::BUILDING_SELECTION,building);
            gui.requestWorkerAllocation(*building,0);gui.orderQueue.clear();
            tap((ui.panel.x+24)*unit,rowY);require(gui.orderQueue.empty(),"Allocation at zero must not queue duplicates");
            tap((ui.panel.x+ui.panel.w*.75f)*unit,(ui.panel.y+24)*unit);
            for (int priority : {-1,0,1}) {
                tap((ui.panel.x+ui.panel.w*(priority+1.5f)/3)*unit,rowY);
                require(gui.orderQueue.size()==1,"Priority tap emits exactly one order");
                auto order=std::dynamic_pointer_cast<OrderChangePriority>(gui.orderQueue.front());gui.orderQueue.clear();
                require(order && order->gid==building->gid && order->priority==priority,"Priority uses the shared order format");
                tap((ui.panel.x+ui.panel.w*(priority+1.5f)/3)*unit,rowY);
                require(gui.orderQueue.empty(),"Selected pending priority is a no-op");
            }
            require(gui.game.checkSum()==simulation,"Priority changes stay outside authoritative state");
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-priority-portrait.bmp" : "touch-priority-landscape.bmp");gfx->nextFrame();
            gui.setSelection(GameGUI::BUILDING_SELECTION,rangeFlag);
            tap((ui.panel.x+ui.panel.w*5/6)*unit,(ui.panel.y+24)*unit);
            const int rangeBefore=gui.displayedUnitStayRange(*rangeFlag);
            const auto rangeChecksum=gui.game.checkSum();
            tap(plusX,rowY);tap(plusX,rowY);
            require(gui.orderQueue.size()==2,"Rapid range taps queue two orders");
            for (int delta : {1,2}) {
                auto order=std::dynamic_pointer_cast<OrderModifyFlag>(gui.orderQueue.front());gui.orderQueue.pop_front();
                require(order && order->gid==rangeFlag->gid && order->range==rangeBefore+delta,"Range uses pending values and shared orders");
            }
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-range-portrait.bmp" : "touch-range-landscape.bmp");gfx->nextFrame();
            gui.requestFlagRange(*rangeFlag,rangeFlag->type->maxUnitStayRange);gui.orderQueue.clear();
            tap(plusX,rowY);require(gui.orderQueue.empty(),"Maximum range is a no-op");
            gui.requestFlagRange(*rangeFlag,0);gui.orderQueue.clear();
            tap((ui.panel.x+24)*unit,rowY);require(gui.orderQueue.empty(),"Zero range is a no-op");
            finger(SDL_FINGERDOWN,1,plusX,rowY);
            gui.setSelection(GameGUI::BUILDING_SELECTION,building);
            finger(SDL_FINGERUP,1,plusX,rowY);
            require(gui.orderQueue.empty(),"Changing selected buildings cancels held range controls");
            require(gui.game.checkSum()==rangeChecksum,"Range controls preserve authoritative state");
            tap((ui.panel.x+ui.panel.w*.25f)*unit,(ui.panel.y+24)*unit);
            tap(gfx->getW()*5.5f/6,gfx->getH()-24*unit);
            require(gui.inGameMenu==GameGUI::IGM_MAIN,"Toolbar opens the in-game pause menu");
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-pause-portrait.bmp" : "touch-pause-landscape.bmp");gfx->nextFrame();
            const int columns=width>height ? 2 : 1, rows=(5+columns-1)/columns;
            const float dialogW=std::min(560,width-24), buttonW=(dialogW-(columns-1)*8)/columns;
            const float startX=(width-dialogW)/2, startY=(height-(rows*56+(rows-1)*8))/2;
            const float returnX=(startX+(4%columns)*(buttonW+8)+buttonW/2)*unit;
            const float returnY=(startY+(4/columns)*64+28)*unit;
            finger(SDL_FINGERDOWN,1,returnX,returnY);
            finger(SDL_FINGERMOTION,1,returnX+20*unit,returnY);
            finger(SDL_FINGERUP,1,returnX+20*unit,returnY);
            require(gui.inGameMenu==GameGUI::IGM_MAIN,"A dragged pause button must not activate");
            SDL_Event mouse{};mouse.type=SDL_MOUSEBUTTONDOWN;mouse.button.button=SDL_BUTTON_LEFT;
            mouse.button.x=int(returnX);mouse.button.y=int(returnY);
            gui.processEvent(&mouse);mouse.type=SDL_MOUSEBUTTONUP;gui.processEvent(&mouse);
            require(gui.inGameMenu==GameGUI::IGM_MAIN,"Switching to mouse cannot activate unseen desktop-menu geometry");
            finger(SDL_FINGERDOWN,1,returnX,returnY);
            finger(SDL_FINGERDOWN,2,returnX,returnY);
            finger(SDL_FINGERUP,2,returnX,returnY);
            finger(SDL_FINGERUP,1,returnX,returnY);
            require(gui.inGameMenu==GameGUI::IGM_MAIN,"Switching back to touch consumes the whole gesture before accepting an action");
            gui.drawAll(0);gfx->nextFrame();
            tap(returnX,returnY);
            require(gui.inGameMenu==GameGUI::IGM_NONE && gui.orderQueue.empty(),"Return resumes without leaking a world order");
            tap(gfx->getW()*2.5f/6,gfx->getH()-24*unit); // Close the inspector before the next orientation.
            gui.clearSelection();
        }

    }
};
int main()
{
    if(!SDL_getenv("GLOB2_USER_DATA_DIR")) return 2;
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    try {
        globalContainer=new GlobalContainer("glob2-touch-test");
        globalContainer->settings.screenWidth=800;globalContainer->settings.screenHeight=600;
        globalContainer->settings.screenFlags=GAGCore::GraphicContext::PORTABLEGPU;
        globalContainer->settings.mute=true;globalContainer->load();
        require(SDLNet_Init()==0,"SDL networking init failed");
        GameGUITouchHarness::run();
        delete globalContainer;globalContainer=nullptr;SDLNet_Quit();
        std::puts("PASS: actual gameplay touch, toroidal pan, preview, confirmation, validation, cancellation and duplicate suppression");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
