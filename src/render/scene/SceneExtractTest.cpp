#include "GameEvent.h"
#include "ClientAreaPreview.h"
#include "sim/ClientEvents.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// PresentationFrame extraction: entities by gid, the selection's map-view contribution, and
// the overlay map's refresh cadence.
#include "EngineFixtures.h"
#include "OverlayAreas.h"
#include "SceneExtract.h"
#include "SceneBuffer.h"
#include "render/UnitMotion.h"
#include "UnitTiming.h"
#include "building/hud/BuildingPresentation.h"
#include <nlohmann/json.hpp>
#include <SDLGraphicContext.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include "sim/presentation/SceneInputs.h"
#include "sim/presentation/ScenePreparation.h"
#include <thread>
#include "sim/snapshot/SnapshotStore.h"

namespace {
template<class Source> concept AcceptsLivePresentationSource=requires(SceneExtractor& preparer,const Source& source,const SceneRequest& request,PresentationFrame& frame) {
    preparer.prepare(source,request,frame);
};
static_assert(AcceptsLivePresentationSource<SimulationSnapshot::Handle>);
static_assert(!AcceptsLivePresentationSource<Game>);
static_assert(!AcceptsLivePresentationSource<Map>);
static_assert(!AcceptsLivePresentationSource<Unit>);
static_assert(!AcceptsLivePresentationSource<Building>);
}

