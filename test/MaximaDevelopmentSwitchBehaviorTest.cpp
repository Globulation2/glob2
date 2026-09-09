// Supplemental behavioral evidence against unchanged frozen engine objects.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void repairSwitch()
{
    using namespace AIMaximaPlacement;
    for(int level=0;level<3;++level)for(bool enabled:{false,true})for(bool damaged:{false,true}) {
        combat_regressions::Fixture f;
        auto* inn=f.building(20,20,0,"inn",level);
        if(damaged)inn->hp/=2;
        auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.repairs.enabled=enabled;
        a.budget.allow_upgrades=false;
        a.budget.construction_sites=1;
        a.finalize_director_plan(c);a.configure_development_planner();
        auto world=a.collect_development_world(c);
        a.development_planner.adoptStartingBuildings(world);
        auto limits=a.collect_development_limits(c);
        DevelopmentAction action;
        const bool selected=a.development_planner.selectAction(world,{},limits,action);
        assert(selected==(enabled&&damaged));
        if(selected) {
            assert(action.type==RepairBuilding);
            assert(a.issue_development_action(c,action));
            assert(!c.orders.empty());
            assert(std::dynamic_pointer_cast<OrderConstruction>(c.orders.front()));
        } else assert(c.orders.empty());
    }
}
static void upgradeSwitch()
{
    using namespace AIMaximaPlacement;
    for(bool enabled:{false,true})for(bool eligible:{false,true}) {
        combat_regressions::Fixture f;
        f.building(20,20,0,"racetrack");
        if(eligible)f.building(40,40,0,"school");
        assert(f.game.addUnit(10,10,0,WORKER,1,0,0,0));
        auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.upgrades.enabled=enabled;
        a.strategy.repairs.enabled=false;
        a.snapshot.schools=eligible?1:0;
        a.snapshot.population=a.strategy.upgrades.level1_population_min+1;
        a.budget.allow_upgrades=true;a.budget.allow_level2_upgrades=false;
        a.budget.construction_sites=0;
        a.finalize_director_plan(c);a.configure_development_planner();
        f.player.team->stats.getLatestStat()->upgradeState[BUILD][1]=a.budget.upgrade_level1_trained_units_per_slot;
        auto world=a.collect_development_world(c);
        a.development_planner.adoptStartingBuildings(world);
        auto limits=a.collect_development_limits(c);
        // Isolate one eligible upgrade type while retaining production authorization.
        limits.upgradePriorities.clear();
        limits.upgradePriorities[{IntBuildingType::WALKSPEED_BUILDING,1}]=100;
        DevelopmentAction action;
        const bool selected=a.development_planner.selectAction(world,{},limits,action);
        assert(selected==(enabled&&eligible));
        if(selected) {
            assert(action.type==UpgradeBuilding);
            assert(a.issue_development_action(c,action));
            assert(!c.orders.empty());
            assert(std::dynamic_pointer_cast<OrderConstruction>(c.orders.front()));
        } else assert(c.orders.empty());
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    repairSwitch();upgradeSwitch();
    std::cout<<"repairs.enabled and upgrades.enabled: 16 paired damage/technology eligibility and construction-order cases PASS\n";
}
