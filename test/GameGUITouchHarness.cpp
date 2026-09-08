// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include <GUITextInput.h>
#include <GUISelector.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "GlobalContainer.h"
#include "Order.h"
#include <SDL_net.h>
#include <cstdio>
#include <cstring>
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
            tap((ui.panel.x+ui.panel.w*3/8)*unit,(ui.panel.y+24)*unit);
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
            tap((ui.panel.x+ui.panel.w/2)*unit,(ui.panel.y+24)*unit);
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
            tap((ui.panel.x+ui.panel.w/8)*unit,(ui.panel.y+24)*unit);
            tap(gfx->getW()*5.5f/6,gfx->getH()-24*unit);
            require(gui.inGameMenu==GameGUI::IGM_MAIN,"Toolbar opens the in-game pause menu");
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-pause-portrait.bmp" : "touch-pause-landscape.bmp");gfx->nextFrame();
            const auto menuRows=gui.touch->dialogRows;
            const auto back=std::find_if(menuRows.begin(),menuRows.end(),[](const auto& row) { return row.footer; });
            require(back!=menuRows.end(),"Phone menu keeps Return fixed and reachable");
            const float returnX=back->rect.x+back->rect.w/2, returnY=back->rect.y+back->rect.h/2;
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

        auto fixture=[&](const char* name,int slot) {
            auto* b=new Building(0,0,slot,globalContainer->buildingsTypes.getTypeNum(name,0,false),
                gui.localTeam,&globalContainer->buildingsTypes,1,1);
            gui.localTeam->myBuildings[slot]=b;return b;
        };
        auto* swarm=fixture("swarm",4);
        auto* clearing=fixture("clearingflag",5);
        auto* exploring=fixture("explorationflag",6);
        auto* wall=fixture("stonewall",7);
        auto tab=[&](int id) {
            gui.drawAll(0);gfx->nextFrame();
            const auto tabs=gui.touch->allocationTabs(); const auto r=gui.touch->allocationRect();
            auto i=std::find(tabs.begin(),tabs.end(),id);require(i!=tabs.end(),"Requested inspector tab exists");
            require(r.w/tabs.size()>=48*gfx->logicalUnitsPerPoint(),"Inspector tabs retain 48-point targets");
            tap(r.x+(std::distance(tabs.begin(),i)+.5)*r.w/tabs.size(),r.y+24*gfx->logicalUnitsPerPoint());
            require(gui.touch->activeAllocationTab()==id,"Visible inspector tab responds to touch");
        };
        auto openActions=[&](Building* b) {
            gui.setSelection(GameGUI::BUILDING_SELECTION,b);gui.touch->panelOpen=true;tab(3);gui.orderQueue.clear();
        };
        auto actionPoint=[&](int kind,int value,int side=0) {
            for (int attempt=0;attempt<30;++attempt) {
                gui.drawAll(0);gfx->nextFrame();
                const auto rows=gui.touch->buildingActions();
                const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.kind==kind && row.value==value;});
                require(found!=rows.end(),"Building action must be available");
                const auto r=gui.touch->panelContent();const double u=gfx->logicalUnitsPerPoint();
                const double top=r.y+(std::distance(rows.begin(),found)*56-gui.touch->actionScroll)*u;
                if (top>=r.y && top+48*u<=r.y+r.h)
                    return GAGCore::ViewPoint{side<0 ? r.x+24*u : side>0 ? r.x+r.w-24*u : r.x+r.w/2,top+24*u};
                const float x=r.x+r.w/2,y=r.y+r.h/2;
                const float delta=(top<r.y ? 1 : -1)*std::min(r.h/3,56*u);
                finger(SDL_FINGERDOWN,1,x,y);finger(SDL_FINGERMOTION,1,x,y+delta);finger(SDL_FINGERUP,1,x,y+delta);
            }
            throw std::runtime_error("Building action must be reachable by scrolling");
        };
        auto pressAction=[&](int kind,int value=0,int side=0) { auto p=actionPoint(kind,value,side);tap(p.x,p.y); };
        for (auto [width,height]:{std::pair{320,568},{568,320}}) {
            const int oldW=gfx->getW(),oldH=gfx->getH();
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resized{};resized.type=SDL_WINDOWEVENT;resized.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resized);gui.viewportResized(oldW,oldH,gfx->getW(),gfx->getH());
            openActions(swarm);
            const auto checksum=gui.game.checkSum();
            for (int type=0;type<NB_UNIT_TYPE;++type) {
                const auto before=gui.displayedRatio(*swarm);
                pressAction(0,type,1);pressAction(0,type,1);
                require(gui.orderQueue.size()==2,"Rapid production taps queue exactly two orders");
                for(int delta:{1,2}) {
                    auto order=std::dynamic_pointer_cast<OrderModifySwarm>(gui.orderQueue.front());gui.orderQueue.pop_front();
                    require(order && order->gid==swarm->gid,"Production uses the shared order and building");
                    for(int i=0;i<NB_UNIT_TYPE;++i) require(order->ratio[i]==before[i]+(i==type ? delta : 0),"Ratio edits preserve other pending values");
                }
                auto values=gui.displayedRatio(*swarm);values[type]=MAX_RATIO_RANGE;gui.pendingFor(swarm->gid).pendingRatio=values;
                pressAction(0,type,1);require(gui.orderQueue.empty(),"Maximum ratio tap emits no order");
                values[type]=0;gui.pendingFor(swarm->gid).pendingRatio=values;
                pressAction(0,type,-1);require(gui.orderQueue.empty(),"Zero ratio tap emits no order");
            }
            require(gui.game.checkSum()==checksum,"Ratio UI does not mutate the simulation");
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-actions-portrait.bmp" : "touch-actions-landscape.bmp");gfx->nextFrame();
            openActions(clearing);
            for(int resource=0;resource<BASIC_COUNT;++resource) if(resource!=STONE) {
                const bool before=gui.displayedClearingResource(*clearing,resource);
                pressAction(1,resource);pressAction(1,resource);
                require(gui.orderQueue.size()==2,"Clearing toggles each queue exactly one order");
                for(bool value:{!before,before}) {
                    auto order=std::dynamic_pointer_cast<OrderModifyClearingFlag>(gui.orderQueue.front());gui.orderQueue.pop_front();
                    require(order && order->gid==clearing->gid && order->clearingResources[resource]==value,"Clearing toggle uses pending state");
                }
            }
            for(auto* flag:{rangeFlag,exploring}) {
                openActions(flag);
                const int count=flag==rangeFlag ? NB_UNIT_LEVELS : EXPLORATION_FLAG_OPTION_COUNT;
                for(int level=0;level<count;++level) {
                    const int previous=gui.displayedMinLevelToFlag(*flag);pressAction(2,level);
                    require(gui.orderQueue.size()==size_t(previous!=level),"Requirement changes suppress no-ops");
                    if(previous!=level) {
                        auto order=std::dynamic_pointer_cast<OrderModifyMinLevelToFlag>(gui.orderQueue.front());gui.orderQueue.clear();
                        require(order && order->gid==flag->gid && order->minLevelToFlag==level,"Flag requirement preserves shared order format");
                    }
                }
            }
            openActions(wall);tab(4);
            require(gui.touch->allocationRect().h==48*gfx->logicalUnitsPerPoint(),"Info tab leaves room for legacy building details");
            tab(3);pressAction(4);require(gui.orderQueue.empty() && gui.touch->confirmDestroy,"Destroy first enters confirmation");
            pressAction(5);require(gui.orderQueue.empty() && !gui.touch->confirmDestroy,"Destruction can be canceled");
            pressAction(4);pressAction(4);
            require(gui.orderQueue.size()==1 && std::dynamic_pointer_cast<OrderDelete>(gui.orderQueue.front()),"Confirmed destruction emits one shared order");gui.orderQueue.clear();
            auto p=actionPoint(4,0);finger(SDL_FINGERDOWN,1,p.x,p.y);
            wall->buildingState=Building::WAITING_FOR_DESTRUCTION;
            finger(SDL_FINGERUP,1,p.x,p.y);require(gui.orderQueue.empty(),"A state transition cancels the held action");
            pressAction(4);require(gui.orderQueue.size()==1 && std::dynamic_pointer_cast<OrderCancelDelete>(gui.orderQueue.front()),"Pending destruction can be canceled by touch");gui.orderQueue.clear();
            wall->buildingState=Building::ALIVE;
            openActions(building);
            for(auto state:{Building::REPAIR,Building::UPGRADE}) {
                building->constructionResultState=state;
                pressAction(3);require(gui.orderQueue.size()==1 && std::dynamic_pointer_cast<OrderCancelConstruction>(gui.orderQueue.front()),"Construction cancellation uses shared order");gui.orderQueue.clear();
            }
            building->constructionResultState=Building::NO_CONSTRUCTION;
            gui.clearSelection();gui.touch->panelOpen=false;
        }

        for (const auto* key:{"[Actions]","[Info]","[Minimap]","[Fast forward]","[Hide keyboard]","[shutdown save failed]"})
            require(!GAGCore::Toolkit::getStringTable()->getString(key).empty(),"New interface translations must not be blank");

        auto tr=[](const char* key) { return std::string(GAGCore::Toolkit::getStringTable()->getString(key)); };
        auto pressDialog=[&](const std::string& text) {
            for (int attempt=0;attempt<40;++attempt) {
                gui.drawAll(0);gfx->nextFrame();
                auto rows=gui.touch->dialogRows;
                const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row) { return row.text==text && row.kind; });
                require(found!=rows.end(),("Missing phone dialog action: "+text).c_str());
                const auto r=found->rect, content=gui.touch->dialogContent;
                if (found->footer || (r.y>=content.y && r.y+r.h<=content.y+content.h)) { tap(r.x+r.w/2,r.y+r.h/2); return; }
                const float x=content.x+content.w/2,y=content.y+content.h/2;
                const float move=(r.y<content.y ? 1 : -1)*std::min(content.h/3,80.0*gfx->logicalUnitsPerPoint());
                finger(SDL_FINGERDOWN,1,x,y);finger(SDL_FINGERMOTION,1,x,y+move);finger(SDL_FINGERUP,1,x,y+move);
            }
            require(false,"Dialog action must be reachable by scrolling");
        };
        for (auto [width,height]:{std::pair{320,568},{568,320}}) {
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resized{};resized.type=SDL_WINDOWEVENT;resized.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resized); gui.viewportResized(800,600,gfx->getW(),gfx->getH());
            gui.inGameMenu=GameGUI::IGM_MAIN;gui.gameMenuScreen=std::make_unique<InGameMainScreen>();
            pressDialog(tr("[Options]"));require(gui.inGameMenu==GameGUI::IGM_OPTION,"Phone menu opens options");
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-options-portrait.bmp" : "touch-options-landscape.bmp");gfx->nextFrame();
            pressDialog(tr("[Mute]"));
            require(!globalContainer->settings.mute,"Large mute row updates shared options");
            pressDialog(tr("[ok]"));require(!gui.inGameMenu,"Options footer remains reachable");
            globalContainer->settings.mute=true;
            gui.inGameMenu=GameGUI::IGM_OBJECTIVES;gui.gameMenuScreen=std::make_unique<InGameObjectivesScreen>(&gui,false);
            pressDialog(tr("[hints]"));
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-objectives-portrait.bmp" : "touch-objectives-landscape.bmp");gfx->nextFrame();
            pressDialog(tr("[ok]"));require(!gui.inGameMenu,"Objectives tabs and footer work by touch");
            gui.inGameMenu=GameGUI::IGM_SAVE;gui.gameMenuScreen=std::make_unique<LoadSaveScreen>("games","game",false,tr("[save game]"),"Phone",glob2FilenameToName,glob2NameToFilename);
            pressDialog("Phone");
            SDL_Event text{};text.type=SDL_TEXTINPUT;std::strcpy(text.text.text," test");gui.processEvent(&text);
            require(std::string(static_cast<LoadSaveScreen*>(gui.gameMenuScreen.get())->getName())=="Phone test","Phone filename uses real text input");
            pressDialog(tr("[Hide keyboard]"));
            gui.drawAll(0);gfx->printScreen(width<height ? "touch-save-portrait.bmp" : "touch-save-landscape.bmp");gfx->nextFrame();
            pressDialog(tr("[Cancel]"));require(!gui.inGameMenu,"Save cancellation is always reachable");
            gui.inGameMenu=GameGUI::IGM_MAIN;gui.gameMenuScreen=std::make_unique<InGameMainScreen>();
            pressDialog(tr("[open chat box]"));require(gui.typingInputScreen,"Phone menu opens chat");
            text={};text.type=SDL_TEXTINPUT;std::strcpy(text.text.text,"Hello");gui.processEvent(&text);
            gui.orderQueue.clear();pressDialog(tr("[ok]"));
            require(!gui.typingInputScreen && gui.orderQueue.size()==1 && std::dynamic_pointer_cast<MessageOrder>(gui.orderQueue.front()),"Chat sends once through shared orders");
            gui.orderQueue.clear();
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