TEST_SUITE("SceneExtract")
{
    TEST_CASE("superseded and retired frames promptly release all world leases")
    {
        SceneBuffer<PresentationFrame> buffer;
        const auto fill=[&] {
            auto& frame=buffer.back();
            frame.world.entities=std::make_shared<SimulationSnapshot::Entities>();
            frame.entities.world=frame.world;
            frame.panels.world=frame.world;
            return std::weak_ptr(frame.world.entities);
        };
        auto first=fill();buffer.publish();
        CHECK_FALSE(first.expired());
        auto second=fill();buffer.publish();
        CHECK(first.expired());
        REQUIRE(buffer.acquire());
        auto third=fill();buffer.publish();
        CHECK_FALSE(second.expired());
        REQUIRE(buffer.acquire());
        CHECK(second.expired());
        CHECK_FALSE(third.expired());
    }

    TEST_CASE("map service indicators follow arbitrary and free recipes")
    {
        BuildingType type;
        type.semantics.feeding.enabled=true;
        type.semantics.feeding.cost[ALGA]=2;
        type.maxMaterial[ALGA]=20;
        Sint32 stock[MaterialCount]{}; stock[ALGA]=2;
        CHECK_FALSE(buildingFeedingUnfunded(type,stock));
        CHECK(buildingResourceBarResource(type,stock)==ALGA);
        stock[ALGA]=1;
        CHECK(buildingFeedingUnfunded(type,stock));
        type.semantics.feeding.cost[ALGA]=0;
        CHECK_FALSE(buildingFeedingUnfunded(type,stock));
        CHECK(buildingResourceBarResource(type,stock)==-1);
        auto& recipe=type.semantics.production.recipes[WARRIOR];
        recipe.enabled=true; recipe.cost[STONE]=3;
        type.maxMaterial[STONE]=30;stock[STONE]=9;
        CHECK(buildingResourceBarResource(type,stock)==STONE);
        type.semantics.feeding.cost[ALGA]=2;
        CHECK(buildingResourceBarResource(type,stock)==ALGA);
    }

	TEST_CASE("script area snapshots are opt-in and independent of later map edits")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		world.game.map.setPoint(8, 3, 4);
        world.game.map.setAreaName(8, "Original area");
		PresentationFrame scene; SceneRequest request;
		SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK_FALSE(scene.map.isPointSet(8,3,4));
		request.includeScriptAreas=true;
		SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		world.game.map.unsetPoint(8,3,4);
        world.game.map.setAreaName(8, "Renamed area");
        CHECK(scene.map.getAreaName(8) == "Original area");
        PresentationFrame renamed;
        SceneExtractor().prepare(world.game.captureReadBoundary({},true,SceneExtractor::requirements(request)), request, renamed);
        CHECK(renamed.map.getAreaName(8) == "Renamed area");
		CHECK(scene.map.isPointSet(8,3,4));
		CHECK_FALSE(scene.map.isPointSet(7,3,4));
		request.includeScriptAreas=false;
		SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK_FALSE(scene.map.isPointSet(8,3,4));
	}

    TEST_CASE("retired map storage reuses masks without retaining a world or clearing its values")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world;
        const auto index=world.game.map.coordToIndex(3,4);
        world.game.map.setAreaMask(index,&Tile::forbidden,1);
        SceneRequest request;request.localTeam=0;
        SceneExtractor preparer;
        PresentationFrame frame;
        const auto captured=world.game.captureReadBoundary({},true,SceneExtractor::requirements(request));
        preparer.prepare(captured,request,frame);
        REQUIRE(frame.map.isForbiddenInDisplayedView(3,4));
        frame.releaseWorld();
        CHECK_FALSE(frame.world.entities);
        request.view.viewportX=8;
        preparer.prepare(captured,request,frame);
        CHECK(frame.map.isForbiddenInDisplayedView(3,4));
        world.game.map.setAreaMask(index,&Tile::forbidden,0);
        preparer.prepare(world.game.captureReadBoundary({},true,SceneExtractor::requirements(request)),request,frame);
        CHECK_FALSE(frame.map.isForbiddenInDisplayedView(3,4));
    }

    TEST_CASE("same-tick map edits refresh overlays while retained frames remain immutable")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world;
        SceneExtractor preparer;
        SceneRequest request; request.view.overlay=OverlayArea::Fertility;
        PresentationFrame frame;
        const auto required=SceneExtractor::requirements(request);
        preparer.prepare(world.game.captureReadBoundary({},true,required),request,frame);
        const auto old=frame.overlay;
        const auto value=old->getValue(3,4);
        const Uint16 fertility=value==123 ? 124 : 123;
        world.game.map.setFertility(3,4,fertility);
        preparer.prepare(world.game.captureReadBoundary({},true,required),request,frame);
        REQUIRE(frame.overlay);
        CHECK(frame.overlay!=old);
        CHECK(frame.overlay->getValue(3,4)==fertility);
        CHECK(old->getValue(3,4)==value);
        const auto current=frame.overlay;
        const auto captures=world.game.snapshots().metrics.captures;
        request.view.viewportX=12;
        preparer.prepare(frame.world,request,frame);
        CHECK(frame.overlay==current);
        CHECK(world.game.snapshots().metrics.captures==captures);
    }

	TEST_CASE("units and buildings resolve by gid with their drawn fields and the selection")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .discovered = true, .loadDefaultRace = true});
		Building *inn = world.addBuilding("inn", 4, 4);
		Unit *worker = world.addUnit(WORKER, 10, 10);
		Unit *other = world.addUnit(WORKER, 12, 10, 1);
		worker->hp = 3;

		SceneRequest request;
		request.selectedBuilding = Game::refOf(inn);
		request.selectedUnit = Game::refOf(worker);
		PresentationFrame scene;
		SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);

		const SnapshotUnit *u = scene.entities.unit(worker->gid);
		REQUIRE(u);
		CHECK((u->posX == 10 && u->posY == 10 && u->hp == 3 && u->team == 0 && u->typeNum == WORKER));
		CHECK(scene.entities.owner(*scene.entities.unit(other->gid)).number == 1);
		CHECK(scene.entities.isSelected(*u));
		CHECK_FALSE(scene.entities.isSelected(*scene.entities.unit(other->gid)));

		const SnapshotBuilding *b = scene.entities.building(inn->gid);
		REQUIRE(b);
		CHECK((b->posX == 4 && b->posY == 4 && scene.entities.type(*b)->key == inn->type->key && b->maxHp == inn->getEffectiveMaxHp()));
		CHECK(scene.entities.isSelected(*b));
		CHECK(scene.entities.selectedBuilding.ref == request.selectedBuilding);
		CHECK(scene.map.getGroundUnit(10, 10) == worker->gid);
		CHECK(scene.map.getBuilding(4, 4) == inn->gid);
		CHECK(scene.entities.unit(NOGUID) == nullptr);
	}

	TEST_CASE("overlay maps match a direct computation and refresh per 25-tick window or type change")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 1, .discovered = true, .loadDefaultRace = true});
		Unit *wounded = world.addUnit(WORKER, 10, 10);
		wounded->medical = Unit::MED_DAMAGED;

		SceneExtractor extractor;
		SceneRequest request;
		request.view.overlay = OverlayArea::Damage;
		PresentationFrame scene;
		world.game.stepCounter = 26; // window 1 starts at tick 26
		extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		REQUIRE(scene.overlay);
		OverlayArea direct;
		direct.compute(scene.world, OverlayArea::Damage, 0, scene.world.session->fertilityMaximum);
		CHECK(scene.overlay->getMaximum() == direct.getMaximum());
		CHECK(scene.overlay->getValue(10, 10) == direct.getValue(10, 10));
		CHECK(scene.overlay->getValue(10, 10) > 0);

		// Same window: the snapshot is reused even though the game changed.
		const auto first = scene.overlay;
		wounded->medical = Unit::MED_FREE;
		world.game.stepCounter = 50;
		extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK(scene.overlay == first);

		// Next window (ticks 51..75): recomputed.
		world.game.stepCounter = 51;
		extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK(scene.overlay != first);
		CHECK(scene.overlay->getValue(10, 10) == 0);

		// A type change recomputes at once; None drops the overlay.
		const auto damage = scene.overlay;
		request.view.overlay = OverlayArea::Defence;
		extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK((scene.overlay != damage && scene.overlay->getOverlayType() == OverlayArea::Defence));
		request.view.overlay = OverlayArea::None;
		extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK(!scene.overlay);
	}
    TEST_CASE("published scene retains old entity fields while subsequent scenes reflect changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true});
        auto* unit=world.addUnit(WORKER,10,10);
        SceneExtractor extractor; SceneRequest request; PresentationFrame before,after;
        const auto gid=unit->gid; const auto hp=unit->hp;
        extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, before);
        unit->hp=1;
        world.game.snapshots().invalidateBoundary(); // Explicit same-tick owner edit.
        extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, after);
        REQUIRE(before.entities.unit(gid)); REQUIRE(after.entities.unit(gid));
        CHECK(before.entities.unit(gid)->hp==hp); CHECK(after.entities.unit(gid)->hp==1);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, after);
        CHECK(after.entities.unit(gid)==nullptr);
        CHECK(before.entities.unit(gid)->hp==hp);
    }

    TEST_CASE("stale selection generation never selects a replacement occupying the same gid")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true});
        auto* unit=world.addUnit(WORKER,10,10);
        SceneExtractor extractor; SceneRequest request; PresentationFrame scene;
        request.selectedUnit=Game::refOf(unit); const auto gid=unit->gid;
        extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
        REQUIRE(scene.entities.unit(gid)); CHECK(scene.entities.isSelected(*scene.entities.unit(gid)));
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        auto* replacement=world.addUnit(WORKER,10,10);
        REQUIRE(replacement->gid==gid);
        extractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
        REQUIRE(scene.entities.unit(gid)); CHECK_FALSE(scene.entities.isSelected(*scene.entities.unit(gid)));
    }


	TEST_CASE("smooth unit motion advances the drawn delta within the current action only")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 1, .discovered = true, .loadDefaultRace = true});
		Unit *walker = world.addUnit(WORKER, 10, 10);
		walker->action = WALK;
		walker->dx = 1;
		walker->dy = 1;
		walker->delta = 100;

		SceneRequest request;
		request.tickTime = 1000;
		request.tickInterval = 40;
		PresentationFrame scene;
		SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
		CHECK((scene.tickTime == 1000 && scene.tickInterval == 40));
		const SnapshotUnit *u = scene.entities.unit(walker->gid);
		REQUIRE(u);
		CHECK(unitActionStepSpeed(u->speed,u->action,u->dx,u->dy) == unitActionStepSpeed(walker->speed, WALK, 1, 1));
		REQUIRE(unitActionStepSpeed(u->speed,u->action,u->dx,u->dy) > 0);

		// Off (motion 0) draws exactly the simulated delta.
		CHECK(drawnUnitDelta(*u, 0.f) == 100);
		CHECK(unitMotionFraction(scene, 1000) == 0.f);
		CHECK(unitMotionFraction(scene, 1020) == doctest::Approx(0.5f));
		CHECK(unitMotionFraction(scene, 5000) == 1.f);
		CHECK(drawnUnitDelta(*u, 0.5f) == 100 + int(0.5f * float(unitActionStepSpeed(u->speed,u->action,u->dx,u->dy))));
		// A full interval reaches what the next tick computes, without crossing the action.
		CHECK(drawnUnitDelta(*u, 1.f) == std::min(UNIT_DELTA_MAX, 100 + unitActionStepSpeed(u->speed,u->action,u->dx,u->dy)));
		SnapshotUnit ending = *u;
		ending.delta = UNIT_DELTA_MAX - 1;
		CHECK(drawnUnitDelta(ending, 1.f) == UNIT_DELTA_MAX);

		// Uncapped simulation: no interval, no motion between ticks.
		scene.tickInterval = 0;
		CHECK(unitMotionFraction(scene, 1020) == 0.f);
	}
    TEST_CASE("connected overlays use configured groups and scene retains its catalog")
    {
        glob2test::HeadlessGlobals globals;
        PresentationFrame scene;
        const SnapshotBuilding* extracted=nullptr;
        std::string key;
        {
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true});
        const int firstId=world.game.buildingsTypes.getFinishedTypeNum("warflag");
        const int otherId=world.game.buildingsTypes.getFinishedTypeNum("explorationflag");
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        for (int id : {firstId,otherId})
        {
            catalog["variants"][id]["properties"]["crossConnectMultiImage"]=1;
            catalog["variants"][id]["presentation"]["connectionGroup"]="joined-canopies";
        }
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
        world.game.configureBuildingCatalog();
        Building* first=world.addBuilding("warflag",4,4);
        world.addBuilding("explorationflag",4+first->type->width,4);
        world.addBuilding("explorationflag",4,4+first->type->height,0,1);
        SceneRequest request;
        SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
        extracted=scene.entities.building(first->gid);
        REQUIRE(extracted);
        CHECK(scene.entities.connections(*extracted)==1);
        CHECK(scene.map.getBuilding(4,4)==NOGBID);
        key=scene.entities.type(*extracted)->key;
        }
        CHECK(scene.entities.type(*extracted)->key==key);
        CHECK(scene.entities.type(*extracted)->presentation.connectionGroup=="joined-canopies");
    }

    TEST_CASE("building events preserve concrete catalog IDs and key-only display names")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=1,.loadDefaultRace=true});
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& variants=snapshot["variants"];
        const auto prototype=variants[3];
        while (variants.size()<=256)
        {
            auto variant=prototype;
            variant["id"]=variants.size();
            variant["key"]="fixture.event."+std::to_string(variants.size());
            variant["properties"].erase("type");
            variant["properties"].erase("shortTypeNum");
            variant["previous"]=""; variant["next"]="";
            variant["semantics"]["repairable"]=false;
            variant["presentation"]["displayName"]="Custom refuge";
            variants.push_back(std::move(variant));
        }
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());
        world.game.configureBuildingCatalog();
        for (const auto event : {GameEvent::buildingUnderAttack(0,1,2,256),GameEvent::buildingCompleted(0,1,2,256)})
        {
            CHECK(event.getTypeNum()==256);
            CHECK(event.formatMessage(world.game).find("Custom refuge")!=std::string::npos);
        }
    }

    TEST_CASE("building sprite frames honor damage effective maximum and connection precedence")
    {
        BuildingType type;type.gameSpriteImage=7;type.gameSpriteCount=4;type.hpMax=200;
        CHECK(buildingSpriteFrame(type,200,200,0)==7);
        CHECK(buildingSpriteFrame(type,0,200,0)==10);
        CHECK(buildingSpriteFrame(type,50,200,0)==10);
        CHECK(buildingSpriteFrame(type,51,200,0)==9);
        // A repair site's visual descriptor may have a different HP maximum from its origin.
        CHECK(buildingSpriteFrame(type,200,400,0)==9);
        // Intentional custom-only behavior: no health means the base frame, even for
        // multiple authored frames. The old ground formula selected the last frame.
        CHECK(buildingSpriteFrame(type,0,0,0)==7);
        for(int maximum : {0,1,200,400})for(int hp=0;hp<=maximum;++hp)
        {
            const int frame=buildingSpriteFrame(type,hp,maximum,0);
            CHECK(frame>=7);CHECK(frame<11);
        }
        type.gameSpriteCount=1;
        CHECK(buildingSpriteFrame(type,0,0,0)==7); // unchanged stock healthless flags
        CHECK(buildingSpriteFrame(type,0,200,0)==7);
        type.crossConnectMultiImage=1;
        CHECK(buildingSpriteFrame(type,0,0,15)==22);
        CHECK(buildingSpriteFrame(type,1,200,5)==12); // connections override damage
    }

    TEST_CASE("custom ground and overlay frames render connections and damage [display:1100x1100][artifacts]")
    {
        using Json=nlohmann::json;
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=1024});
        globals->settings.clouds=false;
        globals->settings.cloudShadows=false;
        // Spectator presentation makes both teams' overlays visible. No replay is stepped.
        globals->liveSpectating=true;
        globals->replayVisibleTeams=3;
        globals->replayShowFlags=true;
        const auto evidence=glob2test::artifactDir();
        for (bool overlay : {false,true})
        {
            CAPTURE(overlay);
            const std::string lane=overlay?"overlay":"ground";
            glob2test::HeadlessGame world({.wDec=5,.hDec=5,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=713});
            auto catalog=Json::parse(world.game.buildingsTypes.snapshotJson());
            const auto prototype=catalog["variants"][world.game.buildingsTypes.getFinishedTypeNum("stonewall")];
            for (int variant=0;variant<4;++variant)
            {
                auto custom=variant==3
                    ? catalog["variants"][world.game.buildingsTypes.getFinishedTypeNum("inn")] : prototype;
                custom["id"]=catalog["variants"].size();
                custom["key"]=std::array{"visual.refuge","visual.infirmary","visual.other-group","visual.damage"}[variant];
                custom["previous"]="";custom["next"]="";
                auto& props=custom["properties"];
                props.erase("type");props.erase("shortTypeNum");
                props["maxUnitInside"]=4;
                auto& sem=custom["semantics"];
                sem["occupiesGround"]=!overlay;sem["instantPlacement"]=true;
                sem["placeable"]=true;sem["repairable"]=false;
                sem["assignmentLimit"]=0;sem["regenerationPerTick"]=1;
                sem[variant==1?"healing":"feeding"]={{"enabled",true},{"unitMask",7},{"duration",12},{"cost",Json::object()}};
                custom["presentation"]["connectionGroup"]=variant==3?"":variant==2?"separate":"care-canopies";
                custom["presentation"]["connectsAcrossTeams"]=false;
                custom["presentation"]["defaultAssigned"]=0;
                custom["presentation"]["displayName"]=custom["key"];
                catalog["variants"].push_back(std::move(custom));
            }
            world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
            world.game.buildingsTypes.loadSprites();world.game.configureBuildingCatalog();
            std::vector<std::pair<Uint16,int>> centers;
            const auto add=[&](const char* key,int x,int y,int team=0) {
                const int id=world.game.buildingsTypes.findByKey(key);REQUIRE(id>=0);
                auto* b=world.game.addBuilding(x,y,id,team,0,0);REQUIRE(b);
                return b;
            };
            // All sixteen cardinal masks, with a DIFFERENT key supplying every connection.
            for(int mask=0;mask<16;++mask)
            {
                const int x=3+(mask%4)*6,y=4+(mask/4)*6;
                auto* center=add("visual.refuge",x,y);centers.emplace_back(center->gid,mask);
                if(mask&1)add("visual.infirmary",x+1,y);
                if(mask&2)add("visual.infirmary",x-1,y);
                if(mask&4)add("visual.infirmary",x,y+1);
                if(mask&8)add("visual.infirmary",x,y-1);
            }
            // Same-group foreign team does not connect. Same team/different group does not connect.
            auto* teamBoundary=add("visual.refuge",27,5);add("visual.infirmary",28,5,1);
            auto* groupBoundary=add("visual.refuge",27,11);add("visual.other-group",28,11);
            auto* damage=add("visual.damage",13,28);
            PresentationFrame scene;SceneRequest request;SceneExtractor().prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, scene);
            CHECK(scene.entities.connections(*scene.entities.building(teamBoundary->gid))==0);
            CHECK(scene.entities.connections(*scene.entities.building(groupBoundary->gid))==0);
            for(const auto& [gid,mask]:centers)
            {
                REQUIRE(scene.entities.building(gid));CHECK(scene.entities.connections(*scene.entities.building(gid))==mask);
                const auto* b=scene.entities.building(gid);
                CHECK((world.game.map.getBuilding(b->posX,b->posY)==NOGBID)==overlay);
            }
            const auto checksum=world.checksum();
            const auto render=[&](const PresentationFrame& frame,const std::string& suffix) {
                // Verify the actual selected frame exists in each loaded sprite atlas.
                for(const auto& b:frame.entities.buildings)
                {
                    const int index=buildingSpriteFrame(*frame.entities.type(b),b.hp,b.maxHp,frame.entities.connections(b));
                    REQUIRE(index>=0);REQUIRE(index<frame.entities.type(b)->gameSpritePtr->getFrameCount());
                }
                Game::ViewState view;view.scene=&frame;
                auto* gfx=globals->gfx;
                gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
                gfx->drawFilledRect(0,0,1024,1024,GAGCore::Color(20,30,20));
                gfx->beginMapTransform(1,1,1,0,0,1024,1024);
                glob2test::drawMap(world.game,0,0,1024,1024,0,0,0,0,0,view,Game::DRAW_WHOLE_MAP|Game::DRAW_NO_CLOUD_LAYER,nullptr,nullptr,true);
                gfx->endMapTransform();gfx->nextFrame();
                auto* image=gfx->completedFrame();REQUIRE(image);
                REQUIRE(SDL_SaveBMP(image,(evidence/(lane+"-"+suffix+".bmp")).string().c_str()));
                CHECK(world.checksum()==checksum);
                std::vector<Uint8> pixels;
                const auto* bytes=static_cast<const Uint8*>(image->pixels);
                for(int y=0;y<image->h;++y)
                    pixels.insert(pixels.end(),bytes+y*image->pitch,bytes+y*image->pitch+image->w*SDL_BYTESPERPIXEL(image->format));
                return pixels;
            };
            const auto actual=render(scene,"connected");
            SceneExtractor workerExtractor;
            auto inputs = SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request);
            PresentationFrame prepared;
            workerExtractor.prepare(*inputs, prepared);
            CHECK_MESSAGE(actual == render(prepared, "snapshot"), "snapshot preparation must preserve every rendered pixel");
            CHECK_MESSAGE(actual==render(scene,"repeat"),"identical frozen scenes must render identically before the mask control");
            // Counterfactual RENDER-ONLY control: same scene and entities, masks forced to zero.
            // Both the ground and overlay renderer must use these extracted masks.
            PresentationFrame disconnected=scene;
            std::fill(disconnected.entities.connectionMasks.begin(),disconnected.entities.connectionMasks.end(),0);
            const auto control=render(disconnected,"zero-mask-control");
            CHECK_MESSAGE(actual!=control,lane<<" full-map renderer must use its nonzero connection masks");
            auto woundedWorld=scene.world;
            auto woundedEntities=std::make_shared<SimulationSnapshot::Entities>(*woundedWorld.entities);
            for(auto& b:woundedEntities->buildings)if(b.gid==damage->gid)b.hp=1;
            woundedWorld.entities=std::move(woundedEntities);
            PresentationFrame wounded;
            workerExtractor.prepare(woundedWorld,request,wounded);
            CHECK_MESSAGE(actual!=render(wounded,"wounded"),lane<<" full-map renderer must use the configured damage frame");
        }
    }

}

