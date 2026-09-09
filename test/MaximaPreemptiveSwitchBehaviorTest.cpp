// Additional behavioral evidence; link against the unchanged frozen engine.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void preemptiveDefenseSwitch()
{
    for(bool enabled:{false,true})for(bool eligible:{false,true}) {
        combat_regressions::Fixture f;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            f.game.map.setTerrain(x,y,(x>=5&&x<=50&&y>=20&&y<=27)?0:256);
        f.building(10,22,0);
        f.building(42,22,1)->seenByMask|=f.player.team->me;
        auto& a=*f.ai;auto& c=a.context;c.initialize();
        a.strategy.military.preemptive_defense_enabled=enabled;
        a.snapshot.trained_warriors=eligible?100:0;
        a.snapshot.swimming_warriors=0;
        a.finalize_director_plan(c);
        assert(a.budget.preemptive_defense_active==(enabled&&eligible));
        if(a.budget.preemptive_defense_active)a.budget.preemptive_effective_zone_max=1;
        a.budget.preemptive_inner_distance=4;
        a.budget.preemptive_band_width=8;
        a.budget.preemptive_path_slack=2;
        a.budget.preemptive_probe_radius=8;
        a.budget.preemptive_cross_section_max=10;
        a.budget.preemptive_zone_radius=2;
        a.update_preemptive_defense(c);
        const bool expected=enabled&&eligible;
        assert((a.preemptive_diagnostics.selectedCount==1)==expected);
        assert(!c.managementOrders.empty()==expected);
        bool desired=false;for(auto tile:a.preemptive_diagnostics.desired)desired|=tile!=0;
        assert(desired==expected);
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    preemptiveDefenseSwitch();
    std::cout<<"military.preemptive_defense_enabled: paired on/off, trained-warrior eligibility, choke selection and guard orders PASS\n";
}
