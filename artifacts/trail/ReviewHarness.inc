// Local evidence driver; not part of shipped source or committed tests.
#include "MapEdit.h"
#include "GraphicContext.h"
#include "SettingsScreen.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <iomanip>
namespace trailReview {
void landscape(Game& game) {
    auto& map=game.map;
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) map.setCellTerrain(x,y,GRASS);
    for(int y=17;y<21;++y) for(int x=0;x<9;++x) map.setCellTerrain(x,y,SAND);
    for(int y=19;y<23;++y) for(int x=0;x<5;++x) map.setCellTerrain(x,y,WATER);
    for(int y=18;y<23;++y) for(int x=20;x<26;++x) map.setCellTerrain(x,y,ICE);
    for(int x=1;x<27;++x) map.setCellTerrain(x,10,TRAIL);
    for(int y=1;y<23;++y) map.setCellTerrain(12,y,TRAIL);
    for(int i=0;i<10;++i) map.setCellTerrain(3+i,2+i,TRAIL);
    for(int y=8;y<16;++y) for(int x=16;x<24;++x) map.setCellTerrain(x,y,TRAIL);
    for(int y=16;y<24;++y) map.setCellTerrain(6,y,TRAIL);
    for(int x=19;x<26;++x) map.setCellTerrain(x,20,TRAIL);
    map.setCellTerrain(2,14,TRAIL);
    map.setCellTerrain(4,21,TRAIL);
    for(int y=3;y<7;++y) for(int x=2;x<5;++x) map.setResource(x,y,WOOD,0);
    for(int y=3;y<6;++y) for(int x=18;x<23;++x) map.setResource(x,y,WHEAT,0);
    map.setMapDiscovered();
}
void screenshot(GAGCore::GraphicContext& gfx,const std::string& name) {
    REQUIRE(IMG_SavePNG(gfx.getSDLSurface(),(glob2test::artifactDir()/name).string().c_str()));
}
}
TEST_CASE("Trail review editor and moving colony [display][artifacts]" * doctest::test_suite("TrailReview")) {
    for(bool gpu:{false,true}) {
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768,
            .screenFlags=gpu?Uint32(GAGCore::GraphicContext::USEGPU):0u});
        globals->settings.experiments.set(ExperimentId::TrailTerrain,true);
        globals->settings.experiments.set(ExperimentId::IceTerrain,true);
        const std::string mode=gpu?"opengl":"software";
        {
            MapEdit editor;
            editor.game.map.setSize(5,5,GRASS);editor.game.map.setGame(&editor.game);
            editor.game.addTeam();editor.game.teams[0]->race.loadDefault();
            trailReview::landscape(editor.game);
            auto* building=editor.game.addBuilding(18,11,globals->buildingsTypes.getTypeNum("inn",0,false),0);
            REQUIRE(building);editor.game.map.setBuilding(18,11,building->type->width,building->type->height,building->gid);
            editor.viewportX=editor.viewportY=0;editor.updateCamera();editor.minimap.setGame(editor.game);
            editor.performAction("select road");
            CHECK(editor.terrainType==TerrainSelector::Trail);
            editor.draw(SDL_GetTicks());
            if(!gpu) trailReview::screenshot(*globals->gfx,"editor-"+mode+".png");
            else globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/editor-opengl.bmp");
            globals->gfx->nextFrame();
            REQUIRE(editor.save((glob2test::artifactDir()/"legacy-road.map").string(),"Trail review"));
        }
        if(!gpu) {
            auto* strings=GAGCore::Toolkit::getStringTable();
            for(const auto* lang:{"en","fr","de","ar","zh-cn"}) {
                strings->setLang(strings->getLangCode(lang));
                SettingsScreen settings;settings.beginExecution(globals->gfx);
                settings.selectCategory(SettingsScreen::Category::Experiments);
                settings.paintFrame(SDL_GetTicks());
                settings.host().state("settings/9").scroll=420;settings.invalidate();
                settings.paintFrame(SDL_GetTicks());
                trailReview::screenshot(*globals->gfx,std::string("settings-")+lang+".png");
                globals->gfx->nextFrame();
            }
            strings->setLang(strings->getLangCode("en"));
        }
        glob2test::GameOptions options{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=719};
        options.experiments.set(ExperimentId::TrailTerrain,true);
        options.experiments.set(ExperimentId::IceTerrain,true);
        glob2test::HeadlessGame world(options);
        trailReview::landscape(world.game);
        auto* inn=world.addBuilding("inn",18,11);inn->resources[WHEAT]=100;
        auto* swarm=world.addBuilding("swarm",7,12);swarm->resources[WHEAT]=100;
        for(int x=2;x<10;++x) {world.addUnit(WORKER,x,10);world.addUnit(WARRIOR,x,9);}
        world.addUnit(EXPLORER,3,8);
        world.gui.localTeamNo=world.gui.localPlayer=0;world.gui.adjustLocalTeam();world.gui.init();
        std::ofstream trace(glob2test::artifactDir()/("checksums-"+mode+".txt"));
        for(int tick=0;tick<=512;++tick) {
            trace<<tick<<' '<<std::hex<<world.checksum()<<std::dec<<'\n';
            if(tick==0||tick==64||tick==512) {
                world.gui.drawAll(0);
                world.game.drawMap(0,0,880,720,0,0,0,0,0,world.gui.view,Game::DRAW_WHOLE_MAP);
                if(!gpu) trailReview::screenshot(*globals->gfx,"game-"+mode+"-"+std::to_string(tick)+".png");
                else globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/game-opengl-"+std::to_string(tick)+".bmp");
                globals->gfx->nextFrame();
            }
            if(tick<512) world.step();
        }
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(backend);world.gui.save(&out,"Trail review continuation");out.flush();
        std::ofstream save(glob2test::artifactDir()/"legacy-road.game",std::ios::binary);
        save.write(backend->getBuffer(),backend->getPosition());
    }
}
