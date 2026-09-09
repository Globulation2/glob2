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
#include "Unit.h"
#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "usl.h"
#include "native.h"
#include <ResponsiveDialog.h>
#include "PhoneForm.h"
#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"
#include "ChooseMapScreen.h"
#include "CustomGameScreen.h"
#include "NewMapScreen.h"
#include "SettingsScreen.h"
#include "EndGameScreen.h"
#include "ReplaySaveScreen.h"
#include "MapEdit.h"
#include "MapEditorScreen.h"
#include "CampaignEditor.h"
#include "PhoneEditor.h"
#include <GUITextArea.h>
#include <GUIRatio.h>
#include <ScreenStack.h>
#include <BinaryStream.h>
#include <filesystem>
#include <fstream>
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
        {
            Usl interpreter;
            auto* constant=new NativeValue<int>(&interpreter.heap,42);
            interpreter.setConstant("retained",constant);
            for(int cycle=0;cycle<100;++cycle) {
                interpreter.run(1);
                require(std::find(interpreter.heap.values.begin(),interpreter.heap.values.end(),constant)!=interpreter.heap.values.end(),
                    "Repeated script collection retains bridge constants");
                require(interpreter.getConstant("retained")==constant,"Script constant remains accessible");
            }
        }
        for(double width:{320.,568.,768.}) for(double height:{160.,320.,568.}) {
            std::vector<bool> footers{false,false,true,true,true};
            auto layout=GAGCore::ResponsiveDialog::calculate({0,24,width,height-24},footers,
                [](size_t i,double w){return i==2 ? (w<200 ? 96. : 48.) : 48.;},10000);
            require(layout.offset==layout.maximum,"Dialog scrolling clamps to reachable content");
            require(layout.content.h>=48,"Short dialog keeps room for scrollable controls");
            for(const auto& row:layout.rows) if(row.footer) {
                require(row.rect.y>=24 && row.rect.y+row.rect.h<=height,"Fixed actions respect safe bounds");
                require(row.rect.h>=48,"Fixed actions preserve minimum touch height");
            }
        }
        GameGUI gui;
        auto map=Engine::loadMapHeader("maps/balanced.map");
        GameHeader players; players.setNumberOfPlayers(1);
        players.getBasePlayer(0)=BasePlayer(0,"Touch",0,BasePlayer::P_LOCAL);
        require(gui.loadFromHeaders(map,players,true,true),"Fixture load failed");
        gui.localTeamNo=0; gui.localPlayer=0; gui.adjustLocalTeam();
        gui.viewportX=gui.viewportY=0;
        {
            globalContainer->replayWriter=std::make_unique<ReplayWriter>();
            auto& writer=*globalContainer->replayWriter;writer.init("",gui);
            require(writer.write("replays/touch-empty.replay"),"An unfinished in-memory replay can be exported");
            {ReplayReader empty;require(empty.loadReplay("replays/touch-empty.replay"),"Replay export retains the final buffered header byte");}
            for(int i=0;i<100;++i) writer.advanceStep();
            writer.finish();require(writer.write("replays/touch-preview.replay"),"Replay fixture writes");
            const auto position=writer.getBuffer()->getPosition();
            const auto blocked=std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR"))/"replays/blocked.replay";
            std::filesystem::create_directories(blocked);
            {std::ofstream marker(blocked/"keep");marker<<"preserve";}
            require(!writer.write(blocked.string()),"Replay replacement failure returns false without asserting");
            require(std::filesystem::exists(blocked/"keep") && writer.getBuffer()->getPosition()==position,"Failed replay write preserves destination and live buffer position");
            std::filesystem::remove(blocked/"keep");std::filesystem::remove(blocked);
            require(writer.write(blocked.string()) && writer.getBuffer()->getPosition()==position,"Replay retries after a failed replacement");
            std::ifstream original(std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR"))/"replays/touch-preview.replay",std::ios::binary);
            std::ifstream retried(blocked,std::ios::binary);
            require(std::string(std::istreambuf_iterator<char>(original),{})==std::string(std::istreambuf_iterator<char>(retried),{}),"Replay retry produces identical bytes");
            original.close();retried.close();
            std::filesystem::remove(blocked);
        }

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
            const float panelX=gui.touch->panelOrigin().x;
            const int cameraX=gui.viewportX,cameraY=gui.viewportY;
            finger(SDL_FINGERDOWN,1,(ui.panel.x+8)*unit,(ui.panel.y+70)*unit);
            finger(SDL_FINGERMOTION,1,(ui.panel.x+8)*unit,(ui.panel.y+30)*unit);
            finger(SDL_FINGERUP,1,(ui.panel.x+8)*unit,(ui.panel.y+30)*unit);
            require(gui.viewportX==cameraX && gui.viewportY==cameraY,"Panel scrolling must not pan the world"); noOrder();
            tap(panelX+120*gui.touch->panelScale(),gui.touch->panelOrigin().y+208*gui.touch->panelScale());
            require(gui.selectionMode==GameGUI::TOOL_SELECTION && gui.toolManager.getBuildingName()=="inn",
                    "Scaled panel hit-testing must select the same building after rotation"); noOrder();
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
            const float plusX=(ui.panel.x+ui.panel.w-24)*unit, rowY=(ui.panel.y+120)*unit;
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
            tap((ui.panel.x+ui.panel.w/2)*unit,(ui.panel.y+24)*unit);
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

        int workerSlot=0;
        while (workerSlot<Unit::MAX_COUNT && gui.localTeam->myUnits[workerSlot]) ++workerSlot;
        require(workerSlot<Unit::MAX_COUNT,"Repair fixture has a free worker slot");
        gui.localTeam->myUnits[workerSlot]=new Unit(0,0,workerSlot,WORKER,gui.localTeam,3);
        bool constructionSpace=false;
        for (int y=0;y<gui.game.map.getH() && !constructionSpace;++y)
            for (int x=0;x<gui.game.map.getW() && !constructionSpace;++x) {
                building->posX=x;building->posY=y;
                constructionSpace=building->isHardSpaceForBuildingSite(Building::REPAIR) &&
                    building->isHardSpaceForBuildingSite(Building::UPGRADE);
            }
        require(constructionSpace,"Fixture has space for repair and upgrade");

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
            const auto tabs=gui.touch->allocationTabs();
            auto i=std::find(tabs.begin(),tabs.end(),id);require(i!=tabs.end(),"Requested inspector tab exists");
            const auto r=gui.touch->allocationTabRect(std::distance(tabs.begin(),i));
            require(r.w>=48*gfx->logicalUnitsPerPoint(),"Inspector tabs retain 48-point targets");
            tap(r.x+r.w/2,r.y+r.h/2);
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
            for(bool repair:{true,false}) {
                building->hp=building->type->hpMax-(repair ? 1 : 0);
                const auto stateBefore=gui.game.checkSum();
                pressAction(3);
                require(gui.orderQueue.size()==1,"Repair/upgrade starts with exactly one order");
                auto order=std::dynamic_pointer_cast<OrderConstruction>(gui.orderQueue.front());
                require(order && order->gid==building->gid,"Repair/upgrade uses shared construction order");
                gui.orderQueue.clear();
                require(gui.game.checkSum()==stateBefore,"Construction requests do not mutate simulation");
            }
            building->hp=building->type->hpMax-1;
            auto repairPoint=actionPoint(3,0);
            finger(SDL_FINGERDOWN,1,repairPoint.x,repairPoint.y);
            building->hp=building->type->hpMax;
            finger(SDL_FINGERUP,1,repairPoint.x,repairPoint.y);
            require(gui.orderQueue.empty(),"Healing must not turn a held Repair into Upgrade");
            for(auto state:{Building::REPAIR,Building::UPGRADE}) {
                building->constructionResultState=state;
                pressAction(3);require(gui.orderQueue.size()==1 && std::dynamic_pointer_cast<OrderCancelConstruction>(gui.orderQueue.front()),"Construction cancellation uses shared order");gui.orderQueue.clear();
            }
            building->constructionResultState=Building::NO_CONSTRUCTION;
            gui.clearSelection();gui.touch->panelOpen=false;
        }

        for (const auto* key:{"[Actions]","[Info]","[Minimap]","[Fast forward]","[Hide keyboard]","[shutdown save failed]","[menu]","[Editor tools]","[Editor map]","[Pan map]","[Edit map]"})
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
            pressDialog(tr("[Dialog text size]")+": 150%");
            require(globalContainer->settings.mobileDialogTextPercent==150,"Text size changes without leaving menu");
            pressDialog(tr("[Dialog text size]")+": 100%");
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

        SDL_setenv("GLOB2_PHONE_FORMS","1",1);SDL_setenv("GLOB2_RESPONSIVE_UI","1",1);
        for(int textPercent:{100,150}) for(auto [width,height]:{std::pair{320,568},{568,320}}) {
            globalContainer->settings.mobileDialogTextPercent=textPercent;
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resized{};resized.type=SDL_WINDOWEVENT;resized.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resized);
            GAGGUI::ScreenStack menus(*gfx);
            auto chooser=std::make_unique<CampaignSelectorScreen>(false);GAGGUI::Screen* current=chooser.get();
            auto phone=[&]() -> PhoneForm& {
                if(auto* screen=dynamic_cast<ReplaySaveScreen*>(current)) return *screen->form;
                if(auto* screen=dynamic_cast<Glob2Screen*>(current)) return *screen->phoneForm;
                return *static_cast<Glob2TabScreen*>(current)->phoneForm;
            };
            menus.push(std::move(chooser));menus.frame(SDL_GetTicks(),{});
            require(dynamic_cast<Glob2Screen*>(current)->phoneForm!=nullptr,"Campaign selector uses the phone form");
            auto* statusScreen=dynamic_cast<Glob2Screen*>(current);
            auto* notice=new GAGGUI::Text(0,0,ALIGN_FILL,ALIGN_TOP,"menu","Initial notice");
            statusScreen->addWidget(notice);
            phone().offset=200;
            statusScreen->showPhoneStatus(notice,"Import failed");
            require(phone().offset==0,"A new import status is revealed above a scrolled phone form");
            phone().offset=120;
            statusScreen->showPhoneStatus(notice,"Import failed");
            require(phone().offset==120,"An unchanged import status does not interrupt scrolling");
            require((gfx->getW()<gfx->getH())==(width<height),"Phone form uses the requested orientation");
            auto tapForm=[&](int kind,std::string caption="",int side=0) {
                for(int attempt=0;attempt<240;++attempt) {
                    phone().prepare();
                    auto& form=phone();
                    auto found=std::find_if(form.rows.begin(),form.rows.end(),[&](const auto& row){return row.kind==kind && (caption.empty() || row.text==caption);});
                    require(found!=form.rows.end(),"Phone form action exists");
                    auto r=found->rect;
                    require(r.h>=40*gfx->logicalUnitsPerPoint(),"Compact form targets preserve 40-point touch height");
                    if(found->footer || (r.y>=form.placement.content.y && r.y+r.h<=form.placement.content.y+form.placement.content.h)) {
                        SDL_Event event{};event.type=SDL_FINGERDOWN;event.tfinger.touchId=20;event.tfinger.fingerId=1;
                        event.tfinger.x=(side<0 ? r.x+24*gfx->logicalUnitsPerPoint() : side>0 ? r.x+r.w-24*gfx->logicalUnitsPerPoint() : r.x+r.w/2)/gfx->getW();event.tfinger.y=(r.y+r.h/2)/gfx->getH();
                        current->handleExecutionEvent(event);event.type=SDL_FINGERUP;current->handleExecutionEvent(event);return;
                    }
                    form.offset+=r.y<form.placement.content.y ? -48 : 48;
                }
                require(false,("Phone form action is reachable: "+std::to_string(kind)+" "+caption).c_str());
            };
            tapForm(2);
            gfx->printScreen(width<height ? "phone-campaign-portrait.bmp" : "phone-campaign-landscape.bmp");current->drawExecution();
            phone().prepare();
            auto cancelRow=std::find_if(phone().rows.begin(),phone().rows.end(),[&](const auto& row){return row.kind==1 && row.text==tr("[Cancel]");});
            require(cancelRow!=phone().rows.end(),"Campaign cancel remains visible");
            SDL_Event held{};held.type=SDL_FINGERDOWN;held.tfinger.touchId=20;held.tfinger.fingerId=1;
            held.tfinger.x=(cancelRow->rect.x+cancelRow->rect.w/2)/gfx->getW();held.tfinger.y=(cancelRow->rect.y+cancelRow->rect.h/2)/gfx->getH();
            current->handleExecutionEvent(held);current->viewportResized(gfx->getW(),gfx->getH(),gfx->getH(),gfx->getW());
            held.type=SDL_FINGERUP;current->handleExecutionEvent(held);
            require(current->isExecutionRunning(),"Resize cancels held form actions");
            tapForm(1,tr("[Cancel]"));menus.frame(SDL_GetTicks(),{});
            require(!menus.running(),"Phone campaign cancel calls the existing screen callback");
            GAGGUI::ScreenStack setup(*gfx);
            auto custom=std::make_unique<CustomGameScreen>(setup);auto* customScreen=custom.get();current=custom.get();
            setup.push(std::move(custom));setup.frame(SDL_GetTicks(),{});
            phone().prepare();
            auto mapRow=std::find_if(phone().rows.begin(),phone().rows.end(),[](const auto& row){return row.kind==2 && row.text.find("balanced")!=std::string::npos;});
            require(mapRow!=phone().rows.end(),"Custom setup exposes balanced map");
            tapForm(2,mapRow->text);
            const auto playersBefore=customScreen->getGameHeader().getNumberOfPlayers();
            tapForm(7); // The first player checkbox follows the original clickability rules.
            require(customScreen->getGameHeader().getNumberOfPlayers()==playersBefore,"Local-player toggle keeps original setup rules");
            phone().prepare();
            auto aiRow=std::find_if(phone().rows.begin(),phone().rows.end(),[](const auto& row){return row.kind==6;});
            require(aiRow!=phone().rows.end(),"AI choices are visible");
            auto* aiButton=static_cast<GAGGUI::MultiTextButton*>(aiRow->widget);int aiBefore=aiButton->getIndex();
            tapForm(6,aiRow->text);
            require(aiButton->getIndex()!=aiBefore,"Phone AI choice cycles the existing widget");
            tapForm(1,tr("[Cancel]"));setup.frame(SDL_GetTicks(),{});
            require(!setup.running(),"Custom setup cancel remains reachable");
            GAGGUI::ScreenStack editorSetup(*gfx);
            auto create=std::make_unique<NewMapScreen>();auto* newMap=create.get();current=create.get();
            editorSetup.push(std::move(create));editorSetup.frame(SDL_GetTicks(),{});
            const int previousWidth=newMap->descriptor.wDec;
            tapForm(9,"",1);
            require(newMap->descriptor.wDec==previousWidth+1,"Phone map width updates the shared descriptor");
            tapForm(9,"",-1);
            require(newMap->descriptor.wDec==previousWidth,"Phone map width can be restored");
            phone().prepare();
            require(std::none_of(phone().rows.begin(),phone().rows.end(),[](const auto& row){return row.kind==10;}),"Uniform terrain hides generation ratios");
            tapForm(2,tr("[swamp terrain]"));
            phone().prepare();
            auto ratioRow=std::find_if(phone().rows.begin(),phone().rows.end(),[](const auto& row){return row.kind==10;});
            require(ratioRow!=phone().rows.end(),"Generated terrain exposes ratios");
            auto* ratio=static_cast<GAGGUI::Ratio*>(ratioRow->widget);const int ratioBefore=ratio->get();
            tapForm(10,"",1);require(ratio->get()==ratioBefore+1,"Phone ratio increases through shared callback");
            tapForm(10,"",-1);require(ratio->get()==ratioBefore,"Phone ratio decreases");
            ratio->set(0);tapForm(10,"",-1);require(ratio->get()==0,"Phone ratio clamps at zero");
            ratio->set(ratio->maximumValue());tapForm(10,"",1);require(ratio->get()==ratio->maximumValue(),"Phone ratio clamps at maximum");
            tapForm(1,tr("[Cancel]"));editorSetup.frame(SDL_GetTicks(),{});
            require(!editorSetup.running(),"Map creation cancellation remains reachable");
            GAGGUI::ScreenStack preferences(*gfx);
            auto settings=std::make_unique<SettingsScreen>();current=settings.get();
            const auto beforeSettings=globalContainer->settings;
            preferences.push(std::move(settings));preferences.frame(SDL_GetTicks(),{});
            tapForm(7,tr("[mute]")+": [x]");
            require(!globalContainer->settings.mute,"Phone settings mute invokes shared settings callback");
            tapForm(11,"",1);
            require(globalContainer->settings.gameSpeed==std::min(int(beforeSettings.gameSpeed)+1,int(Settings::GAME_SPEED_MAXIMUM)),"Phone game speed updates the shared preference");
            for(int i=0;i<=Settings::GAME_SPEED_MAXIMUM;++i) tapForm(11,"",-1);
            require(globalContainer->settings.gameSpeed==0,"Phone speed clamps at its minimum");
            for(int i=0;i<=Settings::GAME_SPEED_MAXIMUM;++i) tapForm(11,"",1);
            require(globalContainer->settings.gameSpeed==Settings::GAME_SPEED_MAXIMUM,"Phone speed clamps at its maximum");
            for(int i=globalContainer->settings.gameSpeed;i>int(beforeSettings.gameSpeed);--i) tapForm(11,"",-1);
            gfx->printScreen(width<height ? "phone-settings-portrait.bmp" : "phone-settings-landscape.bmp");current->drawExecution();
            tapForm(1,tr("[building settings]"));
            phone().prepare();
            require(std::any_of(phone().rows.begin(),phone().rows.end(),[](const auto& row){return row.kind==9;}),"Building defaults tab exposes numeric controls");
            tapForm(9,"",1);
            tapForm(1,tr("[keyboard settings]"));
            phone().prepare();
            const auto keyRow=std::find_if(phone().rows.begin(),phone().rows.end(),[](const auto& row){return row.kind==12;});
            require(keyRow!=phone().rows.end(),"Phone keyboard tab exposes key binding controls");
            auto* keyWidget=static_cast<GAGGUI::KeySelector*>(keyRow->widget);
            tapForm(12);
            require(keyWidget->caption()==tr("[waiting for key]"),"Touch activates the original key capture widget");
            SDL_Event keyEvent{};keyEvent.type=SDL_KEYDOWN;keyEvent.key.keysym.sym=SDLK_F12;
            current->handleExecutionEvent(keyEvent);
            require(keyWidget->getKey()==KeyPress(keyEvent.key.keysym,true),"Phone shortcut capture uses the original keyboard callback");
            tapForm(1,tr("[Cancel]"));preferences.frame(SDL_GetTicks(),{});
            require(!preferences.running() && globalContainer->settings.mute==beforeSettings.mute && globalContainer->settings.gameSpeed==beforeSettings.gameSpeed,"Settings cancel restores preferences");
            const auto previousTick=gui.game.stepCounter;
            gui.game.stepCounter=0;
            for(int team=0;team<gui.game.teamsCount();++team) {
                auto* owner=gui.game.teams[team];
                while(owner->stats.getEndOfGameStats().size()<2) owner->stats.step(owner);
            }
            gui.game.stepCounter=previousTick;
            GAGGUI::ScreenStack results(*gfx);
            auto end=std::make_unique<EndGameScreen>(&gui,results);current=end.get();
            results.push(std::move(end));results.frame(SDL_GetTicks(),{});
            tapForm(13);
            const auto resultChecksum=gui.game.checkSum();
            tapForm(1,tr("[Buildings]"));tapForm(7);
            require(gui.game.checkSum()==resultChecksum,"Result graph controls do not change the game");
            phone().offset=0;
            gfx->printScreen(width<height ? "phone-results-portrait.bmp" : "phone-results-landscape.bmp");current->drawExecution();
            tapForm(1,tr("[save replay]"));
            results.frame(SDL_GetTicks(),{}); // Opening returns to the host instead of polling SDL recursively.
            SDL_Event cancelReplay{};cancelReplay.type=SDL_KEYDOWN;cancelReplay.key.keysym.sym=SDLK_ESCAPE;
            results.frame(SDL_GetTicks(),{cancelReplay});results.frame(SDL_GetTicks(),{});results.frame(SDL_GetTicks(),{});
            require(current->isExecutionRunning(),"Cancelling replay child returns to results");
            tapForm(1,tr("[quit]"));results.frame(SDL_GetTicks(),{});
            require(!results.running(),"Phone results quit remains reachable");
            GAGGUI::ScreenStack replaySave(*gfx);
            auto save=std::make_unique<ReplaySaveScreen>(*globalContainer->replayWriter);
            auto* saveScreen=save.get();current=save.get();
            const std::string replayName="phone-replay-"+std::to_string(width)+"-"+std::to_string(textPercent);
            const auto replayPath=std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR"))/"replays"/(replayName+".replay");
            if(std::filesystem::is_regular_file(replayPath)) std::filesystem::remove(replayPath);
            std::filesystem::create_directories(replayPath);
            {std::ofstream marker(replayPath/"keep");marker<<"preserve";}
            replaySave.push(std::move(save));replaySave.frame(SDL_GetTicks(),{});
            tapForm(1,tr("[ok]"));replaySave.frame(SDL_GetTicks(),{});
            require(saveScreen->isExecutionRunning() && saveScreen->dialog.endValue<0,"An empty replay name cannot complete a save");
            phone().prepare();
            const auto closeRow=std::find_if(phone().rows.begin(),phone().rows.end(),[&](const auto& row){return row.kind==1 && row.text==tr("[Cancel]");});
            require(closeRow!=phone().rows.end(),"Replay cancellation remains reachable");
            SDL_Event heldCancel{};heldCancel.type=SDL_FINGERDOWN;heldCancel.tfinger.touchId=20;heldCancel.tfinger.fingerId=1;
            heldCancel.tfinger.x=(closeRow->rect.x+closeRow->rect.w/2)/gfx->getW();heldCancel.tfinger.y=(closeRow->rect.y+closeRow->rect.h/2)/gfx->getH();
            current->handleExecutionEvent(heldCancel);
            SDL_Event background{};background.type=SDL_APP_WILLENTERBACKGROUND;
            replaySave.frame(SDL_GetTicks(),{background});
            SDL_Event foreground{};foreground.type=SDL_APP_DIDENTERFOREGROUND;
            heldCancel.type=SDL_FINGERUP;
            replaySave.frame(SDL_GetTicks(),{foreground,heldCancel});
            require(saveScreen->dialog.endValue<0,"Backgrounding discards held replay actions");
            tapForm(3);
            SDL_Event nameInput{};nameInput.type=SDL_TEXTINPUT;
            std::strncpy(nameInput.text.text,replayName.c_str(),sizeof(nameInput.text.text)-1);
            current->handleExecutionEvent(nameInput);
            tapForm(5);tapForm(1,tr("[ok]"));replaySave.frame(SDL_GetTicks(),{});
            require(replaySave.running() && saveScreen->dialog.endValue<0 && std::filesystem::exists(replayPath/"keep"),"Replay save failure keeps dialog and prior destination");
            phone().prepare();
            require(std::any_of(phone().rows.begin(),phone().rows.end(),[&](const auto& row){return row.text==tr("[save failed retry]") && row.rect.y>=phone().placement.content.y && row.rect.y+row.rect.h<=phone().placement.content.y+phone().placement.content.h;}),"Replay save failure is visible without scrolling on phone");
            gfx->printScreen(width<height ? "phone-replay-failure-portrait.bmp" : "phone-replay-failure-landscape.bmp");current->drawExecution();
            std::filesystem::remove(replayPath/"keep");std::filesystem::remove(replayPath);
            tapForm(1,tr("[ok]"));replaySave.frame(SDL_GetTicks(),{});
            require(replaySave.running(),"Replay save waits for persistence completion");
            replaySave.frame(SDL_GetTicks(),{});replaySave.frame(SDL_GetTicks(),{});
            require(!replaySave.running() && std::filesystem::is_regular_file(replayPath),"Replay retry closes after successful persistence");
            ReplayReader savedReplay;
            require(savedReplay.loadReplay("replays/"+replayName+".replay"),"Phone-saved replay loads through the original reader");
            {
                auto hosted=std::make_unique<MapEdit>();require(hosted->load("maps/balanced.map"),"Hosted phone editor loads");
                GAGGUI::ScreenStack host(*gfx);auto screen=std::make_unique<MapEditorScreen>(host,std::move(hosted));
                require(screen->usesResponsiveViewport(),"Editor declares its phone viewport to the screen host");
                host.push(std::move(screen));host.frame(SDL_GetTicks(),{});
                require(gfx->getW()<=std::max(width,320)*2 && gfx->getH()<=std::max(height,320)*2,"Editor host does not retain an oversized legacy canvas");
                host.stop();host.frame(SDL_GetTicks(),{});
            }
            {
                GAGGUI::ScreenStack campaignHost(*gfx);auto campaign=std::make_unique<CampaignEditor>("",campaignHost);
                auto* campaignScreen=campaign.get();campaignHost.push(std::move(campaign));campaignHost.frame(SDL_GetTicks(),{});
                require(bool(campaignScreen->phoneForm),"Campaign authoring uses the native phone form");
                campaignScreen->phoneForm->prepare();require(std::any_of(campaignScreen->phoneForm->rows.begin(),campaignScreen->phoneForm->rows.end(),[](const auto& row){return row.kind==14;}),"Campaign descriptions expose editable phone rows");
                campaignHost.stop();campaignHost.frame(SDL_GetTicks(),{});
            }
            {
                MapEdit editor;require(editor.load("maps/balanced.map"),"Phone editor fixture loads");editor.beginEditing();
                require(bool(editor.phone),"Map editor enables native phone workspace");auto& ui=*editor.phone;
                auto send=[&](Uint32 type,double x,double y,int id=11) {
                    SDL_Event e{};e.type=type;e.tfinger.touchId=31;e.tfinger.fingerId=id;
                    e.tfinger.x=x/gfx->getW();e.tfinger.y=y/gfx->getH();editor.advanceEditing({e},SDL_GetTicks());
                };
                auto tapEditor=[&](double x,double y){send(SDL_FINGERDOWN,x,y);send(SDL_FINGERUP,x,y);};
                ui.prepare();double unit=gfx->logicalUnitsPerPoint();
                const auto before=editor.game.checkSum();int vx=editor.viewportX;
                send(SDL_FINGERDOWN,100*unit,140*unit);send(SDL_FINGERMOTION,164*unit,140*unit);send(SDL_FINGERUP,164*unit,140*unit);
                require(editor.viewportX==((vx-int(2*unit))&editor.game.map.wMask) && editor.game.checkSum()==before,"Phone editor pans without changing map data");
                tapEditor(ui.safe.x+ui.safe.w/2,ui.safe.y+24*unit);require(ui.tools,"Phone tool drawer opens");
                auto tapTool=[&](MapEditorWidget* widget,double relX=.5,double relY=.5) {
                    for(int attempt=0;attempt<160;++attempt) {
                        ui.prepare();auto it=std::find_if(ui.rows.begin(),ui.rows.end(),[&](const auto& row){return row.widget==widget;});
                        require(it!=ui.rows.end(),"Original editor control is reachable in phone drawer");
                        const auto r=it->rect;require(r.h>=48*unit,"Editor row has a full touch target");
                        double y=r.y+r.h*relY;
                        if(it->fixed || (y>=ui.content.y && y<ui.content.y+ui.content.h)) {tapEditor(r.x+r.w*relX,y);return;}
                        double x=ui.content.x+ui.content.w-6*unit,mid=ui.content.y+ui.content.h/2;
                        send(SDL_FINGERDOWN,x,mid);send(SDL_FINGERMOTION,x,mid+(y<ui.content.y ? 80 : -80)*unit);send(SDL_FINGERUP,x,mid+(y<ui.content.y ? 80 : -80)*unit);
                    }
                    throw std::runtime_error("Editor scrolling failed to reach a tool");
                };
                tapTool(editor.teamsView);const auto count=editor.game.mapHeader.getNumberOfTeams();
                tapTool(editor.increaseTeams);require(editor.game.mapHeader.getNumberOfTeams()==count+1,"Phone editor adds a team with original action");
                tapTool(editor.decreaseTeams);require(editor.game.mapHeader.getNumberOfTeams()==count,"Phone editor removes the added team");
                tapTool(editor.terrainView);require(editor.panelMode==MapEdit::Terrain,"Phone drawer switches terrain category");
                tapTool(editor.water);require(editor.terrainType==TerrainSelector::Water,"Phone terrain selection uses original action");
                ui.offset=0;editor.drawEditing();gfx->printScreen(width<height ? "phone-editor-tools-portrait.bmp":"phone-editor-tools-landscape.bmp");editor.drawEditing();
                tapEditor(ui.safe.x+ui.safe.w*5/6,ui.safe.y+24*unit);require(!ui.pan && !ui.tools,"Edit mode closes drawer and arms map editing");
                send(SDL_FINGERDOWN,100*unit,140*unit);send(SDL_FINGERMOTION,164*unit,140*unit);send(SDL_FINGERUP,164*unit,140*unit);
                require(editor.hasMapBeenModified && !editor.isDraggingTerrain,"Phone paint stroke changes map and releases original brush");
                const auto edited=editor.game.checkSum();
                send(SDL_FINGERDOWN,100*unit,140*unit);editor.suspendInput();const auto suspended=editor.game.checkSum();
                send(SDL_FINGERMOTION,220*unit,140*unit);send(SDL_FINGERUP,220*unit,140*unit);
                require(editor.game.checkSum()==suspended && !editor.isDraggingTerrain,"Suspension cancels held editor painting");
                (void)edited;
                tapEditor(ui.safe.x+ui.safe.w/6,ui.safe.y+24*unit);ui.syncOverlay();
                require(editor.showingMenuScreen && ui.form,"Editor menu uses touch form");
                editor.performAction("close menu screen");ui.syncOverlay();
                editor.performAction("open scenario editor");ui.syncOverlay();ui.form->prepare();
                require(std::any_of(ui.form->rows.begin(),ui.form->rows.end(),[](const auto& row){return row.kind==14;}),"Scenario text offers editable phone rows");
                auto tapOverlay=[&](int kind,const std::string& caption="") {
                    for(int attempt=0;attempt<240;++attempt) {
                        ui.syncOverlay();require(bool(ui.form),"Editor overlay exists");ui.form->prepare();
                        auto it=std::find_if(ui.form->rows.begin(),ui.form->rows.end(),[&](const auto& row){return row.kind==kind && (caption.empty() || row.text==caption);});
                        require(it!=ui.form->rows.end(),"Editor overlay control exists");auto r=it->rect;auto c=ui.form->placement.content;
                        if(it->footer || (r.y>=c.y && r.y+r.h<=c.y+c.h)) {tapEditor(r.x+r.w/2,r.y+r.h/2);return;}
                        double mid=c.y+c.h/2,x=c.x+c.w-4*unit;
                        send(SDL_FINGERDOWN,x,mid);send(SDL_FINGERMOTION,x,mid+(r.y<c.y ? 64 : -64)*unit);send(SDL_FINGERUP,x,mid+(r.y<c.y ? 64 : -64)*unit);
                    }
                    throw std::runtime_error("Editor overlay scroll exhausted");
                };
                tapOverlay(1,tr("[briefing]"));ui.form->prepare();
                auto textRow=std::find_if(ui.form->rows.begin(),ui.form->rows.end(),[](const auto& row){return row.kind==14;});
                auto* multiline=static_cast<GAGGUI::TextArea*>(textRow->widget);auto originalText=multiline->getText();
                tapOverlay(14);SDL_Event typed{};typed.type=SDL_TEXTINPUT;SDL_strlcpy(typed.text.text,"Phone Ω\nsecond",sizeof(typed.text.text));
                editor.advanceEditing({typed},SDL_GetTicks());
                require(multiline->getText().find("Phone Ω\nsecond")!=std::string::npos,"Phone scenario input retains Unicode and newlines");
                tapOverlay(5,tr("[Hide keyboard]"));tapOverlay(1,tr("[Cancel]"));
                require(!editor.showingScriptEditor && editor.game.missionBriefing==originalText,"Cancel leaves the scenario briefing unchanged");
                editor.performAction("open save screen");ui.syncOverlay();tapOverlay(3);
                auto* name=ui.form->editing;require(name!=nullptr,"Map save filename gets text focus");
                static_cast<GAGGUI::TextInput*>(name)->setText("");tapOverlay(5,tr("[Hide keyboard]"));tapOverlay(1,tr("[ok]"));
                require(editor.showingSave && !editor.needsFertility(),"Empty map name shows retry without starting fertility or asserting");
                tapOverlay(1,tr("[Cancel]"));require(!editor.showingSave,"Phone editor save cancellation returns to workspace");
                editor.performAction("open save screen");ui.syncOverlay();ui.form->prepare();
                for(const auto& row:ui.form->rows) if(row.kind==3) static_cast<GAGGUI::TextInput*>(row.widget)->setText("Phone event batch");
                ui.form->offset=ui.form->placement.maximum;ui.form->prepare();
                std::vector<SDL_Event> batch;
                for(const auto& caption:{tr("[ok]"),tr("[Cancel]")}) {
                    auto row=std::find_if(ui.form->rows.begin(),ui.form->rows.end(),[&](const auto& r){return r.kind==1 && r.text==caption;});
                    require(row!=ui.form->rows.end(),"Save/cancel controls exist for batched input test");
                    for(Uint32 type:{SDL_FINGERDOWN,SDL_FINGERUP}) {SDL_Event e{};e.type=type;e.tfinger.touchId=31;e.tfinger.fingerId=11;
                        e.tfinger.x=(row->rect.x+row->rect.w/2)/gfx->getW();e.tfinger.y=(row->rect.y+row->rect.h/2)/gfx->getH();batch.push_back(e);}
                }
                editor.advanceEditing(batch,SDL_GetTicks());
                require(editor.needsFertility() && editor.showingSave,"Save suspends the input batch before a following Cancel can delete its dialog");
                editor.finishFertility(false);require(!editor.showingSave,"Cancelled fertility releases pending phone save safely");
            }

        }
        globalContainer->settings.mobileDialogTextPercent=100;
        SDL_setenv("GLOB2_PHONE_FORMS","",1);SDL_setenv("GLOB2_RESPONSIVE_UI","1",1);
        gui.clearSelection();gui.touch->panelOpen=false;
        globalContainer->replayReader=std::make_unique<ReplayReader>();
        require(globalContainer->replayReader->loadReplay("replays/touch-preview.replay"),"Real replay fixture loads");
        globalContainer->replaying=true;
        globalContainer->replayVisibleTeams=0xffffffff;
        auto replayMap=Engine::loadMapHeader("replays/touch-preview.replay");
        require(gui.loadFromHeaders(replayMap,players,true,true,false,"replays/touch-preview.replay"),"Replay world loads");
        gui.localTeamNo=0;gui.localPlayer=0;gui.adjustLocalTeam();
        for(auto [width,height]:{std::pair{320,568},{568,320}}) {
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resized{};resized.type=SDL_WINDOWEVENT;resized.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resized);gui.viewportResized(800,600,gfx->getW(),gfx->getH());
            gui.drawAll(0);gfx->nextFrame();
            const auto before=gui.game.checkSum();
            const double unit=gfx->logicalUnitsPerPoint(), y=gfx->getH()-24*unit;
            gui.gamePaused=false;globalContainer->replayFastForward=false;gui.orderQueue.clear();
            tap(gfx->getW()/12.0,y);require(gui.gamePaused,"Replay toolbar pauses");
            tap(gfx->getW()/4.0,y);require(!gui.gamePaused && globalContainer->replayFastForward,"Replay speed resumes fast playback");
            tap(gfx->getW()/4.0,y);require(!globalContainer->replayFastForward,"Replay speed returns to normal");
            tap(gfx->getW()*5.5/6,y);pressDialog(tr("[Fast forward]"));
            require(globalContainer->replayFastForward,"Replay menu speed uses shared playback state");
            require(gui.orderQueue.empty() && !gui.toolManager.getOrder() && gui.game.checkSum()==before,"Replay controls issue no simulation orders");
            if(gui.inGameMenu) {gui.inGameMenu=GameGUI::IGM_NONE;gui.gameMenuScreen.reset();}
        }
        globalContainer->replaying=false;globalContainer->replayReader.reset();

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
