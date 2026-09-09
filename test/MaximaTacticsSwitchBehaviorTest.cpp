#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

static void quarantineSwitch()
{
    using namespace AIMaxima;
    for(bool enabled:{false,true})for(bool parent:{false,true})for(bool siege:{false,true})for(bool expired:{false,true}) {
        combat_regressions::Fixture f;f.building(10,10,0);auto target=f.building(35,30,1);
        for(int i=0;i<12;++i)f.warrior(6+i,6);
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
        a.strategy.tactics.enabled=parent;a.strategy.tactics.siege_enabled=siege;
        a.strategy.tactics.failed_target_quarantine_enabled=enabled;
        a.attack_target_quarantine_until[target->gid]=a.timer+(expired?-1:1000);
        a.plan_tactical_authorization(c);
        assert((a.budget.tactical_kind==Tactics::MissionSiege)==(parent&&siege&&(!enabled||expired)));
        assert(a.tactical_diagnostics.siegeRejections["quarantined"]==(parent&&siege&&enabled&&!expired?1:0));
    }
}
static void digOutSwitch()
{
    for(bool enabled:{false,true})for(bool parent:{false,true})for(bool siege:{false,true})for(bool labor:{false,true}) {
        combat_regressions::Fixture f;f.building(10,10,0);auto target=f.building(35,30,1);
        for(int i=0;i<12;++i)f.warrior(6+i,6)->performance[SWIM]=0;
        // Enclose the target with resources: land and swimming paths are blocked.
        for(int y=27;y<=37;++y)for(int x=32;x<=42;++x)if(x==32||x==42||y==27||y==37)f.game.map.setResource(x,y,WOOD,1);
        auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
        a.strategy.tactics.enabled=parent;a.strategy.tactics.siege_enabled=siege;a.strategy.tactics.dig_out_enabled=enabled;
        a.budget.attack_flags=1;a.budget.attack_clearing_workers=labor?10:0;
        a.opponents[1].alive=true;a.opponents[1].known_buildings=1;a.opponents[1].reachable_buildings=0;a.opponents[1].score=100;
        a.plan_tactical_authorization(c);
        assert(a.budget.tactical_kind==AIMaxima::Tactics::MissionNone);
        assert(a.budget.tactical_dig_out_team==(enabled&&parent&&siege&&labor?1:-1));
    }
}
static void targetLockSwitch()
{
    for(bool enabled:{false,true})for(bool progress:{false,true})for(bool alive:{false,true})for(bool expired:{false,true}) {
        combat_regressions::Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();
        f.player.team->enemies=f.game.teams[1]->me|f.game.teams[2]->me;
        f.game.teams[1]->isAlive=alive;f.game.teams[2]->isAlive=true;
        a.strategy.tactics.siege_target_lock_enabled=enabled;a.finalize_director_plan(c);
        a.opponents[1].alive=alive;a.opponents[1].score=100;
        a.opponents[2].alive=true;a.opponents[2].score=10000;
        a.campaign.target_team=1;a.campaign.buildings_destroyed=progress?1:0;
        a.timer=100000;a.campaign.last_progress_tick=a.timer-(expired?a.budget.tactical_target_lock_ticks+1:0);
        a.target=-1;a.choose_enemy_target(c);
        assert(a.target==(enabled&&progress&&alive&&!expired?1:2));
    }
}
int main()
{
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.buildingsTypes.init();IntBuildingType::init();
    quarantineSwitch();digOutSwitch();targetLockSwitch();
    std::cout<<"Three tactics switches: 48 native quarantine, sealed route and target selection cases PASS\n";
}
