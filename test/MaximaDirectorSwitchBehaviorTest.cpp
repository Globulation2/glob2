#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void postureSwitches()
{
    using A=AIMaxima::Maxima;
    for(int target=0;target<A::PostureCount;++target)for(bool enabled:{false,true})for(bool preferred:{false,true})for(bool current:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();auto& s=a.strategy.postures;
        bool* switches[]={&s.expand_enabled,&s.develop_enabled,&s.mobilize_enabled,&s.campaign_enabled,&s.finish_enabled,&s.defend_enabled,&s.recover_enabled};
        for(bool* x:switches)*x=true;*switches[target]=enabled;
        a.strategy.emergencies.food_enabled=false;a.strategy.emergencies.colony_enabled=false;
        a.score_postures();assert((a.posture_utilities[target]==INT_MIN)==!enabled);
        // Exercise the real selection/commitment boundary with a controlled
        // ranking. Disabled candidates must never win, even if currently active.
        const int fallback=target==A::PostureExpand?A::PostureDevelop:A::PostureExpand;
        for(int i=0;i<A::PostureCount;++i)a.posture_utilities[i]=-1000;
        a.posture_utilities[fallback]=0;a.posture_utilities[target]=enabled?(preferred?1000:-1000):INT_MIN;
        a.director.initialized=true;a.timer=100000;a.posture_since=0;
        a.posture=A::StrategicPosture(current?target:fallback);a.select_posture();
        assert(a.posture==(enabled&&preferred?target:fallback));
        a.finalize_director_plan(c);assert(a.budget.recovery_active==(a.posture==A::PostureRecover));
    }
}
static void emergencySwitches()
{
    using A=AIMaxima::Maxima;
    for(bool colony:{false,true})for(bool enabled:{false,true})for(bool postureParent:{false,true})for(bool triggered:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.emergencies.food_enabled=!colony&&enabled;a.strategy.emergencies.colony_enabled=colony&&enabled;
        a.strategy.postures.recover_enabled=postureParent;a.strategy.postures.defend_enabled=postureParent;
        a.snapshot.population=100;a.snapshot.unserved_food=!colony&&triggered?100:0;
        a.snapshot.visible_colony_threat=colony&&triggered?100:0;
        for(int i=0;i<A::PostureCount;++i)a.posture_utilities[i]=-1000;
        a.posture_utilities[A::PostureExpand]=1000;a.director.initialized=false;a.select_posture();
        assert(a.posture==(enabled&&triggered&&postureParent?(colony?A::PostureDefend:A::PostureRecover):A::PostureExpand));
        a.finalize_director_plan(c);assert((colony?a.budget.colony_emergency:a.budget.food_emergency)==(enabled&&triggered));
    }
}
static void colonizationSwitch()
{
    for(bool enabled:{false,true})for(bool builders:{false,true})for(bool recovering:{false,true}) {
        combat_regressions::Fixture f;f.building(20,20,0,"swarm");auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.colonization.enabled=enabled;a.snapshot.free_workers=builders?20:0;a.snapshot.worker_jobs_open=0;
        a.posture=recovering?AIMaxima::Maxima::PostureRecover:AIMaxima::Maxima::PostureExpand;
        a.finalize_director_plan(c);a.budget.desired_swarms=0;
        const auto world=a.collect_development_world(c);int count=0;
        for(const auto& intent:a.collect_development_intents(world))if(intent.purpose==AIMaximaPlacement::ColonySeed){assert(intent.unmetCount==1);++count;}
        assert(count==(enabled&&builders?1:0));
    }
}
static void fruitSwitch()
{
    for(bool enabled:{false,true})for(bool present:{false,true})for(bool recovering:{false,true}) {
        combat_regressions::Fixture f;f.building(20,20,0);if(present)f.game.map.setResource(30,30,CHERRY,1);
        auto& a=*f.ai;auto& c=a.context;c.initialize();c.detect_fruit();
        a.strategy.fruit.enabled=enabled;a.snapshot.population=200;
        a.posture=recovering?AIMaxima::Maxima::PostureRecover:AIMaxima::Maxima::PostureExpand;
        a.finalize_director_plan(c);a.update_fruit_flags(c);
        assert(c.buildingOrders.size()==(enabled&&present&&!recovering?1u:0u));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    postureSwitches();emergencySwitches();colonizationSwitch();fruitSwitch();
    std::cout<<"Eleven director switches: 88 paired selection, emergency, expansion and fruit cases PASS\n";
}
