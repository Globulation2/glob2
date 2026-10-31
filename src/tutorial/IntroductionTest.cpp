// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TutorialModel.h"
#include "scripting/javascript/ScriptValue.h"
#include "scripting/javascript/ScriptOrders.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Order.h"
#include "Player.h"
#include "WinningConditions.h"
#include "ReplayWriter.h"
#include "Brush.h"
#include "FertilityCalculator.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <zlib.h>

namespace
{
std::string source(const std::string &name="introduction")
{
    std::ifstream catalogFile(glob2test::sourceRoot()/"scripts/tutorials/catalog.json");
    const auto catalog=Tutorial::contentJson({std::istreambuf_iterator<char>(catalogFile),{}});
    std::string path;
    for(const auto &entry:catalog.items)
        if(std::filesystem::path(entry.string("definition")).stem()==name) path=entry.string("script");
    REQUIRE_FALSE(path.empty());
    std::ifstream file(glob2test::sourceRoot()/path);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file),{}};
}
void setup(glob2test::HeadlessGame &w,const std::string &name="introduction")
{
    const auto path=glob2test::sourceRoot()/("campaigns/"+name+".map.gz");
    gzFile file=gzopen(path.string().c_str(),"rb"); REQUIRE(file);
    std::string bytes;char buffer[4096];int count;
    while((count=gzread(file,buffer,sizeof(buffer)))>0) bytes.append(buffer,count);
    REQUIRE(gzclose(file)==Z_OK);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);REQUIRE(w.game.load(&input));w.game.setWaitingOnMask(0);
}

Building *find(Game &g,int family,bool finished=true)
{
    for (int i=0;i<Building::MAX_COUNT;++i) { auto *b=g.teams[0]->myBuildings[i];
        if (b && b->shortTypeNum==family && (!finished || !b->type->isBuildingSite)) return b; }
    return nullptr;
}
void order(Game &g,std::shared_ptr<Order> o)
{
    std::ofstream log(glob2test::artifactDir()/"introduction.orders",std::ios::app);
    log << g.stepCounter << " " << int(o->getOrderType()) << " ";
    const auto *bytes=o->getData();
    for (int i=0;i<o->getDataLength();++i) log << std::hex << std::setw(2) << std::setfill('0') << int(bytes[i]);
    log << "\n";
    o->sender=0; g.executeOrder(o,0);
}
void create(Game &g,const char *name,int x,int y,int workers,bool site=false)
{
    const int type=g.buildingsTypes.getTypeNum(name,0,site);
    REQUIRE(type>=0);
    REQUIRE(g.checkRoomForBuilding(x,y,g.buildingsTypes.get(type),0,false));
    order(g,std::make_shared<OrderCreate>(0,x,y,type,workers,site ? 0 : workers, site ? std::nullopt : std::optional<Sint32>(4)));
}
void continuation(glob2test::HeadlessGame &w)
{
    auto *memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    w.game.save(&output,false,w.game.mapHeader.getMapName()); output.flush();
    const std::string bytes(memory->getBuffer(),memory->getPosition());
    std::vector<Uint32> trace;
    std::ofstream log(glob2test::artifactDir()/"introduction.trace",std::ios::app);
    for (int i=0;i<96;++i) { w.step(); trace.push_back(w.game.checkSum()); log << w.game.stepCounter << " " << trace.back() << "\n"; }
    GameGUI restored(false);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    REQUIRE(restored.game.load(&input));
    restored.game.setWaitingOnMask(0);
    for (int i=0;i<96;++i) {
        restored.game.step(0);
        REQUIRE(restored.game.checkSum()==trace[i]);
    }
}
void until(glob2test::HeadlessGame &w,const std::function<bool()> &done,int limit=24000)
{
    std::ofstream log(glob2test::artifactDir()/"introduction.trace",std::ios::app);
    for (int i=0;i<limit && !done();++i) { w.step(); log << w.game.stepCounter << " " << w.game.checkSum() << "\n"; }
    std::ostringstream diagnosis;
    if (!done()) {
        for(int team=0;team<2;++team) {
            diagnosis << " team=" << team << " enemies=" << w.game.teams[team]->enemies;
            for(const auto *u:w.game.teams[team]->liveUnits.entries()) if(u->typeNum==WARRIOR)
                diagnosis << " warrior=" << u->posX << ',' << u->posY << " hp=" << u->hp << " hungry=" << u->hungry << " activity=" << int(u->activity) << " displacement=" << int(u->displacement) << " attached=" << (u->attachedBuilding ? int(u->attachedBuilding->gid) : -1);
            for(const auto *b:w.game.teams[team]->liveBuildings.entries()) if(b->type->isVirtual)
                diagnosis << " flag=" << b->posX << ',' << b->posY << " working=" << b->unitsWorking.size() << '/' << b->maxUnitWorking << " range=" << b->unitStayRange;
        }
        diagnosis << "tick=" << w.game.stepCounter << " objectives=";
        for (int i=0;i<5;++i) diagnosis << w.game.objectives.isObjectiveComplete(i);
        diagnosis << " units=" << w.game.teams[0]->liveUnits.entries().size();
        for (auto *b : w.game.teams[0]->liveBuildings.entries())
            diagnosis << " building=" << b->shortTypeNum << " target=" << b->maxUnitWorking
                << " assigned=" << b->unitsWorking.size() << " wheat=" << b->material(WHEAT)
                << " ratios=" << b->productionRatio(0) << "," << b->productionRatio(1) << "," << b->productionRatio(2);
    }
    INFO(diagnosis.str());
    REQUIRE(done());
}
}