TEST_SUITE("SceneExtract")
{
TEST_CASE("snapshot preparation retains definitions after the source game is destroyed")
{
    glob2test::HeadlessGlobals globals;
    SceneExtractor extractor;
    std::shared_ptr<SceneInputs> input;
    Uint16 unitId = 0xffff, buildingId = 0xffff;
    {
        glob2test::HeadlessGame world{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .loadDefaultRace=true}};
        auto* unit = world.addUnit(WORKER, 12, 12, 0);
        auto* building = world.addBuilding("inn", 4, 4, 0, 0);
        REQUIRE(unit); REQUIRE(building);
        unitId = unit->gid; buildingId = building->gid;
        SceneRequest request;
        request.selectedUnit = Game::refOf(unit);
        request.selectedBuilding = Game::refOf(building);
        input = SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request);
    }
    PresentationFrame scene;
    extractor.prepare(*input, scene);
    input.reset();
    REQUIRE(scene.entities.unit(unitId));
    REQUIRE(scene.entities.building(buildingId));
    CHECK(scene.entities.units.data()==scene.world.entities->units.data());
    CHECK(scene.entities.type(*scene.entities.building(buildingId))->key == "inn.0.finished");
    CHECK(scene.panels.unit.unitTypes.data()==scene.world.catalogs->unitTypes[scene.panels.unit.state().typeNum].data());
    CHECK(scene.map.getW() == 32);
    CHECK(scene.map.getBuilding(4, 4) == buildingId);
}

