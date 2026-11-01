// SPDX-License-Identifier: GPL-3.0-or-later
#include "InGameStatistics.h"
#include "EngineFixtures.h"
#include "GameGUIViewport.h"
#include "MapZoomControls.h"
#include "GameGUIInternal.h"
#include "touch/GameGUITouch.h"
#include <nlohmann/json.hpp>
#include "Order.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include "EndGameScreen.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <FormatableString.h>
#include "LoadSaveDialog.h"
#include "MapEditDialog.h"
#include <SDLGraphicContext.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
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
        auto& gui=world.gui;gui.localTeamNo=0;gui.localPlayer=0;gui.localTeam=world.team;
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
    panel.host().tapAt(panel.host().bounds("stats/topic/1").center());
    CHECK(panel.topic()==InGameStatisticsPanel::Topic::Colony);
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
    strings->setLang(language);GAGCore::userTextScale=textScale;
    gui.setPublishedScene(nullptr);
    gui.init(); CHECK_FALSE(gui.statisticsPanel);
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