TEST_CASE("packaged lesson maps embed their source and share geography" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    std::ifstream catalogFile(glob2test::sourceRoot()/"scripts/tutorials/catalog.json");
    const auto catalog=Tutorial::contentJson({std::istreambuf_iterator<char>(catalogFile),{}});
    std::vector<Uint16> reference;
    for(const auto &entry:catalog.items) {
        const auto name=std::filesystem::path(entry.string("definition")).stem().string();
        glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
        setup(w,name);
        CHECK(w.game.map.areaNames.size()==9);
        CHECK(w.game.gameHeader.getBasePlayer(0).type==BasePlayer::P_LOCAL);
        CHECK(w.game.gameHeader.getBasePlayer(1).type==BasePlayer::P_AI);
        CHECK(w.game.mapscript.getMapScript()==source(name));
        CHECK(w.game.map.getW()==256);CHECK(w.game.map.getH()==256);
        std::vector<Uint16> geography;
        for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
            const auto tile=w.game.map.getTile(x,y);
            geography.push_back(int(w.game.map.vertexTerrainAt(x,y)));
            geography.push_back(tile.resource.type);geography.push_back(tile.resource.amount);
            geography.push_back(tile.scriptAreas);
        }
        if(reference.empty()) reference=geography; else CHECK(geography==reference);
    }
}

TEST_CASE("training algae requires swimming and named regions survive loading" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
    setup(w,"swimming");
    int algae=0;
    for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
        if(w.game.map.getTile(x,y).resource.type!=ALGA) continue;
        ++algae;CHECK(w.game.map.terrainPropertiesAt(x,y).swimmable);
        CHECK_FALSE(w.game.map.terrainPropertiesAt(x,y).walkable);
    }
    REQUIRE(algae>0);
    Script::JavaScriptMap probe;
    probe.step("function step(ctx){const a=ctx.game.map.area('defense');if(!a||a.x!==93||a.y!==94||a.width!==8||a.height!==8||ctx.game.map.area('absent')!==null)throw 'area bounds';}",w.game,w.gui);
    w.step(32);
    // Matched negative: workers can deliver land resources, but without a Pool
    // none can obtain the algae needed to finish this same School.
    create(w.game,"school",89,97,3,true);
    w.step(9000);
    auto *school=find(w.game,6,false);REQUIRE(school);
    CHECK(school->type->isBuildingSite);CHECK(school->material(ALGA)==0);
    for(const auto *u:w.game.teams[0]->liveUnits.entries()) if(u->typeNum==WORKER) CHECK(u->level[SWIM]==0);
    CHECK_FALSE(w.game.objectives.isObjectiveComplete(2));continuation(w);
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"water-access.trace");
}

TEST_CASE("training colonies remain supplied while a learner takes time" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
    setup(w);
    CHECK(w.game.gameHeader.getWinningConditions().size()==1);
    w.step(18000);
    CHECK(w.game.teams[0]->liveUnits.size()>=13);
    CHECK(w.game.teams[1]->liveUnits.size()>=8);
    CHECK_FALSE(w.game.teams[0]->hasWon);
    CHECK_FALSE(w.game.objectives.isObjectiveComplete(0));
    continuation(w);
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"sustainability.trace");
}

