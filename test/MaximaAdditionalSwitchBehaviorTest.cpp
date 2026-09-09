// Additional behavioral evidence against the unchanged v12 engine objects.
// Keep separate from the frozen original qualification fixtures.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void reactiveDefenseSwitch()
{
    for(bool invaders:{false,true}) for(bool enabled:{false,true}) for(bool eligible:{false,true})
    {
        combat_regressions::Fixture f;
        f.building(20,24,0);
        auto& a=*f.ai; auto& c=a.context;
        c.initialize();
        if(eligible)
        {
            Unit* unit=f.game.addUnit(28,29,invaders ? 1 : 0,
                invaders ? WARRIOR : WORKER,1,0,0,0);
            assert(unit);
            if(!invaders) unit->underAttackTimer=100;
        }
        a.strategy.reactive_defense.enabled=enabled;
        a.finalize_director_plan(c);
        assert(a.budget.reactive_defense_enabled==enabled);
        a.budget.reactive_defense_flag_radius=5;
        a.budget.reactive_defense_unit_cap=10;
        a.budget.reactive_defense_advantage_min=3;
        a.budget.defense_reserve=30;
        a.compute_defense_flag_positioning(c);
        assert(!c.buildingOrders.empty()==(enabled && eligible));
        if(enabled && eligible)
        {
            bool covered=false;
            for(auto order:c.buildingOrders)
            {
                bool complete=false;
                auto location=order->find_location(c,1,complete);
                assert(complete && location.found && order->workers>0);
                covered|=f.game.map.warpDistSquare(28,29,location.value.x,location.value.y)<=25;
            }
            assert(covered);
        }
    }
}

int main()
{
    GlobalContainer container; globalContainer=&container;
    container.runNoX=true; container.buildingsTypes.init(); IntBuildingType::init();
    reactiveDefenseSwitch();
    std::cout << "defense.reactive.enabled: paired on/off, invader and attacked-worker eligibility, inactive-world silence, flag staffing and coverage PASS\n";
}
