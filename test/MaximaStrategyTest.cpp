#include "../src/GlobalContainer.h"
#include "../src/AIMaximaStrategy.h"
#include "../src/GameHeader.h"
#include "../src/ai/AI.h"
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
        assert(StrategyResolver::restoreValues(text,restored,error));
        assert(StrategyResolver::canonicalValues(restored)==text);
        assert(!StrategyResolver::restoreValues("staffing.inn_level1_normal_workers=3",restored,error));
        assert(!StrategyResolver::restoreValues(text+",unknown.key=1",restored,error));
        assert(!StrategyResolver::restoreValues(text+",staffing.inn_level1_normal_workers=3",restored,error));
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
        if(expected) assert(resolved.values.staffing.inn_level1_normal_workers==3);
        else assert(!error.empty());
    };
    check("staffing.inn_level1_normal_workers = 3\n",true);
    check("unknown.key = 3\n",false);
    check("staffing.inn_level1_normal_workers = -1\n",false);
    check("staffing.inn_level1_normal_workers = three\n",false);
    check("staffing.inn_level1_normal_workers = 3\nstaffing.inn_level1_normal_workers = 4\n",false);
    std::filesystem::remove(layer);std::filesystem::remove(directory);
    std::cout<<"strategy defaults, round trips and invalid layers passed\n";
}
