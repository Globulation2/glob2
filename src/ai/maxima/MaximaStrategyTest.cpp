#include "EngineFixtures.h"
#include <string>
#include "Version.h"
#include <vector>
#include "GlobalContainer.h"
#include "../../src/ai/maxima/AIMaximaStrategy.h"
#include "GameHeader.h"
#include "BasePlayer.h"
#include "../../src/ai/AI.h"
#include <fstream>
#include <sstream>
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
	    check("tactics.flag_proximity_weight = -1\n",false);
	    check("tactics.flag_proximity_weight = 101\n",false);
	    check("staffing.control_minimum_workers = 3\ntactics.flag_proximity_weight = 0\n",true);
	    std::filesystem::remove(layer);std::filesystem::remove(directory);
	}
}

TEST_SUITE("Maxima.Strategy")
{
TEST_CASE("legacy strategy absence disables new tactical controls without masking missing old keys")
{
    glob2test::HeadlessGlobals globals;AIMaxima::ResolvedStrategy resolved;std::string error;
    REQUIRE(AIMaxima::StrategyResolver::resolveForFormat(AIMaxima::StrategyConfigOptions(),AIMaxima::MatchFormatFfa5Plus,resolved,error));
    const auto canonical=AIMaxima::StrategyResolver::canonicalValues(resolved.values);
    std::istringstream input(canonical);std::string item,legacy;
    while(std::getline(input,item,','))
        if(item.rfind("tactics.enemy_commitment=",0) && item.rfind("tactics.arrival_grace_ticks=",0)
            && item.rfind("tactics.flag_proximity_weight=",0)
            && item.rfind("forward_support.",0)) {if(!legacy.empty())legacy+=',';legacy+=item;}
    AIMaxima::MaximaStrategy restored{};
    REQUIRE(AIMaxima::StrategyResolver::restoreValues(legacy,restored,error,187));
    CHECK(restored.tactics.enemy_commitment==0);CHECK(restored.tactics.arrival_grace_ticks==0);
    CHECK(restored.tactics.flag_proximity_weight==0);
    CHECK_FALSE(restored.forward_support.inns_enabled);CHECK_FALSE(restored.forward_support.hospitals_enabled);
    CHECK(restored.forward_support.distance_tiles==24);
    CHECK(restored.forward_support.coverage_distance_tiles==40);
    const auto legacyCanonical=AIMaxima::StrategyResolver::canonicalValues(restored);
    CHECK_FALSE(AIMaxima::StrategyResolver::restoreValues(legacy,restored,error,VERSION_MINOR));
    GameHeader header;header.setNumberOfPlayers(1);
    header.getBasePlayer(0)=BasePlayer(0,"Maxima",0,
        BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
    header.setAIConfig(0,legacy);
    REQUIRE(AIMaxima::StrategyResolver::upgradeSavedConfigurations(header,186,error));
    AIMaxima::ResolvedStrategy migrated;
    REQUIRE(AIMaxima::StrategyResolver::resolveForPlayer(header,0,migrated,error));
    CHECK(AIMaxima::StrategyResolver::canonicalValues(migrated.values)==legacyCanonical);
    REQUIRE(AIMaxima::StrategyResolver::upgradeSavedConfigurations(header,VERSION_MINOR,error));
    header.setAIConfig(0,legacy);
    CHECK_FALSE(AIMaxima::StrategyResolver::upgradeSavedConfigurations(header,VERSION_MINOR,error));
    const auto comma=legacy.find(',');REQUIRE(comma!=std::string::npos);
    CHECK_FALSE(AIMaxima::StrategyResolver::restoreValues(legacy.substr(comma+1),restored,error,186));
}

TEST_CASE("new games use the accepted ring40 policy in every format and saved values remain exact")
{
    glob2test::HeadlessGlobals globals;std::string error;
    for(const auto format:{AIMaxima::MatchFormatDuel,AIMaxima::MatchFormat2v2,
        AIMaxima::MatchFormatFfa3,AIMaxima::MatchFormatFfa4,AIMaxima::MatchFormatFfa5Plus})
    {
        AIMaxima::ResolvedStrategy resolved;
        REQUIRE(AIMaxima::StrategyResolver::resolveForFormat(AIMaxima::StrategyConfigOptions(),format,resolved,error));
        CHECK(resolved.values.tactics.enemy_commitment==1);
        CHECK(resolved.values.tactics.arrival_grace_ticks==4000);
        CHECK(resolved.values.tactics.flag_proximity_weight==4);
        CHECK(resolved.values.forward_support.inns_enabled);
        CHECK(resolved.values.forward_support.hospitals_enabled);
        CHECK(resolved.values.forward_support.distance_tiles==40);
        CHECK(resolved.values.forward_support.coverage_distance_tiles==40);
        const auto text=AIMaxima::StrategyResolver::canonicalValues(resolved.values);
        AIMaxima::MaximaStrategy restored{};
        REQUIRE(AIMaxima::StrategyResolver::restoreValues(text,restored,error,VERSION_MINOR));
        CHECK(AIMaxima::StrategyResolver::canonicalValues(restored)==text);
    }
}

TEST_CASE("format188 strategies preserve ring40 and disable absent flag proximity")
{
    glob2test::HeadlessGlobals globals;AIMaxima::ResolvedStrategy resolved;std::string error;
    REQUIRE(AIMaxima::StrategyResolver::resolveForFormat(AIMaxima::StrategyConfigOptions(),
        AIMaxima::MatchFormatFfa5Plus,resolved,error));
    std::istringstream input(AIMaxima::StrategyResolver::canonicalValues(resolved.values));
    std::string item,legacy;
    while(std::getline(input,item,','))
        if(item.rfind("tactics.flag_proximity_weight=",0))
        {if(!legacy.empty())legacy+=',';legacy+=item;}
    AIMaxima::MaximaStrategy restored{};
    REQUIRE(AIMaxima::StrategyResolver::restoreValues(legacy,restored,error,188));
    CHECK(restored.tactics.flag_proximity_weight==0);
    CHECK(restored.tactics.enemy_commitment==1);
    CHECK(restored.tactics.arrival_grace_ticks==4000);
    CHECK(restored.forward_support.distance_tiles==40);
    CHECK(restored.forward_support.inns_enabled);
    CHECK(restored.forward_support.hospitals_enabled);
    GameHeader header;header.setNumberOfPlayers(1);
    header.getBasePlayer(0)=BasePlayer(0,"Maxima",0,
        BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
    header.setAIConfig(0,legacy);
    REQUIRE(AIMaxima::StrategyResolver::upgradeSavedConfigurations(header,188,error));
    AIMaxima::ResolvedStrategy migrated;
    REQUIRE(AIMaxima::StrategyResolver::resolveForPlayer(header,0,migrated,error));
    CHECK(migrated.values.tactics.flag_proximity_weight==0);
    CHECK(migrated.values.forward_support.distance_tiles==40);
    CHECK_FALSE(AIMaxima::StrategyResolver::restoreValues(legacy,restored,error,VERSION_MINOR));
}
}
