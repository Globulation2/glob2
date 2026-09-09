// Supplemental behavioral evidence against unchanged frozen engine objects.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void pressureCoordinationSwitch()
{
    using namespace AIMaxima;
    for(bool tactics:{false,true})for(bool teamplay:{false,true})
        for(bool enabled:{false,true})for(bool eligible:{false,true}) {
        combat_regressions::Fixture f;
        f.building(10,10,0);
        auto* enemy=f.building(45,45,1);
        for(int i=0;i<8;++i)f.warrior(6+i,6,1);
        f.player.team->allies=f.player.team->me|f.game.teams[2]->me;
        f.player.team->enemies=f.game.teams[1]->me;
        auto* ally=f.game.addUnit(eligible?12:40,40,2,WARRIOR,1,0,0,0);
        assert(ally);
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(enemy);
        a.snapshot.trained_warriors=8;
        a.strategy.tactics.enabled=tactics;
        a.strategy.tactics.siege_enabled=false;
        a.strategy.raiding.enabled=true;
        a.strategy.raiding.min_force=4;
        a.strategy.raiding.allied_pressure_bonus=10000;
        a.strategy.teamplay.enabled=teamplay;
        a.strategy.teamplay.defense_enabled=false;
        a.strategy.teamplay.pressure_coordination_enabled=enabled;
        a.strategy.teamplay.allied_pressure_radius=5;
        a.tactics.beginObservation(a.timer);
        // Without allied pressure, the larger, nearer cluster wins.
        for(int i=0;i<5;++i)
            a.tactics.observeWorker(Tactics::WorkerSighting(100+i,1,30+i,10,a.timer,true,true,1));
        for(int i=0;i<4;++i)
            a.tactics.observeWorker(Tactics::WorkerSighting(200+i,1,10+i,40,a.timer,true,true,1));
        Tactics::RaidRules rules;rules.width=rules.height=64;rules.tick=a.timer;
        a.tactics.finishObservation(rules);
        assert(a.tactics.raidCandidates().size()==2);
        a.finalize_director_plan(c);
        a.plan_tactical_authorization(c);
        const bool coordinated=tactics&&teamplay&&enabled&&eligible;
        assert((a.budget.tactical_kind==Tactics::MissionRaid)==tactics);
        if(tactics) {
            assert(a.budget.tactical_target_y==(coordinated?40:10));
            assert(a.tactical_diagnostics.raidAlliedBonus==(coordinated?10000:0));
        }
        a.control_attacks(c);
        assert((a.tactical_mission.kind==Tactics::MissionRaid)==tactics);
        assert(!c.buildingOrders.empty()==tactics);
        if(tactics)assert(a.tactical_mission.targetY==(coordinated?40:10));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    pressureCoordinationSwitch();
    std::cout<<"teamplay.pressure_coordination_enabled: 16 switch, parent, proximity and changed raid-target cases PASS\n";
}