TEST_CASE("real orders complete scouting construction supply growth and attack" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::writeFile(glob2test::artifactDir()/"introduction.trace","");
    glob2test::writeFile(glob2test::artifactDir()/"introduction.orders","");
    for (const auto &[siteX,siteY] : {std::pair{83,93},std::pair{94,100}}) {
        CAPTURE(siteX); CAPTURE(siteY);
        {
            std::ofstream trace(glob2test::artifactDir()/"introduction.trace",std::ios::app);
            trace << "# Inn placement " << siteX << "," << siteY << "\n";
        }
        glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
        setup(w); until(w,[&]{return w.game.stepCounter==32;},32);
        continuation(w);
        CHECK_FALSE(w.game.objectives.isObjectiveComplete(0));
        create(w.game,"explorationflag",101,94,1);
        auto *cancelledFlag=find(w.game,IntBuildingType::EXPLORATION_FLAG); REQUIRE(cancelledFlag);
        order(w.game,std::make_shared<OrderDelete>(cancelledFlag->gid));
        until(w,[&]{return !find(w.game,IntBuildingType::EXPLORATION_FLAG);},256);
        CHECK_FALSE(w.game.objectives.isObjectiveComplete(0));
        create(w.game,"explorationflag",101,94,1);
        until(w,[&]{return w.game.objectives.isObjectiveComplete(0);});
        continuation(w);
        const auto newInn=[&]() -> Building* {
            for (auto *b : w.game.teams[0]->liveBuildings.entries())
                if (b->shortTypeNum==IntBuildingType::FOOD_BUILDING && b->posX==siteX && b->posY==siteY) return b;
            return nullptr;
        };
        create(w.game,"inn",siteX,siteY,2,true);
        auto *cancelledSite=newInn(); REQUIRE(cancelledSite);
        order(w.game,std::make_shared<OrderDelete>(cancelledSite->gid));
        until(w,[&]{return !newInn();},256);
        CHECK_FALSE(w.game.objectives.isObjectiveComplete(1));
        create(w.game,"inn",siteX,siteY,2,true);
        until(w,[&]{return w.game.objectives.isObjectiveComplete(1);});
        continuation(w);
        CHECK_FALSE(w.game.objectives.isObjectiveComplete(2));
        REQUIRE(newInn());
        order(w.game,std::make_shared<OrderModifyBuilding>(newInn()->gid,1));
        until(w,[&]{return w.game.objectives.isObjectiveComplete(2);});
        continuation(w);
        auto *swarm=find(w.game,IntBuildingType::SWARM_BUILDING); REQUIRE(swarm);
        order(w.game,std::make_shared<OrderModifyBuilding>(swarm->gid,2));
        until(w,[&]{return w.game.objectives.isObjectiveComplete(3);});
        continuation(w);
        create(w.game,"warflag",113,97,4);
        until(w,[&]{return w.game.objectives.isObjectiveComplete(4);},48000);
        continuation(w);
    }
}

TEST_CASE("idle time and acknowledgments cannot complete the introduction" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    setup(w); w.gui.setIsSpaceSet(true); w.step(1504);
    for (int i=0;i<5;++i) CHECK_FALSE(w.game.objectives.isObjectiveComplete(i));
    CHECK(w.game.gameHints.isHintVisible(5));
}

TEST_CASE("recommendation migrates existing profiles and persists an explicit choice" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    const auto file=(glob2test::profileDir()/"intro-preferences.txt").string();
    Settings settings;
    settings.load(file);
    CHECK_FALSE(settings.introductionOffered);
    { std::ofstream out(file); out << "version=1\n"; }
    settings.load(file);
    CHECK(settings.introductionOffered);
    settings.introductionOffered=false;
    REQUIRE(settings.save(file));
    Settings restored; restored.load(file);
    CHECK_FALSE(restored.introductionOffered);
    restored.introductionOffered=true;
    REQUIRE(restored.save(file));
    settings.load(file);
    CHECK(settings.introductionOffered);
}

