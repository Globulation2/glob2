#include "../src/Version.h"
#include <vector>
#include "../src/GlobalContainer.h"
#include "../src/AIMaximaStrategy.h"
#include "../src/GameHeader.h"
#include "../src/BasePlayer.h"
#include "../src/ai/AI.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <set>

GlobalContainer* globalContainer=nullptr;
static std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> parts;
    std::string::size_type start=0;
    while(start<=text.size())
    {
        const std::string::size_type comma=text.find(',',start);
        parts.push_back(text.substr(start,
            comma==std::string::npos ? std::string::npos : comma-start));
        if(comma==std::string::npos) break;
        start=comma+1;
    }
    return parts;
}

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

        // Version 98 retired the muster/relief keys and added the offense's
        // own. An older save therefore names keys this build dropped and omits
        // keys it gained; it must still load, taking every value it recorded
        // and defaulting only what it never held.
        MaximaStrategy legacy=resolved.values;
        legacy.tactics.min_force=1;
        std::string olderText;
        for(const auto& assignment:split(StrategyResolver::canonicalValues(legacy)))
            if(assignment.rfind("tactics.min_force=",0)!=0
               && assignment.rfind("tactics.dwell_ticks=",0)!=0)
                olderText+=(olderText.empty() ? "" : ",")+assignment;
        olderText+=",teamplay.defense_enabled=true,military.counterattack_enabled=false";
        MaximaStrategy older=resolved.values;
        assert(StrategyResolver::restoreValues(olderText,older,error,97));
        // Recorded values win; the keys the save never had keep this build's.
        assert(older.tactics.retarget_margin==legacy.tactics.retarget_margin);
        assert(older.tactics.min_force==resolved.values.tactics.min_force);
        assert(older.tactics.dwell_ticks==resolved.values.tactics.dwell_ticks);
        // Retired keys are tolerated only for a save, never for a live source.
        assert(!StrategyResolver::restoreValues(olderText,older,error,VERSION_MINOR));
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
