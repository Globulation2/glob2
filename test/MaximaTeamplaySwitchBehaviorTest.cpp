// Additional behavioral evidence; link against the unchanged frozen engine.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void teamplayDefenseSwitches()
{
    using namespace AIMaxima;
    for(bool tactics:{false,true}) for(bool teamplay:{false,true})
        for(bool defense:{false,true}) for(bool eligible:{false,true})
    {
        combat_regressions::Fixture f;
        f.building(10,10,0);
        auto* ally=f.building(10,40,1);
        for(int i=0;i<8;++i)f.warrior(6+i,6,1);
        f.player.team->allies=f.player.team->me|f.game.teams[1]->me;
        f.player.team->enemies=f.game.teams[2]->me;
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(ally);
        a.snapshot.trained_warriors=8;
        a.strategy.tactics.enabled=tactics;
        a.strategy.tactics.siege_enabled=false;
        a.strategy.teamplay.enabled=teamplay;
        a.strategy.teamplay.defense_enabled=defense;
        a.strategy.teamplay.defense_min_force=4;
        a.strategy.teamplay.defense_strength_percent=125;
        a.tactics.beginObservation(a.timer);
        if(eligible) {
            auto* unit=f.player.team->myUnits[0];
            const int power=unit->getRealAttackStrength()*unit->performance[ATTACK_SPEED];
            a.tactics.observeThreat(Tactics::ThreatSighting(500,2,12,40,power*3));
        }
        Tactics::RaidRules rules;rules.width=rules.height=64;
        a.tactics.finishObservation(rules);
        a.finalize_director_plan(c);
        assert(a.budget.teamplay_enabled==teamplay);
        assert(a.budget.teamplay_defense_enabled==defense);
        a.plan_tactical_authorization(c);
        const bool expected=tactics&&teamplay&&defense&&eligible;
        assert((a.budget.tactical_kind==Tactics::MissionRelief)==expected);
        a.control_attacks(c);
        assert((a.tactical_mission.kind==Tactics::MissionRelief)==expected);
        assert(!c.buildingOrders.empty()==expected);
        if(expected) {
            assert(a.budget.tactical_requested_force>=4);
            assert(a.budget.tactical_target_team==1);
        }
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    teamplayDefenseSwitches();
    std::cout<<"teamplay.enabled and teamplay.defense_enabled: 16 paired parent/child, eligibility, relief mission and flag-order cases PASS\n";
}
