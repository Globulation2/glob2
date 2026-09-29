#include "../../src/Version.h"
#include <vector>
#include "../../src/GlobalContainer.h"
#include "../../src/ai/maxima/AIMaximaStrategy.h"
#include "../../src/GameHeader.h"
#include "../../src/BasePlayer.h"
#include "../../src/ai/AI.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <set>

GlobalContainer* globalContainer=nullptr;
int main()
{
    using namespace AIMaxima;
    assert(AI::CORTEX==6 && AI::MAXIMA==7);
    const auto schema=StrategyResolver::schema();
    std::set<std::string> keys;
    for(const auto& parameter:schema) assert(keys.insert(parameter.key).second);
    for(auto format:{MatchFormatDuel,MatchFormatFfa3,MatchFormatFfa4,MatchFormatFfa5Plus,MatchFormat2v2})
    {
        ResolvedStrategy resolved;
        std::string error;
        assert(StrategyResolver::resolveForFormat(StrategyConfigOptions(),format,resolved,error));
        const auto text=StrategyResolver::canonicalValues(resolved.values);
        MaximaStrategy restored{};
        assert(StrategyResolver::restoreValues(text,restored,error,VERSION_MINOR));
        assert(StrategyResolver::canonicalValues(restored)==text);
        // A save at the current version must be exact, duplicate-free and free
        // of keys this build does not have.
        assert(!StrategyResolver::restoreValues("staffing.control_minimum_workers=3",restored,error,VERSION_MINOR));
        assert(!StrategyResolver::restoreValues(text+",unknown.key=1",restored,error,VERSION_MINOR));
        assert(!StrategyResolver::restoreValues(text+",staffing.control_minimum_workers=3",restored,error,VERSION_MINOR));

        assert(!StrategyResolver::restoreValues(text,restored,error,114));
        assert(resolved.values.fruit.enabled);

    }
    {
        // The player count picks the match format, and four players split into
        // two pairs are a 2v2 rather than a free-for-all.
        const auto header=[](int players, const std::vector<int>& allyTeams) {
            GameHeader made;
            made.setNumberOfPlayers(players);
            for(int seat=0; seat<players; ++seat)
            {
                made.getBasePlayer(seat)=BasePlayer(seat,"Maxima",seat,
                    BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
                made.setAllyTeamNumber(seat, allyTeams.at(seat));
            }
            return made;
        };
        assert(StrategyResolver::inferFormat(header(2,{1,2}))==MatchFormatDuel);
        assert(StrategyResolver::inferFormat(header(3,{1,2,3}))==MatchFormatFfa3);
        assert(StrategyResolver::inferFormat(header(4,{1,2,3,4}))==MatchFormatFfa4);
        assert(StrategyResolver::inferFormat(header(4,{1,1,2,2}))==MatchFormat2v2);
        assert(StrategyResolver::inferFormat(header(5,{1,2,3,4,5}))==MatchFormatFfa5Plus);
        assert(StrategyResolver::inferFormat(header(6,{1,1,2,2,3,3}))==MatchFormatFfa5Plus);
    }

    const auto directory=std::filesystem::temp_directory_path()/"glob2-maxima-strategy-test";
    std::filesystem::create_directories(directory);
    const auto layer=directory/"layer.strategy";
    StrategyConfigOptions options;
    options.layerFiles.push_back(layer.string());
    auto check=[&](const char* content,bool expected) {
        {std::ofstream output(layer); output<<content;}
        ResolvedStrategy resolved;std::string error;
        assert(StrategyResolver::resolveForFormat(options,MatchFormatDuel,resolved,error)==expected);
        if(expected) assert(resolved.values.staffing.control_minimum_workers==3);
        else assert(!error.empty());
    };
    check("staffing.control_minimum_workers = 3\n",true);
    check("unknown.key = 3\n",false);
    check("staffing.control_minimum_workers = -1\n",false);
    check("staffing.control_minimum_workers = three\n",false);
    check("staffing.control_minimum_workers = 3\nstaffing.control_minimum_workers = 4\n",false);
    std::filesystem::remove(layer);std::filesystem::remove(directory);
    std::cout<<"strategy defaults, round trips and invalid layers passed\n";
}
