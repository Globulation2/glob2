#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "Engine.h"
#include "render/scene/Scene.h"
#include <GraphicContext.h>
#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <utility>

#include "sim/RenderBenchProbe.h"
#include <unistd.h>
#include <sys/resource.h>
TEST_CASE("matched revision rendering benchmark [benchmark][display][artifacts]" * doctest::test_suite("RenderRevisionBenchmark"))
{
    glob2test::ScopedEnvironment desktop("GLOB2_MOBILE_UI","0");
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=800,.height=600,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
    REQUIRE(NET_Init());
    struct NetworkScope { ~NetworkScope(){NET_Quit();} } network;
    globalContainer->automaticEndingGame=false;
    globalContainer->settings.autosaveGames=false;
    globalContainer->settings.gameSpeed=Settings::GAME_SPEED_MAXIMUM;
    globalContainer->aiThreads=RenderBench::option("GLOB2_BENCH_THREADS",2);
    setSyncRandSeed(123);
    Engine engine;
    const char* fixture=std::getenv("GLOB2_BENCH_FIXTURE");
    if(fixture) REQUIRE(engine.initCustom(fixture)==Engine::EE_NO_ERROR);
    else REQUIRE(engine.initCampaign("maps/balanced.map")==Engine::EE_NO_ERROR);
    globalContainer->settings.gameSpeed=Settings::GAME_SPEED_MAXIMUM;
    const auto initialChecksum=engine.gui.game.checkSum(nullptr,nullptr,nullptr,true);
    auto entities=[&] { unsigned units=0,buildings=0;for(int t=0;t<engine.gui.game.mapHeader.getNumberOfTeams();++t) {
      auto* team=engine.gui.game.teams[t];if(!team)continue;
      for(int i=0;i<Unit::MAX_COUNT;++i)if(team->myUnits[i])++units;for(int i=0;i<Building::MAX_COUNT;++i)if(team->myBuildings[i])++buildings;
    } return std::pair(units,buildings);};
    const auto initialEntities=entities();
    engine.beginSession(SDL_GetTicks());
    // Warm graphics resources before timing, without advancing the world or
    // retaining serial-producer snapshot leases in the threaded benchmark.
    { Scene scene; engine.extractScene(scene);
      for(unsigned i=0;i<20;++i) {engine.drawFrame(*engine.session,true,&scene);SDL_Delay(16);}
    }
    engine.gui.setPublishedScene(nullptr);
    auto& p=RenderBench::probe;p.setup(engine.gui.game.stepCounter);
    const bool draw=RenderBench::option("GLOB2_BENCH_DRAW",1);
    const unsigned fps=RenderBench::option("GLOB2_BENCH_FPS",60);
    std::vector<uint64_t> rss,frames,ages;
    rss.reserve(20000);frames.reserve(20000);ages.reserve(20000);
    REQUIRE(engine.startSimulationThread(SDL_GetTicks()));
    uint64_t nextRss=0;auto next=RenderBench::ns();const auto deadline=next+300000000000ULL;
    while(p.phase.load()!=2) {
      REQUIRE(RenderBench::ns()<deadline);
      const auto now=RenderBench::ns();
      if(draw && now>=next) {
        const auto phase=p.phase.load();
        SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.x=250;motion.motion.y=250;
        const bool running=engine.threadedClientFrame(SDL_GetTicks(),{motion});
        engine.drawSession();
        if(phase==1) {frames.push_back(RenderBench::ns()-now);if(engine.gui.drawnScene().tickTime)ages.push_back(SDL_GetTicks()-engine.gui.drawnScene().tickTime);}
        if(!running)break;
        next=std::max<uint64_t>(next+1000000000ULL/fps,RenderBench::ns());
      }
      if(p.phase.load()==1 && (rss.empty() || RenderBench::ns()>=nextRss)) {
        std::ifstream stat("/proc/self/statm");uint64_t size=0,resident=0;stat>>size>>resident;
        rss.push_back(resident*sysconf(_SC_PAGESIZE));nextRss=RenderBench::ns()+20000000;
      }
      SDL_Delay(1);
    }
    engine.stopSimulationThread();
    REQUIRE(p.phase.load()==2);REQUIRE(engine.gui.game.stepCounter==p.last);REQUIRE(p.rows.size()==p.last-p.warm);
    const auto finalChecksum=engine.gui.game.checkSum(nullptr,nullptr,nullptr,true);
    const auto finalEntities=entities();
    rusage usage{};getrusage(RUSAGE_SELF,&usage);
    auto pct=[](std::vector<uint64_t> values,unsigned q) {if(values.empty())return uint64_t(0);std::sort(values.begin(),values.end());return values[std::min(values.size()-1,values.size()*q/100)];};
    const auto dir=glob2test::artifactDir();
    std::ofstream summary(dir/"metrics.csv");
    summary<<"width,height,initial_tick,warmup_ticks,measured_ticks,threads,draw,fps,wall_ns,process_cpu_ns,owner_cpu_ns,rss_median_bytes,rss_max_bytes,process_peak_rss_bytes,frames,frame_p50_ns,frame_p95_ns,scene_age_p95_ms,initial_checksum,final_checksum,initial_units,final_units,initial_buildings,final_buildings\n";
    summary<<engine.gui.game.map.getW()<<','<<engine.gui.game.map.getH()<<','<<p.first<<','<<p.warm-p.first<<','<<p.rows.size()<<','<<globalContainer->aiThreads<<','<<draw<<','<<fps<<','<<p.wallEnd-p.wallStart<<','<<p.processEnd-p.processStart<<','<<p.ownerEnd-p.ownerStart<<','<<pct(rss,50)<<','<<pct(rss,100)<<','<<uint64_t(usage.ru_maxrss)*1024<<','<<frames.size()<<','<<pct(frames,50)<<','<<pct(frames,95)<<','<<pct(ages,95)<<','<<initialChecksum<<','<<finalChecksum<<','<<initialEntities.first<<','<<finalEntities.first<<','<<initialEntities.second<<','<<finalEntities.second<<'\n';
    std::ofstream ticks(dir/"ticks.csv");ticks<<"tick,simulation_ns,owner_iteration_ns,completion_interval_ns\n";for(const auto& row:p.rows)ticks<<row.tick<<','<<row.simulation<<','<<row.owner<<','<<row.interval<<'\n';
    std::ofstream trace(dir/"checksums.txt");for(size_t i=0;i<p.checksums.size();++i)trace<<p.first+i+1<<' '<<p.checksums[i]<<'\n';
    p.enabled=false;engine.gui.isRunning=false;CHECK_FALSE(engine.finishSession());
}
