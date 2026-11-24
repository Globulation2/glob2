#include <PerformanceTelemetry.h>
#include "Map.h"
#include "sim/snapshot/SnapshotStore.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "InGameStatistics.h"
#include "stats/TeamStatChart.h"
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "DrawFixtures.h"
#include "GameGUIViewport.h"
#include "MapZoomControls.h"
#include "GameGUIInternal.h"
#include "Player.h"
#include "UnitConsts.h"
#include "hive/HiveWorker.h"
#include "hive/LocalCommander.h"
#include "hive/GameStats.h"
#include "GameGUIToolManager.h"
#include <functional>
#include "touch/GameGUITouch.h"
#include <nlohmann/json.hpp>
#include "Order.h"
#include "Version.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include "UIRecordingCanvas.h"
#include "EndGameScreen.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <FormatableString.h>
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
#include <SDL3_image/SDL_image.h>
#include <ScopedEnvironment.h>
#include <InterfacePresentation.h>
#include <GestureScroll.h>
#include <algorithm>
#include <cmath>
#include <set>
#include "Unit.h"
#include "Building.h"

namespace
{
// Every frame these tests paint also checks the per-frame touch layout memo.
[[maybe_unused]] const bool verifyTouchLayoutFrames = (GameGUITouch::verifyLayoutFrames = true);
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
// A frozen presentation fixture exercises non-empty series without advancing or
// modifying the simulation during UI input. The owning frame pins every sample.
PresentationFrame statisticsHistoryFixture(GameGUI &gui)
{
    auto frame = gui.drawnScene();
    auto samples = std::make_shared<TeamStats>();
    samples->initializeMeasurements(0);
    for (unsigned i = 0; i <= 16; ++i) {
        samples->endOfGameStats.emplace_back(60 + 4*i, 10 + i/4, 0, 1000 + 50*i, 30 + 5*i, 20 + 3*i);
        GameplayMeasurements m; m.configureUnits(NB_UNIT_TYPE); m.tick = i * 512;
        m.births[WORKER] = i * 5; m.deaths[WORKER][0] = i;
        m.lowFood[0][WORKER] = 1 + i%3;
        m.lowFood[1][WORKER] = m.lowFood[0][WORKER] + 5 + i%4;
        m.lowFood[2][WORKER] = m.lowFood[1][WORKER] + 15 + i%5;
        m.lowHP[0][WORKER] = 1; m.lowHP[1][WORKER] = 3; m.lowHP[2][WORKER] = 8;
        m.consumed[0][0] = i * 8;
        m.consumed[3][3] = i * i * 3;
        m.labour[GameplayMeasurements::IDLE] = i * 100;
        m.labour[GameplayMeasurements::OTHER_ACTIVITY] = i * 400;
        samples->measurementHistory.push_back(std::move(m));
    }
    auto *latest = samples->getLatestStat(); latest->totalUnit = 124;
    latest->numberUnitPerType[WORKER] = 84; latest->numberUnitPerType[EXPLORER] = 16; latest->numberUnitPerType[WARRIOR] = 24;
    latest->isFree[WORKER] = 8; latest->totalNeeded = 4;
    auto history = std::make_shared<SimulationSnapshot::History>(); history->teams.push_back(samples);
    frame.world.history = history; frame.panels.localStats = samples; frame.tick = 16 * 512;
    return frame;
}

void captureStatisticsFrame(GameGUI &gui, std::filesystem::path output)
{
    auto *gfx = globalContainer->gfx;
    std::shared_ptr<SDL_Surface> pixels;
    std::string error;
    // completedFrame() may be only the software backing store on GPU hosts.
    // Request readback before presentation to capture the actual rendered UI.
    const auto capture = gfx->requestMemoryCapture([&](auto image, auto, auto why) { pixels = std::move(image); error = why; });
    REQUIRE(capture);
    glob2test::drawGUI(gui, 0); gfx->nextFrame();
    if (!pixels) gfx->cancelMemoryCapture(capture);
    INFO(error); REQUIRE(pixels);
    bool visible = false;
    for (int y = 0; y < pixels->h; y += std::max(1, pixels->h/16))
    for (int x = 0; x < pixels->w; x += std::max(1, pixels->w/16)) {
        Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(pixels.get(), x, y, &r, &g, &b, &a));
        visible |= r || g || b;
    }
    REQUIRE(visible);
    output.replace_extension(".png");
    REQUIRE(IMG_SavePNG(pixels.get(), output.string().c_str()));
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

    TEST_CASE("Bulgarian building failure rows wrap inside the panel and remain scrollable [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=640,.height=360,
            .beforeLoad=[](GlobalContainer& container) { container.settings.language="bg"; }});
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* building=world.game.addBuilding(8,8,world.game.buildingsTypes.getFinishedTypeNum("inn"),0,0,0);
        REQUIRE(building);
        auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.adjustLocalTeam();
        gui.setSelection(GameGUI::BUILDING_SELECTION,building);gui.prepareLocalPresentation(gui.frameScene);
        auto& panel=gui.frameScene.panels.building;REQUIRE(panel.valid);
        // Exercise counterfactual immutable observations, leaving simulation state untouched.
        SnapshotBuilding record=panel.state();panel.record=&record;
        record.working.count=0;record.desiredMaxUnitWorking=10;
        std::fill_n(record.unitsFailingRequirements,Building::UnitCantWorkReasonSize,0);
        const auto checksum=world.checksum();
        auto* gfx=globals->gfx;
        const auto begin=[&] {
            gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(24,35,28));
        };
        const auto frameHash=[&] {
            auto* image=gfx->completedFrame();REQUIRE(image);
            Uint64 hash=1469598103934665603ull;
            const auto* bytes=static_cast<const Uint8*>(image->pixels);
            for(int i=0;i<image->h*image->pitch;++i)hash=(hash^bytes[i])*1099511628211ull;
            return hash;
        };
        const auto renderReasons=[&](int& y) {
            begin();gui.drawBuildingFailureReasons(&panel,building->type,y);gfx->nextFrame();
            auto* image=gfx->completedFrame();REQUIRE(image);
            // Render without the outer panel clip: wrapping itself must preserve
            // the four-pixel right padding, rather than hide overflow by clipping.
            for(int row=8;row<y;++row)for(int x=image->w-4;x<image->w;++x)
            {
                Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(image,x,row,&r,&g,&b,&a));
                CHECK((r==24&&g==35&&b==28));
            }
            CHECK(world.checksum()==checksum);
            return frameHash();
        };
        // Counts at the end of these long labels must still appear. A one-line
        // clipped draw would lose the count and produce identical images.
        for(int reason=1;reason<Building::UnitCantWorkReasonSize;++reason)
        {
            CAPTURE(reason);
            record.unitsFailingRequirements[reason]=7;
            int y=8;const auto seven=renderReasons(y);
            CHECK(y>8+YOFFSET_RESOURCE_LINE);
            record.unitsFailingRequirements[reason]=8;
            y=8;CHECK(renderReasons(y)!=seven);
            record.unitsFailingRequirements[reason]=0;
        }
        // Short existing labels retain a single row and its nominal pitch.
        auto* strings=Toolkit::getStringTable();
        strings->setLang(strings->getLangCode("en"));
        record.unitsFailingRequirements[Building::UnitTooLowLevel]=7;
        int shortY=8;renderReasons(shortY);
        const auto shortText=FormattableString(strings->getString("[%0 units too low level]")).arg(7);
        CHECK(shortY==8+std::max(YOFFSET_RESOURCE_LINE,globals->littleFont->getStringHeight(shortText)));
        strings->setLang(strings->getLangCode("bg"));
        std::fill_n(record.unitsFailingRequirements,Building::UnitCantWorkReasonSize,0);
        begin();gui.drawBuildingInfos();gfx->nextFrame();
        const int baselineScroll=gui.buildingInfoScrollMaximum;
        std::fill_n(record.unitsFailingRequirements,Building::UnitCantWorkReasonSize,7);
        int failureBottom=8;renderReasons(failureBottom);
        const int failureHeight=failureBottom-8;
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"bulgarian-failure-rows.bmp").string().c_str()));
        begin();gui.drawBuildingInfos();gfx->nextFrame();
        CHECK(gui.buildingInfoScrollMaximum>baselineScroll);
        if(baselineScroll>0)CHECK(gui.buildingInfoScrollMaximum==baselineScroll+failureHeight);
        else CHECK(gui.buildingInfoScrollMaximum<=failureHeight);
        // Wheel dispatch is only handled while the pointer is over the info panel.
        gui.mouseX=gfx->getW()-RIGHT_MENU_WIDTH+8;
        gui.mouseY=YPOS_BASE_BUILDING+8;
        REQUIRE(gui.scrollBuildingChoices(-1));
        CHECK(gui.buildingInfoScroll>0);
        while(gui.buildingInfoScroll<gui.buildingInfoScrollMaximum)REQUIRE(gui.scrollBuildingChoices(-1));
        begin();gui.drawBuildingInfos();gfx->nextFrame();
        CHECK(gui.buildingInfoScroll==gui.buildingInfoScrollMaximum);
        CHECK(world.checksum()==checksum);
        REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/"bulgarian-failure-panel-scrolled.bmp").string().c_str()));
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
            auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.adjustLocalTeam();
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
                    Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(image,x,(rowY+3)*image->h/gfx->getH(),&r,&g,&b,&a));
                    if(r==63&&g==111&&b==149)++markerPixels;
                    // The existing smooth marker has a +/-2px halo, including at
                    // the right endpoint one pixel past the128px rectangle.
                    const int logicalX=x*gfx->getW()/image->w;
                    if(instant || logicalX<left-2 || logicalX>left+128+2)
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
        auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.adjustLocalTeam();
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

// Lesson maps carry English map and seat names baked by the lesson builder. The
// opening cover must show the translated lesson title and colony names instead.
TEST_CASE("lesson opening cover shows translated lesson and colony names [display][writes-preferences]" *
          doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 800, .height = 600,
        .beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::Software; }});
    auto *strings = Toolkit::getStringTable();
    struct RestoreLanguage { GAGCore::StringTable *table; int index; ~RestoreLanguage() { table->setLang(index); } }
        restore{strings, strings->getLang()};
    strings->setLang(strings->getLangCode("de"));
    const auto lessons = Tutorial::lessonCatalog();
    REQUIRE(lessons.size() >= 10);
    const auto &lesson = lessons.at(2);
    GameGUI gui;
    auto map = Engine::loadMapHeader(lesson.map);
    auto players = Engine::loadGameHeader(lesson.map);
    REQUIRE(gui.loadFromHeaders(map, players, true, true));
    const auto title = gui.introCoverTitle();
    CHECK(title.map == strings->getString(lesson.title.c_str()));
    CHECK(title.map != lesson.mapName);
    REQUIRE(title.players.size() >= 2);
    int local = 0;
    for (int i = 0; i < int(title.players.size()); ++i) {
        const bool isLocal = players.getBasePlayer(i).type == BasePlayer::P_LOCAL;
        local += isLocal;
        CHECK(title.players[i].name == strings->getString(isLocal ? "[Your colony]" : "[Practice colony]"));
        CHECK(title.players[i].name != players.getBasePlayer(i).name);
    }
    CHECK(local == 1);
}