TEST_CASE("only published scenario presentation activates tutorial guidance" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    setup(w);
    auto frame=w.game.observeCompletedWorld({.presentation=true});
    REQUIRE(frame.session->objectives);
    REQUIRE(frame.session->objectives->getNumberOfObjectives()==5);
    // The shipped map has the same briefing/objective captions as the retired
    // prototype. They must not manufacture its old 64x64 guidance or targets
    // while the new scenario is waiting for its first callback.
    CHECK_FALSE(frame.session->tutorial);
    CHECK_FALSE(Tutorial::view(*frame.session));
    w.step(32);
    frame=w.game.observeCompletedWorld({.presentation=true});
    REQUIRE(frame.session->tutorial);
    CHECK(Tutorial::view(*frame.session)==frame.session->tutorial);
    CHECK(frame.session->tutorial->lessonId=="introduction");
    REQUIRE(frame.session->tutorial->destination);
    CHECK(frame.session->tutorial->destination->x==101);
    CHECK(frame.session->tutorial->destination->y==95);

    auto captionsOnly=*frame.session;
    captionsOnly.tutorial.reset();
    CHECK_FALSE(Tutorial::view(captionsOnly));
}

TEST_CASE("second lesson completes and resumes with the shared runner" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    glob2test::writeFile(glob2test::artifactDir()/"introduction.trace","");
    setup(w,"food-supply");w.step(32);
    auto model=w.game.mapscript.tutorialView();REQUIRE(model);
    CHECK(model->lessonId=="food-supply");CHECK(model->count==2);CHECK(model->stepId=="build");
    CHECK_FALSE(model->destination);
    continuation(w);
    create(w.game,"inn",83,93,2,true);
    until(w,[&]{return w.game.objectives.isObjectiveComplete(0);});
    model=w.game.mapscript.tutorialView();REQUIRE(model);CHECK(model->stepId=="supply");
    continuation(w);
    Building *inn=nullptr;
    for(auto *b:w.game.teams[0]->liveBuildings.entries()) if(b->posX==83 && b->posY==93) inn=b;
    REQUIRE(inn);order(w.game,std::make_shared<OrderModifyBuilding>(inn->gid,1));
    until(w,[&]{return w.game.objectives.isObjectiveComplete(1);});
    model=w.game.mapscript.tutorialView();REQUIRE(model);CHECK(model->complete());
    continuation(w);
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"food-supply.trace");
}

TEST_CASE("prototype saves retain embedded source objectives and hints without special guidance" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    glob2test::writeFile(glob2test::artifactDir()/"introduction.trace","");
    const auto path=glob2test::fixture("tutorial/introduction-prototype.map.gz");
    gzFile file=gzopen(path.string().c_str(),"rb");REQUIRE(file);
    std::string bytes;char buffer[4096];int count;
    while((count=gzread(file,buffer,sizeof(buffer)))>0) bytes.append(buffer,count);
    REQUIRE(gzclose(file)==Z_OK);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);REQUIRE(w.game.load(&input));w.game.setWaitingOnMask(0);
    const auto embeddedSource=w.game.mapscript.getMapScript();
    w.step(32);CHECK_FALSE(w.game.mapscript.tutorialView());
    auto frame=w.game.observeCompletedWorld({.presentation=true});
    CHECK_FALSE(Tutorial::view(*frame.session));
    REQUIRE(frame.session->objectives);REQUIRE(frame.session->hints);
    CHECK_FALSE(frame.session->objectives->isObjectiveComplete(0));
    create(w.game,"explorationflag",34,32,1);
    until(w,[&]{return w.game.objectives.isObjectiveComplete(0);});
    REQUIRE(w.game.gameHints.getNumberOfHints()>0);
    w.game.gameHints.setHintVisible(0);
    auto *checkpoint=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream saved(checkpoint);
    w.game.save(&saved,false,"Prototype save compatibility");saved.flush();
    const std::string state(checkpoint->getBuffer(),checkpoint->getPosition());
    GameGUI restored(false);
    GAGCore::BinaryInputStream reload(new GAGCore::MemoryStreamBackend(state.data(),state.size()));
    reload.seekFromStart(0);REQUIRE(restored.game.load(&reload));
    CHECK(restored.game.mapscript.getMapScript()==embeddedSource);
    CHECK(restored.game.objectives.isObjectiveComplete(0));
    CHECK_FALSE(restored.game.objectives.isObjectiveComplete(1));
    CHECK(restored.game.gameHints.isHintVisible(0));
    CHECK(restored.game.gameHints.getGameHintText(0)==w.game.gameHints.getGameHintText(0));
    CHECK_FALSE(Tutorial::view(*restored.game.observeCompletedWorld({.presentation=true}).session));
    continuation(w);
    frame=w.game.observeCompletedWorld({.presentation=true});
    CHECK_FALSE(Tutorial::view(*frame.session));
    CHECK(frame.session->objectives->isObjectiveComplete(0));
    CHECK_FALSE(frame.session->objectives->isObjectiveComplete(1));
    CHECK_FALSE(w.game.mapscript.tutorialView());
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"prototype.trace");
}

