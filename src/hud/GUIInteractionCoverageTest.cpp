// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GameGUIViewport.h"
#include "GameGUIInternal.h"
#include "touch/GameGUITouch.h"
#include <nlohmann/json.hpp>
#include "Order.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include "EndGameScreen.h"
#include <StringTable.h>
#include <Toolkit.h>
#include "LoadSaveDialog.h"
#include "ConnectionOverlay.h"
#include "hive/HiveDialog.h"
#include "tutorial/TutorialGuidance.h"
#include "render/overlay/MapZoomControls.h"
#include "Engine.h"
#include "GameSpeedControl.h"
#include "MapEditDialog.h"
#include <SDLGraphicContext.h>
#include <SDL3/SDL.h>
#include <ScopedEnvironment.h>
#include <InterfacePresentation.h>
#include <algorithm>
#include <set>

namespace
{
Uint64 renderPanel(GameGUI& gui)
{
    auto* gfx=globalContainer->gfx;
    gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
    gfx->setClipRect();
    gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(0,0,0));
    gui.game.snapshots().invalidateCatalog(); gui.prepareLocalPresentation(gui.frameScene);
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
    TEST_CASE("in-game music selection uses readable phone popup rows [display][artifacts][writes-preferences]")
    {
        glob2test::ScopedEnvironment touch("GLOB2_MOBILE_UI", "1");
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true,
            .width=360,.height=720,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        struct RestoreText { double old=GAGCore::userTextScale; ~RestoreText(){ GAGCore::userTextScale=old; } } restore;
        GameGUI gui;
        globals->settings.mute=0;
        auto* gfx=globals->gfx;
        gfx->setTargetRenderFps(0);
        gfx->setResponsiveViewport(true,800,600);
        for(const auto [width,height]:{std::pair{360,720},std::pair{720,360}})
        for(const double text:{1.,1.5})
        {
            REQUIRE(SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height));
            REQUIRE(SDL_SyncWindow(SDL_GetWindowFromID(gfx->windowID())));
            SDL_Event resize{}; resize.type=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resize);
            GAGCore::userTextScale=text;
            InGameOptionScreen dialog(&gui);
            dialog.attach(*gfx); dialog.update(0);
            auto& host=dialog.host();
            host.scrollIntoView("music-set"); dialog.update(0);
            const auto control=host.bounds("music-set");
            REQUIRE_FALSE(control.empty());
            CHECK(control.w > host.rootBounds().w*.7);
            auto capture=[&](const char* state)
            {
                gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
                gfx->setClipRect(); gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(60,110,50));
                dialog.draw(0);
                const auto name=std::string("music-")+state+"-"+std::to_string(width)+"-"+std::to_string(int(text*100))+".bmp";
                gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/"+name);
                gfx->nextFrame();
            };
            capture("closed");
            host.tapAt(control.center()); dialog.update(0);
            REQUIRE(host.popupOpen());
            REQUIRE(host.popupRoot());
            CHECK(host.popupRoot()->bounds.w > host.rootBounds().w*.7);
            capture("open");
            host.closePopup();
        }
    }

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
            properties["armor"]=0;properties["maxMaterial"]=std::vector<int>(15,0);properties["maxUnitInside"]=3;
            const int feed=instant?0:10,heal=instant?0:100,training=instant?0:250;
            semantics["feeding"]={{"enabled",true},{"duration",feed},{"cost",nlohmann::json::object()}};
            semantics["healing"]={{"enabled",true},{"duration",heal},{"cost",nlohmann::json::object()}};
            semantics["training"]={{"walk",{{"enabled",true},{"duration",training},{"targetLevel",1},{"cost",nlohmann::json::object()}}}};
            semantics["production"]={{"scheduling","weighted_committed_job"},{"fallbackUnit",0},{"initialRatios",{1,0,0}},
                {"recipes",{{"worker",{{"enabled",true},{"duration",1},{"cost",nlohmann::json::object()}}}}}};
            world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.buildingsTypes.loadSprites();world.game.configureBuildingCatalog();
            auto* building=world.game.addBuilding(8,8,typeId,0,0,0);REQUIRE(building);
            auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.localTeam=world.team;
            gui.setSelection(GameGUI::BUILDING_SELECTION,building);gui.prepareLocalPresentation(gui.frameScene);
            auto& panel=gui.frameScene.panels.building;REQUIRE(panel.valid);
            const auto checksum=world.checksum();
            const int rowY=YPOS_BASE_BUILDING+YOFFSET_NAME+YOFFSET_ICON+YOFFSET_B_SEP+YOFFSET_INFOS;
            const int left=gfx->getW()-RIGHT_MENU_RIGHT_OFFSET;
            const auto begin=[&] {
                gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
                gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(24,35,28));
            };
            // Counterfactual immutable records exercise visit clocks without changing the simulation.
            std::array<SnapshotUnit,3> visits{};
            std::array<UnitRef,3> visitRefs{};
            std::array<Uint32,3> visitIndices{0,1,2};
            for (unsigned i=0;i<visits.size();++i) {
                visits[i].identity=UnitRef{Uint16(i),1}; visits[i].gid=i;
                visits[i].displacement=UnitState::DIS_INSIDE; visitRefs[i]=visits[i].identity;
            }
            gui.frameScene.entities.units=visits; gui.frameScene.entities.unitIndex=visitIndices;
            // Render valid visit clocks into the presentation snapshot only.
            // Calling the real row helper without the outer panel clip exposes overflow.
            for(bool smooth : {false,true})for(int duration : {feed,heal,training})
                for(int remaining : {duration,0})for(int delta : {0,127,255})
            {
                CAPTURE(smooth);CAPTURE(duration);CAPTURE(remaining);CAPTURE(delta);
                globals->settings.smoothProgressIndicators=smooth;
                visits[0].insideTimeout=-remaining; visits[0].delta=delta;
                panel.insideUnits=std::span<const UnitRef>(visitRefs).first(1);
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
            SnapshotBuilding panelRecord=panel.state(); panelRecord.inside.count=3; panel.record=&panelRecord;
            visits[0].insideTimeout=-feed; visits[0].delta=64;
            visits[1].insideTimeout=-heal; visits[1].delta=127;
            visits[2].insideTimeout=-training; visits[2].delta=0;
            panel.insideUnits=visitRefs;
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
            gui.game.snapshots().invalidateCatalog(); gui.prepareLocalPresentation(gui.frameScene);
            gui.drawChoiceInfoPanel("inn"); gfx->nextFrame();
            auto* frame=gfx->completedFrame(); REQUIRE(frame);
            Uint64 hash=1469598103934665603ull;
            const auto* pixels=static_cast<const Uint8*>(frame->pixels);
            for (int i=0; i<frame->h*frame->pitch; ++i) hash=(hash^pixels[i])*1099511628211ull;
            return hash;
        };
        const auto original=render();
        site->maxMaterial[WOOD]+=17;
        CHECK(render()==original);
        site->semantics.constructionCost[WOOD]+=17;
        const auto changedCost=render(); CHECK(changedCost!=original);
        for (int resource=HAPPINESS_BASE; resource<MaterialCount; ++resource)
        {
            const auto before=render();
            site->semantics.constructionCost[resource]=resource+1;
            if (resource<8) CHECK(render()!=before);
            else
            {
                CHECK(render()==before); // A configured cost alone must not expose absent materials.
                world.team->teamMaterials[resource]=1;
                CHECK(render()!=before); // Stored stock counts as presence without a natural deposit.
                if (resource==int(materialIndex(MaterialId::Fabric)))
                    REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"construction-fabric-cost.bmp").string().c_str()));
                site->semantics.constructionCost[resource]=0;
                CHECK(render()==before); // Present but unused inputs stay hidden.
            }
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
        from["properties"]["maxMaterial"][STONE]=15;
        for(int r=HAPPINESS_BASE;r<MaterialCount;++r)from["properties"]["maxMaterial"][r]=20;
        from["semantics"]["market"]["interTeamFruitExchange"]=true;
        const int producer=world.game.buildingsTypes.getFinishedTypeNum("swarm");
        from["semantics"]["production"]=catalog["variants"][producer]["semantics"]["production"];
        from["semantics"]["repairCost"]={{"wood",8},{"orange",13},{"prune",17}};
        auto& to=catalog["variants"][next];to["properties"]["hpMax"]=1379;
        to["properties"]["maxMaterial"][WOOD]=7;to["properties"]["maxMaterial"][STONE]=0;
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
            if(extract) { gui.game.snapshots().invalidateCatalog(); gui.prepareLocalPresentation(gui.frameScene); }
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
        const auto gold=materialIndex(MaterialId::Gold);
        const auto priorGold=target->maxMaterial[gold];
        target->maxMaterial[gold]=19;render();CHECK(frameHash()==original);
        worker->carriedMaterial=gold;render();CHECK(frameHash()!=original);
        worker->carriedMaterial=-1;target->maxMaterial[gold]=priorGold;render();CHECK(frameHash()==original);
        target->maxMaterial[WOOD]=9;render();CHECK(frameHash()!=original);
        const auto changedWood=frameHash();target->maxMaterial[STONE]=3;render();CHECK(frameHash()!=changedWood);
        target->maxMaterial[STONE]=0;target->maxMaterial[WOOD]=7;render();
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
            gui.game.snapshots().invalidateCatalog(); gui.prepareLocalPresentation(gui.frameScene);
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
                if (type==WORKER) unit->carriedMaterial=WHEAT;
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
        glob2test::drawGUI(gui,0); // The menu describes the scene currently drawn.
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
        gui.game.finishOrderPhase(false);
        gui.prepareLocalPresentation();
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

    TEST_CASE("initial camera survives waiting for the first presentation [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=640,.height=480});
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=w.gui;
        gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=w.team;
        w.team->startPosX=31; w.team->startPosY=31;
        gui.adjustInitialViewport();
        const int x=gui.viewportX, y=gui.viewportY;
        PresentationFrame unavailable;
        gui.setPublishedScene(&unavailable);
        gui.updateCamera();
        // The client scrolling loop copies these coordinates back even while
        // asynchronous extraction has not supplied map dimensions yet.
        CHECK(gui.camera.tileX()==x);
        CHECK(gui.camera.tileY()==y);
        gui.viewportX=gui.camera.tileX(); gui.viewportY=gui.camera.tileY();
        gui.setPublishedScene(nullptr);
        gui.prepareLocalPresentation();
        gui.updateCamera();
        CHECK(gui.viewportX==x);
        CHECK(gui.viewportY==y);
    }

    TEST_CASE("home keyboard action wraps the camera and script messages preserve history [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=640,.height=480});
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=w.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=w.team;
        w.team->startPosX=31; w.team->startPosY=31;
        gui.game.snapshots().invalidateBoundary();
        gui.prepareLocalPresentation();
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

    TEST_CASE("local victory popup and results show the same deciding condition [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true,.width=640,.height=480});
        glob2test::HeadlessGame world(glob2test::GameOptions{.teams=2,.discovered=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui;
        gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
        gui.init();
        for (int t=0;t<2;++t) world.game.teams[t]->allies=world.game.teams[t]->me;
        world.game.prestigeToReach=10;
        world.team->prestige=10;
        world.game.prestigeSyncStep(); world.game.wonSyncStep();
        REQUIRE(world.team->hasWon);
        const auto checksum=world.checksum();
        glob2test::drawGUI(gui,0);
        gui.checkWonConditions();
        REQUIRE(gui.inGameMenu==GameGUI::IGM_END_OF_GAME);
        auto* dialog=dynamic_cast<InGameEndOfGameScreen*>(gui.gameMenuScreen.get());
        REQUIRE(dialog);
        dialog->update(0);
        CHECK(dialog->title==GAGCore::Toolkit::getStringTable()->getString("[you have won]"));
        auto* explanation=dialog->host().find("outcome/reason");
        REQUIRE(explanation);
        CHECK(explanation->accessibleText()==GAGCore::Toolkit::getStringTable()->getString("[outcome reason prestige]"));
        CHECK(EndGameScreen::describe(world.game,*world.team).reason==explanation->accessibleText());
        CHECK(world.checksum()==checksum);
        CHECK(dialog->host().bounds("ok").bottom()<=globalContainer->gfx->getH());
        auto* gfx=globalContainer->gfx;
        gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
        gfx->setClipRect(); gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(60,110,50));
        dialog->draw(0); gfx->nextFrame();
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"victory-prestige-reason.bmp").string().c_str()));
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
        InGameEndOfGameScreen outcome("Victory",true,{},true,"Win probability threshold reached.");
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

