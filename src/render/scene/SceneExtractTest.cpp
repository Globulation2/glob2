#include "GameEvent.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Scene extraction: entities by gid, the selection's map-view contribution, and
// the overlay map's refresh cadence.
#include "EngineFixtures.h"
#include "OverlayAreas.h"
#include "SceneExtract.h"
#include "render/UnitMotion.h"
#include "UnitTiming.h"
#include "building/hud/BuildingPresentation.h"
#include <nlohmann/json.hpp>
#include <SDLGraphicContext.h>
#include <array>

TEST_SUITE("SceneExtract")
{
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
		Scene scene; SceneRequest request;
		extractScene(world.game, request, scene);
		CHECK_FALSE(scene.map.isPointSet(8,3,4));
		request.includeScriptAreas=true;
		extractScene(world.game, request, scene);
		world.game.map.unsetPoint(8,3,4);
		CHECK(scene.map.isPointSet(8,3,4));
		CHECK_FALSE(scene.map.isPointSet(7,3,4));
		request.includeScriptAreas=false;
		extractScene(world.game, request, scene);
		CHECK_FALSE(scene.map.isPointSet(8,3,4));
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
		Scene scene;
		SceneExtractor().extract(world.game, request, scene);

		const SceneUnit *u = scene.entities.unit(worker->gid);
		REQUIRE(u);
		CHECK((u->posX == 10 && u->posY == 10 && u->hp == 3 && u->team == 0 && u->typeNum == WORKER));
		CHECK(scene.entities.owner(*scene.entities.unit(other->gid)).teamNumber == 1);
		CHECK(scene.entities.isSelected(*u));
		CHECK_FALSE(scene.entities.isSelected(*scene.entities.unit(other->gid)));

		const SceneBuilding *b = scene.entities.building(inn->gid);
		REQUIRE(b);
		CHECK((b->posX == 4 && b->posY == 4 && b->type == inn->type && b->effectiveMaxHp == inn->getEffectiveMaxHp()));
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
		Scene scene;
		world.game.stepCounter = 26; // window 1 starts at tick 26
		extractor.extract(world.game, request, scene);
		REQUIRE(scene.overlay);
		OverlayArea direct;
		direct.compute(world.game, OverlayArea::Damage, 0);
		CHECK(scene.overlay->getMaximum() == direct.getMaximum());
		CHECK(scene.overlay->getValue(10, 10) == direct.getValue(10, 10));
		CHECK(scene.overlay->getValue(10, 10) > 0);

		// Same window: the snapshot is reused even though the game changed.
		const auto first = scene.overlay;
		wounded->medical = Unit::MED_FREE;
		world.game.stepCounter = 50;
		extractor.extract(world.game, request, scene);
		CHECK(scene.overlay == first);

		// Next window (ticks 51..75): recomputed.
		world.game.stepCounter = 51;
		extractor.extract(world.game, request, scene);
		CHECK(scene.overlay != first);
		CHECK(scene.overlay->getValue(10, 10) == 0);

		// A type change recomputes at once; None drops the overlay.
		const auto damage = scene.overlay;
		request.view.overlay = OverlayArea::Defence;
		extractor.extract(world.game, request, scene);
		CHECK((scene.overlay != damage && scene.overlay->getOverlayType() == OverlayArea::Defence));
		request.view.overlay = OverlayArea::None;
		extractor.extract(world.game, request, scene);
		CHECK(!scene.overlay);
	}
    TEST_CASE("published scene retains old entity fields while subsequent scenes reflect changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true});
        auto* unit=world.addUnit(WORKER,10,10);
        SceneExtractor extractor; SceneRequest request; Scene before,after;
        const auto gid=unit->gid; const auto hp=unit->hp;
        extractor.extract(world.game,request,before);
        unit->hp=1;
        extractor.extract(world.game,request,after);
        REQUIRE(before.entities.unit(gid)); REQUIRE(after.entities.unit(gid));
        CHECK(before.entities.unit(gid)->hp==hp); CHECK(after.entities.unit(gid)->hp==1);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        extractor.extract(world.game,request,after);
        CHECK(after.entities.unit(gid)==nullptr);
        CHECK(before.entities.unit(gid)->hp==hp);
    }

    TEST_CASE("stale selection generation never selects a replacement occupying the same gid")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true});
        auto* unit=world.addUnit(WORKER,10,10);
        SceneExtractor extractor; SceneRequest request; Scene scene;
        request.selectedUnit=Game::refOf(unit); const auto gid=unit->gid;
        extractor.extract(world.game,request,scene);
        REQUIRE(scene.entities.unit(gid)); CHECK(scene.entities.isSelected(*scene.entities.unit(gid)));
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        auto* replacement=world.addUnit(WORKER,10,10);
        REQUIRE(replacement->gid==gid);
        extractor.extract(world.game,request,scene);
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
		Scene scene;
		SceneExtractor().extract(world.game, request, scene);
		CHECK((scene.tickTime == 1000 && scene.tickInterval == 40));
		const SceneUnit *u = scene.entities.unit(walker->gid);
		REQUIRE(u);
		CHECK(u->stepSpeed == unitActionStepSpeed(walker->speed, WALK, 1, 1));
		REQUIRE(u->stepSpeed > 0);

		// Off (motion 0) draws exactly the simulated delta.
		CHECK(drawnUnitDelta(*u, 0.f) == 100);
		CHECK(unitMotionFraction(scene, 1000) == 0.f);
		CHECK(unitMotionFraction(scene, 1020) == doctest::Approx(0.5f));
		CHECK(unitMotionFraction(scene, 5000) == 1.f);
		CHECK(drawnUnitDelta(*u, 0.5f) == 100 + int(0.5f * float(u->stepSpeed)));
		// A full interval reaches what the next tick computes, without crossing the action.
		CHECK(drawnUnitDelta(*u, 1.f) == std::min(UNIT_DELTA_MAX, 100 + u->stepSpeed));
		SceneUnit ending = *u;
		ending.delta = UNIT_DELTA_MAX - 1;
		CHECK(drawnUnitDelta(ending, 1.f) == UNIT_DELTA_MAX);

		// Uncapped simulation: no interval, no motion between ticks.
		scene.tickInterval = 0;
		CHECK(unitMotionFraction(scene, 1020) == 0.f);
	}
    TEST_CASE("connected overlays use configured groups and scene retains its catalog")
    {
        glob2test::HeadlessGlobals globals;
        Scene scene;
        const SceneBuilding* extracted=nullptr;
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
        extractScene(world.game,request,scene);
        extracted=scene.entities.building(first->gid);
        REQUIRE(extracted);
        CHECK(extracted->connectionMask==1);
        CHECK(scene.map.getBuilding(4,4)==NOGBID);
        key=extracted->type->key;
        }
        CHECK(extracted->type->key==key);
        CHECK(extracted->type->presentation.connectionGroup=="joined-canopies");
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
            Scene scene;SceneRequest request;extractScene(world.game,request,scene);
            CHECK(scene.entities.building(teamBoundary->gid)->connectionMask==0);
            CHECK(scene.entities.building(groupBoundary->gid)->connectionMask==0);
            for(const auto& [gid,mask]:centers)
            {
                REQUIRE(scene.entities.building(gid));CHECK(scene.entities.building(gid)->connectionMask==mask);
                const auto* b=scene.entities.building(gid);
                CHECK((world.game.map.getBuilding(b->posX,b->posY)==NOGBID)==overlay);
            }
            const auto checksum=world.checksum();
            const auto render=[&](const Scene& frame,const std::string& suffix) {
                // Verify the actual selected frame exists in each loaded sprite atlas.
                for(const auto& b:frame.entities.buildings)
                {
                    const int index=buildingSpriteFrame(*b.type,b.hp,b.effectiveMaxHp,b.connectionMask);
                    REQUIRE(index>=0);REQUIRE(index<b.type->gameSpritePtr->getFrameCount());
                }
                Game::ViewState view;view.scene=&frame;
                auto* gfx=globals->gfx;
                gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);gfx->setClipRect();
                gfx->drawFilledRect(0,0,1024,1024,GAGCore::Color(20,30,20));
                gfx->beginMapTransform(1,1,1,0,0,1024,1024);
                world.game.drawMap(0,0,1024,1024,0,0,0,0,0,view,Game::DRAW_WHOLE_MAP|Game::DRAW_NO_CLOUD_LAYER,nullptr,nullptr,true);
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
            CHECK_MESSAGE(actual==render(scene,"repeat"),"identical frozen scenes must render identically before the mask control");
            // Counterfactual RENDER-ONLY control: same scene and entities, masks forced to zero.
            // Both the ground and overlay renderer must use these extracted masks.
            Scene disconnected=scene;
            for(auto& building:disconnected.entities.buildings)building.connectionMask=0;
            const auto control=render(disconnected,"zero-mask-control");
            CHECK_MESSAGE(actual!=control,lane<<" full-map renderer must use its nonzero connection masks");
            Scene wounded=scene;
            for(auto& b:wounded.entities.buildings)if(b.gid==damage->gid)b.hp=1;
            CHECK_MESSAGE(actual!=render(wounded,"wounded"),lane<<" full-map renderer must use the configured damage frame");
        }
    }

}