TEST_CASE("tutorial metadata is validated atomically with scenario effects" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    setup(w);w.step(32);
    const auto model=w.game.mapscript.tutorialView();REQUIRE(model);
    CHECK(model->lessonId=="introduction");CHECK(model->stepId=="scout");
    REQUIRE(model->destination);CHECK(model->destination->x==101);CHECK(model->destination->y==95);
    const auto retained=w.game.observeCompletedWorld({.presentation=true});
    REQUIRE(retained.session->tutorial);CHECK(retained.session->tutorial==model);
    const std::string view=R"({schema:1,lessonId:'test',version:1,stepId:'step',index:0,count:1,
        restartMap:'campaigns/introduction.map.gz',title:'',instruction:'',touchInstruction:'',
        help:'',touchHelp:'',completionTitle:'',completionBody:'',hint:'',highlights:[],destination:null})";
    Script::JavaScriptMap repeated;
    const std::string repeatedSource="function step(){return [{type:'tutorial',view:"+view+"}];}";
    repeated.step(repeatedSource,w.game,w.gui);
    const auto repeatedModel=repeated.tutorialView();REQUIRE(repeatedModel);
    repeated.step(repeatedSource,w.game,w.gui);
    CHECK(repeated.tutorialView()==repeatedModel);
    const auto serialized=[](const Script::JavaScriptMap &backend) {
        auto *memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory);
        backend.save(&output);output.flush();
        return std::string(memory->getBuffer(),memory->getPosition());
    };
    for(const std::string noEffects:{"null","undefined","[]"}) {
        Script::JavaScriptMap idle;
        const auto initialTick=w.game.stepCounter;
        const std::string idleSource="function step(ctx){if(ctx.tick==="+std::to_string(initialTick)+
            ")return [{type:'tutorial',view:"+view+"}];return "+noEffects+";}";
        idle.step(idleSource,w.game,w.gui);
        const auto idleModel=idle.tutorialView();REQUIRE(idleModel);
        const auto beforeIdle=serialized(idle);
        w.step();
        idle.step(idleSource,w.game,w.gui);
        CHECK(idle.tutorialView()==idleModel);
        CHECK(serialized(idle)==beforeIdle);
    }
    Script::JavaScriptMap overwritten;
    const auto beforeOverwrite=w.game.checkSum();
    CHECK_THROWS_AS(overwritten.step("function step(){return [{type:'objective',id:0,action:'complete'},"
        "{type:'tutorial',view:{schema:2}},{type:'tutorial',view:"+view+"}];}",w.game,w.gui),Script::ScenarioFailure);
    CHECK(w.game.checkSum()==beforeOverwrite);
    CHECK_FALSE(w.game.objectives.isObjectiveComplete(0));
    // Unknown controls, malformed coordinates and unsupported schema versions
    // fail before committing even an earlier objective effect in the same batch.
    for(const std::string bad:{"{schema:2}","{schema:1}","null"}) {
        if(bad=="null") {CHECK_FALSE(Tutorial::parse(Script::Value()));continue;}
        Script::JavaScriptMap backend;
        const auto before=w.game.checkSum();
        CHECK_THROWS_AS(backend.step("function step(){return [{type:'objective',id:0,action:'complete'},{type:'tutorial',view:"+bad+"}];}",w.game,w.gui),Script::ScenarioFailure);
        CHECK_FALSE(w.game.objectives.isObjectiveComplete(0));CHECK(w.game.checkSum()==before);
    }
    for(const std::string mutation:{"v.schema=2;","v.highlights=['unknown'];","v.destination.x=-1;",
        "v.destination.x='bad';","v.restartMap='../save.map.gz';","v.index=99;"}) {
        Script::JavaScriptMap backend;
        auto script=source();
        const auto callback=script.find("function step(ctx)");REQUIRE(callback!=std::string::npos);
        script.replace(callback,18,"function originalStep(ctx)");
        script+="\nfunction step(ctx){let effects=originalStep(ctx);let v=effects.find(e=>e.type==='tutorial').view;"+mutation+
            "effects.unshift({type:'objective',id:0,action:'complete'});return effects;}";
        const auto before=w.game.checkSum();
        CHECK_THROWS_AS(backend.step(script,w.game,w.gui),Script::ScenarioFailure);
        CHECK_FALSE(w.game.objectives.isObjectiveComplete(0));CHECK(w.game.checkSum()==before);
    }
    CHECK(retained.session->tutorial->stepId=="scout");
}