TEST_CASE("snapshot preparation matches retained queries and survives later live mutations")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
    auto* unit = world.addUnit(WORKER,12,12,0);
    auto* building = world.addBuilding("inn",4,4,0,0);
    REQUIRE(unit); REQUIRE(building);
    unit->levelUpAnimation = 7; unit->magicActionAnimation = 3;
    unit->medical = Unit::MED_DAMAGED; unit->hp = 1; unit->hungry = 0;
    building->lastShootStep = 19; building->lastShootSpeedX = 2;
    SceneRequest request;
    request.selectedUnit = Game::refOf(unit); request.selectedBuilding = Game::refOf(building);
    request.view.displayW = 800; request.view.displayH = 600;
    request.includeScriptAreas = true;
    world.game.map.setPoint(8,3,4);
    for (auto overlay : {OverlayArea::None, OverlayArea::Starving, OverlayArea::Damage, OverlayArea::Defence, OverlayArea::Fertility})
    {
        CAPTURE(overlay);
        request.view.overlay = overlay;
        SceneExtractor legacyExtractor, snapshotExtractor;
        PresentationFrame legacy, actual;
        legacyExtractor.prepare((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request, legacy);
        auto input = SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements(request)), request);
        // No later read may reach the live unit or any map layer.
        const auto hp = unit->hp;
        unit->hp = 123;
        world.game.map.setGroundUnit(20,20,42);
        snapshotExtractor.prepare(*input, actual);
        unit->hp = hp;
        world.game.map.setGroundUnit(20,20,legacy.map.getGroundUnit(20,20));
        CHECK(actual.tick == legacy.tick);
        CHECK(actual.entities.units.size() == legacy.entities.units.size());
        CHECK(actual.entities.buildings.size() == legacy.entities.buildings.size());
        REQUIRE(actual.entities.unit(unit->gid));
        const auto& u = *actual.entities.unit(unit->gid);
        CHECK(u.hp == hp); CHECK(u.levelUpAnimation == 7); CHECK(u.magicActionAnimation == 3);
        CHECK(drawnUnitDelta(u,0.5f)==drawnUnitDelta(*legacy.entities.unit(unit->gid),0.5f));
        CHECK(actual.entities.building(building->gid)->lastShootStep == 19);
        CHECK(actual.entities.building(building->gid)->lastShootSpeedX == 2);
        CHECK(actual.panels.unit.state().hp == legacy.panels.unit.state().hp);
        CHECK(actual.panels.building.hardSpaceForUpgrade == legacy.panels.building.hardSpaceForUpgrade);
        CHECK(actual.map.materialPresence() == legacy.map.materialPresence());
        for (int y=0; y<32; ++y) for (int x=0; x<32; ++x)
        {
            CHECK(actual.map.cellCorners(x,y) == legacy.map.cellCorners(x,y));
            CHECK(actual.map.terrainTypeAt(x,y) == legacy.map.terrainTypeAt(x,y));
            CHECK(actual.map.appearanceAt(x,y) == legacy.map.appearanceAt(x,y));
            CHECK(actual.map.vertexTerrainAt(x,y) == legacy.map.vertexTerrainAt(x,y));
            CHECK(actual.map.getGroundUnit(x,y) == legacy.map.getGroundUnit(x,y));
            CHECK(actual.map.getAirUnit(x,y) == legacy.map.getAirUnit(x,y));
            CHECK(actual.map.getBuilding(x,y) == legacy.map.getBuilding(x,y));
            CHECK(actual.map.getResource(x,y).type == legacy.map.getResource(x,y).type);
            CHECK(actual.map.canResourcesGrow(x,y) == legacy.map.canResourcesGrow(x,y));
            CHECK(actual.map.isMapDiscovered(x,y,1) == legacy.map.isMapDiscovered(x,y,1));
            CHECK(actual.map.isFOWDiscovered(x,y,1) == legacy.map.isFOWDiscovered(x,y,1));
            CHECK(actual.map.isPointSet(8,x,y) == legacy.map.isPointSet(8,x,y));
            CHECK(actual.map.isHardSpaceForBuilding(x,y,1,1) == legacy.map.isHardSpaceForBuilding(x,y,1,1));
            for (unsigned m=0; m<MaterialCount; ++m)
                CHECK(actual.map.materialAmountAt(actual.map.coordToIndex(x,y),m) == legacy.map.materialAmountAt(legacy.map.coordToIndex(x,y),m));
            if (overlay != OverlayArea::None)
            {
                REQUIRE(actual.overlay);
                CHECK(actual.overlay->getMaximum() == legacy.overlay->getMaximum());
                CHECK(actual.overlay->getValue(x,y) == legacy.overlay->getValue(x,y));
            }
        }
    }
}
}