// MCP inspection can lay out the HUD while a game is still loading, before the
// first scene carries the local team's statistics; the top bar must not read them.
TEST_CASE("HUD inspection before the first scene does not read missing statistics [display][writes-preferences]" *
          doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 800, .height = 600,
        .beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::Software; }});
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI", "0");
    GameGUI gui;
    auto map = Engine::loadMapHeader("maps/balanced.map");
    GameHeader players;
    players.setNumberOfPlayers(1);
    players.getBasePlayer(0) = BasePlayer(0, "Loading", 0, BasePlayer::P_LOCAL);
    REQUIRE(gui.loadFromHeaders(map, players, true, true));
    gui.localPlayer = gui.localTeamNo = 0;
    gui.adjustLocalTeam();
    REQUIRE_FALSE(gui.drawnScene().panels.localStats);
    REQUIRE_FALSE(gui.drawnScene().buildingTypes);
    // Unit counts are placeholders until statistics exist.
    const auto layout = gui.topBarLayout();
    for (int i = 0; i < 3; ++i) CHECK(layout.values[i] == "-/-");
    CHECK(gui.topBarSpeedX() > 0);
    // The building palette falls back to its stable type names.
    gui.displayMode = GameGUI::CONSTRUCTION_VIEW;
    const auto snapshot = gui.hudInspectionHost().inspect();
    CHECK_FALSE(snapshot.controls.empty());
    bool palette = false;
    auto visit = [&](auto &&self, const auto &nodes) -> void {
        for (const auto &node : nodes) {
            if (node.key == "game/buildings/inn.0.site") { palette = true; CHECK(node.label == "inn.0.site"); }
            self(self, node.children);
        }
    };
    visit(visit, snapshot.controls);
    CHECK(palette);
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
    // Lesson cards move the feed below themselves, so a Hive card panel, which the
    // feed does not avoid, covers the left half of this row instead.
    gui.tutorialGuidance.reset();
    struct HiveView final : Hive::CommanderView
    {
        bool available() const override { return true; }
        bool local() const override { return true; }
        Hive::Json standingOrders() const override { return Hive::Json::array(); }
        void stop() override {}
        void change(const std::string &, const std::string &) override {}
    };
    const GAGGUI::ui::Rect rowCover{row.bounds.x, row.bounds.y, row.bounds.w / 2, row.bounds.h};
    const GAGGUI::ui::Point rowHit{row.bounds.x + row.bounds.w * 3 / 4, row.bounds.center().y};
    gui.hiveCards = std::make_unique<Hive::Dialog>(std::make_shared<HiveView>());
    gui.hiveCards->attach(*globals->gfx);
    {
        auto &cards = gui.hiveCards->host();
        cards.layoutIfNeeded();
        const int padding = cards.metrics().padding;
        const int width = std::max(1, std::min(240, rowCover.right() - 2 * padding));
        const GAGGUI::ui::Rect inner{rowCover.right() - padding - width, rowCover.y + padding, width, 120};
        cards.setAvailable([inner](const auto &, const auto &) { return inner; });
        cards.setPlacement([](GAGGUI::ui::Size, GAGGUI::ui::Rect bounds) { return bounds; });
        cards.invalidate(); cards.layoutIfNeeded();
    }
    gui.prepareLocalPresentation(gui.frameScene);
    const auto rowCoverPanel = gui.hiveCards->panelBounds();
    REQUIRE(gui.describeEventControls().front().bounds == row.bounds);
    const auto rowCoverOverlap = row.bounds.intersect(rowCoverPanel);
    REQUIRE(rowCoverOverlap.w > 0);
    REQUIRE(rowCoverOverlap.h > 0);
    REQUIRE(rowCoverOverlap.w < row.bounds.w);
    REQUIRE(row.bounds.contains(rowHit));
    REQUIRE_FALSE(rowCoverPanel.contains(rowHit));
    const auto rowSnapshot = gui.hudInspectionHost().inspect();
    const auto *blockedRow = findControl(findControl, rowSnapshot.controls, row.key);
    REQUIRE(blockedRow); CHECK(blockedRow->enabled); CHECK(blockedRow->blocked);
    const auto origin = std::pair{gui.viewportX, gui.viewportY};
    error.clear();
    CHECK_FALSE(gui.hudInspectionHost().act(row.key, {}, error));
    CHECK(error.find("blocked") != std::string::npos);
    click(rowHit);
    CHECK((std::pair{gui.viewportX, gui.viewportY} == origin));
    gui.hiveCards.reset();
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

