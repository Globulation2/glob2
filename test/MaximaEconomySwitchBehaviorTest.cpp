// Paired switch fixtures linked to the unchanged qualified engine.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void innStaffingSwitch()
{
    for(int level=0;level<3;++level)for(bool enabled:{false,true})for(bool completed:{false,true}) {
        combat_regressions::Fixture f;
        auto* inn=f.game.addBuilding(20,20,globalContainer->buildingsTypes.getTypeNum("inn",level,!completed),0,1,1);
        assert(inn);auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.staffing.inn_adaptive_staffing_enabled=enabled;
        a.finalize_director_plan(c);
        for(int i=0;i<3;++i){a.budget.inn_normal_workers[i]=3;a.budget.inn_low_corn_workers[i]=4;}
        // Dry grass has no renewable corn supply. Adaptive staffing must release
        // carriers; the disabled policy retains its fixed staffing allocation.
        assert(a.nearby_farm_capacity(c,f.id(inn))==0);
        a.manage_buildings(c);c.update_management_orders();
        int count=0;
        for(auto order:c.orders)if(auto o=std::dynamic_pointer_cast<OrderModifyBuilding>(order)) {
            assert(o->gid==inn->gid);assert(o->numberRequested==(enabled?0:3));++count;
        }
        assert(count==(completed?1:0));
    }
}

static void retirementSwitch()
{
    for(bool enabled:{false,true})for(bool safe:{false,true})for(bool mature:{false,true}) {
        combat_regressions::Fixture f;auto* swarm=f.building(20,20,0,"swarm");
        auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.economy.swarm_retirement_enabled=enabled;a.finalize_director_plan(c);
        a.budget.desired_swarms=0;a.budget.recovery_active=!safe;
        a.snapshot.critical_food=0;a.snapshot.own_buildings_under_attack=0;a.snapshot.own_units_under_attack=0;
        a.timer=1;a.update_swarm_retirement(c);a.timer=mature?3001:2999;a.update_swarm_retirement(c);
        c.update_management_orders();int count=0;
        for(auto order:c.orders)if(auto o=std::dynamic_pointer_cast<OrderDelete>(order)){assert(o->gid==swarm->gid);++count;}
        assert(count==(enabled&&safe&&mature?1:0));
        c.orders.clear();a.update_swarm_retirement(c);c.update_management_orders();assert(c.orders.empty());
        if(!enabled)assert(a.remote_swarm_since.empty()&&a.remote_swarms_ready.empty());
    }
}

static void birthThrottleSwitch()
{
    for(bool enabled:{false,true})for(bool surplus:{false,true})for(bool funded:{false,true}) {
        combat_regressions::Fixture f;auto* swarm=f.building(20,20,0,"swarm");
        auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.economy.worker_birth_throttle_enabled=enabled;
        a.snapshot.population=a.strategy.economy.worker_birth_stop_population_min;
        a.snapshot.workers=100;a.snapshot.free_workers=surplus?100:0;a.snapshot.worker_jobs_open=0;
        a.budget.worker_ratio=5;a.budget.swarm_workers=funded?3:0;
        a.finalize_director_plan(c);
        f.player.team->stats.getLatestStat()->totalUnit=100;
        a.manage_swarm(c,f.id(swarm));c.update_management_orders();int count=0;
        for(auto order:c.orders)if(auto o=std::dynamic_pointer_cast<OrderModifySwarm>(order)) {
            assert(o->gid==swarm->gid);assert(o->ratio[WORKER]==(funded?(enabled&&surplus?1:5):0));++count;
        }
        assert(count==1);
    }
}

static void largeEconomySwitch()
{
    for(bool enabled:{false,true})for(bool fertile:{false,true})for(bool foodSafe:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;
        a.strategy.economy.large_economy_adaptation_enabled=enabled;
        a.environment.terrain_abundance=fertile?a.strategy.environment.large_economy_terrain_min:0;
        a.environment.food_security=foodSafe?a.strategy.environment.large_economy_food_min:0;
        a.environment.connected_abundance=a.strategy.environment.large_economy_connected_min;
        a.snapshot.swimming_workers=0;a.large_economy_committed=false;
        a.arbitrate_policy_bids();
        const int base=a.policy_bids[AIMaxima::Maxima::PolicyGrowth].utility;
        assert(a.budget.priority_swarms==base+(enabled&&fertile&&foodSafe?a.strategy.scoring.priority_abundance_swarm_bonus:0));
    }
}

int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    innStaffingSwitch();retirementSwitch();birthThrottleSwitch();largeEconomySwitch();
    std::cout<<"Four economy/staffing switches: 36 paired eligibility and runtime-order cases PASS\n";
}
