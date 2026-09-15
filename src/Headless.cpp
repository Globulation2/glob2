// SPDX-License-Identifier: GPL-3.0-or-later
#include "Headless.h"
#include "PerformanceTelemetry.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "AINames.h"
#include "AIMaximaStrategy.h"
#include "ai/cortex/CortexTuning.h"
#include "Game.h"
#include "Player.h"
#include "TeamStat.h"
#include "Unit.h"
#include "Version.h"
#include "GenerationRequest.h"
#include "GeneratorRegistry.h"
#include "ReplayWriter.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace Headless
{
std::string quote(const std::string &value)
{
	std::ostringstream out;
	out << '"';
	for (unsigned char c : value)
	{
		if (c == '"' || c == '\\') out << '\\' << c;
		else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
		else out << c;
	}
	out << '"'; return out.str();
}
void writeJson(const std::string &path, const std::string &json)
{
	const std::string temporary = path + ".tmp";
	std::ofstream out(temporary, std::ios::binary);
	out << json << '\n'; out.close();
	if (!out) throw std::runtime_error("cannot write " + temporary);
	fs::rename(temporary, path);
}
}
namespace
{
using Headless::quote;
using Options = std::map<std::string, std::vector<std::string>>;
long long integer(const std::string &text, long long minimum, long long maximum)
{
	size_t used = 0; long long value;
	try { value = std::stoll(text, &used); }
	catch (...) { throw std::invalid_argument("invalid integer: " + text); }
	if (used != text.size() || value < minimum || value > maximum)
		throw std::invalid_argument("integer out of range: " + text);
	return value;
}
std::string one(const Options &options, const std::string &key, const std::string &fallback = "")
{
	auto found = options.find(key);
	if (found == options.end()) return fallback;
	if (found->second.size() != 1) throw std::invalid_argument("duplicate " + key);
	return found->second[0];
}
std::vector<std::string> many(const Options &options, const std::string &key)
{
	auto found = options.find(key); return found == options.end() ? std::vector<std::string>() : found->second;
}
void isolateEnvironment()
{
	// Every engine-affecting diagnostic/tuning environment variable is opt-in in
	// this interface. The legacy entry points retain their environment behavior.
	const char* keys[] = {"GLOB2_CORTEX_TUNING", "GLOB2_MAXIMA_BASE", "GLOB2_MAXIMA_LAYERS",
		"GLOB2_MAXIMA_FORMAT", "GLOB2_MAXIMA_OVERRIDES", "GLOB2_MAXIMA_TEAM_OVERRIDES",
		"GLOB2_MAXIMA_PLAYER_OVERRIDES", "GLOB2_MAXIMA_TUNING", "GLOB2_NICOWAR_V3_OVERRIDES",
		"GLOB2_NICOWAR_V3_TUNING", "GLOB2_MAXIMA_TELEMETRY", "GLOB2_DATASET_PATH",
		"GLOB2_CHECKSUM_SIDECAR", "GLOB2_REPLAY_PATH", "GLOB2_TEAM_TIMELINE", "GLOB2_TEAM_RESULTS",
		"GLOB2_DUMP_GAME", "GLOB2_STUDY_EXPLAIN", "GLOB2_USER_DIR",
		"GLOB2_PERF_DISABLE", "GLOB2_PERF_BUILD_LABEL",
		"GLOB2_CORTEX_POLICY", "GLOB2_CORTEX_NET", "GLOB2_CORTEX_DECISION_NET",
		"GLOB2_CORTEX_TRACE", "GLOB2_CORTEX_DECIDE_TRACE", "GLOB2_CORTEX_INN_TRACE",
		"GLOB2_CHECKSUM_SIDECAR_MAX_TICKS", "CORTEX_DUMP_PERIODIC", "CORTEX_DUMP_OFFENSE",
		"CORTEX_DUMP_ATTACK", "CORTEX_DUMP_GATES", "CORTEX_DUMP_POSTURE", "CORTEX_DUMP_AMPHIB"};
	for (const char* key : keys) SDL_setenv(key, "", 1);
	// getenv-based presence flags need removal, including on Windows.
	for (const char* key : keys)
#ifdef WIN32
		_putenv_s(key, "");
#else
		unsetenv(key);
#endif
}
void setHeadlessEnvironment(const char* key, const char* value)
{
	SDL_setenv(key, value, 1);
#ifdef WIN32
	// The engine reads these flags with the C runtime's getenv. On Windows,
	// SDL's environment update does not repopulate the runtime view after the
	// isolation step removed the key with _putenv_s.
	_putenv_s(key, value);
#endif
}
void jsonArray(std::ostream& out, int value) { out << value; }
template<class T, size_t N> void jsonArray(std::ostream& out, const T (&values)[N])
{
	out << '[';
	for(size_t i=0;i<N;++i){if(i)out<<',';jsonArray(out,values[i]);}
	out << ']';
}
void standardStatistics(std::ostream& out, const TeamStat& stat)
{
	out << '{'; bool comma=false;
#define STAT(name) if(comma)out<<',';comma=true;out<<quote(#name)<<':';jsonArray(out,stat.name)
	STAT(totalUnit); STAT(numberUnitPerType); STAT(totalFree); STAT(isFree);
	STAT(totalNeeded); STAT(totalNeededPerLevel); STAT(totalBuilding);
	STAT(numberBuildingPerType); STAT(numberBuildingPerTypePerLevel);
	STAT(needFoodCritical); STAT(needFoodNoInns); STAT(needFood); STAT(needHeal); STAT(needNothing);
	STAT(upgradeState); STAT(upgradeStatePerType); STAT(totalFood); STAT(totalFoodCapacity);
	STAT(totalUnitFoodable); STAT(totalUnitFooded); STAT(totalHP); STAT(totalAttackPower);
	STAT(totalDefensePower); STAT(happiness);
#undef STAT
	out << '}';
}
void manifest(const fs::path &directory)
{
	std::ostringstream out; out << "{\"schema_version\":1,\"artifacts\":[";
	bool comma=false;
	for (const auto &entry : fs::recursive_directory_iterator(directory))
	{
		const auto name=fs::relative(entry.path(),directory).generic_string();
		if(name.rfind("profile/",0)==0 || name.find("/profile/")!=std::string::npos)continue;
		if (!entry.is_regular_file() || name == "artifacts.json" || entry.path().extension() == ".tmp") continue;
		if(comma) out << ','; comma=true;
		out << "{\"path\":" << quote(name) << ",\"bytes\":" << entry.file_size() << '}';
	}
	out << "]}"; Headless::writeJson((directory / "artifacts.json").string(), out.str());
}
}