TEST_CASE("Serial hosts publish immutable Scenes through workers or explicit fallback" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    for (unsigned threads : {1u,2u})
    {
        if (threads>1 && !GAGCore::ThreadSupport::available) continue;
        glob2test::HeadlessGame world;
        auto* unit=world.addUnit(WORKER,2,2);
        ComputeExecutor executor; executor.configure(threads);
        SceneExtractor extractor;
        ScenePreparation presentation(executor);
        REQUIRE(presentation.readyToCapture());
        const auto gid=unit->gid;
        const auto hp=unit->hp;
        presentation.submit(SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {}));
        unit->hp=hp-1;
        const PresentationFrame* scene=nullptr;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do { scene=presentation.acquire(); if (!scene) std::this_thread::yield(); }
        while (!scene && std::chrono::steady_clock::now()<deadline);
        REQUIRE(scene);
        REQUIRE(scene->entities.unit(gid));
        CHECK(scene->entities.unit(gid)->hp==hp);
        // Publishing can precede the task's final return; allow that narrow tail.
        while (!presentation.readyToCapture() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
        REQUIRE(presentation.readyToCapture());
        presentation.submit(SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {}));
        // Destruction cancels without waiting; an active chunk owns its storage.
    }
}

TEST_SUITE("SceneExtract")
{
TEST_CASE("client paint survives old Scenes and retires only with its execution revision")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    SceneExtractor extractor;
    PresentationFrame oldScene;
    extractor.prepare(*SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {}),oldScene);
    ClientAreaPreview preview;
    preview.refresh(oldScene,0);
    const auto index=oldScene.map.coordToIndex(3,4);
    Utilities::BitArray mask(1,true);
    auto add=std::make_shared<OrderAlterForbidden>(0,BrushTool::MODE_ADD,3,4,1,1,mask);
    preview.set(0,index,true);
    preview.track(add,true);
    preview.acknowledge(*add,1);
    preview.refresh(oldScene,0);
    CHECK(preview.shown[0].get(index));
    CHECK_FALSE(oldScene.map.displayedArea(0).get(index));
    auto remove=std::make_shared<OrderAlterForbidden>(0,BrushTool::MODE_DEL,3,4,1,1,mask);
    preview.track(remove);
    PresentationFrame next;
    auto tile=world.game.map.getTile(3,4); tile.forbidden|=1;
    world.game.map.replaceTile(3,4,tile);
    extractor.prepare(*SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {}),next);
    next.executedOrderRevision=1;
    preview.refresh(next,0);
    CHECK_FALSE(preview.shown[0].get(index));
    // An active stroke remains above both the acknowledged and queued layers.
    preview.set(0,index,true);
    next.tick++;
    preview.refresh(next,0);
    CHECK(preview.shown[0].get(index));
    preview.refresh(next,1);
    CHECK(preview.shown[0].get(index)==next.map.displayedArea(0).get(index));
}
TEST_CASE("order execution revisions are captured even without advancing the tick")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    ClientEvents events;
    world.game.clientEvents=&events;
    SceneExtractor extractor;
    PresentationFrame before,after;
    extractor.prepare(*SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {}),before);
    world.game.publishClientEvent(ClientEvent::OrderExecuted{std::make_shared<NullOrder>()});
    extractor.prepare(*SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {}),after);
    CHECK(before.tick==after.tick);
    CHECK(after.executedOrderRevision==before.executedOrderRevision+1);
    events.drain([&](auto event) {
        if(auto* done=std::get_if<ClientEvent::OrderExecuted>(&event))
            CHECK(done->revision==after.executedOrderRevision);
    });
    world.game.clientEvents=nullptr;
}
}

