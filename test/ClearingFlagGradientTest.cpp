// Exercise resource eligibility through both real clearing-flag gradients.
#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main
#include <cstddef>

namespace {
void fruitIsNeverAClearingTarget() {
    combat_regressions::Fixture f;
    auto* flag=f.building(20,20,0,"clearingflag");
    flag->unitStayRange=6;
    // Fruit IDs 5..7 used to index beyond the five clearing switches into
    // alignment padding. Vary only that padding to reproduce different heap
    // histories (including a fresh load) without changing any game state.
    auto* bytes=reinterpret_cast<unsigned char*>(flag);
    const auto begin=reinterpret_cast<unsigned char*>(flag->clearingResources)-bytes+BASIC_COUNT;
    const auto end=reinterpret_cast<unsigned char*>(&flag->minLevelToFlag)-bytes;
    assert(end>=begin);
    for(int swim=0;swim<2;++swim) {
        flag->globalGradient[swim]=new Uint8[64*64];
        for(unsigned char padding:{0,1}) {
            std::fill(bytes+begin,bytes+end,padding);
            for(int resource:{WOOD,CORN,PAPYRUS,STONE,ALGA,CHERRY,ORANGE,PRUNE,NO_RES_TYPE}) {
                auto& tile=f.game.map.getResource(21,20);
                tile.type=resource;tile.amount=resource==NO_RES_TYPE?0:1;
                for(bool enabled:{false,true}) {
                    if(resource<BASIC_COUNT)flag->clearingResources[resource]=enabled;
                    f.game.map.updateLocalGradient(flag,swim);
                    f.game.map.updateGlobalGradient(flag,swim);
                    const bool expected=resource<BASIC_COUNT && enabled;
                    assert((flag->localGradient[swim][16+15*32]==255)==expected);
                    assert((flag->globalGradient[swim][21+20*64]==255)==expected);
                }
            }
        }
    }
}
}
int main() {
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    fruitIsNeverAClearingTarget();
    std::cout<<"ClearingFlagGradientTest: basic switches, all fruit, empty tiles, padding independence and swimming variants PASS\n";
}