struct HeadlessRunner
{
	static int game(const Options &options, const fs::path &output)
	{
		GlobalContainer globals(one(options, "--profile", "glob2-tournament").c_str());
		globalContainer=&globals;
		globals.runNoX=true;
		globals.structuredHeadless=true;
		globals.automaticEndingGame=true;
		globals.automaticGameGlobalEndConditions=true;
		globals.automaticEndingSteps=integer(one(options, "--ticks", "90000"), 1, std::numeric_limits<int>::max());
		globals.headlessReplay=one(options, "--replay", "false") == "true";
		if(options.count("--replay") && one(options,"--replay")!="true" && one(options,"--replay")!="false")
			throw std::invalid_argument("--replay must be true or false");
		const std::string replay=(output/"game.replay").string();
		setHeadlessEnvironment("GLOB2_REPLAY_PATH", replay.c_str());
		for(const auto &telemetry : many(options,"--telemetry"))
		{
			if(telemetry=="checksums") setHeadlessEnvironment("GLOB2_CHECKSUM_SIDECAR", "1");
			else if(telemetry=="team-timeline") setHeadlessEnvironment("GLOB2_TEAM_TIMELINE", "1");
			else if(telemetry=="maxima") setHeadlessEnvironment("GLOB2_MAXIMA_TELEMETRY", "1");
			else throw std::invalid_argument("unknown telemetry: " + telemetry);
		}
		globals.load();
		Engine engine;
		engine.headlessOutput=output.string();
		bool initial=false, final=false;
		for(const auto &save : many(options,"--save"))
		{
			if(save=="initial") initial=true;
			else if(save=="final") final=true;
			else if(save.rfind("every:",0)==0) engine.headlessSaveInterval=integer(save.substr(6),1,std::numeric_limits<int>::max());
			else throw std::invalid_argument("unknown save request: " + save);
		}
		const auto mapFile=one(options,"--map-file"), saved=one(options,"--load-game");
		if(mapFile.empty() == saved.empty()) throw std::invalid_argument("choose exactly one of --map-file and --load-game");
		if(!fs::is_regular_file(mapFile.empty() ? saved : mapFile)) throw std::invalid_argument("input file does not exist");
		if(!saved.empty())
		{
			for(const auto &key : {"--player","--ai-param","--alliance","--win-condition","--game-seed"})
				if(options.count(key)) throw std::invalid_argument(std::string(key)+" cannot override a saved game");
			if(Engine::loadGameHeader(saved).getNumberOfPlayers()==0) throw std::invalid_argument("saved game has no players");
			if(engine.initCustom(saved)!=Engine::EE_NO_ERROR) throw std::invalid_argument("cannot load saved game");
			if(globals.automaticEndingSteps <= int(engine.gui.game.stepCounter)) throw std::invalid_argument("tick limit must exceed the saved tick");
		}
		else
		{
			MapHeader map=Engine::loadMapHeader(mapFile);
			GameHeader header;
			const auto players=many(options,"--player");
			if(players.empty() || players.size()!=size_t(map.getNumberOfTeams()) || players.size()>Team::MAX_COUNT)
				throw std::invalid_argument("one --player AI is required per map team");
			header.setNumberOfPlayers(players.size());
			header.setRandomSeed(integer(one(options,"--game-seed"),0,UINT32_MAX));
			for(size_t p=0;p<players.size();++p)
			{
				int ai=AINames::parseAIName(players[p]);
				if(ai<0) throw std::invalid_argument("unknown AI: " + players[p]);
				header.getBasePlayer(p)=BasePlayer(p,players[p],p,BasePlayer::playerTypeFromImplementationID(AI::ImplementationID(ai)));
			}
			const auto allies=many(options,"--alliance");
			if(!allies.empty() && allies.size()!=players.size()) throw std::invalid_argument("one alliance per team required");
			for(size_t t=0;t<allies.size();++t) header.setAllyTeamNumber(t,integer(allies[t],1,Team::MAX_COUNT));
			const auto conditions=many(options,"--win-condition");
			if(!conditions.empty()) header.getWinningConditions().clear();
			for(const auto &condition : conditions)
			{
				std::shared_ptr<WinningCondition> value;
				if(condition=="death") value=std::make_shared<WinningConditionDeath>();
				else if(condition=="allies") value=std::make_shared<WinningConditionAllies>();
				else if(condition=="prestige") value=std::make_shared<WinningConditionPrestige>();
				else if(condition=="opponents") value=std::make_shared<WinningConditionOpponentsDefeated>();
				else if(condition=="script") value=std::make_shared<WinningConditionScript>();
				else throw std::invalid_argument("unknown winning condition: " + condition);
				header.getWinningConditions().push_back(value);
			}
			std::map<int,std::string> overrides;
			std::set<std::pair<int,std::string>> seen;
			for(const auto &assignment : many(options,"--ai-param"))
			{
				const auto colon=assignment.find(':'), eq=assignment.find('=');
				if(colon==std::string::npos || eq==std::string::npos || eq<=colon+1) throw std::invalid_argument("expected player:key=value");
				int p=integer(assignment.substr(0,colon),0,players.size()-1);
				if(!seen.insert({p,assignment.substr(colon+1,eq-colon-1)}).second) throw std::invalid_argument("duplicate AI override");
				overrides[p]+=assignment.substr(colon+1)+"\n";
			}
			for(size_t p=0;p<players.size();++p)
			{
				const int ai=BasePlayer::implementationIdFromPlayerType(header.getBasePlayer(p).type);
				std::string error;
				if(ai==AI::CORTEX)
				{
					Cortex::CortexTuning values;
					if(!Cortex::applyTuning(values,overrides[p],error)) throw std::invalid_argument(error);
					header.setAIConfig(p,Cortex::tuningValues(values));
				}
				else if(ai==AI::MAXIMA)
				{
					AIMaxima::StrategyConfigOptions config;
					config.inlineOverrides=overrides[p];
					std::replace(config.inlineOverrides.begin(),config.inlineOverrides.end(),'\n',',');
					if(!config.inlineOverrides.empty())config.inlineOverrides.pop_back();
					AIMaxima::ResolvedStrategy resolved;
					if(!AIMaxima::StrategyResolver::resolve(config,&header,resolved,error)) throw std::invalid_argument(error);
					header.setAIConfig(p,AIMaxima::StrategyResolver::canonicalValues(resolved.values));
				}
				else if(!overrides[p].empty()) throw std::invalid_argument("AI has no runtime parameters: " + players[p]);
			}
			engine.gui.localPlayer=0;engine.gui.localTeamNo=0;
			if(engine.initGame(map,header,true,false,false,mapFile)!=Engine::EE_NO_ERROR) throw std::invalid_argument("cannot initialize map");
		}
		if(initial) engine.saveInitialGameStateOrExit((output/"initial.game").string(),"initial",engine.gui.game.mapHeader.getMapName());
		engine.run();
		if(final) engine.saveInitialGameStateOrExit((output/"final.game").string(),"final",engine.gui.game.mapHeader.getMapName());
		PerformanceTelemetry::collector().capture(engine.gui.game.stepCounter, true, true);
		PerformanceTelemetry::collector().reset();
		Game &game=engine.gui.game;
		engine.trackTeamEliminations();
		std::ostringstream result;
		result << "{\"schema_version\":1,\"job_type\":\"game\",\"status\":\"completed\",\"ticks\":" << game.stepCounter
			<< ",\"game_seed\":" << game.gameHeader.getRandomSeed() << ",\"termination\":"
			<< quote(game.isGameEnded || game.totalPrestigeReached ? "engine_end" : "tick_cap")
			<< ",\"resolved\":{\"tick_limit\":" << globals.automaticEndingSteps << ",\"map\":" << quote(game.mapHeader.getMapName())
			<< ",\"save_version\":" << VERSION_MINOR << ",\"winning_conditions\":[";
		bool comma=false;
		for(const auto &c:game.gameHeader.getWinningConditions()) { if(comma)result<<',';comma=true;result<<int(c->getType()); }
		result << "]},\"players\":[";
		for(int p=0;p<game.gameHeader.getNumberOfPlayers();++p)
		{
			const auto &bp=game.gameHeader.getBasePlayer(p);
			if(p) result << ',';
			result << "{\"player\":" << p << ",\"team\":" << bp.teamNumber << ",\"ai\":"
				<< quote(bp.type>=BasePlayer::P_AI ? AINames::getCLIName(BasePlayer::implementationIdFromPlayerType(bp.type)) : "local")
				<< ",\"runtime_values\":" << quote(game.gameHeader.getAIConfig(p)) << '}';
		}
		result << "],\"teams\":[";
		std::set<int> winningAlliances; std::vector<int> winningTeams;
		for(int t=0;t<game.mapHeader.getNumberOfTeams();++t)
		{
			Team *team=game.teams[t];
			int units=0,workers=0,explorers=0,warriors=0,buildings=0,sites=0;
			long long warriorHP=0,warriorAttack=0;
			for(int i=0;i<Unit::MAX_COUNT;++i) if(const Unit *u=team->myUnits[i])
			{
				++units;workers+=u->typeNum==WORKER;explorers+=u->typeNum==EXPLORER;
				if(u->typeNum==WARRIOR){++warriors;warriorHP+=u->hp;warriorAttack+=u->getRealAttackStrength();}
			}
			for(int i=0;i<Building::MAX_COUNT;++i) if(const Building *b=team->myBuildings[i])
				if(!b->type->isVirtual){if(b->type->isBuildingSite)++sites;else ++buildings;}
			int alliance=game.gameHeader.getAllyTeamNumber(t);
			if(team->hasWon){winningTeams.push_back(t);winningAlliances.insert(alliance);}
			if(t) result << ',';
			result << "{\"team\":" << t << ",\"alliance\":" << alliance << ",\"allies_mask\":" << team->allies
				<< ",\"start\":[" << team->startPosX << ',' << team->startPosY << "],\"alive\":" << (team->isAlive?"true":"false")
				<< ",\"outcome\":" << quote(team->hasWon?"won":team->hasLost?"lost":"unresolved")
				<< ",\"eliminated_tick\":" << engine.teamEliminatedTick[t] << ",\"prestige\":" << team->prestige
				<< ",\"units\":" << units << ",\"workers\":" << workers << ",\"explorers\":" << explorers
				<< ",\"warriors\":" << warriors << ",\"warrior_hp\":" << warriorHP << ",\"warrior_attack\":" << warriorAttack
				<< ",\"buildings\":" << buildings << ",\"sites\":" << sites;
			const TeamStat &stats=*team->stats.getLatestStat();
			result << ",\"standard_statistics\":"; standardStatistics(result,stats);
			result << ",\"statistics\":{\"total_units\":" << stats.totalUnit << ",\"total_buildings\":" << stats.totalBuilding
				<< ",\"total_hp\":" << stats.totalHP << ",\"total_attack_power\":" << stats.totalAttackPower
				<< ",\"total_defense_power\":" << stats.totalDefensePower << ",\"food\":" << stats.totalFood
				<< ",\"food_capacity\":" << stats.totalFoodCapacity << ",\"need_food\":" << stats.needFood << "},\"history\":[";
			comma=false;
			for(const auto &stat:team->stats.getEndOfGameStats())
			{
				if(comma)result<<',';comma=true;result<<'[';
				for(int k=0;k<EndOfGameStat::TYPE_NB_STATS;++k){if(k)result<<',';result<<stat.value[k];}
				result<<']';
			}
			result << "]}";
		}
		result << "],\"winning_teams\":[";
		comma=false;for(int t:winningTeams){if(comma)result<<',';comma=true;result<<t;}
		result << "],\"winning_alliances\":[";
		comma=false;for(int t:winningAlliances){if(comma)result<<',';comma=true;result<<t;}
		result << "],\"unresolved\":" << (winningTeams.empty()?"true":"false");
		if(fs::exists(output/"generated/result.json"))
		{
			std::ifstream generation(output/"generated/result.json");
			result << ",\"generation\":" << generation.rdbuf();
		}
		result << '}';
		Headless::writeJson((output/"result.json").string(),result.str());
		return 0;
	}
};