TEST_CASE("expanded lessons complete through player orders and saved continuation" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::writeFile(glob2test::artifactDir()/"introduction.trace","");
    for(const std::string name:{"healing","walking-training","swimming","schools","upgrades","warrior-training","fortifications","defensive-tactics"}) {
        if(const auto *selected=std::getenv("GLOB2_LESSON_TEST"); selected && name!=selected) continue;
        CAPTURE(name);
        {std::ofstream trace(glob2test::artifactDir()/"introduction.trace",std::ios::app);trace<<"# Lesson "<<name<<"\n";}
        glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
        setup(w,name);w.step(32);
        const auto done=[&](int i){until(w,[&]{return w.game.objectives.isObjectiveComplete(i);},std::getenv("GLOB2_LESSON_TEST") ? 12000 : 48000);continuation(w);};
        const auto zone=[&](bool add){
            auto descriptor=Script::Value::object().set("type","guardArea").set("mode",add?1:2).set("x",93).set("y",94).set("width",8).set("height",8);
            auto mask=Script::Value::array();for(int i=0;i<64;++i)mask.items.push_back(Script::Value(true));descriptor.set("mask",mask);
            order(w.game,Script::order(w.game,0,descriptor));
        };
        if(name=="healing") {create(w.game,"hospital",86,91,3,true);done(0);done(1);}
        if(name=="walking-training") {create(w.game,"racetrack",86,91,3,true);done(0);done(1);}
        if(name=="swimming") {create(w.game,"swimmingpool",83,84,3,true);done(0);done(1);create(w.game,"school",89,97,3,true);done(2);}
        if(name=="schools") {create(w.game,"school",86,91,3,true);done(0);done(1);}
        if(name=="upgrades") {
            auto *track=find(w.game,IntBuildingType::WALKSPEED_BUILDING);REQUIRE(track);
            order(w.game,std::make_shared<OrderConstruction>(track->gid,4,0));done(0);done(1);
        }
        if(name=="warrior-training") {
            auto *swarm=find(w.game,IntBuildingType::SWARM_BUILDING);REQUIRE(swarm);
            Sint32 ratios[3]={0,0,1};order(w.game,std::make_shared<OrderModifySwarm>(swarm->gid,ratios));
            order(w.game,std::make_shared<OrderModifyBuilding>(swarm->gid,3));done(0);
            order(w.game,std::make_shared<OrderModifyBuilding>(swarm->gid,0));
            create(w.game,"barracks",86,91,3,true);done(1);done(2);
        }
        if(name=="fortifications") {
            create(w.game,"defencetower",96,96,3,true);done(0);
            create(w.game,"stonewall",99,95,2,true);create(w.game,"stonewall",99,98,2,true);done(1);
            CHECK(w.game.teams[1]->liveBuildings.size()==3);
            auto *tower=find(w.game,IntBuildingType::DEFENSE_BUILDING);REQUIRE(tower);
            order(w.game,std::make_shared<OrderModifyBuilding>(tower->gid,2));done(2);
            CHECK(w.game.teams[1]->liveBuildings.size()==3);done(3);
        }
        if(name=="defensive-tactics") {
            create(w.game,"explorationflag",113,97,1);done(0);zone(true);done(1);
            CHECK(w.game.teams[1]->liveBuildings.size()==3);done(2);zone(false);
            create(w.game,"warflag",114,99,4);done(3);
        }
        auto model=w.game.mapscript.tutorialView();REQUIRE(model);CHECK(model->complete());
    }
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"expanded.trace");
}