TEST_CASE("statistics topics history and overlays preserve gameplay state [display:1024x768][artifacts]" * doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto &gui = world.gui;
    gui.init(); gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
    gui.clearSelection(); gui.displayMode=GameGUI::STAT_TEXT_VIEW;
    gui.prepareLocalPresentation(gui.frameScene);
    auto fixture = statisticsHistoryFixture(gui); gui.setPublishedScene(&fixture);
    auto draw=[&](const std::string &name) {
        captureStatisticsFrame(gui, glob2test::artifactDir()/name);
    };
    draw("statistics-overview.bmp");
    auto &panel=gui.statistics();
    CHECK(panel.topic()==InGameStatisticsPanel::Topic::Overview);
    CHECK(panel.host().rootBounds().w==200);
    CHECK(gui.minimap.originX() - panel.host().rootBounds().x == (200-128)/2);
    CHECK(gui.minimap.originX()+128 == panel.host().rootBounds().right()-(200-128)/2);
    CHECK(panel.host().rootBounds().bottom() < mapZoomControlsLayout(gui.camera, true, true).y);
    REQUIRE(panel.host().find("stats/metric/population"));
    CHECK(panel.host().find("stats/metric/population")->accessibleText().find("124") != std::string::npos);
    REQUIRE(panel.host().find("stats/metric/blocked buildings"));
    CHECK(panel.host().find("stats/metric/blocked buildings")->accessibleText().ends_with("\n0"));
    const auto hosts = gui.inspectionHosts();
    REQUIRE(std::find(hosts.begin(), hosts.end(), &panel.host()) != hosts.end());
    auto revisionFor = [&](auto &&self, const auto &controls, const std::string &key) -> std::uint64_t {
        for (const auto &control : controls) {
            if (control.key == key) return control.revision;
            if (const auto revision = self(self, control.children, key)) return revision;
        }
        return 0;
    };
    auto pending = GAGGUI::ui::semanticDispatch();
    auto inspected = panel.host().inspect();
    pending->surface = inspected.instance;
    pending->key = "stats/topic/1";
    pending->revision = revisionFor(revisionFor, inspected.controls, pending->key);
    REQUIRE(pending->revision);
    auto event = pending->event();
    gui.processEvent(&event);
    CHECK(pending->consumed);
    CHECK(pending->error.empty());
    CHECK(panel.topic()==InGameStatisticsPanel::Topic::Colony);
    panel.selectTopic(InGameStatisticsPanel::Topic::Overview); panel.update(0);
    panel.host().tapAt(panel.host().bounds("stats/topic/1").center());
    CHECK(panel.topic()==InGameStatisticsPanel::Topic::Colony);
    // An unchanged topic label is still a different target after a team switch.
    inspected = panel.host().inspect();
    auto stale = GAGGUI::ui::semanticDispatch();
    stale->surface = inspected.instance; stale->key = "stats/topic/1";
    stale->revision = revisionFor(revisionFor, inspected.controls, stale->key);
    panel.sync(fixture, 1, false, false); panel.update(0);
    event = stale->event(); panel.host().event(event);
    CHECK(stale->consumed); CHECK_FALSE(stale->error.empty());
    gui.statistics(); panel.update(0);
    panel.selectTopic(InGameStatisticsPanel::Topic::Overview);panel.update(0);
    CHECK(gui.sceneRequest(false).includeHistory);
    auto waiting = gui.drawnScene();
    waiting.world.history.reset();
    panel.sync(waiting, 0, false, false); panel.update(0);
    REQUIRE(panel.host().find("stats/metric/population"));
    CHECK(panel.host().find("stats/metric/population")->accessibleText().ends_with("\n—"));
    gui.statistics(); panel.update(0);
    const auto checksum=world.game.checkSum();
    std::set<std::string> seen;
    for(int topic=1;topic<=4;++topic) {
        panel.selectTopic(InGameStatisticsPanel::Topic(topic)); panel.update(0);
        for(const auto &m:panel.metrics()) {
            constexpr int expectedTopics[] = {1, 1, 2, 2, 2, 3, 4, 4};
            static_assert(std::size(expectedTopics) == std::size_t(Stats::Group::Count));
            const bool belongs=expectedTopics[int(m.group)]==topic;
            CHECK(bool(panel.host().find("stats/metric/"+std::string(m.id)))==belongs);
            if(belongs)seen.insert(m.id);
        }
    }
    CHECK(seen.size()==panel.metrics().size());
    CHECK_FALSE(seen.count("win probability"));
    {
        auto &perf = PerformanceTelemetry::collector();
        perf.history = std::make_unique<PerformanceTelemetry::History>();
        for (unsigned i = 0; i < 4; ++i) {
            auto sample = std::make_unique<PerformanceTelemetry::Window>();
            sample->tickStart = i * 512; sample->tickEnd = (i + 1) * 512;
            sample->startNs = i * 1000000000ULL; sample->endNs = (i + 1) * 1000000000ULL;
            auto &m = sample->metrics[unsigned(PerformanceTelemetry::Id::OwnerAdvance)];
            m.calls = 512; m.time.add((i + 1) * 1000000);
            perf.history->append(std::move(*sample));
        }
        panel.selectTopic(InGameStatisticsPanel::Topic::Engine); panel.update(0);
        REQUIRE(panel.host().find("engine-performance/open"));
        REQUIRE(panel.host().find("engine-performance/summary"));
        CHECK_FALSE(gui.sceneRequest(false).includeHistory);
        draw("engine-performance-wall.bmp");
        panel.host().tapAt(panel.host().bounds("engine-performance/open").center());
        REQUIRE(gui.inGameMenu==GameGUI::IGM_STATISTICS);
        REQUIRE(gui.gameMenuScreen);
        CHECK_FALSE(gui.gamePaused);
        gui.gameMenuScreen->update(0);
        REQUIRE(gui.gameMenuScreen->host().find("engine-performance/chart"));
        REQUIRE(gui.gameMenuScreen->host().find("engine-performance/chart/5"));
        auto* performance=static_cast<InGamePerformanceHistory*>(gui.gameMenuScreen.get());
        // Plot hit testing uses the font-dependent bounds measured during drawing.
        draw("engine-performance-expanded.bmp");
        gui.gameMenuScreen->host().tapAt(gui.gameMenuScreen->host().bounds("engine-performance/chart").center());
        CHECK(performance->panel.cursorMinute>=0);
        gui.gameMenuScreen->host().tapAt(gui.gameMenuScreen->host().bounds("engine-performance/follow-latest").center());
        CHECK(performance->panel.cursorMinute<0);
        gui.gameMenuScreen->host().tapAt(gui.gameMenuScreen->host().bounds("engine-performance/range/1").center());
        gui.gameMenuScreen->update(0);
        draw("engine-performance-recent.bmp");
        gui.gameMenuScreen->host().scrollIntoView("engine-performance/all-metrics");
        gui.gameMenuScreen->update(0);
        gui.gameMenuScreen->host().tapAt(gui.gameMenuScreen->host().bounds("engine-performance/all-metrics").center());
        gui.gameMenuScreen->update(0);
        REQUIRE(gui.gameMenuScreen->host().find("engine-performance/explorer-chart"));
        gui.gameMenuScreen->host().tapAt(gui.gameMenuScreen->host().bounds("performance/history/close").center());
        gui.processGameMenu(nullptr);
        CHECK(world.game.checkSum() == checksum);
    }
    panel.selectTopic(InGameStatisticsPanel::Topic::Economy); panel.update(0);
    panel.host().scrollIntoView("stats/metric/spending"); panel.update(0);
    REQUIRE(panel.host().find("stats/scroll/2")->scrollOffset() > 0);
    draw("statistics-economy.bmp");
    const auto metricBounds=panel.host().bounds("stats/metric/spending");
    SDL_Event click{};click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.button=SDL_BUTTON_LEFT;
    click.button.x=metricBounds.center().x;click.button.y=metricBounds.center().y;
    gui.processEvent(&click);click.type=SDL_EVENT_MOUSE_BUTTON_UP;gui.processEvent(&click);
    CHECK(gui.inGameMenu==GameGUI::IGM_NONE);
    CHECK(panel.selectedMetric()=="spending");
    panel.update(0);
    REQUIRE(panel.host().find("stats/inline/chart"));
    panel.host().scrollIntoView("stats/full-history/spending"); panel.update(0);
    draw("statistics-inline-history.bmp");
    const auto position=panel.host().find("stats/scroll/2")->scrollOffset();
    panel.host().tapAt(panel.host().bounds("stats/full-history/spending").center());
    REQUIRE(gui.inGameMenu==GameGUI::IGM_STATISTICS);
    REQUIRE(gui.gameMenuScreen);
    CHECK_FALSE(gui.gamePaused);
    gui.gameMenuScreen->update(0);
    auto *history=static_cast<InGameStatisticsHistory*>(gui.gameMenuScreen.get());
    auto view=Stats::defaultView(*panel.metric("spending")); view.total=true;
    history->setView(view); history->update(0);
    draw("statistics-history.bmp");
    const auto chart = history->host().bounds("stats/history/chart");
    REQUIRE_FALSE(chart.empty());
    history->host().tapAt(chart.center());
    CHECK(history->hoverX >= 0); CHECK(history->hoverY >= 0);
    draw("statistics-history-inspected.bmp");
    history->host().tapAt(history->host().bounds("stats/history/details").center());history->update(0);
    CHECK(history->detailsOpen);
    history->host().tapAt(history->host().bounds("stats/history/details").center());history->update(0);
    CHECK_FALSE(history->detailsOpen);
    SDL_Event escape{};escape.type=SDL_EVENT_KEY_DOWN;escape.key.key=SDLK_ESCAPE;
    gui.processEvent(&escape);
    CHECK(gui.inGameMenu==GameGUI::IGM_NONE);
    CHECK(panel.topic()==InGameStatisticsPanel::Topic::Economy);
    CHECK(panel.host().find("stats/scroll/2")->scrollOffset()==position);
    CHECK(panel.views.at("spending").total);
    CHECK_FALSE(gui.gamePaused);
    draw("statistics-return.bmp");
    CHECK(gui.orderQueue.empty()); CHECK(world.game.checkSum()==checksum);
    gui.displayMode=GameGUI::STAT_GRAPH_VIEW;
    CHECK_FALSE(gui.sceneRequest(false).includeHistory);
    gui.handleMenuClick(16, YPOS_BASE_STAT+29, SDL_BUTTON_LEFT);
    CHECK(gui.showStarvingMap); CHECK_FALSE(gui.showDamagedMap);
    gui.handleMenuClick(16, YPOS_BASE_STAT+53, SDL_BUTTON_LEFT);
    CHECK(gui.showDamagedMap); CHECK_FALSE(gui.showStarvingMap);
    CHECK(world.game.checkSum()==checksum);
    gui.displayMode=GameGUI::STAT_TEXT_VIEW;
    auto *strings=GAGCore::Toolkit::getStringTable();
    const auto language=strings->getLang();
    const auto textScale=GAGCore::userTextScale;
    strings->setLang(strings->getLangCode("de"));GAGCore::userTextScale=1.5;
    panel.invalidate();draw("statistics-desktop-large-german.bmp");
    for(const auto *node:panel.host().interactiveNodes()) {
        CHECK(node->bounds.x>=panel.host().rootBounds().x);
        CHECK(node->bounds.right()<=panel.host().rootBounds().right());
    }
    strings->setLang(strings->getLangCode("ms"));
    CHECK(std::string(strings->getString("[language-code]"))=="ms");
    panel.selectTopic(InGameStatisticsPanel::Topic::Overview);
    panel.invalidate();draw("statistics-desktop-large-malay.bmp");
    const auto *overview=panel.host().find("stats/topic/0");
    REQUIRE(overview);
    const auto &presentation=panel.host().presentation();
    GAGGUI::ui::ToolkitTextMeasurer measurer(panel.host().theme(),presentation.touch,presentation.textUnit);
    glob2test::RecordingCanvas canvas(presentation.viewport.size(),measurer);
    panel.host().paint(canvas,0);
    std::string paintedOverview;
    for(const auto &[at,line]:canvas.texts) {
        if(!overview->bounds.contains(at))continue;
        if(!paintedOverview.empty())paintedOverview+=' ';
        paintedOverview+=line;
        CHECK(overview->bounds.contains(GAGGUI::ui::Rect{
            at.x,at.y,measurer.width(GAGGUI::ui::FontRole::Support,line),
            measurer.lineHeight(GAGGUI::ui::FontRole::Support)}));
    }
    // Joining the actually painted lines with spaces must preserve whole words;
    // the narrow half-width button split "Gambaran keseluruhan" inside both words.
    CHECK(paintedOverview==overview->accessibleText());
    CHECK(world.game.checkSum()==checksum);
    // A pending read-only history projection must show the complete Tamil
    // waiting status inside the real narrow chart, including its second line.
    strings->setLang(strings->getLangCode("ta")); GAGCore::userTextScale=textScale;
    auto pendingHistory=fixture;
    pendingHistory.world.history.reset();
    gui.setPublishedScene(&pendingHistory);
    panel.invalidate(); draw("statistics-desktop-tamil-waiting.bmp");
    REQUIRE(panel.history()==nullptr);
    const auto waitingChart=panel.host().bounds("stats/inline/chart");
    REQUIRE_FALSE(waitingChart.empty());
    const auto &waitingPresentation=panel.host().presentation();
    GAGGUI::ui::ToolkitTextMeasurer waitingMeasure(panel.host().theme(),waitingPresentation.touch,waitingPresentation.textUnit);
    glob2test::RecordingCanvas waitingCanvas(waitingPresentation.viewport.size(),waitingMeasure);
    panel.host().paint(waitingCanvas,0);
    std::string paintedWaiting;
    unsigned waitingLines=0;
    int previousWaitingBottom=waitingChart.y;
    auto *waitingFont=waitingMeasure.font(GAGGUI::ui::FontRole::Support);
    REQUIRE(waitingFont);
    waitingFont->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL,GAGCore::Color(249,232,187)));
    for (const auto &[at,line]:waitingCanvas.texts) {
        if (!waitingChart.contains(at)) continue;
        if (!paintedWaiting.empty()) paintedWaiting+=' ';
        paintedWaiting+=line; ++waitingLines;
        const int glyphHeight=int(std::ceil(waitingFont->getStringHeight(line)*waitingMeasure.scale()));
        CHECK(at.y>=previousWaitingBottom);
        previousWaitingBottom=at.y+glyphHeight;
        CHECK(waitingChart.contains(GAGGUI::ui::Rect{at.x,at.y,
            waitingMeasure.width(GAGGUI::ui::FontRole::Support,line),glyphHeight}));
    }
    waitingFont->popStyle();
    CHECK(waitingLines>1);
    CHECK(paintedWaiting==strings->getString("[stats waiting]"));
    CHECK(world.game.checkSum()==checksum);
    strings->setLang(language);GAGCore::userTextScale=textScale;
    gui.setPublishedScene(nullptr);
    gui.init(); CHECK_FALSE(gui.statisticsPanel);
}

