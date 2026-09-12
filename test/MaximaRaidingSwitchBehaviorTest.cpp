// Supplemental behavioral evidence against unchanged frozen engine objects.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void raidingSwitch()
{
    using namespace AIMaxima;
    for(bool parent:{false,true})for(bool enabled:{false,true})for(bool eligible:{false,true}) {
        combat_regressions::Fixture f;
        f.building(10,10,0);
        auto* enemy=f.building(45,45,1);
        for(int i=0;i<8;++i)f.warrior(6+i,6,1);
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(enemy);
        a.snapshot.trained_warriors=8;
        a.strategy.tactics.enabled=parent;
        a.strategy.tactics.siege_enabled=false;
        a.strategy.raiding.enabled=enabled;
        a.strategy.tactics.min_force=4;
        a.tactics.beginObservation(a.timer);
        if(eligible)for(int i=0;i<4;++i)
            a.tactics.observeWorker(Tactics::WorkerSighting(100+i,1,30+i,30,a.timer,true,true,1));
        Tactics::RaidRules rules;rules.width=rules.height=64;rules.tick=a.timer;
        a.tactics.finishObservation(rules);
        a.finalize_director_plan(c);
        a.plan_offense(c);
        const bool expected=parent&&enabled&&eligible;
        assert((a.budget.tactical_kind==Tactics::MissionRaid)==expected);
        a.control_offense(c);
        assert((a.tactical_mission.kind==Tactics::MissionRaid)==expected);
        assert(!c.buildingOrders.empty()==expected);
        if(expected) {
            assert(a.budget.tactical_target_team==1);
            assert(a.budget.tactical_requested_force>=4);
        }
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    raidingSwitch();
    std::cout<<"raiding.enabled: eight paired on/off, tactics-parent and visible-worker eligibility cases PASS\n";
}
