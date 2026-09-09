#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void missionSwitches()
{
    // Both gates must control actual exploration flag creation, including
    // independent exploration and emergency negative controls.
    for(bool master:{false,true})for(bool scouting:{false,true})for(bool explored:{false,true})for(bool emergency:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.reconnaissance.enabled=master;a.strategy.reconnaissance.scouting_missions_enabled=scouting;
        a.strategy.reconnaissance.economic_watch_enabled=false;
        a.reconnaissance.beginObservation(0,{1});a.reconnaissance.finishObservation();
        a.reconnaissance.setExploredPercent(explored?80:79);a.timer=10000;
        a.snapshot.population=150;a.budget.colony_emergency=emergency;
        // Undiscovered frontier cells provide a concrete scouting destination.
        f.game.map.unsetMapDiscovered();
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)if(x<52)f.game.map.setMapDiscovered(x,y,f.player.team->me);
        a.plan_reconnaissance_objectives(c);a.update_reconnaissance_missions(c);
        const bool active=master&&scouting&&explored&&!emergency;
        assert(!c.buildingOrders.empty()==active);
        assert(!a.reconnaissance.report().missions.empty()==active);
        assert(a.budget.reconnaissance_suspended==!active);
    }
}
static void economicWatchSwitch()
{
    for(bool enabled:{false,true})for(bool parent:{false,true})for(bool prestige:{false,true})for(bool contact:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.reconnaissance.enabled=true;a.strategy.reconnaissance.scouting_missions_enabled=parent;
        a.strategy.reconnaissance.economic_watch_enabled=enabled;a.snapshot.prestige=prestige?1:0;
        a.snapshot.population=300;a.timer=10000;a.reconnaissance.beginObservation(a.timer,{1});
        if(contact)a.reconnaissance.observeEconomicActivity(1,30,30);
        a.reconnaissance.finishObservation();a.reconnaissance.setExploredPercent(90);
        a.plan_reconnaissance_objectives(c);a.update_reconnaissance_missions(c);
        int count=0;for(const auto& m:a.reconnaissance.report().missions)if(m.economicWatch){assert(m.x==30&&m.y==30);++count;}
        assert(count==(enabled&&parent&&prestige&&contact?1:0));
        assert(c.buildingOrders.size()==a.reconnaissance.report().missions.size());
    }
}
static void forceMemorySwitch()
{
    for(bool enabled:{false,true})for(bool visible:{false,true})for(bool expired:{false,true}) {
        // Apply the public resolver setting before AI construction. This checks
        // the real constructor-to-recon configuration handoff, not a test shim.
        globalContainer->maximaStrategyOptions.inlineOverrides=std::string("recon.force_memory_enabled=")+(enabled?"true":"false");
        combat_regressions::Fixture f;globalContainer->maximaStrategyOptions.inlineOverrides.clear();
        auto& a=*f.ai;auto& r=a.reconnaissance;assert(a.strategy.reconnaissance.force_memory_enabled==enabled);
        r.beginForceObservation(100,{1});for(int i=0;i<10;++i)r.observeUnit(1,true,false,false,false,false);r.finishForceObservation();
        assert(r.opponent(1)->estimatedWarriors==10);
        r.beginForceObservation(100+(expired?a.strategy.reconnaissance.memory_horizon_ticks+1:1),{1});
        if(visible)for(int i=0;i<10;++i)r.observeUnit(1,true,false,false,false,false);
        r.finishForceObservation();assert(r.opponent(1)->estimatedWarriors==(visible||(enabled&&!expired)?10:0));
        assert(r.opponent(1)->visibleWarriors==(visible?10:0));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    missionSwitches();economicWatchSwitch();forceMemorySwitch();
    std::cout<<"Four recon switches: 40 native mission and force memory cases PASS\n";
}