TEST_CASE("zone observations and scenario orders preserve map callback validation" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    setup(w,"fortifications");
    globalContainer->replayWriter=std::make_unique<ReplayWriter>();
    globalContainer->replayWriter->init("",w.gui);
    struct RecordingScope { ~RecordingScope(){globalContainer->replayWriter.reset();} } recordingScope;
    const auto recordedBefore=globalContainer->replayWriter->getOrderCount();
    const std::string spawn="const t=ctx.game.buildingTypes().find(t=>t.name==='warflag'&&!t.site&&t.level===0);const e={type:'scenarioOrder',team:1,order:{type:'create',buildingType:t.id,x:97,y:98,workers:1,futureWorkers:1,range:3}};";
    Script::JavaScriptMap rejected;
    const auto before=w.game.checkSum();
    CHECK_THROWS_AS(rejected.step("function step(ctx){"+spawn+"return[e,{type:'objective',id:999,action:'complete'}];}",w.game,w.gui),Script::ScenarioFailure);
    CHECK(w.game.checkSum()==before);CHECK(w.game.teams[1]->liveBuildings.size()==3);
    CHECK_THROWS_AS(rejected.step("function step(ctx){"+spawn+"return Array(17).fill(e);}",w.game,w.gui),Script::ScenarioFailure);
    CHECK(w.game.checkSum()==before);
    Script::JavaScriptMap accepted;
    accepted.step("function step(ctx){"+spawn+"return[e];}",w.game,w.gui);
    CHECK(w.game.teams[1]->liveBuildings.size()==4);
    CHECK(globalContainer->replayWriter->getOrderCount()==recordedBefore);
    create(w.game,"explorationflag",101,94,1);
    CHECK(globalContainer->replayWriter->getOrderCount()==recordedBefore+1);
    Script::JavaScriptMap zones;
    zones.step("function step(ctx){const z=ctx.game.map.zones(29,30,2,2,0);if(z.length!==4||z.some(t=>t.guard||t.clear||t.forbidden))throw 'zones';if('guard' in ctx.game.map.tile(29,30))throw 'tile contract changed';}",w.game,w.gui);
    for(const std::string args:{"0,0,-1,2,0","0,0,257,2,0","0,0,2,2,8","0,0,2,2"})
        CHECK_THROWS_AS(zones.step("function step(ctx){ctx.game.map.zones("+args+");}",w.game,w.gui),Script::ScenarioFailure);
}

TEST_CASE("scenario area and queued-building orders need no local presentation player" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.teams=2;options.loadDefaultRace=true;options.header=true;
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame w(options);
    for(int y=0;y<w.game.map.getH();++y)
        for(int x=0;x<4;++x) w.game.map.paintVertexSquare(x,y,WATER,1);
    for(int team=0;team<2;++team)
        REQUIRE(w.game.map.incResourceByIndex(8+team*8,7,WHEAT,0));
    Script::JavaScriptMap zones;
    zones.step(R"(function step(){
        const effects=[];
        for(let team=0;team<2;team++)
            ['forbidden','guardArea','clearArea','farmArea'].forEach((type,i)=>effects.push({
                type:'scenarioOrder',team,order:{type,mode:1,x:8+team*8,y:4+i,width:1,height:1,mask:[true]}}));
        return effects;
    })",w.game,w.gui);
    for(int team=0;team<2;++team) {
        const int x=8+team*8;const auto mask=w.game.teams[team]->me;
        CHECK(w.game.map.isForbidden(x,4,mask));
        CHECK(w.game.map.isGuardArea(x,5,mask));
        CHECK(w.game.map.isClearArea(x,6,mask));
        CHECK(w.game.map.isFarmArea(x,7,mask));
        CHECK_FALSE(w.game.map.isForbiddenInDisplayedView(x,4));
        CHECK_FALSE(w.game.map.isGuardAreaInDisplayedView(x,5));
        CHECK_FALSE(w.game.map.isClearAreaInDisplayedView(x,6));
        CHECK_FALSE(w.game.map.isFarmAreaInDisplayedView(x,7));
        REQUIRE(w.addUnit(WORKER,20,20+team*4,team));
    }
    Script::JavaScriptMap projects;
    projects.step(R"(function step(ctx){
        const inn=ctx.game.buildingTypes().find(t=>t.name==='inn'&&t.site&&t.level===0);
        return [0,1].map(team=>({type:'scenarioOrder',team,order:{type:'create',buildingType:inn.id,
            x:20,y:20+team*4,workers:1,futureWorkers:1}}));
    })",w.game,w.gui);
    REQUIRE(w.game.buildProjects.size()==2);
    for(int team=0;team<2;++team) {
        CHECK(w.game.map.isForbidden(20,20+team*4,w.game.teams[team]->me));
        CHECK_FALSE(w.game.map.isForbiddenInDisplayedView(20,20+team*4));
    }
    // Ordinary local orders still update their presentation overlay.
    auto local=std::make_shared<OrderAlterGuardArea>();
    local->sender=0;local->teamNumber=0;local->type=BrushTool::MODE_ADD;local->centerX=6;local->centerY=6;
    local->minX=local->minY=0;local->maxX=local->maxY=1;local->mask.resize(1);local->mask.set(0,true);
    CHECK(w.game.executeOrder(local,0));
    CHECK(w.game.map.isGuardAreaInDisplayedView(6,6));
    continuation(w);
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"scripted-areas.trace");
}

