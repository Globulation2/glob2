#define main originalFarmingRegressionMain
#include "MaximaFarmingIntegrationTest.cpp"
#undef main

static void barrierSwitch()
{
    for(bool enabled:{false,true})for(bool master:{false,true})for(bool coast:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        if(coast)for(int y=0;y<64;++y)for(int x=0;x<=10;++x)map.getCase(x,y).terrain=x<6?256:128;
        assert(f.game.addBuilding(18,30,globalContainer->buildingsTypes.getTypeNum("inn",0,false),0));
        a.strategy.farming.enabled=master;a.strategy.farming.barrier_topology_enabled=enabled;
        a.finalize_director_plan(c);a.budget.farming_economic_envelope_radius=20;
        a.initialize_farming_cache(c);a.update_barrier_topology(c);
        assert(!a.strategic_gates.empty()==(enabled&&master&&coast));
        assert((std::count(a.strategic_barrier_mask.begin(),a.strategic_barrier_mask.end(),Uint8(1))>0)==(enabled&&master&&coast));
    }
}
static void porositySwitch()
{
    for(bool parent:{false,true})for(bool home:{false,true}) {
        std::vector<Uint8> masks[2];
        for(int enabled=0;enabled<2;++enabled) {
            Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
            for(int y=0;y<64;++y)for(int x=0;x<64;++x)map.getCase(x,y).terrain=(x>=20&&x<=28&&y>=20&&y<=28)?0:256;
            for(int y=20;y<=28;++y)for(int x=20;x<=28;++x)map.setResource(x,y,CORN,1);
            if(home){f.player.team->startPosSet=1;f.player.team->startPosX=24;f.player.team->startPosY=24;}
            a.strategy.farming.coastal_porosity_enabled=enabled;a.strategy.farming.farm_protection_enabled=parent;
            a.strategy.farming.barrier_topology_enabled=false;a.finalize_director_plan(c);a.budget.farming_management_radius=0;
            a.update_farming(c);applyAreaContracts(f);masks[enabled]=a.farm_protection_mask;
        }
        assert((masks[0]!=masks[1])==(parent&&!home));
        if(parent&&!home)assert(std::count(masks[1].begin(),masks[1].end(),Uint8(1))<std::count(masks[0].begin(),masks[0].end(),Uint8(1)));
    }
}
static void gateSwitch()
{
    for(bool enabled:{false,true})for(bool master:{false,true})for(bool blocked:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        a.strategy.farming.enabled=master;a.strategy.farming.gate_clearing_enabled=enabled;
        a.strategy.farming.maintenance_clearing_enabled=false;a.strategy.farming.proactive_clearing_enabled=false;
        a.finalize_director_plan(c);a.initialize_farming_cache(c);a.budget.farming_gate_clearing_radius=1;
        a.strategic_gates={{20*64+20,20*64+19,19*64+20},{40*64+40,40*64+39,39*64+40}};
        if(blocked){map.setResource(19,20,WOOD,1);map.setResource(40,40,WOOD,1);}
        a.manage_land_clearing(c);
        assert((a.proactive_clearing_flag>=0)==(enabled&&master&&blocked));
        assert(!c.buildingOrders.empty()==(enabled&&master&&blocked));
    }
}
static void proactiveSwitch()
{
    for(bool enabled:{false,true})for(bool master:{false,true})for(bool allowed:{false,true})for(bool wood:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        assert(f.game.addBuilding(20,20,globalContainer->buildingsTypes.getTypeNum("inn",0,false),0));c.buildings.tick();
        if(wood)for(int y=25;y<=28;++y)for(int x=25;x<=28;++x)map.setResource(x,y,WOOD,1);
        a.strategy.farming.enabled=master;a.strategy.farming.proactive_clearing_enabled=enabled;
        a.strategy.farming.gate_clearing_enabled=false;a.strategy.farming.maintenance_clearing_enabled=false;
        a.finalize_director_plan(c);a.budget.farming_allow_proactive_clearing=allowed;
        a.timer=100000;f.player.team->stats.getLatestStat()->numberUnitPerType[WORKER]=100;
        a.manage_land_clearing(c);
        assert((a.proactive_clearing_flag>=0)==(enabled&&master&&allowed&&wood));
        assert(!c.buildingOrders.empty()==(enabled&&master&&allowed&&wood));
    }
}
static void circulationSwitch()
{
    using namespace AIMaximaPlacement;
    for(bool enabled:{false,true})for(bool maintenance:{false,true})for(bool wood:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        a.initialize_farming_cache(c);a.configure_development_planner();assert(f.game.addUnit(10,10,0,WORKER,0,0,0,0));
        DevelopmentIntent intent;intent.buildingType=IntBuildingType::HEAL_BUILDING;intent.unmetCount=4;intent.priority=100;intent.workers=2;
        DevelopmentLimits limits;limits.newConstruction=4;for(int member=0;member<4;++member){auto world=a.collect_development_world(c);DevelopmentAction action;
        assert(a.development_planner.selectAction(world,{intent},limits,action));assert(a.development_planner.reserve(world,action));assert(a.issue_development_action(c,action));
        assert(f.game.addBuilding(map.normalizeX(action.centerX+action.initialFootprint.left),map.normalizeY(action.centerY+action.initialFootprint.top),globalContainer->buildingsTypes.getTypeNum("hospital",0,false),0));
        c.orders.clear();c.buildings.tick();a.development_planner.observe(a.collect_development_world(c));}
        assert(a.development_planner.campuses().size()==1);
        const auto& reservation=a.development_planner.reservations().begin()->second;assert(!reservation.circulationTiles.empty());
        if(wood)for(int i:reservation.circulationTiles)map.setResource(i%64,i/64,WOOD,1);
        a.strategy.farming.resource_preserving_circulation_enabled=enabled;a.strategy.farming.maintenance_clearing_enabled=maintenance;
        a.strategy.farming.wood_firebreak_enabled=false;a.strategy.farming.wheat_invasion_clearing_enabled=false;
        a.finalize_director_plan(c);a.update_maintenance_clearing_areas(c);applyAreaContracts(f);
        int cleared=0;for(int i:reservation.circulationTiles)cleared+=a.maintenance_circulation_mask[i]!=0;
        if(!maintenance)assert(cleared==0);
        else if(enabled&&wood)assert(cleared>0&&cleared<int(reservation.circulationTiles.size()));
        else assert(cleared==int(reservation.circulationTiles.size()));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    barrierSwitch();porositySwitch();gateSwitch();proactiveSwitch();circulationSwitch();
    std::cout<<"Five farming access switches: 48 native topology, flag and circulation cases PASS\n";
}
