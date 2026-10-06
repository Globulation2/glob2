// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GameGUIViewport.h"
#include "GameGUIInternal.h"
#include <nlohmann/json.hpp>
#include "Order.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include "LoadSaveDialog.h"
#include "MapEditDialog.h"
#include <SDLGraphicContext.h>
#include <SDL3/SDL.h>
#include <set>

namespace
{
Uint64 renderPanel(GameGUI& gui)
{
    auto* gfx=globalContainer->gfx;
    gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
    gfx->setClipRect();
    gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(0,0,0));
    gui.extractScene(gui.frameScene);
    gui.drawUnitInfos(); gfx->nextFrame();
    auto* frame=gfx->completedFrame(); REQUIRE(frame);
    Uint64 hash=1469598103934665603ull;
    const int left=(gfx->getW()-GAME_GUI_RIGHT_MENU_WIDTH)*frame->w/gfx->getW();
    const auto* pixels=static_cast<const Uint8*>(frame->pixels);
    for (int y=0; y<frame->h; ++y)
        for (int x=left*SDL_BYTESPERPIXEL(frame->format); x<frame->w*SDL_BYTESPERPIXEL(frame->format); ++x)
            hash=(hash^pixels[y*frame->pitch+x])*1099511628211ull;
    return hash;
}
}
TEST_SUITE("GUIInteractionCoverage")
{
    TEST_CASE("service progress horizon covers mixed visits and preserves stock horizons")
    {
        glob2test::HeadlessGlobals globals;
        const auto& catalog=globals->buildingsTypes;
        for(size_t id=0;id<catalog.size();++id)
        {
            const auto& type=*catalog.get(id);CAPTURE(type.key);
            int prior=type.timeToFeedUnit ? type.timeToFeedUnit : type.timeToHealUnit;
            if(!prior)for(int ability=0;ability<NB_ABILITY;++ability)prior=std::max(prior,type.upgradeTime[ability]);
            CHECK(buildingServiceProgressTimeout(type)==prior);
        }
        BuildingType mixed;mixed.timeToFeedUnit=10;mixed.timeToHealUnit=100;mixed.upgradeTime[WALK]=250;
        CHECK(buildingServiceProgressTimeout(mixed)==250);
        mixed.upgradeTime[WALK]=20;CHECK(buildingServiceProgressTimeout(mixed)==100);
        mixed.timeToFeedUnit=200;CHECK(buildingServiceProgressTimeout(mixed)==200);
        mixed.timeToFeedUnit=mixed.timeToHealUnit=mixed.upgradeTime[WALK]=0;
        CHECK(buildingServiceProgressTimeout(mixed)==0);
    }

    TEST_CASE("mixed service progress stays in its row and preserves downstream hit testing [display:1700x900][artifacts]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1600,.height=800});
        auto* gfx=globals->gfx;
        for(bool instant : {false,true})
        {
            CAPTURE(instant);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            const int typeId=world.game.buildingsTypes.getFinishedTypeNum("inn");
            auto& spec=catalog["variants"][typeId];auto& properties=spec["properties"];auto& semantics=spec["semantics"];
            spec["previous"]="";spec["next"]="";semantics["repairable"]=false;
            semantics["assignmentLimit"]=0;spec["presentation"]["defaultAssigned"]=0;
            properties["armor"]=0;properties["maxResource"]=std::vector<int>(15,0);properties["maxUnitInside"]=3;
            const int feed=instant?0:10,heal=instant?0:100,training=instant?0:250;
            semantics["feeding"]={{"enabled",true},{"duration",feed},{"cost",nlohmann::json::object()}};
            semantics["healing"]={{"enabled",true},{"duration",heal},{"cost",nlohmann::json::object()}};
            semantics["training"]={{"walk",{{"enabled",true},{"duration",training},{"targetLevel",1},{"cost",nlohmann::json::object()}}}};
            semantics["production"]={{"scheduling","weighted_committed_job"},{"fallbackUnit",0},{"initialRatios",{1,0,0}},
                {"recipes",{{"worker",{{"enabled",true},{"duration",1},{"cost",nlohmann::json::object()}}}}}};
            world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.buildingsTypes.loadSprites();world.game.configureBuildingCatalog();
            auto* building=world.game.addBuilding(8,8,typeId,0,0,0);REQUIRE(building);
            auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.localTeam=world.team;
            gui.setSelection(GameGUI::BUILDING_SELECTION,building);gui.extractScene(gui.frameScene);
            auto& panel=gui.frameScene.panels.building;REQUIRE(panel.valid);
            const auto checksum=world.checksum();
            const int rowY=YPOS_BASE_BUILDING+YOFFSET_NAME+YOFFSET_ICON+YOFFSET_B_SEP+YOFFSET_INFOS;
            const int left=gfx->getW()-RIGHT_MENU_RIGHT_OFFSET;
            const auto begin=[&] {
                gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
                gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(24,35,28));
            };
            // Render valid visit clocks into the extracted presentation snapshot only.
            // Calling the real row helper without the outer panel clip exposes overflow.
            for(bool smooth : {false,true})for(int duration : {feed,heal,training})
                for(int remaining : {duration,0})for(int delta : {0,127,255})
            {
                CAPTURE(smooth);CAPTURE(duration);CAPTURE(remaining);CAPTURE(delta);
                globals->settings.smoothProgressIndicators=smooth;
                panel.insideUnits={{true,-remaining,delta}};
                begin();int y=rowY;unsigned rowHeight=0;
                gui.drawBuildingTimeToLeaveBar(&panel,building->type,y,rowHeight);gfx->nextFrame();
                CHECK(y==rowY+(instant?0:YOFFSET_PROGRESS_BAR));
                CHECK(rowHeight==unsigned(instant?0:YOFFSET_PROGRESS_BAR));
                auto* image=gfx->completedFrame();REQUIRE(image);
                unsigned markerPixels=0;
                for(int x=0;x<image->w;++x)
                {
                    Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(image,x,rowY+3,&r,&g,&b,&a));
                    if(r==63&&g==111&&b==149)++markerPixels;
                    // The existing smooth marker has a +/-2px halo, including at
                    // the right endpoint one pixel past the128px rectangle.
                    if(instant || x<left-2 || x>left+128+2)
                        CHECK((r==24&&g==35&&b==28));
                }
                CHECK((markerPixels>0)==!instant);CHECK(world.checksum()==checksum);
            }
            // Full actual panel capture and actual production-slider click, with
            // both a present row and the existing all-zero/no-row behavior.
            panel.insideUnits={{true,-feed,64},{true,-heal,127},{true,-training,0}};panel.unitsInside=3;
            begin();gui.drawBuildingInfos();gfx->nextFrame();
            REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/(instant?"instant-services.bmp":"mixed-service-progress.bmp")).string().c_str()));
            const int productionY=rowY+(instant?0:YOFFSET_PROGRESS_BAR)+YOFFSET_B_SEP+YOFFSET_RESOURCE_SECTION_PAD+YOFFSET_SWARM_PROGRESS_BAR;
            const int ratio=gui.displayedRatio(*building)[WORKER];
            gui.handleMenuClickBuildingSelection(RIGHT_MENU_OFFSET+119,productionY+8,SDL_BUTTON_LEFT);
            REQUIRE(gui.orderQueue.size()==1);CHECK(gui.orderQueue.front()->getOrderType()==ORDER_MODIFY_SWARM);
            CHECK(gui.displayedRatio(*building)[WORKER]==ratio+1);CHECK(world.checksum()==checksum);
            gui.orderQueue.clear();
        }
    }

    TEST_CASE("construction choice displays recipe costs independently of storage [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui;
        auto* site=world.game.buildingsTypes.getByType("inn",0,true);
        REQUIRE(site);
        auto* gfx=globalContainer->gfx;
        const auto render=[&]() {
            gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
            gfx->setClipRect();
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(24,35,28));
            gui.drawChoiceInfoPanel("inn"); gfx->nextFrame();
            auto* frame=gfx->completedFrame(); REQUIRE(frame);
            Uint64 hash=1469598103934665603ull;
            const auto* pixels=static_cast<const Uint8*>(frame->pixels);
            for (int i=0; i<frame->h*frame->pitch; ++i) hash=(hash^pixels[i])*1099511628211ull;
            return hash;
        };
        const auto original=render();
        site->maxResource[WOOD]+=17;
        CHECK(render()==original);
        site->semantics.constructionCost[WOOD]+=17;
        const auto changedCost=render(); CHECK(changedCost!=original);
        for (int resource=HAPPINESS_BASE; resource<MAX_RESOURCES; ++resource)
        {
            const auto before=render();
            site->semantics.constructionCost[resource]=resource+1;
            CHECK(render()!=before);
        }
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"construction-fruit-costs.bmp").string().c_str()));
    }

    TEST_CASE("mixed upgrade preview tracks actual rows scroll and recipe inputs [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.localTeam=world.team;
        const int current=world.game.buildingsTypes.getTypeNum("inn",0,false);
        const int nextSite=world.game.buildingsTypes.get(current)->nextLevel;
        const int next=world.game.buildingsTypes.get(nextSite)->nextLevel;
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& from=catalog["variants"][current];
        from["properties"]["defaultUnitStayRange"]=0;from["properties"]["maxUnitStayRange"]=16;
        from["properties"]["zonable"]={1,1,1};
        from["properties"]["maxResource"][STONE]=15;
        for(int r=HAPPINESS_BASE;r<MAX_RESOURCES;++r)from["properties"]["maxResource"][r]=20;
        from["semantics"]["market"]["interTeamFruitExchange"]=true;
        const int producer=world.game.buildingsTypes.getFinishedTypeNum("swarm");
        from["semantics"]["production"]=catalog["variants"][producer]["semantics"]["production"];
        from["semantics"]["repairCost"]={{"wood",8},{"orange",13},{"prune",17}};
        auto& to=catalog["variants"][next];to["properties"]["hpMax"]=1379;
        to["properties"]["maxResource"][WOOD]=7;to["properties"]["maxResource"][STONE]=0;
        catalog["variants"][nextSite]["semantics"]["constructionCost"]={{"wood",8},{"cherry",23},{"orange",29},{"prune",31}};
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.buildingsTypes.loadSprites();world.game.configureBuildingCatalog();
        auto* building=world.game.addBuilding(8,8,current,0);REQUIRE(building);
        auto* worker=world.addUnit(WORKER,4,4);worker->constructionLevel=3;
        gui.setSelection(GameGUI::BUILDING_SELECTION,building);
        auto* gfx=globalContainer->gfx;
        const auto render=[&](bool extract=true) {
            const auto before=world.checksum();
            gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(24,35,28));
            if(extract)gui.extractScene(gui.frameScene);
            gui.drawBuildingInfos();gfx->nextFrame();
            CHECK(world.checksum()==before);REQUIRE(gfx->completedFrame());
        };
        const auto crop=[&](int y) {
            const auto* frame=gfx->completedFrame();const int bytes=SDL_BYTESPERPIXEL(frame->format);
            const auto* pixels=static_cast<const Uint8*>(frame->pixels);
            std::vector<Uint8> result;
            for(int row=y;row<y+12;++row)
                result.insert(result.end(),pixels+row*frame->pitch+(frame->w-40)*bytes,pixels+row*frame->pitch+frame->w*bytes);
            return result;
        };
        const auto headerIsBlank=[&]() {
            auto* frame=gfx->completedFrame();
            for(int y=0;y<YPOS_BASE_BUILDING;++y)
                for(int x=frame->w-RIGHT_MENU_WIDTH;x<frame->w;++x)
                {
                    Uint8 red,green,blue,alpha;
                    if(!SDL_ReadSurfacePixel(frame,x,y,&red,&green,&blue,&alpha)
                        || red!=24 || green!=35 || blue!=28) return false;
                }
            return true;
        };
        const auto frameHash=[&]() {
            const auto* frame=gfx->completedFrame();const auto* pixels=static_cast<const Uint8*>(frame->pixels);
            Uint64 result=1469598103934665603ull;
            for(int i=0;i<frame->h*frame->pitch;++i)result=(result^pixels[i])*1099511628211ull;
            return result;
        };
        render();
        // A physical attractor with a zero initial radius still has counts and
        // range controls. Its extra header moves controls, not HP/inside rows.
        const int controlY=YPOS_BASE_BUILDING+YOFFSET_NAME+YOFFSET_ICON+YOFFSET_B_SEP+buildingExtraHeaderHeight(*building->type);
        const int assigned=building->maxUnitWorking;
        gui.handleMenuClickBuildingSelection(RIGHT_MENU_OFFSET+119,controlY+YOFFSET_TEXT_BAR+8,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==1);CHECK(gui.displayedMaxUnitWorking(*building)==assigned+1);
        gui.orderQueue.clear();
        const int rangeY=controlY+2*(YOFFSET_BAR+YOFFSET_B_SEP)+YOFFSET_B_SEP;
        gui.handleMenuClickBuildingSelection(RIGHT_MENU_OFFSET+119,rangeY+YOFFSET_TEXT_BAR+8,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==1);CHECK(gui.displayedUnitStayRange(*building)==1);gui.orderQueue.clear();
        gui.mouseX=gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+64;
        gui.mouseY=gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET+8;
        render();REQUIRE(gui.hoveredBuildingPreview(gui.frameScene.panels.building)==GameGUI::BuildingPreview::Upgrade);
        REQUIRE(gui.buildingInfoScrollMaximum>=32);
        const auto hpAtTop=crop(YPOS_BASE_BUILDING+YOFFSET_NAME+YOFFSET_TEXT_LINE);
        REQUIRE(gui.scrollBuildingChoices(-1));render();CHECK(gui.buildingInfoScroll==32);
        CHECK(crop(YPOS_BASE_BUILDING+YOFFSET_NAME+YOFFSET_TEXT_LINE-32)==hpAtTop);
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"mixed-upgrade-scrolled.bmp").string().c_str()));
        while(gui.buildingInfoScroll<gui.buildingInfoScrollMaximum)REQUIRE(gui.scrollBuildingChoices(-1));
        render();
        // Click the visible worker recipe after the attraction controls,
        // service progress, exchange stock and ordinary storage rows.
        const int flagRows=(YOFFSET_B_SEP+(1+BASIC_COUNT-1)*YOFFSET_TEXT_PARA)
            +2*(YOFFSET_B_SEP+(1+NB_UNIT_LEVELS)*YOFFSET_TEXT_PARA)
            +(YOFFSET_B_SEP+(1+EXPLORATION_FLAG_OPTION_COUNT)*YOFFSET_TEXT_PARA);
        const int productionY=rangeY+YOFFSET_BAR+YOFFSET_B_SEP+flagRows+YOFFSET_INFOS
            +YOFFSET_PROGRESS_BAR+YOFFSET_B_SEP+(1+HAPPINESS_COUNT)*YOFFSET_TEXT_PARA
            +2*YOFFSET_RESOURCE_LINE+YOFFSET_RESOURCE_SECTION_PAD+YOFFSET_SWARM_PROGRESS_BAR;
        const int visibleRecipeY=productionY-gui.buildingInfoScroll+8;
        REQUIRE(visibleRecipeY>=YPOS_BASE_BUILDING);
        REQUIRE(visibleRecipeY<gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET-4);
        const int ratio=gui.displayedRatio(*building)[WORKER];
        gui.handleMenuClickBuildingSelection(RIGHT_MENU_OFFSET+119,visibleRecipeY,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==1);
        CHECK(gui.displayedRatio(*building)[WORKER]==ratio+1);gui.orderQueue.clear();
        render();const auto original=frameHash();
        // New and removed storage capacities and fruit costs must remain
        // reachable while the pointer stays on the fixed upgrade button.
        auto* target=world.game.buildingsTypes.get(next);
        target->maxResource[WOOD]=9;render();CHECK(frameHash()!=original);
        const auto changedWood=frameHash();target->maxResource[STONE]=3;render();CHECK(frameHash()!=changedWood);
        target->maxResource[STONE]=0;target->maxResource[WOOD]=7;render();
        CHECK(headerIsBlank());
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"mixed-upgrade-costs.bmp").string().c_str()));
        auto* site=world.game.buildingsTypes.get(nextSite);
        const auto beforeFruit=frameHash();site->semantics.constructionCost[PRUNE]+=5;render();CHECK(frameHash()!=beforeFruit);
        building->hp/=2;render();
        REQUIRE(gui.hoveredBuildingPreview(gui.frameScene.panels.building)==GameGUI::BuildingPreview::Repair);
        while(gui.buildingInfoScroll<gui.buildingInfoScrollMaximum)REQUIRE(gui.scrollBuildingChoices(-1));
        render();CHECK(headerIsBlank());
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"mixed-repair-costs.bmp").string().c_str()));
        // A retained frame must resolve upgrade IDs through its own catalog,
        // even after the game installs a differently ordered catalog.
        building->hp=building->type->hpMax;render();
        while(gui.buildingInfoScroll<gui.buildingInfoScrollMaximum)REQUIRE(gui.scrollBuildingChoices(-1));
        render();const auto retainedHash=frameHash();
        {
            // Restore the original storage, including existing entity pointers,
            // even if a rendering assertion exits this fixture early.
            struct RestoreCatalog
            {
                BuildingsTypes& destination;
                BuildingsTypes saved;
                ~RestoreCatalog() { destination=std::move(saved); }
            } restore{world.game.buildingsTypes,std::move(world.game.buildingsTypes)};
            std::reverse(catalog["variants"].begin(),catalog["variants"].end());
            for(size_t id=0;id<catalog["variants"].size();++id)catalog["variants"][id]["id"]=id;
            world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
            REQUIRE(world.game.buildingsTypes.findByKey(building->type->key)!=current);
            render(false);CHECK(frameHash()==retainedHash);
        }
        gui.clearSelection();
    }

    TEST_CASE("custom mixed building panels render without mutating simulation [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        world.game.buildingsTypes.loadManifest(glob2test::fixture("building-catalog/composition/seed-713.manifest.json").string());
        world.game.buildingsTypes.loadSprites();
        world.game.configureBuildingCatalog();
        auto& gui=world.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
        auto* gfx=globalContainer->gfx;
        int x=4;
        for(const char* key : {"refuge","forge","signal","warehouse"})
        {
            CAPTURE(key);
            const int type=world.game.buildingsTypes.findByKey(key);
            REQUIRE(type>=0);
            auto* building=world.game.addBuilding(x,4,type,0,0,0);
            REQUIRE(building);
            gui.setSelection(GameGUI::BUILDING_SELECTION,building);
            const auto before=world.checksum();
            gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
            gfx->setClipRect();
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(24,35,28));
            gui.extractScene(gui.frameScene);
            gui.drawBuildingInfos();
            gfx->nextFrame();
            CHECK(world.checksum()==before);
            REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/(std::string(key)+".bmp")).string().c_str()));
            x+=6;
        }
        gui.clearSelection();
    }

    TEST_CASE("unit information reflects damage hunger ownership and abilities without mutating simulation [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=0});
        glob2test::HeadlessGame world(glob2test::GameOptions{
            .teams=3,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
        world.team->allies=3; world.team->enemies=4;
        std::set<Uint64> ownerPanels;
        for (int team=0; team<3; ++team)
            for (int type : {WORKER,EXPLORER,WARRIOR})
            {
                CAPTURE(team); CAPTURE(type);
                auto* unit=world.addUnit(type,3+type*3,4+team*3,team,2);
                gui.setSelection(GameGUI::UNIT_SELECTION,unit);
                const auto before=world.checksum();
                const auto healthy=renderPanel(gui);
                CHECK(world.checksum()==before);
                unit->hp=1; unit->hungry=0;
                if (type==WORKER) unit->carriedResource=WHEAT;
                if (type==WARRIOR) world.game.gameHeader.setGlassCannonLevel(1);
                const auto damagedState=world.checksum();
                const auto damaged=renderPanel(gui);
                CHECK(world.checksum()==damagedState); CHECK(damaged!=healthy);
                if (type==WORKER) ownerPanels.insert(healthy);
                const auto path=glob2test::artifactDir()/(
                    "unit-"+std::to_string(type)+"-team-"+std::to_string(team)+".bmp");
                REQUIRE(SDL_SaveBMP(globalContainer->gfx->completedFrame(),path.string().c_str()));
                world.game.gameHeader.setGlassCannonLevel(0);
            }
        CHECK(ownerPanels.size()==3);
        gui.clearSelection();
    }

    TEST_CASE("desktop building menu clicks queue authoritative worker and priority orders [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=0});
        glob2test::HeadlessGame world(glob2test::GameOptions{
            .discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
        auto* inn=world.addBuilding("inn",4,4);
        gui.init();
        gui.setSelection(GameGUI::BUILDING_SELECTION,inn);
        gui.drawAll(0); // The menu describes the scene currently drawn.
        const int original=inn->maxUnitWorking;
        const int content=(GAME_GUI_RIGHT_MENU_WIDTH-128)/2;
        // Real desktop sidebar coordinates: worker bar at y=292..308,
        // priority checks at y=336..348. The rightmost arrow increments.
        gui.handleMenuClick(content+119,300,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==1);
        CHECK(inn->maxUnitWorking==original);
        CHECK(gui.displayedMaxUnitWorking(*inn)==original+1);
        gui.handleMenuClick(content+3,342,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==2);
        CHECK(gui.displayedPriority(*inn)==-1);
        while (!gui.orderQueue.empty())
        {
            auto order=gui.orderQueue.front(); gui.orderQueue.pop_front(); order->sender=0;
            gui.executeOrder(order);
        }
        CHECK(inn->maxUnitWorking==original+1); CHECK(inn->priority==-1);
        CHECK_FALSE(gui.pendingFor(inn->gid).pendingMaxUnitWorking.has_value());
        CHECK_FALSE(gui.pendingFor(inn->gid).pendingPriority.has_value());
        globalContainer->liveSpectating=true;
        gui.handleMenuClick(content+119,300,SDL_BUTTON_LEFT);
        CHECK(gui.orderQueue.empty()); globalContainer->liveSpectating=false;
    }
    TEST_CASE("configured keyboard actions toggle presentation and queue pause without changing simulation [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=1024,.height=768});
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=w.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=w.team;
        auto key=[&](Uint32 action) {
            gui.keyboardManager.getKeyboardShortcuts().clear();
            SDL_KeyboardEvent symbol{}; symbol.key=SDLK_F9;
            KeyboardShortcut shortcut; shortcut.addKeyPress(KeyPress(symbol,true)); shortcut.setAction(action);
            gui.keyboardManager.getKeyboardShortcuts().push_back(shortcut);
            gui.handleKey(symbol,true,false);
        };
        const auto checksum=w.checksum();
        const bool bars=gui.drawHealthFoodBar;
        key(GameGUIKeyActions::ToggleDrawInformation); CHECK(gui.drawHealthFoodBar!=bars);
        key(GameGUIKeyActions::ToggleDrawInformation); CHECK(gui.drawHealthFoodBar==bars);
        const bool aids=gui.drawAccessibilityAids;
        key(GameGUIKeyActions::ToggleDrawAccessibilityAids); CHECK(gui.drawAccessibilityAids!=aids);
        key(GameGUIKeyActions::PauseGame);
        REQUIRE(gui.orderQueue.size()==1);
        CHECK(std::dynamic_pointer_cast<PauseGameOrder>(gui.orderQueue.front())!=nullptr);
        CHECK(w.checksum()==checksum);
        gui.orderQueue.clear();
        gui.swallowSpaceKey=true;
        SDL_KeyboardEvent space{}; space.key=SDLK_SPACE;
        gui.handleKey(space,false,false); CHECK_FALSE(gui.isSpaceSet());
        gui.handleKey(space,true,false); CHECK(gui.isSpaceSet());
        CHECK(gui.orderQueue.empty());
    }

    TEST_CASE("home keyboard action wraps the camera and script messages preserve history [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=640,.height=480});
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=w.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=w.team;
        w.team->startPosX=31; w.team->startPosY=31;
        gui.keyboardManager.getKeyboardShortcuts().clear();
        SDL_KeyboardEvent symbol{}; symbol.key=SDLK_F9;
        KeyboardShortcut shortcut; shortcut.addKeyPress(KeyPress(symbol,true)); shortcut.setAction(GameGUIKeyActions::GoToHome);
        gui.keyboardManager.getKeyboardShortcuts().push_back(shortcut);
        gui.viewportX=0; gui.viewportY=0;
        const auto checksum=w.checksum(); gui.handleKey(symbol,true,false);
        CHECK(gui.viewportX>=0); CHECK(gui.viewportX<32); CHECK(gui.viewportY>=0); CHECK(gui.viewportY<32);
        CHECK(gui.viewportX!=0); CHECK(gui.viewportY!=0);
        gui.showScriptText("first message"); gui.hideScriptText();
        gui.showScriptText("second message"); gui.hideScriptText();
        CHECK(w.checksum()==checksum); CHECK(gui.orderQueue.empty());
    }

    TEST_CASE("match and editor dialogs wear the in-match theme; results dialogs stay on paper [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true,.width=1024,.height=768});
        const auto& match=Glob2UI::themeFor(Glob2UI::Surface::Match);
        const auto& paper=Glob2UI::themeFor(Glob2UI::Surface::Frontend);
        CHECK(&match==&Glob2UI::inGameTheme());
        CHECK(&paper==&Glob2UI::frontendTheme());
        CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Editor)==&Glob2UI::inGameTheme());
        CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Results)==&Glob2UI::frontendTheme());
        auto capture=[&](Glob2UI::InGameDialog& dialog,const char* name)
        {
            auto* gfx=globalContainer->gfx;
            dialog.attach(*gfx); dialog.update(0);
            gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
            gfx->setClipRect();
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(60,110,50));
            dialog.draw(0); gfx->nextFrame();
            REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/name).string().c_str()));
        };
        InGameMainScreen menu(false,true,false);
        CHECK(&menu.theme()==&match);
        capture(menu,"dialog-ingame-menu.bmp");
        InGameEndOfGameScreen outcome("Victory",true);
        CHECK(&outcome.theme()==&match);
        capture(outcome,"dialog-ingame-outcome.bmp");
        LoadSaveDialog save("games","game",false,"Save game");
        CHECK(&save.theme()==&match);
        capture(save,"dialog-ingame-save.bmp");
        MapEditMenuScreen editor;
        CHECK(&editor.theme()==&Glob2UI::themeFor(Glob2UI::Surface::Editor));
        capture(editor,"dialog-editor-menu.bmp");
        LoadSaveDialog replay("replays","replay",false,"Save replay",nullptr,nullptr,nullptr,Glob2UI::Surface::Results);
        CHECK(&replay.theme()==&paper);
    }

    TEST_CASE("match dialogs accept return and distinguish continuing from ending a game [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true,.width=640,.height=480});
        SDL_Event key{}; key.type=SDL_EVENT_KEY_DOWN; key.key.key=SDLK_ESCAPE;
        for(bool replay:{false,true}) {
            InGameMainScreen dialog(replay,false,true); dialog.attach(*globals->gfx); dialog.update(0);
            CHECK(dialog.host().find("save")==nullptr);
            CHECK(dialog.host().bounds("return").w>0);
            dialog.eventLogical(key); REQUIRE(dialog.finished()); CHECK(dialog.result()==InGameMainScreen::RETURN_GAME);
        }
        for(bool canContinue:{false,true}) {
            InGameEndOfGameScreen dialog("Outcome",canContinue); dialog.attach(*globals->gfx); dialog.update(0);
            dialog.eventLogical(key); REQUIRE(dialog.finished());
            CHECK(dialog.result()==(canContinue ? InGameEndOfGameScreen::CONTINUE : InGameEndOfGameScreen::QUIT));
        }
        InGameEndOfGameScreen accepted("Outcome",true); accepted.attach(*globals->gfx); accepted.update(0);
        key.key.key=SDLK_RETURN; accepted.eventLogical(key);
        REQUIRE(accepted.finished()); CHECK(accepted.result()==InGameEndOfGameScreen::QUIT);
    }

}