int runHeadlessCommand(int argc,char **argv)
{
	if(argc<2) return -1;
	const std::string command=argv[1];
	if(command!="--headless-catalog" && command!="--run-game" && command!="--generate-map") return -1;
	fs::path output;
	try
	{
		isolateEnvironment();
		if(command=="--headless-catalog")
		{
			if(argc!=2) throw std::invalid_argument("catalog takes no arguments");
			GlobalContainer globals("glob2-tournament-catalog");
			globalContainer=&globals;globals.runNoX=true;
			std::cout << "{\"schema_version\":1,\"save_version\":" << VERSION_MINOR << ",\"protocol_version\":" << NET_PROTOCOL_VERSION
				<< ",\"map_report_version\":2,\"generation_telemetry_version\":1,\"gameplay_telemetry_version\":2,\"ai_telemetry_version\":1,\"performance_telemetry_version\":1,\"commands\":[\"game\",\"generate_map\"],\"telemetry\":[\"checksums\",\"team-timeline\",\"maxima\"],\"ais\":[";
			bool comma=false;
			for(int ai:AINames::selectionOrder())
			{
				if(ai==AI::NONE)continue;if(comma)std::cout<<',';comma=true;
				std::string name=AINames::getCLIName(ai);std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
				std::cout << "{\"id\":" << ai << ",\"name\":" << quote(name) << ",\"parameters\":";
				if(ai==AI::CORTEX)std::cout<<Cortex::tuningSchemaJson();
				else if(ai==AI::MAXIMA)std::cout<<AIMaxima::StrategyResolver::schemaJson();
				else std::cout<<"[]";
				std::cout<<'}';
			}
			std::cout << "],\"generators\":" << std::flush;
			char a[]="study",b[]="--catalog";char *args[]={a,b};runMapStudy(2,args);
			std::cout << "}" << std::endl;return 0;
		}
		const std::set<std::string> common={"--output-dir","--profile"};
		const std::set<std::string> gameKeys={"--map-file","--load-game","--game-seed","--player","--ai-param","--alliance","--win-condition","--ticks","--save","--telemetry","--replay","--generator","--map-seed","--param","--candidates"};
		const std::set<std::string> mapKeys={"--generator","--map-seed","--param","--candidates","--rotations","--write-map","--report"};
		Options options;
		for(int i=2;i<argc;++i)
		{
			std::string key=argv[i];
			if(!common.count(key) && !(command=="--run-game"?gameKeys:mapKeys).count(key)) throw std::invalid_argument("unknown option: " + key);
			if(++i>=argc)throw std::invalid_argument("missing value for " + key);
			options[key].push_back(argv[i]);
		}
		if(one(options,"--output-dir").empty())throw std::invalid_argument("--output-dir is required");
		output=fs::absolute(one(options,"--output-dir"));
		fs::create_directories(output);
		fs::create_directories(output / "profile");
		SDL_setenv("GLOB2_USER_DIR", (output / "profile").string().c_str(), 1);
		if(fs::exists(output/"result.json")) throw std::invalid_argument("output directory already contains a result");
		int code;
		if(command=="--run-game")
		{
			if(options.count("--generator"))
			{
				if(options.count("--map-file") || options.count("--load-game")) throw std::invalid_argument("generator conflicts with file input");
				std::vector<std::string> generation={"glob2","--generate-map","--output-dir",(output/"generated").string(),"--write-map","true"};
				for(const auto &key : {"--generator","--map-seed","--param","--candidates"})
				{
					for(const auto &value : many(options,key)){generation.push_back(key);generation.push_back(value);}
					options.erase(key);
				}
				std::vector<char*> raw;for(auto &value:generation)raw.push_back(&value[0]);
				const int generated=runHeadlessCommand(raw.size(),raw.data());
				if(generated!=0)
				{
					if(fs::exists(output/"generated/result.json")) fs::copy_file(output/"generated/result.json",output/"result.json");
					manifest(output);return generated;
				}
				options["--map-file"]={(output/"generated/map-r0.map").string()};
				SDL_setenv("GLOB2_USER_DIR",(output/"profile").string().c_str(),1);
			}
			else if(options.count("--map-seed") || options.count("--param") || options.count("--candidates"))
				throw std::invalid_argument("generator options require --generator");
			code=HeadlessRunner::game(options,output);
		}
		else
		{
			int method=integer(one(options,"--generator"),0,INT32_MAX);
			if(!GeneratorRegistry::builtins().find(method))throw std::invalid_argument("unknown generator");
			integer(one(options,"--map-seed"),0,UINT32_MAX);
			std::vector<std::string> args={"study",std::to_string(method),one(options,"--map-seed"),one(options,"--profile","glob2-tournament"),"tuning","quality","result="+(output/"result.json").string()};
			std::set<std::string> seen;
			for(const auto &param:many(options,"--param"))
			{
				auto eq=param.find('=');
				if(eq==std::string::npos || !seen.insert(param.substr(0,eq)).second)throw std::invalid_argument("invalid or duplicate generator parameter");
				integer(param.substr(eq+1),INT32_MIN,INT32_MAX);
				const auto key=param.substr(0,eq);
				bool known=key=="width"||key=="height"||key=="teams"||key=="workers";
				for(const auto &c:GenerationRequest::controls(method))known=known||c.id==key;
				if(!known)throw std::invalid_argument("unknown generator parameter: " + key);
				args.push_back(param);
			}
			args.push_back("candidates="+std::to_string(integer(one(options,"--candidates","0"),0,10000)));
			args.push_back("rotations="+std::to_string(integer(one(options,"--rotations","1"),1,Team::MAX_COUNT)));
			const auto write=one(options,"--write-map","false");
			if(write!="true"&&write!="false")throw std::invalid_argument("--write-map must be true or false");
			if(write=="true")args.push_back("save="+(output/"map").string());
			for(const auto &report:many(options,"--report"))
				if(report=="headroom")args.push_back(report);
				else if(report=="terrain")args.push_back("dump="+(output/"terrain.txt").string());
				else throw std::invalid_argument("unknown report: " + report);
			Headless::writeJson((output/"progress.json").string(),"{\"schema_version\":1,\"stage\":\"generation\"}");
			std::vector<char*> raw;for(auto &arg:args)raw.push_back(&arg[0]);
			code=runMapStudy(raw.size(),raw.data());
		}
		manifest(output);return code;
	}
	catch(const std::exception &error)
	{
		std::cerr << error.what() << std::endl;
		const bool invalid=dynamic_cast<const std::invalid_argument*>(&error)!=nullptr;
		const std::string status=invalid ? "invalid_request" : "artifact_failure";
		if(!output.empty() && !fs::exists(output/"result.json"))
			try { Headless::writeJson((output/"result.json").string(),"{\"schema_version\":1,\"status\":"+quote(status)+",\"diagnostic\":"+quote(error.what())+"}");manifest(output); } catch(...) {}
		return invalid ? 2 : 3;
	}
}
