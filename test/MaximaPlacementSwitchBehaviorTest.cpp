#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void placementSwitches()
{
    using namespace AIMaximaPlacement;
    for(int feature=0;feature<4;++feature)for(bool authorized:{false,true}) {
        DevelopmentAction actions[2];
        for(int enabled=0;enabled<2;++enabled) {
            combat_regressions::Fixture f;f.building(10,10,0);auto& a=*f.ai;auto& c=a.context;c.initialize();
            bool* switches[]={&a.strategy.placement.food_preservation_enabled,&a.strategy.placement.defensive_siting_enabled,&a.strategy.placement.spacing_compactness_enabled,&a.strategy.placement.artery_routing_enabled};
            *switches[feature]=enabled;a.configure_development_planner();
            auto world=a.collect_development_world(c);
            for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                auto& tile=world.tile(x,y);
                if(feature==0)tile.farmCapacity=x<32?65536:0;
                if(feature==1){tile.threat=x<32?100:0;tile.protectedness=50;}
            }
            a.development_planner.adoptStartingBuildings(world);
            DevelopmentIntent intent;intent.buildingType=feature==1?IntBuildingType::FOOD_BUILDING:IntBuildingType::HEAL_BUILDING;intent.unmetCount=1;intent.priority=100;intent.workers=2;
            DevelopmentLimits limits;limits.allowRepairs=false;limits.newConstruction=authorized?1:0;
            bool found=a.development_planner.selectAction(world,{intent},limits,actions[enabled]);std::cout<<"trial "<<feature<<" "<<enabled<<" "<<authorized<<" found "<<found<<std::endl;assert(found==authorized);
            if(found)std::cout<<"feature "<<feature<<" enabled "<<enabled<<" chosen "<<actions[enabled].centerX<<","<<actions[enabled].centerY<<" artery "<<actions[enabled].arteryTiles.size()<<std::endl;
        }
        if(authorized) {
            if(feature==3)assert(actions[0].arteryTiles.empty()&&!actions[1].arteryTiles.empty());
            else assert(actions[0].centerX!=actions[1].centerX||actions[0].centerY!=actions[1].centerY||actions[0].templateId!=actions[1].templateId);
        }
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    placementSwitches();std::cout<<"Four placement switches: 16 native construction selection cases PASS\n";
}
