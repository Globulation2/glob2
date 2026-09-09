#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void trainingSwitch()
{
    for(bool enabled:{false,true})for(bool backlog:{false,true})for(bool funded:{false,true}) {
        combat_regressions::Fixture f;auto* swarm=f.building(20,20,0,"swarm");auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.military.warrior_training_backlog_throttle_enabled=enabled;
        a.snapshot.barracks=1;a.snapshot.trained_warriors=0;a.snapshot.warriors=backlog?100:0;
        a.policy_bids[AIMaxima::Maxima::PolicyGrowth].swarm_workers=funded?3:0;
        auto& defense=a.policy_bids[AIMaxima::Maxima::PolicyDefense];defense.warrior_ratio=3;defense.desired_warriors=200;
        a.arbitrate_policy_bids();a.finalize_director_plan(c);
        f.player.team->stats.getLatestStat()->totalUnit=100;a.manage_swarm(c,f.id(swarm));c.update_management_orders();int count=0;
        for(auto order:c.orders)if(auto o=std::dynamic_pointer_cast<OrderModifySwarm>(order)){assert(o->ratio[WARRIOR]==(funded&&!(enabled&&backlog)?3:0));++count;}
        assert(count==1);
    }
}
static void explorerDefenseSwitch()
{
    for(bool enabled:{false,true})for(bool threat:{false,true}) {
        combat_regressions::Fixture f;auto* tower=f.building(20,20,0,"defencetower");auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.military.explorer_defense_enabled=enabled;
        a.snapshot.visible_colony_explorer_threat=threat?100:0;
        a.manage_buildings(c);c.update_management_orders();int count=0;
        for(auto order:c.orders)if(auto o=std::dynamic_pointer_cast<OrderModifyBuilding>(order)) {
            assert(o->gid==tower->gid);assert(o->numberRequested==(enabled&&threat?a.strategy.staffing.completed_tower_emergency_workers:a.strategy.staffing.completed_tower_workers));++count;
        }
        assert(count==1);
    }
}
static void counterattackSwitch()
{
    for(bool enabled:{false,true})for(bool parent:{false,true})for(bool siege:{false,true})for(bool threat:{false,true})for(bool ready:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;
        a.strategy.military.counterattack_enabled=enabled;a.strategy.tactics.enabled=parent;a.strategy.tactics.siege_enabled=siege;
        a.snapshot.visible_colony_threat=threat?1:0;a.snapshot.critical_food=0;a.snapshot.unserved_food=0;
        a.snapshot.trained_warriors=ready?100:0;a.snapshot.workers=100;a.snapshot.population=200;
        a.opponents[1].alive=true;a.opponents[1].score=100;a.opponents[1].estimated_warriors=1;
        a.allocate_resources();assert((a.budget.attack_flags>0)==(parent&&siege&&ready&&(!threat||enabled)));
    }
}
static void amphibiousMaintenanceSwitch()
{
    for(bool enabled:{false,true})for(bool parent:{false,true})for(bool opportunity:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;
        a.strategy.economy.amphibious_network_maintenance_enabled=enabled;
        a.strategy.economy.large_economy_adaptation_enabled=parent;a.large_economy_committed=true;
        a.snapshot.population=a.strategy.economy.amphibious_population_min;
        a.environment.mobility_opportunity=opportunity?a.strategy.economy.amphibious_opportunity_min:0;
        a.environment.terrain_abundance=0;a.demands.mobility=0;a.demands.access=0;
        a.build_policy_bids();const auto& access=a.policy_bids[AIMaxima::Maxima::PolicyAccess];
        assert(access.utility==(enabled&&parent&&opportunity?a.strategy.economy.amphibious_utility_floor:0));
        a.arbitrate_policy_bids();assert(a.budget.desired_pools==(enabled&&parent&&opportunity?a.strategy.economy.second_pool_target:0));
    }
}
static void foodServiceSwitch()
{
    for(bool enabled:{false,true})for(bool pressure:{false,true})for(bool supplies:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;
        a.strategy.economy.food_service_safeguards_enabled=enabled;
        a.snapshot.population=200;a.snapshot.unserved_food=pressure?100:0;a.snapshot.critical_food=0;
        a.environment.accessible_corn=supplies?10000:0;a.environment.food_headroom=100;
        // Keep demographic demand below the observed-service target so this
        // fixture exercises the service safeguard rather than another bidder.
        a.strategy.economy.inn_population_divisor=10000;a.demands.food=0;
        a.build_policy_bids();a.arbitrate_policy_bids();
        const int reliable=std::max(1,a.strategy.model.inn_capacity_level1*a.strategy.economy.reliable_inn_percent/100);
        const int nominal=(200+reliable-1)/reliable;
        const int cap=std::max(a.strategy.economy.sustainable_inn_floor,a.environment.accessible_corn/a.strategy.economy.sustainable_inn_corn_divisor+a.strategy.economy.sustainable_inn_offset);
        assert(a.budget.desired_inns==std::min(cap,std::min(a.strategy.economy.inn_target_cap,nominal+(enabled&&pressure?1:0))));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    trainingSwitch();explorerDefenseSwitch();counterattackSwitch();amphibiousMaintenanceSwitch();foodServiceSwitch();
    std::cout<<"Five policy switches: 60 paired activation, suppression and dependency cases PASS\n";
}
