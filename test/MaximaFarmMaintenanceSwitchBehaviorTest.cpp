#define main originalFarmingRegressionMain
#include "MaximaFarmingIntegrationTest.cpp"
#undef main

static void protectionSwitches()
{
    for(bool master:{false,true})for(bool protection:{false,true})for(bool crop:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        for(int y=0;y<64;++y)map.getTile(10,y).terrain=256;
        if(crop)map.setResource(11,21,CORN,1);
        a.strategy.farming.enabled=master;a.strategy.farming.farm_protection_enabled=protection;
        a.finalize_director_plan(c);a.budget.farming_management_radius=0;
        a.update_farming(c);applyAreaContracts(f);
        assert(bool(a.farm_protection_mask[21*64+11])==(master&&protection&&crop));
        assert(map.isForbidden(11,21,f.player.team->me)==(master&&protection&&crop));
    }
}
static void firebreakSwitches()
{
    for(bool master:{false,true})for(bool maintenance:{false,true})for(bool firebreak:{false,true})for(bool wood:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        if(wood)map.setResource(11,21,WOOD,1);
        a.strategy.farming.enabled=master;a.strategy.farming.maintenance_clearing_enabled=maintenance;
        a.strategy.farming.wood_firebreak_enabled=firebreak;
        a.strategy.farming.wood_firebreak_fertility_min_percent=0;a.strategy.farming.wood_firebreak_fertility_max_percent=100;
        a.finalize_director_plan(c);a.budget.farming_management_radius=0;
        a.update_maintenance_clearing_areas(c);applyAreaContracts(f);
        const bool active=master&&maintenance&&firebreak&&wood;
        assert(bool(a.wood_firebreak_mask[21*64+11])==active);
        assert(map.isClearArea(11,21,f.player.team->me)==active);
    }
}
static void invasionSwitch()
{
    for(bool enabled:{false,true})for(bool maintenance:{false,true})for(bool wheat:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;auto& map=f.game.map;
        for(int y=0;y<64;++y)for(int x=0;x<12;++x)map.getTile(x,y).terrain=256;
        if(wheat)map.setResource(18,21,CORN,1);map.setResource(18,22,WOOD,1);
        a.strategy.farming.wheat_invasion_clearing_enabled=enabled;a.strategy.farming.maintenance_clearing_enabled=maintenance;
        a.strategy.farming.wood_firebreak_enabled=false;
        a.finalize_director_plan(c);a.budget.farming_management_radius=0;a.budget.farming_minimum_wood_fertility=3276;
        a.update_farming(c);applyAreaContracts(f);a.update_maintenance_clearing_areas(c);applyAreaContracts(f);
        assert(bool(a.maintenance_circulation_mask[22*64+18])==(enabled&&maintenance&&wheat));
        assert(map.isClearArea(18,22,f.player.team->me)==(enabled&&maintenance&&wheat));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    protectionSwitches();firebreakSwitches();invasionSwitch();
    std::cout<<"Five farming switches: 32 real forbidden and clearing area cases PASS\n";
}