TEST_CASE("archived SGSL save retains embedded source and continuation" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals({.loadStrings=true});
    glob2test::writeFile(glob2test::artifactDir()/"introduction.trace","");
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true,.seed=20261026});
    const auto path=glob2test::fixture("tutorial/legacy/tutorial-part1.map.gz");
    gzFile file=gzopen(path.string().c_str(),"rb");REQUIRE(file);
    std::string bytes;char buffer[4096];int count;
    while((count=gzread(file,buffer,sizeof(buffer)))>0)bytes.append(buffer,count);
    REQUIRE(gzclose(file)==Z_OK);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);REQUIRE(w.game.load(&input));w.game.setWaitingOnMask(0);w.step(32);
    CHECK_FALSE(w.game.mapscript.tutorialView());
    const auto script=w.game.sgslScript.sourceCode;REQUIRE_FALSE(script.empty());
    const auto progress=w.game.sgslScript.checkSum();
    auto *saved=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(saved);w.game.save(&output,false,"Archived SGSL continuity");output.flush();
    const std::string state(saved->getBuffer(),saved->getPosition());
    // An ordinary old-save load repairs historical coordinates and caches.
    // Compare two normal loads of that same checkpoint after the repair.
    GameGUI baseline(false),restored(false);
    GAGCore::BinaryInputStream a(new GAGCore::MemoryStreamBackend(state.data(),state.size())),
        b(new GAGCore::MemoryStreamBackend(state.data(),state.size()));
    a.seekFromStart(0);b.seekFromStart(0);REQUIRE(baseline.game.load(&a));REQUIRE(restored.game.load(&b));
    CHECK(restored.game.sgslScript.sourceCode==script);CHECK(restored.game.sgslScript.checkSum()==progress);
    CHECK_FALSE(Tutorial::view(*restored.game.observeCompletedWorld({.presentation=true}).session));
    baseline.game.setWaitingOnMask(0);restored.game.setWaitingOnMask(0);
    std::ofstream trace(glob2test::artifactDir()/"introduction.trace",std::ios::app);
    for(int i=0;i<96;++i) {
        baseline.game.step(0);restored.game.step(0);
        REQUIRE(baseline.game.checkSum()==restored.game.checkSum());
        trace<<baseline.game.stepCounter<<" "<<baseline.game.checkSum()<<"\n";
    }
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"legacy-campaign.trace");
}

TEST_CASE("scripted diplomacy is atomic and rejects invalid relationships" * doctest::test_suite("Introduction"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
    setup(w,"fortifications");w.step(32);
    CHECK(w.game.teams[0]->enemies==0);CHECK(w.game.teams[1]->enemies==0);
    Script::JavaScriptMap script;
    for(const std::string bad:{"[2]","[0]","[1,2,3]","true"}) {
        const auto before=w.game.checkSum();
        CHECK_THROWS_AS(script.step("function step(){return [{type:'scenarioOrder',team:1,order:{type:'alliance',enemies:[0]}},{type:'scenarioOrder',team:0,order:{type:'alliance',enemies:"+bad+"}}];}",w.game,w.gui),Script::ScenarioFailure);
        CHECK(w.game.checkSum()==before);
    }
    script.step("function step(){return [{type:'scenarioOrder',team:0,order:{type:'alliance',enemies:[1]}},{type:'scenarioOrder',team:1,order:{type:'alliance',enemies:[0]}}];}",w.game,w.gui);
    CHECK(w.game.teams[0]->enemies==2);CHECK(w.game.teams[1]->enemies==1);
    continuation(w);
    std::filesystem::rename(glob2test::artifactDir()/"introduction.trace",glob2test::artifactDir()/"diplomacy.trace");
}