TEST_CASE("resource inspectors show infinity only for stocked infinite yields [display:1024x768][artifacts]" * doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
    auto entry=definitions["resources"][0];entry.erase("requiredExperiment");
    entry["key"]="test:inspector-infinite";
    entry["properties"]={{"primaryMaterial","wood"},{"persistsWhenEmpty",true}};
    entry["yields"]={{"wood",{{"capacity",5},{"initial",2},{"consumption","one"}}},
        {"food",{{"capacity",4},{"initial",0},{"consumption","infinite"}}}};
    definitions["resources"].push_back(entry);map.installResourceDefinitions(definitions.dump());
    const auto id=map.resourceRegistry().find("test:inspector-infinite");REQUIRE(id.has_value());
    map.setResource(20,20,*id,0);const auto index=map.coordToIndex(20,20);
    auto& gui=world.gui;gui.init();REQUIRE(gui.touch);gui.localTeamNo=0;gui.localPlayer=0;gui.localTeam=world.team;
    gui.setSelection(GameGUI::RESOURCE_SELECTION,unsigned(index));
    auto* gfx=globals->gfx;
    const auto inspect=[&](const std::string& expected) {
        gui.game.snapshots().invalidateCatalog(); gui.prepareLocalPresentation(gui.frameScene);
        const auto info=gui.touch->resourceInfo();REQUIRE(info.has_value());CHECK(info->amount==expected);
        gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(0,0,0));
        gui.drawResourceInfos();gfx->nextFrame();
        auto* frame=gfx->completedFrame();REQUIRE(frame);
        const int left=(gfx->getW()-GAME_GUI_RIGHT_MENU_WIDTH)*frame->w/gfx->getW();
        const int top=(YPOS_BASE_RESOURCE+2*YOFFSET_TEXT_PARA+40)*frame->h/gfx->getH();
        const auto* pixels=static_cast<const Uint8*>(frame->pixels);Uint64 hash=1469598103934665603ull;
        for(int y=top;y<frame->h;++y)
            for(int x=left*SDL_BYTESPERPIXEL(frame->format);x<frame->w*SDL_BYTESPERPIXEL(frame->format);++x)
                hash=(hash^pixels[y*frame->pitch+x])*1099511628211ull;
        return hash;
    };
    const auto prefix=getMaterialName(materialIndex(MaterialId::Wood))+": 2/5\n"+
        getMaterialName(materialIndex(MaterialId::Food))+": ";
    const auto empty=inspect(prefix+"0/4");
    REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"resource-infinite-empty.bmp").string().c_str()));
    map.setMaterialAmount(index,MaterialId::Food,1);const auto stocked=inspect(prefix+"∞");CHECK(stocked!=empty);
    REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"resource-infinite-stocked.bmp").string().c_str()));
    map.setMaterialAmount(index,MaterialId::Food,2);CHECK(inspect(prefix+"∞")==stocked);
    map.setMaterialAmount(index,MaterialId::Food,0);CHECK(inspect(prefix+"0/4")==empty);
    map.setMaterialAmount(index,MaterialId::Wood,0);
    inspect(getMaterialName(materialIndex(MaterialId::Wood))+": 0/5\n"+
        getMaterialName(materialIndex(MaterialId::Food))+": 0/4");
    CHECK(map.getResource(index).type==resourceIndex(*id));
}