TEST_CASE("desktop statistics list stays inside its viewport and on the right menu background [display:1024x768][artifacts]" * doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto &gui = world.gui;
    gui.init(); gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
    gui.clearSelection(); gui.displayMode=GameGUI::STAT_TEXT_VIEW;
    gui.prepareLocalPresentation(gui.frameScene);
    auto fixture = statisticsHistoryFixture(gui); gui.setPublishedScene(&fixture);
    auto *gfx = globalContainer->gfx;
    auto capture = [&](const std::string &name) {
        std::shared_ptr<SDL_Surface> pixels;
        std::string error;
        const auto request = gfx->requestMemoryCapture([&](auto image, auto, auto why) { pixels = std::move(image); error = why; });
        REQUIRE(request);
        glob2test::drawGUI(gui, 0); gfx->nextFrame();
        if (!pixels) gfx->cancelMemoryCapture(request);
        INFO(error); REQUIRE(pixels);
        REQUIRE(IMG_SavePNG(pixels.get(), (glob2test::artifactDir()/(name + ".png")).string().c_str()));
        return pixels;
    };
    auto &panel = gui.statistics();
    panel.selectTopic(InGameStatisticsPanel::Topic::Economy); panel.update(0);
    capture("statistics-scroll-rest");
    auto &host = panel.host();
    auto *scroll = host.find("stats/scroll/2");
    REQUIRE(scroll);
    REQUIRE(scroll->scrollMaximum() > 0);
    const auto root = host.rootBounds();
    const auto viewport = scroll->bounds;
    REQUIRE(viewport.y > root.y);
    // The header and topic buttons above the list, and the gap below it.
    const GAGGUI::ui::Rect above{root.x, root.y, root.w, viewport.y - root.y};
    const GAGGUI::ui::Rect below{root.x, viewport.bottom(), root.w, root.bottom() - viewport.bottom()};
    // The captured pixels covering a logical rectangle, row by row.
    auto region = [&](const std::shared_ptr<SDL_Surface> &s, GAGGUI::ui::Rect r) {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
            SDL_ConvertSurface(s.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        REQUIRE(rgba);
        const int x0 = r.x * rgba->w / gfx->getW(), x1 = r.right() * rgba->w / gfx->getW();
        const int y0 = r.y * rgba->h / gfx->getH(), y1 = r.bottom() * rgba->h / gfx->getH();
        std::vector<Uint8> values;
        for (int y = y0; y < y1; ++y) {
            const auto *row = static_cast<const Uint8 *>(rgba->pixels) + y * rgba->pitch;
            values.insert(values.end(), row + 4 * x0, row + 4 * x1);
        }
        return values;
    };
    // The map beside the panel, where nothing of the panel may draw.
    const GAGGUI::ui::Rect beside{root.x - 160, root.y, 160, root.h};
    const auto rest = capture("statistics-scroll-top");
    const auto restAbove = region(rest, above), restBelow = region(rest, below), restBeside = region(rest, beside);
    // The panel area is the right menu's own background, not a themed slab:
    // the padding beside the list matches the menu just below the panel.
    {
        const auto inside = region(rest, {root.x + 1, root.bottom() - 2, 1, 1});
        const auto outside = region(rest, {root.x + 1, root.bottom() + 1, 1, 1});
        REQUIRE(inside.size() == outside.size());
        for (std::size_t i = 0; i < inside.size(); ++i)
            CHECK(std::abs(int(inside[i]) - int(outside[i])) <= 16);
    }
    // Page through the list and both overscroll springs: the rows move, the
    // header, topic buttons and the area below the viewport never do.
    const int maximum = scroll->scrollMaximum();
    for (const auto [offset, stretch] : {std::pair{maximum / 2, 0}, std::pair{maximum, 0},
             std::pair{maximum, viewport.h / 3}, std::pair{0, -viewport.h / 3}}) {
        INFO("offset " << offset << " overscroll " << stretch);
        scroll = host.find("stats/scroll/2");
        REQUIRE(scroll);
        scroll->scrollTo(offset, host);
        scroll->setOverscroll(stretch, host);
        const auto frame = capture("statistics-scroll-" + std::to_string(offset) + "-" + std::to_string(stretch));
        REQUIRE(host.find("stats/scroll/2"));
        CHECK(host.find("stats/scroll/2")->overscroll() == stretch);
        CHECK(region(frame, above) == restAbove);
        CHECK(region(frame, below) == restBelow);
    }
    if ((scroll = host.find("stats/scroll/2"))) scroll->setOverscroll(0, host);
    scroll->scrollTo(0, host);
    // The real path: a trackpad gesture past the end, its momentum and the
    // spring back, with new statistics samples rebuilding the panel meanwhile.
    Uint32 time = 1000;
    int step = 0;
    GAGCore::GestureScrollEvent gesture;
    gesture.sequence = 777; gesture.logical = true;
    gesture.x = viewport.center().x; gesture.y = viewport.center().y;
    auto frameCheck = [&](const char *stage, bool check) {
        time += 16; panel.update(time);
        if (step++ % 3 == 0) {
            auto copy = std::make_shared<SimulationSnapshot::History>(*fixture.world.history);
            fixture.world.history = copy;
        }
        if (!check) return;
        INFO(stage << " step " << step << " offset " << host.find("stats/scroll/2")->scrollOffset()
             << " overscroll " << host.find("stats/scroll/2")->overscroll());
        // Paint twice, longer apart than the tooltip delay, as a pointer
        // resting over the moving list would see it.
        capture(std::string("statistics-gesture-") + std::to_string(step) + "a");
        SDL_Delay(650);
        const auto frame = capture(std::string("statistics-gesture-") + std::to_string(step));
        CHECK(region(frame, above) == restAbove);
        CHECK(region(frame, below) == restBelow);
        CHECK(region(frame, beside) == restBeside);
    };
    auto send = [&](GAGCore::ScrollGesturePhase phase, double dy, GAGCore::ScrollGesturePhase momentum = GAGCore::ScrollGesturePhase::None) {
        gesture.timestamp = SDL_MS_TO_NS(time); gesture.phase = phase; gesture.momentum = momentum; gesture.dy = dy;
        auto event = GAGCore::gestureScrollEvent(gesture);
        gui.processEvent(&event);
    };
    // A trackpad scroll happens with the pointer resting over the list.
    SDL_Event motion{}; motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = float(viewport.center().x); motion.motion.y = float(viewport.y + 8);
    gui.processEvent(&motion);
    send(GAGCore::ScrollGesturePhase::Began, 0);
    int deepest = 0;
    for (int i = 0; i < 24; ++i) {
        send(GAGCore::ScrollGesturePhase::Changed, -40);
        frameCheck("drag", i % 6 == 5);
        deepest = std::max(deepest, host.find("stats/scroll/2")->overscroll());
    }
    CHECK(deepest > 0);
    send(GAGCore::ScrollGesturePhase::Ended, 0);
    for (int i = 0; i < 30; ++i) {
        if (i < 20) send(GAGCore::ScrollGesturePhase::None, -20 * std::pow(.9, i), i == 0 ? GAGCore::ScrollGesturePhase::Began : GAGCore::ScrollGesturePhase::Changed);
        frameCheck("bounce", i % 6 == 0);
    }
    send(GAGCore::ScrollGesturePhase::None, 0, GAGCore::ScrollGesturePhase::Ended);
    for (int i = 0; i < 60 && host.animating(); ++i) { time += 16; panel.update(time); }
    CHECK_FALSE(host.animating());
    CHECK(host.find("stats/scroll/2")->overscroll() == 0);
    // Once settled, a resting pointer earns the metric's description, kept
    // within the panel beside the map rather than spilling over it.
    GAGGUI::ui::Rect target;
    for (const auto *node : host.interactiveNodes())
        if (node->key.starts_with("stats/metric/") && viewport.contains(node->bounds))
            target = node->bounds;
    REQUIRE_FALSE(target.empty());
    motion.motion.x = float(target.center().x); motion.motion.y = float(target.center().y);
    gui.processEvent(&motion);
    const auto hovered = capture("statistics-hover");
    SDL_Delay(700);
    const auto described = capture("statistics-tooltip");
    CHECK(region(described, root) != region(hovered, root));
    CHECK(region(described, beside) == restBeside);
    gui.setPublishedScene(nullptr);
}

TEST_CASE("statistics sheet scroll stays open and fits phone rotations [display][artifacts]" * doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI", "1");
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=360,.height=720,
        .screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto &gui=world.gui; auto *gfx=globals->gfx;
    gui.init(); gui.localTeamNo=0;gui.localPlayer=0;gui.localTeam=world.team;
    gui.clearSelection();gui.displayMode=GameGUI::STAT_TEXT_VIEW;
    gfx->setResponsiveViewport(true,800,600);
    gui.prepareLocalPresentation(gui.frameScene);
    auto fixture = statisticsHistoryFixture(gui); gui.setPublishedScene(&fixture);
    struct Restore { double value=GAGCore::userTextScale; ~Restore(){GAGCore::userTextScale=value;} } restore;
    const auto checksum=world.game.checkSum();
    Uint64 time=100000000;
    for(const auto [width,height]:{std::pair{360,720},std::pair{720,360}})
    for(double text:{1.,1.5}) {
        REQUIRE(SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height));
        REQUIRE(SDL_SyncWindow(SDL_GetWindowFromID(gfx->windowID())));
        SDL_Event resize{};resize.type=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
        GAGCore::GraphicContext::translateMouseEvent(&resize);
        GAGCore::userTextScale=text;
        gui.touch->statsOpen=true;
        gui.prepareLocalPresentation(gui.frameScene);
        auto draw=[&](const std::string &name) {
            captureStatisticsFrame(gui, glob2test::artifactDir()/name);
        };
        auto &panel=gui.statistics();panel.selectTopic(InGameStatisticsPanel::Topic::Colony);
        const auto suffix=std::to_string(width)+"-"+std::to_string(int(text*100))+".bmp";
        draw("statistics-mobile-"+suffix);
        const auto bounds=panel.host().rootBounds();
        CHECK(bounds.x>=0); CHECK(bounds.right()<=gfx->getW());
        CHECK(bounds.y>=0); CHECK(bounds.bottom()<=gfx->getH());
        panel.host().scrollIntoView("stats/metric/starvation");panel.update(0);
        auto finger=[&](Uint32 kind,int y) {
            SDL_Event event{};event.type=kind;event.tfinger.touchID=901;event.tfinger.fingerID=902;
            event.tfinger.timestamp=(time+=20000000);
            event.tfinger.x=float(bounds.center().x)/gfx->getW();event.tfinger.y=float(y)/gfx->getH();
            gui.processEvent(&event);
        };
        const int y=bounds.bottom()-20;
        finger(SDL_EVENT_FINGER_DOWN,y);finger(SDL_EVENT_FINGER_MOTION,y-60);finger(SDL_EVENT_FINGER_UP,y-60);
        CHECK(gui.touch->statsOpen); CHECK(gui.inGameMenu==GameGUI::IGM_NONE);
        panel.host().scrollIntoView("stats/metric/hunger");panel.update(0);
        panel.host().tapAt(panel.host().bounds("stats/metric/hunger").center());panel.update(0);
        CHECK(panel.selectedMetric()=="hunger");
        panel.host().scrollIntoView("stats/inline/chart");panel.update(0);
        draw("statistics-mobile-inline-"+suffix);
        panel.host().tapAt(panel.host().bounds("stats/inline/chart").center());
        CHECK(panel.inlineHoverX>=0);CHECK(panel.inlineHoverY>=0);
        CHECK(gui.touch->statsOpen);CHECK(gui.inGameMenu==GameGUI::IGM_NONE);
        panel.host().scrollIntoView("stats/full-history/hunger");panel.update(0);
        panel.host().tapAt(panel.host().bounds("stats/full-history/hunger").center());
        REQUIRE(gui.gameMenuScreen);gui.gameMenuScreen->update(0);
        draw("statistics-mobile-history-"+suffix);
        auto *history=static_cast<InGameStatisticsHistory*>(gui.gameMenuScreen.get());
        history->host().scrollIntoView("stats/history/chart");history->update(0);
        history->host().tapAt(history->host().bounds("stats/history/chart").center());
        CHECK(history->hoverX >= 0);CHECK(history->hoverY >= 0);
        SDL_Event back{};back.type=SDL_EVENT_KEY_DOWN;back.key.key=width>height ? SDLK_AC_BACK : SDLK_ESCAPE;gui.processEvent(&back);
        CHECK(gui.touch->statsOpen);CHECK_FALSE(gui.gamePaused);
        CHECK(world.game.checkSum()==checksum);CHECK(gui.orderQueue.empty());
    }
    gui.setPublishedScene(nullptr);
}