TEST_CASE("farm input eligibility is frozen with presentation inputs" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.header=true;
    options.experiments.set(ExperimentId::FarmAreas);
    glob2test::HeadlessGame world(options);
    SceneExtractor extractor;
    auto input=SceneExtractor::inputs((world.game).captureReadBoundary({},true,SceneExtractor::requirements({})), {});
    PresentationFrame scene;
    extractor.prepare(*input,scene);
    for(int y=0;y<world.game.map.getH();++y)
        for(int x=0;x<world.game.map.getW();++x)
            CHECK(scene.map.canPaintFarmArea(x,y)==world.game.map.canPaintFarmArea(x,y));
    const bool before=scene.map.canPaintFarmArea(4,4);
    world.game.map.setVertexTerrain(4,4,WATER);
    CHECK(scene.map.canPaintFarmArea(4,4)==before);
}

TEST_CASE("published catalog definitions remain immutable across owner reconfiguration" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture;
    auto& game=fixture.game;
    SceneRequest request;
    const auto required=SceneExtractor::requirements(request);
    const auto before=game.captureReadBoundary({},true,required);
    const int type=game.buildingsTypes.getFinishedTypeNum("inn");
    REQUIRE(type>=0);
    const int hp=before.catalogs->typeDefinitions->at(type).hpMax;
    game.buildingsTypes.get(type)->hpMax=hp+17;
    game.configureBuildingCatalog();
    const auto after=game.captureReadBoundary({},true,required);
    CHECK(before.catalogs->typeDefinitions->at(type).hpMax==hp);
    CHECK(after.catalogs->typeDefinitions->at(type).hpMax==hp+17);
    CHECK(before.catalogs!=after.catalogs);
    CHECK(before.observationRevision!=after.observationRevision);
    SceneRequest moved=request; moved.view.viewportX=8;
    PresentationFrame retained;
    SceneExtractor().prepare(before,moved,retained);
    CHECK(retained.buildingTypes->at(type).hpMax==hp);
}