TEST_CASE("Scenario messages wrap unspaced UTF-8 text and continuation indents without changing simulation [display]" * doctest::test_suite("ScenarioMessages"))
{
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=640,.height=480});
    glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true,.header=true});
    std::string text;
    for (int i = 0; i < 20; ++i) text += "日本語の訓練案内。";
    const auto checksum = world.checksum();
    std::vector<std::string> lines;
    world.gui.setMultiLine(text, &lines, "  ");
    REQUIRE(lines.size() > 1);
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        CHECK_FALSE(lines[i].empty());
        CHECK(globalContainer->standardFont->getStringWidth(lines[i].c_str()) <=
            globalContainer->gfx->getW() - GAME_GUI_RIGHT_MENU_WIDTH - 64);
        joined += i ? lines[i].substr(2) : lines[i];
    }
    CHECK(joined == text);
    lines.clear();
    world.gui.setMultiLine("first\nsecond", &lines, "  ");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "first");
    CHECK(lines[1] == "  second");
    lines.clear();
    world.gui.setMultiLine("", &lines);
    CHECK(lines.empty());
    CHECK(world.checksum() == checksum);
    CHECK(world.gui.orderQueue.empty());
}


TEST_CASE("desktop HUD descriptions share human orders and reject selection replacement [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1024, .height = 768});
	glob2test::HeadlessGame world(
		{.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.localTeam = world.team;
	auto *first = world.addBuilding("swarm", 4, 4);
	auto *second = world.addBuilding("swarm", 20, 20);
	REQUIRE(first);
	REQUIRE(second);
	gui.setSelection(GameGUI::BUILDING_SELECTION, first);
	gui.prepareLocalPresentation(gui.frameScene);
	auto descriptors = gui.describeHudControls();
	auto worker = std::find_if(descriptors.begin(), descriptors.end(),
							   [](const auto &c) { return c.key.ends_with("/workers"); });
	REQUIRE(worker != descriptors.end());
	REQUIRE(worker->enabled);
	const auto key = worker->key;
	const auto original = first->maxUnitWorking;
	const int requested = original == 3 ? 2 : 3;
	const int secondOriginal = second->maxUnitWorking;
	auto &host = gui.hudInspectionHost();
	const auto before = host.inspect();
	auto target = host.find(key);
	REQUIRE(target);
	std::string error;
	// Setting the current value is valid even though human request helpers
	// deliberately deduplicate it; no command should be submitted.
	CHECK(host.act(key, {GAGGUI::ui::ActionKind::SetValue, original}, error));
	CHECK(error.empty());
	CHECK(gui.orderQueue.empty());
	const auto retainedWorkerAction = worker->perform;
	const auto ratio = std::find_if(descriptors.begin(), descriptors.end(),
		[](const auto &c) { return c.key.ends_with("/production/0"); });
	REQUIRE(ratio != descriptors.end());
	CHECK(host.act(ratio->key, {GAGGUI::ui::ActionKind::SetValue, std::get<int>(ratio->value)}, error));
	CHECK(gui.orderQueue.empty());
	CHECK_FALSE(host.act(key, {GAGGUI::ui::ActionKind::SetValue, -1}, error));
	CHECK_FALSE(host.act(key, {GAGGUI::ui::ActionKind::SetValue, std::string("invalid")}, error));
	CHECK(gui.orderQueue.empty());
	error.clear();
	CHECK(host.act(key, {GAGGUI::ui::ActionKind::SetValue, requested}, error));
	CHECK(gui.displayedMaxUnitWorking(*first) == requested);
	CHECK(first->maxUnitWorking == original);
	CHECK(gui.orderQueue.size() == 1);
	CHECK(gui.hudInspectionHost().act(key, {GAGGUI::ui::ActionKind::SetValue, requested}, error));
	CHECK(error.empty());
	CHECK(gui.orderQueue.size() == 1); // The pending UI value also deduplicates successfully.

	gui.handleMenuClickBuildingSelection(RIGHT_MENU_OFFSET + SCROLLBOX_BAR_WIDTH - 1,
										 worker->bounds.y + 8, SDL_BUTTON_LEFT);
	CHECK(gui.displayedMaxUnitWorking(*first) == requested + 1);
	CHECK(gui.orderQueue.size() == 2);
	globals->liveSpectating = true;
	CHECK_FALSE(retainedWorkerAction({GAGGUI::ui::ActionKind::SetValue, requested + 1}));
	CHECK(gui.orderQueue.size() == 2); // A matching value must not bypass ownership.
	globals->liveSpectating = false;
	// In-flight commands bind the selection's entity generation, not just its slot.
	auto findRevision = [&](auto &&self, const auto &nodes) -> std::uint64_t
	{
		for (const auto &c : nodes)
		{
			if (c.key == key)
				return c.revision;
			if (auto r = self(self, c.children))
				return r;
		}
		return 0;
	};
	auto pending = GAGGUI::ui::semanticDispatch();
	pending->surface = before.instance;
	pending->key = key;
	pending->revision = findRevision(findRevision, before.controls);
	pending->action = {GAGGUI::ui::ActionKind::SetValue, 7};
	gui.setSelection(GameGUI::BUILDING_SELECTION, second);
	gui.prepareLocalPresentation(gui.frameScene);
	auto &updated = gui.hudInspectionHost();
	CHECK(updated.event(pending->event()));
	CHECK_FALSE(pending->error.empty());
	CHECK(gui.orderQueue.size() == 2);
	CHECK(second->maxUnitWorking == secondOriginal);
	CHECK_FALSE(retainedWorkerAction({GAGGUI::ui::ActionKind::SetValue, secondOriginal}));
	gui.orderQueue.clear();
	auto *flag = world.addBuilding("warflag", 30, 30);
	REQUIRE(flag);
	gui.game.snapshots().invalidateBoundary(); // A new entity was attached outside a simulation step.
	gui.setSelection(GameGUI::BUILDING_SELECTION, flag);
	gui.prepareLocalPresentation(gui.frameScene);
	const auto flagControls = gui.describeHudControls();
	const auto range = std::find_if(flagControls.begin(), flagControls.end(),
		[](const auto &c) { return c.key.ends_with("/range"); });
	REQUIRE(range != flagControls.end());
	error.clear();
	CHECK(gui.hudInspectionHost().act(range->key,
		{GAGGUI::ui::ActionKind::SetValue, std::get<int>(range->value)}, error));
	CHECK(error.empty());
	CHECK(gui.orderQueue.empty());

	gui.clearSelection();
	gui.prepareLocalPresentation(gui.frameScene);
}

TEST_CASE("spectator descriptions expose toggles and prohibit building orders [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1024, .height = 768});
	glob2test::HeadlessGame world({.teams = 2,
								   .discovered = true,
								   .clearImmobile = true,
								   .loadDefaultRace = true,
								   .header = true});
	auto &gui = world.gui;
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.localTeam = world.team;
	struct Restore
	{
		bool old;
		~Restore() { globalContainer->liveSpectating = old; }
	} restore{globals->liveSpectating};
	globals->liveSpectating = true;
	gui.replayDisplayMode = GameGUI::RDM_REPLAY_VIEW;
	gui.prepareLocalPresentation(gui.frameScene);
	std::string error;
	auto &host = gui.hudInspectionHost();
	CHECK(host.act("spectator/flags", {GAGGUI::ui::ActionKind::SetValue, false}, error));
	CHECK_FALSE(globals->replayShowFlags);
	CHECK(host.act("spectator/flags", {GAGGUI::ui::ActionKind::Activate, {}}, error));
	CHECK(globals->replayShowFlags);
	CHECK(gui.orderQueue.empty());
	const auto descriptions = gui.describeHudControls();
	CHECK(std::count_if(descriptions.begin(), descriptions.end(),
						[](const auto &c) { return c.key.starts_with("spectator/team/"); }) == 2);
}

TEST_CASE("connection controls share layout and modal ownership with gameplay input [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1024, .height = 768});
	glob2test::HeadlessGame world(
		{.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.prepareLocalPresentation(gui.frameScene);
	ConnectionSnapshot state;
	int leaves = 0;
	state.rows.push_back({.seat = 0, .name = "Player", .pingMs = 42, .local = true});
	gui.connectionOverlay = std::make_unique<ConnectionOverlay>();
	gui.connectionOverlay->source = [&] { return state; };
	gui.connectionOverlay->leave = [&] { ++leaves; };
	auto &host = gui.hudInspectionHost();
	std::string error;
	CHECK(host.act("connection/details", {}, error));
	REQUIRE(gui.connectionOverlay->blocksGameplay());
	const auto modal = gui.describeHudControls();
	CHECK(std::none_of(modal.begin(), modal.end(),
					   [](const auto &c) { return c.key == "game/menu"; }));
	CHECK(gui.hudInspectionHost().act("connection/close", {}, error));
	CHECK_FALSE(gui.connectionOverlay->blocksGameplay());
	const auto initial = gui.hudInspectionHost().inspect();
	auto revision = [&](auto &&self, const auto &nodes) -> std::uint64_t
	{
		for (const auto &c : nodes)
		{
			if (c.key == "game/menu")
				return c.revision;
			if (const auto value = self(self, c.children))
				return value;
		}
		return 0;
	};
	auto pending = GAGGUI::ui::semanticDispatch();
	pending->surface = initial.instance;
	pending->key = "game/menu";
	pending->revision = revision(revision, initial.controls);
	REQUIRE(pending->revision != 0);
	// The network source changes after inspection, before actual consumption.
	state.card = ConnectionSnapshot::Card::Reconnecting;
	auto event = pending->event();
	gui.processEvent(&event);
	CHECK(pending->consumed);
	CHECK_FALSE(pending->error.empty());
	CHECK(gui.activeDialog() == nullptr);
	CHECK(gui.hudInspectionHost().act("connection/leave", {}, error));
	CHECK(leaves == 1);
	SDL_Event motion{};
	motion.type = SDL_EVENT_MOUSE_MOTION;
	CHECK(gui.connectionOverlay->handle(motion));
	SDL_Event quit{};
	quit.type = SDL_EVENT_QUIT;
	CHECK_FALSE(gui.connectionOverlay->handle(quit));
	CHECK(gui.orderQueue.empty());
}

TEST_CASE("connection card refresh precedes Hive semantic and human ownership [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1024, .height = 768});
	glob2test::HeadlessGame world(
		{.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.prepareLocalPresentation(gui.frameScene);
	struct View final : Hive::CommanderView
	{
		int stops = 0;
		bool available() const override { return true; }
		bool local() const override { return true; }
		Hive::Json standingOrders() const override { return Hive::Json::array(); }
		void stop() override { ++stops; }
		void change(const std::string &, const std::string &) override {}
	};
	auto view = std::make_shared<View>();
	gui.hiveCards = std::make_unique<Hive::Dialog>(view);
	gui.hiveCards->attach(*globals->gfx);
	gui.hiveCards->update(0);
	const auto before = gui.hiveCards->host().inspect();
	auto revision = [&](auto &&self, const auto &nodes) -> std::uint64_t
	{
		for (const auto &c : nodes)
		{
			if (c.key == "hive/stop")
				return c.revision;
			if (const auto value = self(self, c.children))
				return value;
		}
		return 0;
	};
	auto pending = GAGGUI::ui::semanticDispatch();
	pending->surface = before.instance;
	pending->key = "hive/stop";
	pending->revision = revision(revision, before.controls);
	REQUIRE(pending->revision != 0);
	const auto hit = gui.hiveCards->host().bounds("hive/stop").center();
	ConnectionSnapshot state;
	int reads = 0;
	gui.connectionOverlay = std::make_unique<ConnectionOverlay>();
	gui.connectionOverlay->source = [&]
	{
		++reads;
		return state;
	};
	(void)gui.hudInspectionHost();
	const int prior = reads;
	state.card = ConnectionSnapshot::Card::Reconnecting;
	auto event = pending->event();
	gui.processEvent(&event);
	CHECK_FALSE(pending->consumed);
	CHECK(view->stops == 0);
	CHECK(reads == prior + 1);
	SDL_Event mouse{};
	mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	mouse.button.button = SDL_BUTTON_LEFT;
	mouse.button.x = hit.x;
	mouse.button.y = hit.y;
	gui.processEvent(&mouse);
	CHECK(view->stops == 0);
}

TEST_CASE("tutorial semantic controls bind lesson step and destination [display]" * doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
    int navigations=0;
    Tutorial::Guidance guidance([&](int){++navigations;});
    auto lesson=std::make_shared<Tutorial::Model>();
    lesson->lessonId="test";lesson->stepId="first";lesson->count=2;lesson->index=0;
    lesson->title="[intro help]";lesson->destination=Tutorial::Destination{8,8,"Target"};
    guidance.model(lesson);guidance.attach(*globals->gfx);guidance.update(0);
    const auto initial=guidance.host().inspect();
    auto revision=[&](auto &&self,const auto &nodes,const std::string &key)->std::uint64_t
    {
        for(const auto &c:nodes){if(c.key==key)return c.revision;if(const auto value=self(self,c.children,key))return value;}return 0;
    };
    auto pending=GAGGUI::ui::semanticDispatch();pending->surface=initial.instance;
    pending->key="intro/destination";pending->revision=revision(revision,initial.controls,pending->key);
    REQUIRE(pending->revision!=0);
    auto next=std::make_shared<Tutorial::Model>(*lesson);next->destination->x=12;
    guidance.model(next);guidance.update(0);
    CHECK(guidance.host().event(pending->event()));CHECK_FALSE(pending->error.empty());CHECK(navigations==0);
    const auto second=guidance.host().inspect();
    auto oldHelp=GAGGUI::ui::semanticDispatch();oldHelp->surface=second.instance;
    oldHelp->key="intro/help";oldHelp->revision=revision(revision,second.controls,oldHelp->key);
    REQUIRE(oldHelp->revision!=0);
    auto step=std::make_shared<Tutorial::Model>(*next);step->stepId="second";
    guidance.model(step);guidance.update(0);
    CHECK(guidance.host().event(oldHelp->event()));CHECK_FALSE(oldHelp->error.empty());
}

TEST_CASE("desktop zoom speed and events share higher-surface ownership for semantic and human input [display][writes-preferences]" *
          doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 1024, .height = 768,
        .beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::Software; }});
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI", "0");
    GameGUI gui;
    auto map = Engine::loadMapHeader("maps/balanced.map");
    GameHeader players;
    players.setNumberOfPlayers(1);
    players.getBasePlayer(0) = BasePlayer(0, "Zoom ownership", 0, BasePlayer::P_LOCAL);
    REQUIRE(gui.loadFromHeaders(map, players, true, true));
    gui.localPlayer = gui.localTeamNo = 0;
    gui.adjustLocalTeam();
    gui.prepareLocalPresentation();
    gui.updateCamera();
    gui.camera.setZoom(1, gui.camera.width / 2, gui.camera.height / 2);
    auto findControl = [&](auto &&self, const auto &nodes, const std::string &key) -> const GAGGUI::ui::ControlSnapshot * {
        for (const auto &node : nodes) {
            if (node.key == key) return &node;
            if (const auto *child = self(self, node.children, key)) return child;
        }
        return nullptr;
    };
    {
        const auto initial = gui.hudInspectionHost().inspect();
        const auto *middle = findControl(findControl, initial.controls, "game/zoom/fit");
        const auto *zoomIn = findControl(findControl, initial.controls, "game/zoom/in");
        REQUIRE(middle); REQUIRE(zoomIn);
        CHECK(middle->label == Glob2UI::libraryText("Fit"));
        auto pending = GAGGUI::ui::semanticDispatch();
        pending->surface = initial.instance; pending->key = middle->key; pending->revision = middle->revision;
        gui.camera.setZoom(1.25, gui.camera.width / 2, gui.camera.height / 2);
        const auto changed = gui.hudInspectionHost().inspect();
        const auto *current = findControl(findControl, changed.controls, pending->key);
        const auto *sameZoomIn = findControl(findControl, changed.controls, "game/zoom/in");
        REQUIRE(current); REQUIRE(sameZoomIn);
        CHECK(current->label == "100%");
        CHECK(current->revision != middle->revision);
        CHECK(sameZoomIn->revision == zoomIn->revision); // Its action and geometry did not change.
        auto stale = pending->event();
        CHECK(gui.hudInspectionHost().event(stale));
        CHECK_FALSE(pending->error.empty());
        CHECK(gui.camera.zoom == doctest::Approx(1.25));
        const auto *speed = findControl(findControl, changed.controls, "game/speed");
        REQUIRE(speed);
        const int oldSpeed = globals->settings.gameSpeed;
        globals->settings.gameSpeed = GameSpeedControl::faster(oldSpeed);
        const auto unrelated = gui.hudInspectionHost().inspect();
        const auto *afterSpeed = findControl(findControl, unrelated.controls, "game/speed");
        const auto *afterMiddle = findControl(findControl, unrelated.controls, pending->key);
        REQUIRE(afterSpeed); REQUIRE(afterMiddle);
        CHECK(afterSpeed->value != speed->value);
        CHECK(afterMiddle->revision == current->revision);
        globals->settings.gameSpeed = oldSpeed;
        gui.camera.setZoom(1, gui.camera.width / 2, gui.camera.height / 2);
    }
    const auto button = mapZoomControlBounds(gui.camera, true, true)[2];
    REQUIRE(button.w >= 4);
    REQUIRE(button.h > 0);
    const GAGGUI::ui::Rect requestedCover{button.x, button.y, button.w / 2, button.h};
    const GAGGUI::ui::Point exposed{button.x + button.w * 3 / 4, button.center().y};
    auto cover = [&](GAGGUI::ui::Rect area) {
        auto model = std::make_shared<Tutorial::Model>();
        model->lessonId = "zoom-ownership"; model->stepId = "covered"; model->count = 1;
        model->title = model->instruction = model->touchInstruction = "[intro help]";
        gui.tutorialGuidance = std::make_unique<Tutorial::Guidance>([](int) {});
        gui.tutorialGuidance->model(model);
        gui.tutorialGuidance->attach(*globals->gfx);
        auto &guide = gui.tutorialGuidance->host();
        guide.layoutIfNeeded();
        // A dialog Host lays out its content; panelBounds adds the painted
        // padding. Place that actual outer right edge halfway across the target.
        const int padding = guide.metrics().padding;
        const int width = std::max(1, std::min(240, area.right() - 2 * padding));
        const GAGGUI::ui::Rect inner{area.right() - padding - width,
            area.y + padding, width, 120};
        guide.setAvailable([inner](const auto &, const auto &) { return inner; });
        guide.setPlacement([](GAGGUI::ui::Size, GAGGUI::ui::Rect bounds) { return bounds; });
        guide.invalidate(); guide.layoutIfNeeded();
        const auto panel = gui.tutorialGuidance->panelBounds();
        REQUIRE(panel == inner.inset(-padding));
        REQUIRE(panel.right() == area.right());
        return panel;
    };
    const auto covered = cover(requestedCover);
    const auto overlap = button.intersect(covered);
    REQUIRE(overlap.w > 0);
    REQUIRE(overlap.h > 0);
    REQUIRE(overlap.w < button.w);
    REQUIRE(button.contains(exposed));
    REQUIRE_FALSE(covered.contains(exposed));
    const auto inspected = gui.hudInspectionHost().inspect();
    const auto *target = findControl(findControl, inspected.controls, "game/zoom/in");
    REQUIRE(target);
    CHECK(target->bounds == button);
    CHECK(target->enabled);
    CHECK(target->blocked);
    std::string error;
    const double before = gui.camera.zoom;
    CHECK_FALSE(gui.hudInspectionHost().act("game/zoom/in", {}, error));
    CHECK(error.find("blocked") != std::string::npos);
    CHECK(gui.camera.zoom == before);
    auto click = [&](GAGGUI::ui::Point point, Uint8 mouseButton = SDL_BUTTON_LEFT) {
        SDL_MouseButtonEvent event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button = mouseButton;
        event.x = point.x; event.y = point.y;
        // Enter the ordinary human handler after the higher-surface hit route;
        // this point is outside Guide, so production routing reaches it too.
        gui.handleMouseButtonDown(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        gui.handleMouseButtonUp(event);
    };
    click(exposed);
    CHECK(gui.camera.zoom == before);
    CHECK(gui.orderQueue.empty());
    gui.tutorialGuidance.reset();
    click(exposed);
    const double humanZoom = gui.camera.zoom;
    CHECK(humanZoom > before);
    gui.camera.setZoom(before, gui.camera.width / 2, gui.camera.height / 2);
    error.clear();
    REQUIRE(gui.hudInspectionHost().act("game/zoom/in", {}, error));
    CHECK(gui.camera.zoom == doctest::Approx(humanZoom));
    const auto box = mapZoomControlsLayout(gui.camera, true, true);
    REQUIRE(box.readout() < box.width);
    const double priorReadout = gui.camera.zoom;
    click({box.x + (box.readout() + box.width) / 2, box.y + box.height / 2});
    CHECK(gui.camera.zoom == priorReadout);
    globals->settings.gameSpeed = GameSpeedControl::presets.front();
    const auto speed = gui.hudInspectionHost().bounds("game/speed");
    REQUIRE(speed.w > 4);
    const GAGGUI::ui::Rect speedCover{speed.x, speed.y, speed.w / 2, speed.h};
    const GAGGUI::ui::Point speedHit{speed.x + speed.w * 3 / 4, speed.center().y};
    const auto speedCoverPanel = cover(speedCover);
    const auto speedCoverOverlap = speed.intersect(speedCoverPanel);
    REQUIRE(speedCoverOverlap.w > 0);
    REQUIRE(speedCoverOverlap.h > 0);
    REQUIRE(speedCoverOverlap.w < speed.w);
    REQUIRE(speed.contains(speedHit));
    REQUIRE_FALSE(gui.tutorialGuidance->panelBounds().contains(speedHit));
    const auto speedSnapshot = gui.hudInspectionHost().inspect();
    const auto *blockedSpeed = findControl(findControl, speedSnapshot.controls, "game/speed");
    REQUIRE(blockedSpeed); CHECK(blockedSpeed->enabled); CHECK(blockedSpeed->blocked);
    const int oldSpeed = globals->settings.gameSpeed;
    const int nextSpeed = GameSpeedControl::faster(oldSpeed);
    error.clear();
    CHECK_FALSE(gui.hudInspectionHost().act("game/speed", {GAGGUI::ui::ActionKind::SetValue, nextSpeed}, error));
    CHECK(error.find("blocked") != std::string::npos);
    click(speedHit);
    click(speedHit, SDL_BUTTON_RIGHT);
    CHECK(globals->settings.gameSpeed == oldSpeed);
    gui.tutorialGuidance.reset();
    click(speedHit);
    CHECK(globals->settings.gameSpeed == nextSpeed);
    click(speedHit, SDL_BUTTON_RIGHT);
    CHECK(globals->settings.gameSpeed == oldSpeed);
    gui.eventFeed.clear();
    gui.eventFeed.ingest({GEUnitUnderAttack, WORKER, 0, 20, 25, "Ownership event", GAGCore::Color(255, 255, 255)},
        SDL_GetTicks(), [](int, int, int, int) { return std::int64_t(0); });
    const auto eventRows = gui.describeEventControls();
    REQUIRE(eventRows.size() == 1);
    const auto row = eventRows.front();
    REQUIRE(row.bounds.w > 4);
    const GAGGUI::ui::Rect rowCover{row.bounds.x, row.bounds.y, row.bounds.w / 2, row.bounds.h};
    const GAGGUI::ui::Point rowHit{row.bounds.x + row.bounds.w * 3 / 4, row.bounds.center().y};
    const auto rowCoverPanel = cover(rowCover);
    const auto rowCoverOverlap = row.bounds.intersect(rowCoverPanel);
    REQUIRE(rowCoverOverlap.w > 0);
    REQUIRE(rowCoverOverlap.h > 0);
    REQUIRE(rowCoverOverlap.w < row.bounds.w);
    REQUIRE(row.bounds.contains(rowHit));
    REQUIRE_FALSE(gui.tutorialGuidance->panelBounds().contains(rowHit));
    const auto rowSnapshot = gui.hudInspectionHost().inspect();
    const auto *blockedRow = findControl(findControl, rowSnapshot.controls, row.key);
    REQUIRE(blockedRow); CHECK(blockedRow->enabled); CHECK(blockedRow->blocked);
    const auto origin = std::pair{gui.viewportX, gui.viewportY};
    error.clear();
    CHECK_FALSE(gui.hudInspectionHost().act(row.key, {}, error));
    CHECK(error.find("blocked") != std::string::npos);
    click(rowHit);
    CHECK((std::pair{gui.viewportX, gui.viewportY} == origin));
    gui.tutorialGuidance.reset();
    click(rowHit);
    const auto humanOrigin = std::pair{gui.viewportX, gui.viewportY};
    CHECK(humanOrigin != origin);
    gui.viewportX = origin.first; gui.viewportY = origin.second;
    gui.updateCamera();
    error.clear();
    REQUIRE(gui.hudInspectionHost().act(row.key, {}, error));
    CHECK((std::pair{gui.viewportX, gui.viewportY} == humanOrigin));

    CHECK(gui.orderQueue.empty());
}

TEST_CASE("statistics navigation cycles the active visible catalog for human and semantic input [display]" *
          doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI", "1");
    glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true,
        .width = 360, .height = 720, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
    glob2test::HeadlessGame world({.discovered = true, .clearImmobile = true,
        .loadDefaultRace = true, .header = true});
    auto &gui = world.gui;
    gui.localPlayer = gui.localTeamNo = 0;
    gui.localTeam = world.team;
    gui.touch = std::make_unique<GameGUITouch>(gui);
    gui.prepareLocalPresentation(gui.frameScene);
    REQUIRE(gui.touch);
    gui.touch->menuAction(3);
    REQUIRE(gui.touch->statsOpen);
    REQUIRE(gui.touch->statsCatalog.size() > 2);
    // The active sheet can filter metrics (for example, player-visible data).
    // Its arrows must cycle that published list, not the unfiltered registry.
    gui.touch->statsCatalog.resize(2);
    gui.touch->statsMetric = 0;
    const auto before = gui.game.checkSum();
    std::string error;
    REQUIRE(gui.hudInspectionHost().act("touch/statistics/previous", {}, error));
    REQUIRE(gui.touch->statsMetric == 1);
    REQUIRE(gui.hudInspectionHost().act("touch/statistics/next", {}, error));
    REQUIRE(gui.touch->statsMetric == 0);
    auto centre = [](GAGCore::ViewRect rect) {
        return GAGCore::ViewPoint{rect.x + rect.w / 2, rect.y + rect.h / 2};
    };
    gui.touch->interfaceTap(centre(gui.touch->statsLayout().previous));
    REQUIRE(gui.touch->statsMetric == 1);
    gui.touch->interfaceTap(centre(gui.touch->statsLayout().next));
    REQUIRE(gui.touch->statsMetric == 0);
    CHECK(gui.orderQueue.empty());
    CHECK(gui.game.checkSum() == before);
}