TEST_CASE("Bulgarian empty statistics notices wrap without ink outside their chart [display][artifacts]" *
          doctest::test_suite("GUIInteractionCoverage"))
{
    glob2test::GlobalsOptions options{.display=true, .loadStrings=true};
    options.beforeLoad=[](GlobalContainer &globals) { globals.settings.language="bg"; };
    glob2test::HeadlessGlobals globals(options);
    const auto notice=GAGCore::Toolkit::getStringTable()->getString("[Not enough recorded history yet.]");
    auto *font=globals->littleFont;
    REQUIRE(font);
    Stats::Metric metric{.id="population"};
    TeamStatChart::Options chart;
    chart.metric=&metric;
    for (const int width : {120,176}) {
        INFO(width);
        REQUIRE(font->getStringWidth(notice)>width-16);
        auto *raw=SDL_CreateSurface(480,240,SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(raw);
        GAGCore::DrawableSurface surface(raw,GAGCore::DrawableSurface::AdoptPixels{},false);
        REQUIRE(SDL_FillSurfaceRect(surface.getSDLSurface(),nullptr,0));
        // Deliberately leave the surrounding surface unclipped: the renderer
        // must lay out the complete notice, rather than hide overflow pixels.
        TeamStatChart::paint({},0,surface,16,16,width,200,chart);
        surface.nextFrame();
        auto *pixels=surface.getSDLSurface();
        int outside=0,secondLine=0,total=0;
        for (int y=0;y<pixels->h;++y) for (int x=0;x<pixels->w;++x) {
            Uint8 red,green,blue,alpha;
            REQUIRE(SDL_ReadSurfacePixel(pixels,x,y,&red,&green,&blue,&alpha));
            if (!(red||green||blue)) continue;
            ++total;
            outside+=x<16 || x>=16+width || y<16 || y>=216;
            secondLine+=x>=16 && x<16+width && y>=24+font->getStringHeight("0") && y<216;
        }
        CHECK(total>0);
        CHECK(outside==0);
        CHECK(secondLine>0);
        REQUIRE(IMG_SavePNG(pixels,(glob2test::artifactDir()/("empty-chart-"+std::to_string(width)+".png")).string().c_str()));
    }
}

TEST_CASE("palette controls carry player-facing names and Escape leaves placement before the menu [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 1024, .height = 768});
	glob2test::HeadlessGame world({.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.localTeam = world.team;
	gui.rebuildBuildingChoices(false);
	auto label = [&](const std::string &key)
	{
		gui.prepareLocalPresentation(gui.frameScene);
		for (const auto &control : gui.describeHudControls())
			if (control.key == key)
				return control.label;
		return std::string("<missing>");
	};
	// Semantic clients see the names a player reads, not sprite keys like inn.0.site.
	gui.displayMode = GameGUI::CONSTRUCTION_VIEW;
	CHECK(label("game/buildings/inn.0.site") == Toolkit::getStringTable()->getString("[inn]"));
	CHECK(label("game/buildings/barracks.0.site") == Toolkit::getStringTable()->getString("[barracks]"));
	gui.displayMode = GameGUI::FLAG_VIEW;
	CHECK(label("game/flags/warflag.0.finished") == Toolkit::getStringTable()->getString("[warflag]"));

	auto escape = [&]
	{
		SDL_Event e{};
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = SDLK_ESCAPE;
		e.key.down = true;
		gui.processEvent(&e);
	};
	const auto inn = std::find(gui.buildingsChoiceName.begin(), gui.buildingsChoiceName.end(), "inn.0.site");
	REQUIRE(inn != gui.buildingsChoiceName.end());
	gui.setSelection(GameGUI::TOOL_SELECTION, (void *)inn->c_str());
	REQUIRE(gui.selectionMode == GameGUI::TOOL_SELECTION);
	escape();
	CHECK(gui.selectionMode == GameGUI::NO_SELECTION);
	CHECK(gui.inGameMenu == GameGUI::IGM_NONE);
	auto *swarm = world.addBuilding("swarm", 4, 4);
	REQUIRE(swarm);
	gui.setSelection(GameGUI::BUILDING_SELECTION, swarm);
	escape();
	CHECK(gui.selectionMode == GameGUI::NO_SELECTION);
	CHECK(gui.inGameMenu == GameGUI::IGM_NONE);
	escape(); // With nothing selected, Escape still opens the game menu.
	CHECK(gui.inGameMenu == GameGUI::IGM_MAIN);
	CHECK(gui.orderQueue.empty());
}

namespace
{
// Counts lit pixels of the last completed frame inside a logical-pixel rectangle.
int inkIn(int x, int y, int w, int h)
{
	auto *gfx = globalContainer->gfx;
	auto *frame = gfx->completedFrame();
	REQUIRE(frame);
	const double sx = double(frame->w) / gfx->getW(), sy = double(frame->h) / gfx->getH();
	int lit = 0;
	for (int py = int(y * sy); py < int((y + h) * sy) && py < frame->h; ++py)
		for (int px = int(x * sx); px < int((x + w) * sx) && px < frame->w; ++px)
		{
			Uint8 r, g, b, a;
			REQUIRE(SDL_ReadSurfacePixel(frame, px, py, &r, &g, &b, &a));
			lit += (r || g || b) ? 1 : 0;
		}
	return lit;
}
} // namespace

TEST_CASE("palette icons fill their cells and cost rows clear the zoom bar at 800x600 [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 800, .height = 600});
	glob2test::HeadlessGame world({.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.localTeam = world.team;
	gui.rebuildBuildingChoices(false);
	auto *gfx = globalContainer->gfx;
	auto begin = [&]
	{
		gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
		gfx->setClipRect();
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
		gui.game.snapshots().invalidateCatalog();
		gui.prepareLocalPresentation(gui.frameScene);
	};
	// Icons are centred in 100 px cells; a fixed 64 px clip cut their right edge off.
	const auto inn = std::find(gui.buildingsChoiceName.begin(), gui.buildingsChoiceName.end(), "inn.0.site");
	REQUIRE(inn != gui.buildingsChoiceName.end());
	const size_t index = size_t(inn - gui.buildingsChoiceName.begin());
	std::vector<bool> states(gui.buildingsChoiceName.size(), false);
	states[index] = true;
	gui.mouseX = gui.mouseY = 0;
	begin();
	gui.drawChoice(YPOS_BASE_CONSTRUCTION, gui.buildingsChoiceName, states, 2);
	gfx->nextFrame();
	const auto cell = gui.choiceControlBounds(YPOS_BASE_CONSTRUCTION, index, 2);
	REQUIRE(cell.w > 64);
	CHECK(inkIn(cell.x, cell.y, 64, cell.h) > 0);
	CHECK(inkIn(cell.x + 64, cell.y, cell.w - 64, cell.h) > 0);

	// The bottom 26 px hold the map zoom bar; construction costs must end above it,
	// including when fruit costs add rows.
	auto *site = world.game.buildingsTypes.getByType("inn", 0, true);
	REQUIRE(site);
	const int zoomTop = gfx->getH() - 26;
	begin();
	gui.drawChoiceInfoPanel("inn");
	gfx->nextFrame();
	CHECK(inkIn(gfx->getW() - GAME_GUI_RIGHT_MENU_WIDTH, YPOS_BASE_CONSTRUCTION, GAME_GUI_RIGHT_MENU_WIDTH,
				zoomTop - YPOS_BASE_CONSTRUCTION) > 0);
	CHECK(inkIn(gfx->getW() - GAME_GUI_RIGHT_MENU_WIDTH, zoomTop, GAME_GUI_RIGHT_MENU_WIDTH, 26) == 0);
	for (int fruit = HAPPINESS_BASE; fruit < MaterialCount; ++fruit)
	{
		site->semantics.constructionCost[fruit] = 1;
		world.team->teamMaterials[fruit] = 1;
	}
	begin();
	gui.drawChoiceInfoPanel("inn");
	gfx->nextFrame();
	CHECK(inkIn(gfx->getW() - GAME_GUI_RIGHT_MENU_WIDTH, zoomTop, GAME_GUI_RIGHT_MENU_WIDTH, 26) == 0);
}

TEST_CASE("lesson cards keep messages and exits visible and markers aim at building centres [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 800, .height = 600});
	glob2test::HeadlessGame world({.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.localTeam = world.team;
	auto lesson = std::make_shared<Tutorial::Model>();
	lesson->lessonId = "test";
	lesson->stepId = "first";
	lesson->count = 2;
	lesson->index = 0;
	lesson->title = "[intro goal scout]";
	lesson->instruction = "[intro instruction scout]";
	gui.tutorialGuidance = std::make_unique<Tutorial::Guidance>([](int) {});
	gui.tutorialGuidance->model(lesson);
	gui.tutorialGuidance->attach(*globalContainer->gfx);
	gui.tutorialGuidance->update(0);
	gui.prepareLocalPresentation(gui.frameScene);
	// Game messages share the card's corner; they must start below it.
	const auto card = gui.tutorialGuidance->panelBounds();
	REQUIRE(card.h > 0);
	const auto feed = gui.eventFeedStartY();
	REQUIRE(feed);
	CHECK(*feed >= card.y + card.h);

	// A finished lesson shows all three exits without scrolling, even at 800x600.
	auto finished = std::make_shared<Tutorial::Model>(*lesson);
	finished->index = finished->count;
	finished->completionTitle = "[intro complete]";
	finished->completionBody = "[intro complete body]";
	gui.tutorialGuidance->model(finished);
	gui.tutorialGuidance->update(0);
	const auto snapshot = gui.tutorialGuidance->host().inspect();
	auto find = [&](auto &&self, const auto &nodes, const std::string &key) -> const GAGGUI::ui::ControlSnapshot *
	{
		for (const auto &c : nodes)
		{
			if (c.key == key)
				return &c;
			if (const auto *found = self(self, c.children, key))
				return found;
		}
		return nullptr;
	};
	for (const char *key : {"intro/replay", "tutorial/lessons", "intro/menu"})
	{
		CAPTURE(key);
		const auto *exit = find(find, snapshot.controls, key);
		REQUIRE(exit);
		CHECK(exit->visible.w == exit->bounds.w);
		CHECK(exit->visible.h == exit->bounds.h);
	}
	gui.tutorialGuidance.reset();

	// Destinations naming a building's origin tile aim at its footprint centre.
	auto *track = world.addBuilding("racetrack", 10, 10);
	REQUIRE(track);
	gui.game.snapshots().invalidateBoundary();
	gui.prepareLocalPresentation(gui.frameScene);
	const auto &scene = gui.drawnScene();
	const auto [cx, cy] = Tutorial::destinationCentre(scene, {10, 10, ""});
	CHECK(cx == doctest::Approx(10 + track->type->width / 2.0));
	CHECK(cy == doctest::Approx(10 + track->type->height / 2.0));
	// Area destinations and tiles that are not a building origin are unchanged.
	const auto [ix, iy] = Tutorial::destinationCentre(scene, {11, 11, ""});
	CHECK(ix == doctest::Approx(11));
	CHECK(iy == doctest::Approx(11));
	const auto [ex, ey] = Tutorial::destinationCentre(scene, {40.5, 40.5, ""});
	CHECK(ex == doctest::Approx(40.5));
	CHECK(ey == doctest::Approx(40.5));
}

TEST_CASE("desktop HUD top bar counters are readable through inspection [display]" *
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
	gui.prepareLocalPresentation(gui.frameScene);
	const auto descriptors = gui.describeHudControls();
	auto find = [&](const std::string &key)
	{
		const auto found = std::find_if(descriptors.begin(), descriptors.end(),
										[&](const auto &c) { return c.key == key; });
		return found == descriptors.end() ? nullptr : &*found;
	};
	// Each drawn counter is a read-only, labelled value, so an agent can read the
	// colony's state without a screenshot.
	for (const char *key : {"game/stats/worker-count", "game/stats/explorer-count", "game/stats/warrior-count",
							"game/stats/prestige", "game/stats/win-chance",
							"game/stats/converted-in", "game/stats/converted-out"})
	{
		INFO(key);
		const auto *stat = find(key);
		REQUIRE(stat);
		CHECK(stat->role == "label");
		CHECK_FALSE(stat->label.empty());
		CHECK(stat->actions.empty());
		REQUIRE(std::holds_alternative<std::string>(stat->value));
		CHECK_FALSE(std::get<std::string>(stat->value).empty());
		CHECK(stat->bounds.y + stat->bounds.h <= 16); // Inside the top bar, off the map.
	}
	const auto workers = std::get<std::string>(find("game/stats/worker-count")->value);
	CHECK(workers.find('/') != std::string::npos);
	const auto *map = find("game/map");
	REQUIRE(map);
	CHECK(map->bounds.y >= 16); // The counters never overlap the map canvas.
}

TEST_CASE("inspection names the active placement tool and what a flag requests [display]" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 1024, .height = 768});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world({.discovered = true, .clearImmobile = true, .loadDefaultRace = true, .header = true});
	auto &gui = world.gui;
	gui.localTeamNo = 0;
	gui.localPlayer = 0;
	gui.localTeam = world.team;
	gui.rebuildBuildingChoices(false);
	// Tools resolve building types through the published frame, as in a running game.
	auto publish = [&]
	{
		gui.prepareLocalPresentation(published);
		gui.setPublishedScene(&published);
	};
	publish();
	auto control = [&](const std::function<bool(const std::string &)> &match)
	{
		publish();
		for (const auto &c : gui.describeHudControls())
			if (match(c.key))
				return c;
		FAIL("control not described");
		return GAGGUI::ui::PositionedControl{};
	};
	auto placement = [&]
	{
		const auto c = control([](const std::string &key) { return key == "game/placement"; });
		CHECK(c.role == "label");
		CHECK(c.actions.empty());
		return std::get<std::string>(c.value);
	};
	CHECK(placement() == "none");
	gui.setSelection(GameGUI::TOOL_SELECTION, (void *)"inn.0.site");
	CHECK(placement() == "building:inn");
	gui.setSelection(GameGUI::TOOL_SELECTION, (void *)"warflag.0.finished");
	CHECK(placement() == "flag:warflag");
	gui.setSelection(GameGUI::BRUSH_SELECTION);
	gui.brush.defaultSelection();
	gui.toolManager.activateZoneTool(GameGUIToolManager::Guard);
	CHECK(placement() == "zone:guard:paint");
	gui.clearSelection();
	CHECK(placement() == "none");

	// A flag's slider requests the unit type it attracts.
	auto workersLabel = [&](const char *type, int x)
	{
		auto *b = world.addBuilding(type, x, 30);
		REQUIRE(b);
		gui.game.snapshots().invalidateBoundary();
		gui.setSelection(GameGUI::BUILDING_SELECTION, b);
		const auto c = control([](const std::string &key) { return key.ends_with("/workers"); });
		gui.clearSelection();
		return c.label;
	};
	CHECK(workersLabel("warflag", 30) == "Assigned warriors");
	CHECK(workersLabel("explorationflag", 40) == "Assigned explorers");
	CHECK(workersLabel("inn", 50) == "Assigned workers");
}

using Json = Hive::Json;
TEST_CASE("MCP game tools read the colony, place by tile and centre the view" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world(
		{.wDec = 6, .hDec = 6, .teams = 2, .discovered = true, .clearImmobile = true,
		 .loadDefaultRace = true, .header = true});
	auto *swarm = world.addBuilding("swarm", 4, 4);
	REQUIRE(swarm);
	REQUIRE(world.addBuilding("inn", 40, 40, 0, 1));
	world.addUnit(WORKER);
	world.addUnit(WORKER);
	world.addUnit(WARRIOR);
	world.addUnit(WORKER, 43, 40, 1);
	// Give the colony current vision over the other team's area, on both fog planes.
	const auto vision = world.game.teams[0]->me;
	world.game.map.setMapDiscovered(30, 30, 20, 20, vision);
	world.game.map.switchFogOfWar();
	world.game.map.setMapDiscovered(30, 30, 20, 20, vision);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.localTeam = world.team;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.game.snapshots().invalidateBoundary();
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});

	// The colony and what it sees, in map tiles, with footprints and placeable sizes.
	const auto state = commander.call("game_world", Json::object());
	const auto &own = state.at("colony").at("buildings");
	REQUIRE(own.size() == 1);
	CHECK(own[0].at("type") == "swarm");
	CHECK(own[0].at("x") == 4);
	CHECK(own[0].at("y") == 4);
	CHECK(own[0].at("width") == swarm->type->width);
	CHECK(own[0].at("height") == swarm->type->height);
	CHECK(own[0].at("level") == 1);
	CHECK_FALSE(own[0].contains("range")); // Only flags have a range.
	CHECK(state.at("colony").at("units") == Json{{"warrior", 1}, {"worker", 2}});
	const auto &others = state.at("others").at("buildings");
	REQUIRE(others.size() == 1);
	CHECK(others[0].at("team") == 1);
	CHECK(others[0].at("type") == "inn");
	CHECK(state.at("others").at("units_by_team_and_type") == Json{{"1:worker", 1}});
	const auto &placeable = state.at("placeable");
	const auto inn = std::find_if(placeable.begin(), placeable.end(),
								  [](const Json &p) { return p.at("name") == "inn"; });
	REQUIRE(inn != placeable.end());
	CHECK(inn->at("width") == 2);
	CHECK(state.at("map").at("width") == world.game.map.getW());
	CHECK(state.at("view").contains("width"));
	// detail full lists units; an area filter keeps only what lies inside it.
	const auto detailed = commander.call("game_world", {{"detail", "full"}});
	CHECK(detailed.at("colony").at("unit_list").size() == 3);
	const auto filtered = commander.call(
		"game_world", {{"area", {{"x", 30}, {"y", 30}, {"width", 20}, {"height", 20}}}});
	CHECK(filtered.at("colony").at("buildings").empty());
	CHECK(filtered.at("others").at("buildings").size() == 1);

	// Placement goes through the click path and reports why it refuses.
	auto refusal = [&](Json args)
	{
		try
		{
			commander.call("game_place", args);
		}
		catch (const std::exception &error)
		{
			return std::string(error.what());
		}
		return std::string("placed");
	};
	CHECK(refusal({{"building", "castle"}, {"x", 20}, {"y", 20}}).find("placeable here: ") !=
		  std::string::npos);
	CHECK(refusal({{"building", "inn"}, {"x", 5}, {"y", 5}}).find("No room for") !=
		  std::string::npos);
	CHECK(refusal({{"building", "inn"}, {"x", 100000}, {"y", 5}}).find("outside") !=
		  std::string::npos);
	const auto hospital = std::find(world.gui.buildingsChoiceName.begin(),
									world.gui.buildingsChoiceName.end(), "hospital.0.site");
	REQUIRE(hospital != world.gui.buildingsChoiceName.end());
	world.gui.buildingsChoiceState[hospital - world.gui.buildingsChoiceName.begin()] = false;
	CHECK(refusal({{"building", "hospital"}, {"x", 20}, {"y", 20}}).find("not available") !=
		  std::string::npos);
	CHECK_FALSE(world.gui.toolManager.getOrder()); // Refusals queue nothing.

	// A player's own placement tool survives an automated placement.
	world.gui.setSelection(GameGUI::TOOL_SELECTION, (void *)"explorationflag.0.finished");
	const auto placed = commander.call("game_place", {{"building", "inn"}, {"x", 50}, {"y", 50}});
	CHECK(placed.at("queued") == true);
	CHECK(world.gui.toolManager.getBuildingName() == "explorationflag.0.finished");
	auto order = std::dynamic_pointer_cast<OrderCreate>(world.gui.toolManager.getOrder());
	REQUIRE(order);
	CHECK(order->posX == 50);
	CHECK(order->posY == 50);
	CHECK(order->typeNum == world.game.buildingsTypes.getTypeNum("inn", 0, true));
	world.gui.clearSelection();

	// The camera centres on a tile and reports where that tile is drawn. A headless GUI
	// has no window layout, so give it the 800x600 map view the HUD would.
	world.gui.camera.resize(600, 584, world.game.map.getW() * 32, world.game.map.getH() * 32, 0, 16);
	const auto view = commander.call("game_view", {{"x", 30}, {"y", 25}});
	const auto &rect = view.at("view");
	CHECK(rect.at("x").get<int>() <= 30);
	CHECK(rect.at("x").get<int>() + rect.at("width").get<int>() > 30);
	CHECK(rect.at("y").get<int>() <= 25);
	CHECK(rect.at("y").get<int>() + rect.at("height").get<int>() > 25);
	const auto &camera = world.gui.camera;
	CHECK(std::abs(view.at("tile_screen").at("x").get<int>() -
				   int(camera.offsetX + camera.width / 2)) <= 1);
	CHECK(std::abs(view.at("tile_screen").at("y").get<int>() -
				   int(camera.offsetY + camera.height / 2)) <= 1);
	CHECK_THROWS(commander.call("game_view", {{"x", 100000}, {"y", 0}}));

	// zoom reuses the HUD camera's limits and keeps the tile centred at the new scale.
	const auto zoomed = commander.call("game_view", {{"x", 30}, {"y", 25}, {"zoom", 2}});
	CHECK(camera.zoom == doctest::Approx(2));
	CHECK(zoomed.at("zoom") == doctest::Approx(2));
	CHECK(zoomed.at("tile_pixels") == doctest::Approx(64));
	CHECK(zoomed.at("zoom_range")[1] == doctest::Approx(MapCamera::MAX_ZOOM));
	CHECK(std::abs(zoomed.at("tile_screen").at("x").get<int>() -
				   int(camera.offsetX + camera.width / 2)) <= 1);
	CHECK(zoomed.at("view").at("width").get<int>() < rect.at("width").get<int>());
	commander.call("game_view", {{"x", 30}, {"y", 25}, {"zoom", 0}});
	CHECK(camera.zoom == doctest::Approx(camera.minimumZoom()));
	commander.call("game_view", {{"x", 30}, {"y", 25}, {"zoom", 1}});
	CHECK(camera.zoom == doctest::Approx(1));
	commander.call("game_view", {{"x", 30}, {"y", 25}});
	CHECK(camera.zoom == doctest::Approx(1)); // Omitted zoom leaves it alone.

	// game_time holds the match with the client-side hard pause, steps a budget and
	// resumes, without queueing an order; a menu pause blocks stepping until resumed.
	auto time = [&](Json args) { return commander.call("game_time", std::move(args)); };
	auto status = time({{"action", "status"}});
	CHECK(status.at("paused") == false);
	CHECK(status.at("paused_by") == "none");
	CHECK(status.at("tick") == world.game.stepCounter);
	CHECK(status.at("speed") == "1x");
	CHECK(status.at("ticks_per_second") == doctest::Approx(30));
	status = time({{"action", "pause"}});
	CHECK(world.gui.hardPause);
	CHECK(world.gui.simulationHeld());
	CHECK(status.at("paused") == true);
	CHECK(status.at("paused_by") == "agent");
	status = time({{"action", "step"}, {"ticks", 12}});
	CHECK(world.gui.hardPause);
	CHECK(world.gui.heldSteps == 12);
	CHECK_FALSE(world.gui.simulationHeld());
	CHECK(status.at("steps_remaining") == 12);
	CHECK(status.at("target_tick") == world.game.stepCounter + 12);
	world.gui.spendHeldStep();
	CHECK(time({{"action", "status"}}).at("steps_remaining") == 11);
	status = time({{"action", "resume"}});
	CHECK_FALSE(world.gui.hardPause);
	CHECK(world.gui.heldSteps == 0);
	CHECK(status.at("paused") == false);
	CHECK(world.gui.orderQueue.empty());
	world.gui.gamePaused = true; // As after the menu's Pause.
	CHECK(time({{"action", "status"}}).at("paused_by") == "menu");
	CHECK_THROWS_WITH(time({{"action", "step"}}), doctest::Contains("resume first"));
	time({{"action", "resume"}});
	CHECK_FALSE(world.gui.orderQueue.empty()); // The ordinary, recorded resume order.
	world.gui.gamePaused = false;
}

TEST_CASE("MCP game tools keep fog of war: last-seen buildings, no enemy health, explored ground only" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .teams = 2, .clearImmobile = true,
								   .loadDefaultRace = true, .header = true});
	REQUIRE(world.addBuilding("swarm", 4, 4));
	auto *enemy = world.addBuilding("inn", 40, 40, 0, 1);
	REQUIRE(enemy);
	world.addUnit(WARRIOR, 43, 40, 1);
	const auto me = world.game.teams[0]->me;
	auto see = [&](bool visible)
	{
		// Both fog planes: switching clears the plane it leaves.
		world.game.map.switchFogOfWar();
		if (visible)
			world.game.map.setMapDiscovered(30, 30, 20, 20, me);
		world.game.map.switchFogOfWar();
		if (visible)
			world.game.map.setMapDiscovered(30, 30, 20, 20, me);
		world.game.snapshots().invalidateBoundary();
	};
	see(true);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.localTeam = world.team;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});

	// In sight: listed, but never another team's hit points or warrior training.
	auto state = commander.call("game_world", Json::object());
	REQUIRE(state.at("others").at("buildings").size() == 1);
	CHECK_FALSE(state.at("others").at("buildings")[0].contains("hp"));
	CHECK_FALSE(state.at("forces").contains("visible_enemy_warriors_by_attack_level"));
	const auto full = commander.call("game_world", {{"detail", "full"}});
	REQUIRE_FALSE(full.at("others").at("unit_list").empty());
	CHECK_FALSE(full.at("others").at("unit_list")[0].contains("hp"));

	// Out of sight, the Inn is destroyed: the colony still remembers it as it was.
	see(false);
	enemy->kill();
	world.game.snapshots().invalidateBoundary();
	state = commander.call("game_world", Json::object());
	CHECK(state.at("others").at("buildings").empty());
	const auto &remembered = state.at("others").at("remembered_buildings");
	REQUIRE(remembered.size() == 1);
	CHECK(remembered[0].at("type") == "inn");
	CHECK(remembered[0].at("x") == 40);
	CHECK(remembered[0].at("remembered") == true);
	// Back in sight, the empty tile corrects the memory.
	see(true);
	state = commander.call("game_world", Json::object());
	CHECK(state.at("others").at("remembered_buildings").empty());

	// Buildings need explored ground; flags may go anywhere, as for a player.
	auto refusal = [&](Json args)
	{
		try
		{
			commander.call("game_place", args);
		}
		catch (const std::exception &error)
		{
			return std::string(error.what());
		}
		return std::string("queued");
	};
	CHECK(refusal({{"building", "inn"}, {"x", 10}, {"y", 50}}).find("explored") != std::string::npos);
	CHECK(refusal({{"building", "explorationflag"}, {"x", 10}, {"y", 50}}) == "queued");
	// A pending placement is not claimed twice, even before its order reaches the game.
	CHECK(refusal({{"building", "inn"}, {"x", 32}, {"y", 32}}) == "queued");
	CHECK(refusal({{"building", "inn"}, {"x", 32}, {"y", 32}}).find("No room") != std::string::npos);
	const auto moved = commander.call("game_place", {{"building", "inn"}, {"x", 32}, {"y", 32}, {"near", true}});
	CHECK((moved.at("x") != 32 || moved.at("y") != 32));
	const auto followUp = commander.call("game_world", Json::object()).at("placements");
	CHECK(followUp.size() >= 2);
	CHECK(followUp[0].at("status") == "pending");
}

