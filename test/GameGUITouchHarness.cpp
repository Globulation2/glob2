// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GameGUITouch.h"
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