TEST_CASE("statistics history is optional immutable and reused until a new sample" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture;
    auto& game = fixture.game;
    SceneRequest normal, chart;
    chart.includeHistory = true;
    const auto ordinary = game.captureReadBoundary({}, true, SceneExtractor::requirements(normal));
    CHECK_FALSE(ordinary.history);
    CHECK_FALSE(ordinary.telemetry);
    const auto before = game.captureReadBoundary({}, true, SceneExtractor::requirements(chart));
    REQUIRE(before.history);
    REQUIRE_FALSE(before.history->teams.empty());
    const auto retained = before.history->teams[0];
    const auto samples = retained->measurementHistory.size();
    game.objectives.setGameObjectiveText(0, "original objective");
    game.gameHints.setHintVisible(0);
    game.snapshots().invalidateBoundary();
    const auto unchanged = game.captureReadBoundary({}, true, SceneExtractor::requirements(chart));
    CHECK(unchanged.history->teams[0] == retained);
    game.teams[0]->stats.initializeMeasurements(game.stepCounter);
    game.snapshots().invalidateBoundary();
    const auto changed = game.captureReadBoundary({}, true, SceneExtractor::requirements(chart));
    CHECK(changed.history->teams[0] != retained);
    CHECK(retained->measurementHistory.size() == samples);
    CHECK_FALSE(changed.project(SceneExtractor::requirements(normal)).history);
}