TEST_CASE("MCP game_world shows own stock and Inn happiness, reports losses and polls briefly" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .teams = 2, .discovered = true, .clearImmobile = true,
								   .loadDefaultRace = true, .header = true});
	REQUIRE(world.addBuilding("swarm", 4, 4));
	auto *inn = world.addBuilding("inn", 12, 4);
	auto *hospital = world.addBuilding("hospital", 20, 4);
	auto *school = world.addBuilding("school", 28, 4);
	auto *enemyInn = world.addBuilding("inn", 40, 40, 0, 1);
	REQUIRE(inn);
	REQUIRE(hospital);
	REQUIRE(school);
	REQUIRE(enemyInn);
	inn->setMaterial(WHEAT, 5);
	inn->setMaterial(CHERRY, 3);
	enemyInn->setMaterial(WHEAT, 8);
	enemyInn->setMaterial(PRUNE, 9);
	const auto me = world.game.teams[0]->me;
	world.game.map.setMapDiscovered(30, 30, 20, 20, me);
	world.game.map.switchFogOfWar();
	world.game.map.setMapDiscovered(30, 30, 20, 20, me);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.localTeam = world.team;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.game.snapshots().invalidateBoundary();
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});
	auto ownBuilding = [](const Json &state, const char *type)
	{
		for (const auto &b : state.at("colony").at("buildings"))
			if (b.at("type") == type)
				return b;
		return Json();
	};

	// The panel's stock with capacities, units inside and the happiness the Inn offers:
	// wheat, plus one level for the cherries it holds.
	auto state = commander.call("game_world", Json::object());
	const auto ownInn = ownBuilding(state, "inn");
	REQUIRE(ownInn.is_object());
	const auto capacity = [&](int material) { return "/" + std::to_string(inn->type->maxMaterial[material]); };
	CHECK(ownInn.at("stock").at("food") == "5" + capacity(WHEAT));
	CHECK(ownInn.at("stock").at("cherries") == "3" + capacity(CHERRY));
	CHECK(ownInn.at("stock").at("prunes") == "0" + capacity(PRUNE));
	CHECK(ownInn.at("inside") == "0/" + std::to_string(inn->maxUnitInside));
	CHECK(ownInn.at("happiness") == 2);
	CHECK(ownInn.at("happiness") == inn->availableHappynessLevel());
	CHECK_FALSE(ownBuilding(state, "swarm").contains("happiness"));
	// Another team's Inn shows nothing of its stock or happiness.
	REQUIRE(state.at("others").at("buildings").size() == 1);
	const auto &theirs = state.at("others").at("buildings")[0];
	CHECK_FALSE(theirs.contains("stock"));
	CHECK_FALSE(theirs.contains("happiness"));
	CHECK_FALSE(theirs.contains("inside"));
	CHECK(state.at("losses").at("buildings_destroyed").empty());
	CHECK(state.at("losses").at("units_converted_away_since_last_read") == 0);

	// A destroyed building and a demolished one are reported apart, with tiles, ahead
	// of other alerts; conversion counts come from the colony's exact counter.
	const auto ownSchool = ownBuilding(state, "school");
	REQUIRE(ownSchool.is_object());
	commander.call("game_building", {{"id", ownSchool.at("id")}, {"generation", ownSchool.at("generation")},
									 {"action", "destroy"}});
	hospital->kill();
	school->kill();
	world.game.teams[0]->unitConversionLost = 3;
	world.game.snapshots().invalidateBoundary();
	state = commander.call("game_world", Json::object());
	const auto &alerts = state.at("alerts");
	REQUIRE(alerts.size() >= 2);
	std::set<std::string> kinds = {alerts[0].at("kind"), alerts[1].at("kind")};
	CHECK(kinds == std::set<std::string>{"building_destroyed", "building_demolished"});
	const auto &destroyed = state.at("losses").at("buildings_destroyed");
	REQUIRE(destroyed.size() == 1);
	CHECK(destroyed[0].at("type") == "hospital");
	CHECK(destroyed[0].at("x") == 20);
	CHECK(destroyed[0].at("y") == 4);
	CHECK(state.at("losses").at("units_converted_away_total") == 3);
	CHECK(state.at("losses").at("units_converted_away_since_last_read") == 3);
	CHECK_FALSE(ownBuilding(state, "hospital").is_object());

	// Brief: status, losses and one line per building, without the costlier sections.
	const auto brief = commander.call("game_world", {{"detail", "brief"}});
	for (const char *section : {"status", "units", "forces", "alerts", "losses", "buildings"})
		CHECK(brief.contains(section));
	for (const char *section : {"colony", "others", "resources", "placeable", "view"})
		CHECK_FALSE(brief.contains(section));
	CHECK(brief.at("losses").at("units_converted_away_since_last_read") == 0);
	CHECK(brief.at("buildings").size() == 2);
	std::string innLine;
	for (const auto &line : brief.at("buildings"))
		if (line.get<std::string>().starts_with("inn #"))
			innLine = line;
	CHECK(innLine.find(" at 12,4 ") != std::string::npos);
	CHECK(innLine.find(" food 5" + capacity(WHEAT)) != std::string::npos);
	CHECK(innLine.find(" happiness 2") != std::string::npos);
}

