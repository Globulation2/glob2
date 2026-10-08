// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "editor/BrushSwatches.h"
#include "MapAssetBundle.h"
#include "BuildingArtwork.h"
#include <TextStream.h>
#include "ResourceRegistry.h"
#include "online/Sha256.h"
#include "render/scene/SceneMap.h"
#include "sim/snapshot/WorldSnapshot.h"
#include "render/terrain/TerrainCatalogIO.h"
#include "render/terrain/TerrainCompositor.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <SDL3_image/SDL_image.h>
#include <fstream>
using Json = nlohmann::json;
namespace {
const std::string setId = "11111111-1111-4111-8111-111111111111";
const std::string versionId = "22222222-2222-4222-8222-222222222222";
const std::string prefix = "s1111111111114111811111111111111122222222222242228222222222222222:";
Json package() {
    auto* surface = SDL_CreateSurface(64, 32, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface);
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 80, 120, 160, 255));
    auto* io = SDL_IOFromDynamicMem(); REQUIRE(io);
    REQUIRE(IMG_SavePNG_IO(surface, io, false)); SDL_DestroySurface(surface);
    const std::string png(static_cast<const char*>(SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr)), SDL_GetIOSize(io));
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded; unsigned bits=0, value=0;
    for(unsigned char c:png) { value=(value<<8)|c;bits+=8;while(bits>=6){bits-=6;encoded+=alphabet[(value>>bits)&63];} }
    if(bits)encoded+=alphabet[(value<<(6-bits))&63]; SDL_CloseIO(io);
    const auto hash=Online::Sha256::hex(png);
    Json result={{"schemaVersion",1},{"setId",setId},{"versionId",versionId},{"title","Glacial theme"},{"description",""},{"tags",Json::array({"ice"})},
        {"license","CC-BY-4.0"},{"credits",Json::array({Json{{"author","Fixture artist"},{"license","CC-BY-4.0"}}})},
        {"terrains",Json::array({Json{{"key",prefix+"ice"},{"name","Blue ice"},{"base","grass"},{"appearance","grass"},{"properties",{{"groundSpeedQ8",192}}}}})},
        {"resources",Json::array()}};
    result["assets"]={{"schemaVersion",1},{"sheets",Json::array({Json{{"hash",hash},{"png",encoded},{"frameWidth",32},{"frameHeight",32}}})},{"terrains",Json::object()},{"credits",Json::array()}};
    result["assets"]["terrains"][prefix+"ice"]={{"sprite","data/sets/"+hash},{"profile","soft"},{"preview",Json::array({80,120,160})},{"variants",Json::array({Json{{"frame",0},{"weight",1}}})}};
    return result;
}
// Keep the two independent portable artwork transports in the same saved game.
std::string installBuildingArtwork(Game& game) {
    const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
    const std::string image(reinterpret_cast<const char*>(webp), sizeof(webp));
    const auto hash = Online::Sha256::hex(image);
    const Json sprites = Json::array({Json{{"key", "combined"}, {"frames", Json::array({Json{{"imageHash", hash}, {"width", 2}, {"height", 2}}})}}});
	const Json buildingPackage = {
		{"schemaVersion", 1},
		{"namespace", setId},
		{"experiments", Json::array()},
		{"sprites", sprites},
		{"variants",
		 Json::array({Json{{"key", "b-" + setId + "-combined"},
						   {"properties",
							{{"width", 2},
							 {"height", 2},
							 {"hpInit", 200},
							 {"hpMax", 200},
							 {"gameSprite", "package:combined"},
							 {"miniSprite", "data/gfx/miniinn0b"}}},
						   {"semantics", {{"placeable", true}, {"instantPlacement", true}}}}})}};
	game.buildingsTypes.composePackages({buildingPackage.dump()});
	game.gameHeader.setBuildingCatalogSnapshot(game.buildingsTypes.snapshotJson());
	std::string bytes = "G2BA0001";
	const auto write = [&](std::uint32_t n)
	{
		for (unsigned i = 0; i < 4; ++i)
			bytes += char((n >> (i * 8)) & 255);
	};
	const auto manifest = sprites.dump();
	write(manifest.size());
	bytes += manifest;
	write(1);
	bytes += hash;
	write(image.size());
	bytes += image;
	game.gameHeader.setBuildingArtwork(bytes);
	game.configureBuildingCatalog();
	return bytes;
}

}
TEST_SUITE("MapSets") {
TEST_CASE("custom artwork properties and credits survive offline save and load [artifacts]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
	auto &map = world.game.map;
	CHECK(map.frozenAssetBundle()->isEmpty());
	auto installed = Json::parse(ResourceRegistry::builtins()->serialize()).at("resources")[0];
	installed["key"] = "legacy:installed";
	installed["presentation"]["sprite"] = "data/sets/installed-legacy-art";
	map.installResourceDefinitions(
		Json{{"schemaVersion", 1}, {"resources", Json::array({installed})}}.dump());
	const auto source = package();
	map.game = nullptr;
	map.importSet(source.dump());
	map.setGame(&world.game);
	const auto buildingArtwork = installBuildingArtwork(world.game);
	const auto terrain = *map.terrainRegistry().find(prefix + "ice");
	map.paintCell(7, 8, terrain);
	CHECK(map.terrainRegistry().properties(terrain).groundSpeedQ8 == 192);
	CHECK(map.frozenAssetBundle()->sheets.size() == 1);
	CHECK(map.frozenAssetBundle()->credits[0]["authors"][0]["author"] == "Fixture artist");
	CHECK(map.frozenAssetBundle()->serialize().find("data/gfx/") == std::string::npos);
	auto *backend = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream out(backend);
	world.game.save(&out, true, "set fixture");
	out.flush();
	const auto bytes = backend->takeContents();
	std::ofstream saved(glob2test::artifactDir() / "custom-set.map", std::ios::binary);
	saved.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	saved.close();
	GameGUI restored(false);
	GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(std::string(bytes)));
	REQUIRE(restored.game.load(&input));
	REQUIRE(restored.game.gameHeader.getBuildingArtwork());
	CHECK(restored.game.gameHeader.getBuildingArtwork()->bytes() == buildingArtwork);
	CHECK(restored.game.buildingsTypes.fingerprint() ==
		  world.game.buildingsTypes.fingerprint());
	CHECK(restored.game.map.frozenAssetBundle()->serialize() ==
		  map.frozenAssetBundle()->serialize());
	CHECK(restored.game.map.terrainTypeAt(7, 8) == terrain);
	CHECK(restored.game.map.terrainRegistry().digest() == map.terrainRegistry().digest());
	CHECK(restored.game.map.resourceRegistry()
			  .presentation(*restored.game.map.resourceRegistry().find("legacy:installed"))
			  .sprite == "data/sets/installed-legacy-art");
	auto *saveBackend = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream savedGame(saveBackend);
	world.game.save(&savedGame, false, "set game fixture");
	savedGame.flush();
	const auto savedBytes = saveBackend->takeContents();
	std::ofstream gameFile(glob2test::artifactDir() / "custom-set.game", std::ios::binary);
	gameFile.write(reinterpret_cast<const char *>(savedBytes.data()), savedBytes.size());
	gameFile.close();
	GameGUI restoredGame(false);
	GAGCore::BinaryInputStream gameInput(
		new GAGCore::MemoryStreamBackend(std::string(savedBytes)));
	REQUIRE(restoredGame.game.load(&gameInput));
	REQUIRE(restoredGame.game.gameHeader.getBuildingArtwork());
	CHECK(restoredGame.game.gameHeader.getBuildingArtwork()->bytes() == buildingArtwork);
	CHECK(restoredGame.game.map.frozenAssetBundle()->serialize() ==
		  map.frozenAssetBundle()->serialize());
	CHECK(restoredGame.game.map.terrainRegistry().digest() == map.terrainRegistry().digest());
	CHECK(restoredGame.game.checkSum() == world.game.checkSum());
	auto *textBackend = new GAGCore::MemoryStreamBackend;
	GAGCore::TextOutputStream textOut(textBackend);
	world.game.save(&textOut, false, "combined text game");
	textOut.flush();
	textBackend->seekFromStart(0);
	GAGCore::TextInputStream textInput(new GAGCore::MemoryStreamBackend(*textBackend));
	Game textRestored(nullptr);
	REQUIRE(textRestored.load(&textInput));
	REQUIRE(textRestored.gameHeader.getBuildingArtwork());
	CHECK(textRestored.gameHeader.getBuildingArtwork()->bytes() == buildingArtwork);
	CHECK(textRestored.map.frozenAssetBundle()->serialize() ==
		  map.frozenAssetBundle()->serialize());
	CHECK(textRestored.checkSum() == world.game.checkSum());
	const auto snapshot =
		SimulationSnapshot::capture(world.game, SimulationSnapshot::captureCatalog(world.game));
	CHECK(snapshot.catalogs->assets == map.frozenAssetBundle());
	SceneMap scene;
	scene.bindSnapshot(snapshot, map.displayViewportW, map.displayViewportH, map.displayedTeam);
	auto frozen = scene.frozenAssetBundle();
	map.tile(2, 1);
	CHECK(map.frozenAssetBundle() == frozen);
	CHECK(map.terrainTypeAt(39, 8) == terrain);
	map.clear();
	CHECK(frozen->sheets.size() == 1);
}
TEST_CASE("invalid packages fail atomically and reimports preserve local edits") {
    glob2test::HeadlessGlobals globals; Map map;map.setSize(5,5,GRASS);
    const auto source=package();
    for(unsigned failure=0;failure<7;++failure) {
        auto invalid=source;
        if(failure==0)invalid["assets"]["sheets"][0]["hash"]=std::string(64,'0');
        if(failure==1)invalid["assets"]["terrains"][prefix+"ice"]["variants"][0]["frame"]=2;
        if(failure==2)invalid["assets"]["terrains"][prefix+"ice"]["unknown"]=true;
        if(failure==3)invalid["terrains"][0]["key"]="glob2:grass";
        if(failure==4)invalid["license"]="CC0-1.0";
        if(failure==5)invalid["assets"]["terrains"][prefix+"ice"]["decor"]={{"sprite",invalid["assets"]["terrains"][prefix+"ice"]["sprite"]},{"full",Json::array({2})},{"edge",Json::array({0})}};
        if(failure==6)invalid["resources"].push_back(Json{{"key",prefix+"ice"}});
        CHECK_THROWS(map.importSet(invalid.dump())); CHECK(map.terrainRegistry().size()==TERRAIN_COUNT);CHECK(map.frozenAssetBundle()->isEmpty());
    }
    CHECK_THROWS(MapAssetBundle::parseDocument("{\"a\":1,\"a\":2}"));
    map.importSet(source.dump());
    map.importTerrainDefinitions(Json{{"schemaVersion",1},{"terrains",Json::array({Json{{"key",prefix+"ice"},{"name","Local ice"},{"base","grass"},{"appearance","grass"},{"properties",{{"groundSpeedQ8",512}}}}})}}.dump());
    CHECK_THROWS(map.importSet(source.dump()));
    CHECK(map.terrainRegistry().properties(*map.terrainRegistry().find(prefix+"ice")).groundSpeedQ8==512);
}
TEST_CASE("selected terrain includes custom resources and explicit updates replace local copies") {
    glob2test::HeadlessGlobals globals; Map map;map.setSize(5,5,GRASS);
    auto source=package();
    auto resource=Json::parse(ResourceRegistry::builtins()->serialize()).at("resources")[0];
    resource["key"]=prefix+"tree";resource["requiredExperiment"]="";
    resource["properties"]={{"ecology","land"},{"growthRate",0},{"spreadRate",0},{"blocksGround",false},{"persistsWhenEmpty",false}};
    resource["yields"]={{"food",{{"capacity",3},{"initial",1},{"consumption","one"}}},{"paper",{{"capacity",3},{"initial",0},{"consumption","one"}}}};
    resource["presentation"]["sprite"]=source["assets"]["terrains"][prefix+"ice"]["sprite"];
    resource["presentation"]["levels"]=Json::array({Json{{"stock",0},{"variants",Json::array({Json{{"frame",1},{"weight",1}}})}}});
    source["resources"].push_back(resource);
    source["terrains"][0]["allowedResourceKeys"]=Json::array({prefix+"tree"});
    auto extra=source["terrains"][0];extra["key"]=prefix+"unused";source["terrains"].push_back(extra);
    map.importSet(source.dump(),{prefix+"ice"});
    CHECK_FALSE(map.terrainRegistry().find(prefix+"unused"));
    auto conflicting=source;conflicting["credits"][0]["author"]="Different author";
    CHECK_THROWS(map.importSet(conflicting.dump(),{prefix+"unused"}));
    CHECK_FALSE(map.terrainRegistry().find(prefix+"unused"));
    map.importSet(source.dump(),{prefix+"unused"});
    CHECK(map.frozenAssetBundle()->credits.size()==1);
    CHECK(map.frozenAssetBundle()->credits[0]["entries"].size()==3);
    const auto oldTerrain=*map.terrainRegistry().find(prefix+"ice");
    const auto oldResource=*map.resourceRegistry().find(prefix+"tree");
    map.paintCell(5,5,oldTerrain);map.setResourceByIndex(5,5,resourceIndex(oldResource),1);
    map.setMaterialAmount(map.coordToIndex(5,5),MaterialId::Paper,2);
    map.setMaterialAmount(map.coordToIndex(5,5),MaterialId::Food,0);
    const std::string nextVersion="33333333-3333-4333-8333-333333333333";
    const std::string nextPrefix="s1111111111114111811111111111111133333333333343338333333333333333:";
    auto encoded=source.dump();size_t pos=0;
    while((pos=encoded.find(prefix,pos))!=std::string::npos){encoded.replace(pos,prefix.size(),nextPrefix);pos+=nextPrefix.size();}
    auto next=Json::parse(encoded);next["versionId"]=nextVersion;
    next["terrains"][0]["properties"]["groundSpeedQ8"]=512;
    next["resources"][0]["yields"]["paper"]["capacity"]=1;
    map.updateSet(next.dump(),versionId,{nextPrefix+"ice"});
    CHECK(map.terrainTypeAt(5,5)==*map.terrainRegistry().find(nextPrefix+"ice"));
    CHECK(map.getResource(5,5).type==resourceIndex(*map.resourceRegistry().find(nextPrefix+"tree")));
    CHECK(map.terrainRegistry().properties(map.terrainTypeAt(5,5)).groundSpeedQ8==512);
    CHECK(map.frozenAssetBundle()->credits.size()==2);
    CHECK(map.materialAmountAt(map.coordToIndex(5,5),MaterialId::Food)==0);
    CHECK(map.materialAmountAt(map.coordToIndex(5,5),MaterialId::Paper)==1);
    map.paintCell(5,5,oldTerrain);map.setResourceByIndex(5,5,resourceIndex(oldResource),1);
    map.updateSet(next.dump(),versionId,{nextPrefix+"ice"});
    CHECK(map.terrainTypeAt(5,5)==*map.terrainRegistry().find(nextPrefix+"ice"));
    CHECK(map.frozenAssetBundle()->credits.size()==2);
}
TEST_CASE("custom sprites render from memory without installation [display] [artifacts]") {
    glob2test::HeadlessGlobals globals({.display=true});
    auto source=package();
    auto resource=Json::parse(ResourceRegistry::builtins()->serialize()).at("resources")[0];
    resource["key"]=prefix+"crystal";resource["requiredExperiment"]="";
    resource["presentation"]["sprite"]=source["assets"]["terrains"][prefix+"ice"]["sprite"];
    resource["presentation"]["levels"]=Json::array({Json{{"stock",0},{"variants",Json::array({Json{{"frame",1},{"weight",1}}})}}});
    source["resources"].push_back(resource);
    std::ofstream(glob2test::artifactDir()/"set.json") << source.dump();
    glob2test::HeadlessGame world;auto& map=world.game.map;map.game=nullptr;map.importSet(source.dump());map.setGame(&world.game);map.paintCell(7,8,*map.terrainRegistry().find(prefix+"ice"));
    SceneMap scene;glob2test::observeMap(map,scene);
    TerrainVisual::Compositor compositor(TerrainVisual::loadCatalog(map.frozenAssetBundle()),map.frozenAssetBundle());
    compositor.prepare(false,0);GAGCore::DrawableSurface pixels(32,32);
    compositor.compose(compositor.describe(scene,7,8),pixels.getSDLSurface(),0,0,1);
    Uint8 r,g,b,a;
    REQUIRE(SDL_ReadSurfacePixel(pixels.getSDLSurface(),16,16,&r,&g,&b,&a));
    CHECK(r==80);CHECK(g==120);CHECK(b==160);CHECK(a==255);
    BrushSwatches swatches;swatches.bind(map.frozenTerrainRegistry(),map.frozenResourceRegistry(),map.frozenAssetBundle());
    auto* resourceImage=swatches.resource(*map.resourceRegistry().find(prefix+"crystal"),GRASS,64);
    REQUIRE(resourceImage);
    REQUIRE(SDL_ReadSurfacePixel(resourceImage->getSDLSurface(),32,32,&r,&g,&b,&a));
    CHECK(r==80);CHECK(g==120);CHECK(b==160);CHECK(a==255);
    CHECK(IMG_SavePNG(resourceImage->getSDLSurface(),(glob2test::artifactDir()/"map-set-resource.png").string().c_str()));
    CHECK(IMG_SavePNG(pixels.getSDLSurface(),(glob2test::artifactDir()/"map-set.png").string().c_str()));
}
TEST_CASE("swatches remain textured when alternating inspected map bundles [display]") {
    glob2test::HeadlessGlobals globals({.display=true});
    auto source=package();
    // Different preview and pixel colors make missing preparation observable.
    source["assets"]["terrains"][prefix+"ice"]["preview"]=Json::array({1,2,3});
    Map first, second;first.setSize(5,5,GRASS);second.setSize(5,5,GRASS);
    first.importSet(source.dump());
    source["title"]="Another inspected bundle";second.importSet(source.dump());
    BrushSwatches swatches;swatches.bind(first.frozenTerrainRegistry(),first.frozenResourceRegistry(),first.frozenAssetBundle());
    const auto terrain=*first.terrainRegistry().find(prefix+"ice");
    REQUIRE(swatches.terrain(terrain,64));
    globalContainer->terrainCompositor(second.frozenAssetBundle()).prepare(false,0);
    // A different size forces composition after the single global slot changed.
    auto* image=swatches.terrain(terrain,96);REQUIRE(image);
    Uint8 r,g,b,a;
    REQUIRE(SDL_ReadSurfacePixel(image->getSDLSurface(),48,48,&r,&g,&b,&a));
    CHECK(r==80);CHECK(g==120);CHECK(b==160);CHECK(a==255);
}

}