TEST_CASE("ordinary captures reuse unchanged HUD samples" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture;
    auto& game = fixture.game;
    const auto required = SceneExtractor::requirements({});
    const auto before = game.captureReadBoundary({}, true, required);
    game.snapshots().invalidateBoundary();
    const auto after = game.captureReadBoundary({}, true, required);
    REQUIRE(before.statistics);
    REQUIRE(after.statistics);
    REQUIRE_FALSE(before.statistics->teams.empty());
    CHECK(before.statistics->teams[0] == after.statistics->teams[0]);
}

TEST_CASE("large defence overlay preparation yields and finishes its kernel" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=7, .hDec=7});
    auto* tower = fixture.addBuilding("defencetower", 4, 4);
    REQUIRE(tower);
    fixture.game.buildingsTypes.get(tower->typeNum)->shootingRange = 64;
    fixture.game.configureBuildingCatalog();
    SceneRequest request;
    request.view.overlay = OverlayArea::Defence;
    const auto world = fixture.game.captureReadBoundary({}, true, SceneExtractor::requirements(request));
    const auto input = SceneExtractor::inputs(world, request);
    SceneExtractor extractor;
    PresentationFrame frame;
    SceneRequest previousRequest = request;
    previousRequest.view.overlay = OverlayArea::Fertility;
    extractor.prepare(world, previousRequest, frame);
    const auto previousOverlay = frame.overlay;
    const auto chunks = SceneExtractor::preparationChunks(*input);
    // Interrupt the first defence kernel, then restart with the same request.
    // Its partially updated cache key must not reuse the completed fertility map.
    size_t interrupted = 0;
    while (interrupted < chunks && extractor.prepareChunk(*input, frame, interrupted)) ++interrupted;
    REQUIRE(interrupted < chunks);
    size_t chunk = 0, claims = 0;
    while (chunk < chunks && claims++ < chunks * 32)
        if (extractor.prepareChunk(*input, frame, chunk)) ++chunk;
    REQUIRE(chunk == chunks);
    CHECK(claims > chunks);
    REQUIRE(frame.overlay);
    CHECK(frame.overlay->getOverlayType() == OverlayArea::Defence);
    CHECK(frame.overlay != previousOverlay);
    CHECK(previousOverlay->getOverlayType() == OverlayArea::Fertility);
}

TEST_CASE("empty worlds and a view whose team disappeared prepare without live fallback" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.teams=0});
    SceneRequest request;
    const auto world=fixture.game.captureReadBoundary({},true,SceneExtractor::requirements(request));
    SceneExtractor extractor;
    PresentationFrame frame;
    extractor.prepare(world,request,frame);
    CHECK(frame.entities.teamCount==0);
    CHECK_FALSE(frame.panels.local.record);
    CHECK(frame.world.entities==world.entities);
    request.localTeam=31;
    extractor.prepare(world,request,frame);
    CHECK_FALSE(frame.panels.local.record);
}

TEST_CASE("session narrative payloads are immutable and reused until mutation" * doctest::test_suite("SceneExtract"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture;
    auto& game = fixture.game;
    game.missionBriefing = "original briefing";
    game.objectives.setGameObjectiveText(0, "original objective");
    game.gameHints.addNewHint("original hint", false, 1);
    const auto required = SceneExtractor::requirements({});
    const auto first = game.captureReadBoundary({}, true, required);
    game.objectives.setGameObjectiveText(0, "original objective");
    game.gameHints.setHintVisible(0);
    game.snapshots().invalidateBoundary();
    const auto unchanged = game.captureReadBoundary({}, true, required);
    CHECK(first.session->objectives == unchanged.session->objectives);
    CHECK(first.session->hints == unchanged.session->hints);
    CHECK(first.session->missionBriefing == unchanged.session->missionBriefing);
    game.objectives.setGameObjectiveText(0, "edited objective");
    game.objectives.setObjectiveComplete(0);
    game.gameHints.setHintHidden(0);
    game.missionBriefing = "edited briefing";
    game.snapshots().invalidateBoundary();
    const auto changed = game.captureReadBoundary({}, true, required);
    CHECK(first.session->objectives->getGameObjectiveText(0) == "original objective");
    CHECK_FALSE(first.session->objectives->isObjectiveComplete(0));
    CHECK(first.session->hints->isHintVisible(0));
    CHECK(*first.session->missionBriefing == "original briefing");
    CHECK(changed.session->objectives->getGameObjectiveText(0) == "edited objective");
    CHECK(changed.session->objectives->isObjectiveComplete(0));
    CHECK_FALSE(changed.session->hints->isHintVisible(0));
    CHECK(*changed.session->missionBriefing == "edited briefing");
}