TEST_CASE("MCP game_stats reads only the colony in a match and every team on the results" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.seed = 23});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world(
		{.wDec = 6, .hDec = 6, .teams = 2, .discovered = true, .clearImmobile = true,
		 .loadDefaultRace = true, .header = true});
	REQUIRE(world.addBuilding("swarm", 4, 4));
	REQUIRE(world.addBuilding("inn", 40, 40, 0, 1));
	world.addUnit(WORKER);
	world.addUnit(WORKER);
	world.addUnit(WORKER, 43, 40, 1);
	world.addUnit(WORKER, 44, 40, 1);
	world.addUnit(WORKER, 45, 40, 1);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.localTeam = world.team;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	// A short match: four samples of every team's history.
	world.step(4 * Stats::SAMPLE_INTERVAL_TICKS + 1);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});

	// In a match: the colony's own panel metrics, never another team's.
	const auto match = commander.call("game_stats", Json::object());
	CHECK(match.at("mode") == "match");
	CHECK(match.at("team").at("team") == 0);
	CHECK(match.at("status").contains("win_chance_percent"));
	REQUIRE(match.at("metrics").size() == 8);
	for (const auto &metric : match.at("metrics"))
	{
		INFO(metric.at("id").get<std::string>());
		REQUIRE(metric.at("teams").size() == 1);
		CHECK(metric.at("teams")[0].at("team") == 0);
	}
	const auto &population = match.at("metrics")[0];
	CHECK(population.at("id") == "population");
	CHECK(population.at("teams")[0].at("summary").at("latest") == 2);
	CHECK(population.at("teams")[0].at("seconds").size() ==
		  population.at("teams")[0].at("values").size());
	// The list is the panel's: without the win probability history, which reads
	// hidden opponent state.
	const auto list = commander.call("game_stats", {{"list", true}});
	CHECK(list.at("metrics").size() > 40);
	for (const auto &metric : list.at("metrics"))
		CHECK(metric.at("id") != "win probability");
	CHECK_THROWS_WITH_AS(commander.call("game_stats", {{"metrics", {"win probability"}}}),
						 doctest::Contains("end_game"), std::runtime_error);
	CHECK_THROWS_WITH_AS(commander.call("game_stats", {{"metrics", {"no such metric"}}}),
						 doctest::Contains("list: true"), std::runtime_error);
	// Thinned to the requested points, first and last sample kept; a counter carries its total.
	const auto births = commander.call("game_stats", {{"metrics", {"births"}}, {"points", 2}, {"split", true}});
	const auto &born = births.at("metrics")[0];
	CHECK(born.at("bands").size() >= 3);
	CHECK(born.at("teams")[0].at("seconds").size() == 2);
	CHECK(born.at("teams")[0].at("band_values").size() == born.at("bands").size());
	CHECK(born.at("teams")[0].at("summary").contains("total"));

	// After the match the results screen holds every team, opponents included.
	commander.clear();
	EndGameScreen results(&world.gui);
	const auto finished = Hive::finishedMatchStats(results, Json::object());
	CHECK(finished.at("mode") == "finished");
	CHECK(finished.at("duration_seconds") == results.matchSeconds());
	REQUIRE(finished.at("teams").size() == 2);
	for (const auto &team : finished.at("teams"))
	{
		CHECK(team.at("overview").contains("population peak"));
		CHECK(team.at("color").get<std::string>().size() == 7);
		CHECK(team.value("you", false) == (team.at("team") == 0));
	}
	REQUIRE(finished.at("metrics")[0].at("id") == "win probability");
	for (const auto &metric : finished.at("metrics"))
		if (!metric.value("map_wide", false))
			CHECK(metric.at("teams").size() == 2);
	const auto &opponent = std::find_if(finished.at("metrics")[1].at("teams").begin(),
										finished.at("metrics")[1].at("teams").end(),
										[](const Json &t) { return t.at("team") == 1; });
	CHECK(opponent->at("summary").at("latest") == 3);
	const auto deaths = Hive::finishedMatchStats(results, {{"metrics", {"deaths"}}, {"points", 1000}});
	REQUIRE(deaths.at("metrics").size() == 1);
	CHECK(deaths.at("points") == 300);
	CHECK(Hive::finishedMatchStats(results, {{"list", true}}).at("metrics").size() > list.at("metrics").size());
}
TEST_CASE("MCP game_building and game_zone queue the panel's orders and report caps" *
		  doctest::test_suite("GUIInteractionCoverage"))
{
	glob2test::HeadlessGlobals globals({.seed = 19});
	PresentationFrame published; // Outlives the GUI that points at it.
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .discovered = true, .clearImmobile = true,
								   .loadDefaultRace = true, .header = true});
	auto *swarm = world.addBuilding("swarm", 4, 4);
	auto *flag = world.addBuilding("warflag", 30, 30);
	REQUIRE(swarm);
	REQUIRE(flag);
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.gui.localTeam = world.team;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	world.game.snapshots().invalidateBoundary();
	world.gui.rebuildBuildingChoices(false);
	world.gui.prepareLocalPresentation(published);
	world.gui.setPublishedScene(&published);
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> ready;
		ready.set_value(Hive::invoke(input));
		return ready.get_future();
	});
	auto queued = [&]
	{
		auto orders = world.gui.orderQueue.snapshot();
		while (!world.gui.orderQueue.empty())
			world.gui.orderQueue.pop_front();
		return orders;
	};
	const Json swarmRef = {{"id", swarm->gid}, {"generation", swarm->scriptIdentity}};
	auto result = commander.call("game_building",
		{{"id", swarm->gid}, {"generation", swarm->scriptIdentity}, {"workers", 255},
		 {"production", {{"workers", 1}, {"warriors", 2}}}});
	CHECK(result.at("workers_capped_at") == swarm->type->semantics.assignmentLimit);
	auto orders = queued();
	REQUIRE(orders.size() == 2);
	CHECK(std::dynamic_pointer_cast<OrderModifyBuilding>(orders[0]));
	CHECK(std::dynamic_pointer_cast<OrderModifySwarm>(orders[1]));
	commander.call("game_building", {{"id", flag->gid}, {"generation", flag->scriptIdentity},
									 {"action", "move"}, {"x", 33}, {"y", 34}});
	orders = queued();
	REQUIRE(orders.size() == 1);
	auto move = std::dynamic_pointer_cast<OrderMoveFlag>(orders[0]);
	REQUIRE(move);
	CHECK(move->x == 33);
	CHECK(move->y == 34);
	// A range above the flag type's maximum is capped and reported, not refused, so
	// the rest of the change still applies.
	result = commander.call("game_building", {{"id", flag->gid}, {"generation", flag->scriptIdentity},
											  {"action", "move"}, {"x", 30}, {"y", 31},
											  {"range", 10}, {"workers", 13}});
	CHECK(result.at("range_capped_at") == flag->type->maxUnitStayRange);
	orders = queued();
	REQUIRE(orders.size() == 3);
	auto range = std::dynamic_pointer_cast<OrderModifyFlag>(orders[1]);
	REQUIRE(range);
	CHECK(range->range == flag->type->maxUnitStayRange);
	CHECK(std::dynamic_pointer_cast<OrderMoveFlag>(orders[2]));
	// A stale generation or another team's id is refused, nothing queued.
	CHECK_THROWS(commander.call("game_building", {{"id", swarm->gid}, {"generation", swarm->scriptIdentity + 1},
												  {"workers", 1}}));
	CHECK_THROWS(commander.call("game_building", {{"id", swarm->gid}, {"generation", swarm->scriptIdentity}}));
	CHECK(queued().empty());
	commander.call("game_zone", {{"zone", "guard"}, {"x", 10}, {"y", 12}, {"width", 3}, {"height", 2}});
	orders = queued();
	REQUIRE(orders.size() == 1);
	auto zone = std::dynamic_pointer_cast<OrderAlterGuardArea>(orders[0]);
	REQUIRE(zone);
	CHECK(zone->centerX == 10);
	CHECK(zone->centerY == 12);
	CHECK(zone->maxX - zone->minX == 3);
	CHECK(zone->maxY - zone->minY == 2);
	// Chat queues the chat box's message, to the chat box's recipients; empty text
	// queues nothing.
	world.gui.chatMask = 0x5;
	const auto chat = commander.call("game_chat", {{"text", "hello allies"}});
	CHECK(chat.at("sent") == true);
	orders = queued();
	REQUIRE(orders.size() == 1);
	auto message = std::dynamic_pointer_cast<MessageOrder>(orders[0]);
	REQUIRE(message);
	CHECK(message->recipientsMask == 0x5);
	CHECK(message->messageOrderType == MessageOrder::NORMAL_MESSAGE_TYPE);
	CHECK(std::string(message->getText()) == "hello allies");
	CHECK_THROWS(commander.call("game_chat", {{"text", ""}}));
	CHECK(queued().empty());
	// The order holds 254 bytes of UTF-8: the longest text is sent whole and
	// decodes from its own bytes; one byte more (here, a two-byte letter past
	// 253 ASCII bytes) is refused before anything is queued. A 255-byte message
	// used to encode a length that wrapped to 0 and stop the match's replay.
	const std::string longest(ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES, 'x');
	CHECK(commander.call("game_chat", {{"text", longest}}).at("sent") == true);
	orders = queued();
	REQUIRE(orders.size() == 1);
	message = std::dynamic_pointer_cast<MessageOrder>(orders[0]);
	REQUIRE(message);
	CHECK(std::string(message->getText()) == longest);
	std::vector<Uint8> wire(1 + message->getDataLength());
	wire[0] = message->getOrderType();
	std::copy_n(message->getData(), message->getDataLength(), wire.data() + 1);
	CHECK(Order::getOrder(wire.data(), static_cast<int>(wire.size()), VERSION_MINOR));
	try
	{
		commander.call("game_chat", {{"text", std::string(253, 'x') + "\xc3\xa9"}});
		FAIL("an over-long chat message was accepted");
	}
	catch (const std::exception &e)
	{
		CHECK(std::string(e.what()).find("at most 254") != std::string::npos);
	}
	CHECK(queued().empty());
}
