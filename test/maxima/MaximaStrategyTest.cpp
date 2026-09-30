#include "EngineFixtures.h"
#include "../../src/Version.h"
#include <vector>
#include "../../src/GlobalContainer.h"
#include "../../src/ai/maxima/AIMaximaStrategy.h"
#include "../../src/GameHeader.h"
#include "../../src/BasePlayer.h"
#include "../../src/ai/AI.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <set>

namespace
{

}

TEST_SUITE("Maxima.Strategy")
{
	TEST_CASE("formats; canonical values and version gates")
	{
		glob2test::HeadlessGlobals globals;
	    using namespace AIMaxima;
	    REQUIRE((AI::CORTEX==6 && AI::MAXIMA==7));
	    const auto schema=StrategyResolver::schema();
	    std::set<std::string> keys;
	    for(const auto& parameter:schema) REQUIRE(keys.insert(parameter.key).second);
	    for(auto format:{MatchFormatDuel,MatchFormatFfa3,MatchFormatFfa4,MatchFormatFfa5Plus,MatchFormat2v2})
	    {
	        ResolvedStrategy resolved;
	        std::string error;
	        REQUIRE(StrategyResolver::resolveForFormat(StrategyConfigOptions(),format,resolved,error));
	        const auto text=StrategyResolver::canonicalValues(resolved.values);
	        MaximaStrategy restored{};
	        REQUIRE(StrategyResolver::restoreValues(text,restored,error,VERSION_MINOR));
	        REQUIRE(StrategyResolver::canonicalValues(restored)==text);
	        // A save at the current version must be exact, duplicate-free and free
	        // of keys this build does not have.
	        REQUIRE(!StrategyResolver::restoreValues("staffing.control_minimum_workers=3",restored,error,VERSION_MINOR));
	        REQUIRE(!StrategyResolver::restoreValues(text+",unknown.key=1",restored,error,VERSION_MINOR));
	        REQUIRE(!StrategyResolver::restoreValues(text+",staffing.control_minimum_workers=3",restored,error,VERSION_MINOR));

	        REQUIRE(!StrategyResolver::restoreValues(text,restored,error,114));
	        REQUIRE(resolved.values.fruit.enabled);

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
	        REQUIRE(StrategyResolver::inferFormat(header(2,{1,2}))==MatchFormatDuel);
	        REQUIRE(StrategyResolver::inferFormat(header(3,{1,2,3}))==MatchFormatFfa3);
	        REQUIRE(StrategyResolver::inferFormat(header(4,{1,2,3,4}))==MatchFormatFfa4);
	        REQUIRE(StrategyResolver::inferFormat(header(4,{1,1,2,2}))==MatchFormat2v2);
	        REQUIRE(StrategyResolver::inferFormat(header(5,{1,2,3,4,5}))==MatchFormatFfa5Plus);
	        REQUIRE(StrategyResolver::inferFormat(header(6,{1,1,2,2,3,3}))==MatchFormatFfa5Plus);
	    }

	    const auto directory=std::filesystem::temp_directory_path()/"glob2-maxima-strategy-test";
	    std::filesystem::create_directories(directory);
	    const auto layer=directory/"layer.strategy";
	    StrategyConfigOptions options;
	    options.layerFiles.push_back(layer.string());
	    auto check=[&](const char* content,bool expected) {
	        {std::ofstream output(layer); output<<content;}
	        ResolvedStrategy resolved;std::string error;
	        REQUIRE(StrategyResolver::resolveForFormat(options,MatchFormatDuel,resolved,error)==expected);
	        if(expected) REQUIRE(resolved.values.staffing.control_minimum_workers==3);
	        else REQUIRE(!error.empty());
	    };
	    check("staffing.control_minimum_workers = 3\n",true);
	    check("unknown.key = 3\n",false);
	    check("staffing.control_minimum_workers = -1\n",false);
	    check("staffing.control_minimum_workers = three\n",false);
	    check("staffing.control_minimum_workers = 3\nstaffing.control_minimum_workers = 4\n",false);
	    std::filesystem::remove(layer);std::filesystem::remove(directory);
	}
}
