// SPDX-License-Identifier: GPL-3.0-or-later
#include <Environment.h>
#include "Headless.h"
#include "ResourceGrowth.h"
#include <utility>
#include "scripting/javascript/ScriptCommand.h"
#include "scripting/javascript/ScriptRuntime.h"
#include "scripting/javascript/ScriptValue.h"
#include "PerformanceTelemetry.h"
#include <array>
#include <bit>
#include "Engine.h"
#include "GameDiagnostics.h"
#include "GlobalContainer.h"
#include "BuildingArtwork.h"
#include "Sha256.h"
#include "AINames.h"
#include "AIJavaScript.h"
#include "ComputeThreads.h"
#include "AIMaximaStrategy.h"
#include "ai/cortex/CortexTuning.h"
#include "Game.h"
#include "gradient/BuildingGradientStats.h"
#include "GameRuleOverrides.h"
#include "Player.h"
#include "TeamStat.h"
#include "Unit.h"
#include "Version.h"
#include "GenerationRequest.h"
#include "GeneratorRegistry.h"
#include "ReplayWriter.h"
#include "SimVersion.h"
#include <nlohmann/json.hpp>
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
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
		"GLOB2_CHECKSUM_SIDECAR", "GLOB2_REPLAY_PATH", "GLOB2_TEAM_TIMELINE", "GLOB2_TEAM_RESULTS", "GLOB2_GRADIENT_STATS",
		"GLOB2_DUMP_GAME", "GLOB2_STUDY_EXPLAIN", "GLOB2_USER_DIR", "GLOB2_USER_DATA_DIR",
		"GLOB2_PERF_DISABLE", "GLOB2_PERF_BUILD_LABEL",
		"GLOB2_CORTEX_POLICY", "GLOB2_CORTEX_NET", "GLOB2_CORTEX_DECISION_NET",
		"GLOB2_CORTEX_TRACE", "GLOB2_CORTEX_DECIDE_TRACE", "GLOB2_CORTEX_INN_TRACE",
		"GLOB2_CHECKSUM_SIDECAR_MAX_TICKS", "CORTEX_DUMP_PERIODIC", "CORTEX_DUMP_OFFENSE",
		"CORTEX_DUMP_ATTACK", "CORTEX_DUMP_GATES", "CORTEX_DUMP_POSTURE", "CORTEX_DUMP_AMPHIB"};
	for (const char* key : keys) SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), key);
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
	GAGCore::setProcessEnvironment(key, value, 1);
#ifdef WIN32
	// The engine reads these flags with the C runtime's getenv. On Windows,
	// SDL's environment update does not repopulate the runtime view after the
	// isolation step removed the key with _putenv_s.
	_putenv_s(key, value);
#endif
}
uint64_t processCpuNs()
{
#ifdef WIN32
	FILETIME created, exited, kernel, user;
	if (!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
		throw std::runtime_error("Cannot read process CPU time");
	ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
	u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
	return (k.QuadPart+u.QuadPart)*100;
#elif defined(CLOCK_PROCESS_CPUTIME_ID)
	timespec time{};
	if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&time)!=0) throw std::runtime_error("Cannot read process CPU time");
	return uint64_t(time.tv_sec)*1000000000+time.tv_nsec;
#else
	return uint64_t(std::clock())*1000000000/CLOCKS_PER_SEC;
#endif
}
void jsonArray(std::ostream& out, int value) { out << value; }
void jsonArray(std::ostream& out, const std::vector<int>& values)
{
	out << '[';
	for (size_t i=0; i<values.size(); ++i) { if(i) out << ','; out << values[i]; }
	out << ']';
}
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
	STAT(workersByConstructionLevel); STAT(buildingCountByVariant); STAT(numberBuildingPerType); STAT(numberBuildingPerTypePerLevel);
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

void Headless::writeManifest(const std::string &directory)
{
	manifest(fs::path(directory));
}

void Headless::playersAndTeamsJson(std::ostream &result, Game &game, const std::vector<Sint32> &eliminatedTicks)
{
	bool comma=false;
	result << "\"players\":[";
	for(int p=0;p<game.gameHeader.getNumberOfPlayers();++p)
	{
		const auto &bp=game.gameHeader.getBasePlayer(p);
		if(p) result << ',';
		result << "{\"player\":" << p << ",\"team\":" << bp.teamNumber << ",\"ai\":"
			<< quote(bp.type>=BasePlayer::P_AI ? AINames::getCLIName(BasePlayer::implementationIdFromPlayerType(bp.type)) : bp.type==BasePlayer::P_IP ? "human" : "local")
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
			<< ",\"eliminated_tick\":" << eliminatedTicks[t] << ",\"prestige\":" << team->prestige
			<< ",\"units\":" << units << ",\"workers\":" << workers << ",\"explorers\":" << explorers
			<< ",\"warriors\":" << warriors << ",\"warrior_hp\":" << warriorHP << ",\"warrior_attack\":" << warriorAttack
			<< ",\"buildings\":" << buildings << ",\"sites\":" << sites;
		const TeamStat &stats=*std::as_const(team->stats).getLatestStat();
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
}

struct HeadlessRunner
{
	static int game(const Options &options, const fs::path &output)
	{
		const auto setupStart = std::chrono::steady_clock::now();
		const bool benchmark=options.count("--benchmark-warmup")!=0;
		const auto setupCpuStart=benchmark?processCpuNs():0;
		const unsigned benchmarkWarmup=integer(one(options,"--benchmark-warmup","0"),0,std::numeric_limits<int>::max());
		const auto fields = one(options,"--diagnostic-fields");
		if (!fields.empty() && fields != "maxima") throw std::invalid_argument("Expected --diagnostic-fields maxima");
		if (fields.empty() && (options.count("--diagnostic-interval") || options.count("--diagnostic-png")))
			throw std::invalid_argument("Diagnostic options require --diagnostic-fields maxima");
		const auto diagnosticInterval = unsigned(integer(one(options,"--diagnostic-interval",std::to_string(Cli::DefaultDiagnosticInterval)),1,std::numeric_limits<int>::max()));
		const auto diagnosticPng = one(options,"--diagnostic-png","false");
		if (diagnosticPng != "true" && diagnosticPng != "false") throw std::invalid_argument("Expected --diagnostic-png true|false");
		if (diagnosticPng == "true")
		{
			setHeadlessEnvironment("SDL_VIDEODRIVER","dummy");
			setHeadlessEnvironment("SDL_AUDIODRIVER","dummy");
		}
		const unsigned gradientDelay = integer(one(options, "--gradient-delay", std::to_string(Cli::DefaultGradientDelay)), 1, 16);
		GlobalContainer globals(one(options, "--profile", "glob2-tournament").c_str(), one(options, "--building-catalog"));
		globalContainer=&globals;
        for(const auto &directory:many(options,"--data-dir"))globals.fileManager->addDir(directory);
		globals.runNoX=true;
		globals.structuredHeadless=true;
		globals.automaticEndingGame=true;
		globals.automaticGameGlobalEndConditions=true;
		globals.automaticEndingSteps=integer(one(options, "--ticks", std::to_string(Cli::DefaultTicks)), 1, std::numeric_limits<int>::max());
		const bool recordReplay=one(options, "--replay", "false") == "true";
		globals.headlessReplay=false; // Write the replay header after configuring the saved schedule.
		if(options.count("--replay") && one(options,"--replay")!="true" && one(options,"--replay")!="false")
			throw std::invalid_argument("--replay must be true or false");
		const std::string replay=(output/"game.replay").string();
		setHeadlessEnvironment("GLOB2_REPLAY_PATH", replay.c_str());
		for(const auto &telemetry : many(options,"--telemetry"))
		{
			if(telemetry=="checksums") setHeadlessEnvironment("GLOB2_CHECKSUM_SIDECAR", "1");
			else if(telemetry=="team-timeline") setHeadlessEnvironment("GLOB2_TEAM_TIMELINE", "1");
			else if(telemetry=="maxima") setHeadlessEnvironment("GLOB2_MAXIMA_TELEMETRY", "1");
			else if(telemetry=="gradient-stats") setHeadlessEnvironment("GLOB2_GRADIENT_STATS", "1");
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
		auto mapFile=one(options,"--map-file"); auto saved=one(options,"--load-game");
		std::vector<std::string> forkSettings;
		if(saved.empty() && options.count("--fork-rule"))
			throw std::invalid_argument("--fork-rule requires --load-game");
		if(mapFile.empty() == saved.empty()) throw std::invalid_argument("choose exactly one of --map-file and --load-game");
		auto &requested = mapFile.empty() ? saved : mapFile;
		// A bare ".map"/".game" path prefers an existing ".gz" sibling, matching how
		// the loaders resolve the same path a few lines below.
		if(!glob2IsGzipPath(requested) && fs::exists(requested+".gz")) requested+=".gz";
		if(!fs::is_regular_file(requested)) throw std::invalid_argument("input file does not exist");
		if(!saved.empty())
		{
			for(const auto &key : {"--player","--ai-param","--ai-script","--map-script","--alliance","--win-condition","--game-seed","--experiment","--rule","--ai-order-delay"})
				if(options.count(key)) throw std::invalid_argument(std::string(key)+" cannot override a saved game");
			if(engine.initCustom(saved)!=Engine::EE_NO_ERROR) throw std::invalid_argument("cannot load saved game");
			if(globals.automaticEndingSteps <= int(engine.gui.game.stepCounter)) throw std::invalid_argument("tick limit must exceed the saved tick");
			// An explicit fork, never a silent continuation: the loaded match's
			// rules change before its first tick, the recorded replay starts
			// here and result.json lists the fork. Only settings whose change
			// needs no pending work to be remapped are accepted.
			auto& header=engine.gui.game.gameHeader;
			for(const auto &rule : many(options,"--fork-rule"))
			{
				if(rule.rfind("buildingGradientDelay=",0)!=0) throw std::invalid_argument("--fork-rule accepts only buildingGradientDelay=N");
				applyGameRule(header, rule);
				forkSettings.push_back("rule:"+rule);
			}
			const auto buildings=engine.gui.game.map.buildingGradientPipelineStatus();
			if(!forkSettings.empty() && (buildings.pending || buildings.queued))
				throw std::invalid_argument("cannot fork a saved game with pending building gradients");
			// Apply the forked delay now, so a save before the first tick records it.
			engine.gui.game.map.ensureBuildingGradientPipeline();
		}
		else
		{
			MapHeader map=Engine::loadMapHeader(mapFile);
			GameHeader header;
			// Catalog-declared experiment keys belong to the received map even
			// when this installation has no matching authoring definitions.
			header.setBuildingCatalogSnapshot(Engine::loadGameHeader(mapFile).getBuildingCatalogSnapshot());
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
			// Structured runs are explicit: the profile's Settings > Experiments do
			// not apply here, only --experiment does.
			for(const auto &key : many(options,"--experiment"))
			{
				if(!knownExperimentKey(key, header.catalogExperimentKeys())) throw std::invalid_argument("unknown experiment: " + key);
				header.getExperiments().set(key, true, header.catalogExperimentKeys());
			}
			// Added to the list in force rather than replacing it, so a real
			// elimination or prestige win still ends the game first and only an
			// otherwise-undecided match is stopped early.
			if(options.count("--win-probability"))
			{
				const int permille=integer(one(options,"--win-probability","0"), 501, 1000);
				WinningCondition::setWinProbabilityWinCondition(header.getWinningConditions(),
					std::optional<Uint32>(static_cast<Uint32>(permille)));
			}
			for (const auto& rule:many(options,"--rule")) applyGameRule(header, rule);
			if (options.count("--ai-order-delay")) header.setAIOrderDelay(integer(one(options,"--ai-order-delay"), 0, 8));
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
			std::set<int> scriptedPlayers;
			for(const auto& assignment:many(options,"--ai-script"))
			{
				const auto colon=assignment.find(':');if(colon==std::string::npos)throw std::invalid_argument("Expected --ai-script player:source.js");
				int p=integer(assignment.substr(0,colon),0,players.size()-1);
				if(!scriptedPlayers.insert(p).second || BasePlayer::implementationIdFromPlayerType(header.getBasePlayer(p).type)!=AI::JAVASCRIPT)throw std::invalid_argument("AI script requires a unique JavaScript player");
				const auto source = Script::readSource(assignment.substr(colon + 1));
				header.setAIConfig(p, Script::config(source, Script::inspectAI(source).apiVersion));
			}
			for(size_t p=0;p<players.size();++p)if(BasePlayer::implementationIdFromPlayerType(header.getBasePlayer(p).type)==AI::JAVASCRIPT && !scriptedPlayers.count(p))throw std::invalid_argument("JavaScript player requires --ai-script");
			engine.gui.localPlayer=0;engine.gui.localTeamNo=0;
			if(engine.initGame(map,header,true,false,false,mapFile)!=Engine::EE_NO_ERROR) throw std::invalid_argument("cannot initialize map");
		}
		if(options.count("--map-script"))
		{
			auto& script=engine.gui.game.mapscript;script.setMapScriptMode(MapScript::JavaScript);script.setMapScript(Script::readSource(one(options,"--map-script")));if(!script.compileCode())throw std::invalid_argument(script.getError().getMessage());
		}
		if (!fields.empty()) engine.diagnostics = std::make_shared<GameDiagnostics::Session>(engine.gui.game,(output/"diagnostics").string(),diagnosticInterval,diagnosticPng=="true");
		const std::string requestedSizing = one(options, "--compute-threads", "auto");
		const unsigned computeThreads = resolveComputeThreadCount(parseComputeThreadCount(requestedSizing));
		engine.gui.game.map.configureCompute(computeThreads);
        engine.gui.game.map.setResourceGrowthDelay(integer(
            one(options, "--resource-growth-delay", std::to_string(engine.gui.game.map.resourceGrowthDelay())), 1, 16));
		const auto pipeline = engine.gui.game.map.gradientPipelineStatus();
		if (!pipeline.enabled) engine.gui.game.map.configureGradientPipeline(1, gradientDelay);
		else {
			if (options.count("--gradient-delay") && gradientDelay != pipeline.delay) {
				if (pipeline.pending) throw std::invalid_argument("cannot change the delay with pending gradients");
				engine.gui.game.map.configureGradientPipeline(1, gradientDelay);
			}
			engine.gui.game.map.setGradientWorkerCount(1);
		}
		globals.headlessReplay=recordReplay;
		if(recordReplay) {
			globals.replayWriter=std::make_unique<ReplayWriter>();
			globals.replayWriter->init(replay, engine.gui);
		}
		if(initial) engine.saveInitialGameStateOrExit((output/"initial.game").string(),"initial",engine.gui.game.mapHeader.getMapName());
		for (int i = 0; i < engine.gui.game.gameHeader.getNumberOfPlayers(); ++i)
		{
			auto *player = engine.gui.game.players[i];
			if (player && player->ai && player->ai->implementationID == AI::JAVASCRIPT)
				static_cast<AIJavaScript *>(player->ai->aiImplementation)->enableValidationReporting();
		}
		const auto initialChecksum = engine.gui.game.checkSum(nullptr, nullptr, nullptr, true);
		const auto runStart = std::chrono::steady_clock::now();
		uint64_t setupCpu=0,runCpu=0,measureStart=0;
		unsigned measuredTicks=0;
        std::array<Uint64,64> tickHistogram{};
        std::vector<Uint64> tickDurations;
		if(benchmark)
		{
			const uint64_t first=engine.gui.game.stepCounter;
			const uint64_t start=first+benchmarkWarmup;
			if(start>=uint64_t(globals.automaticEndingSteps)) throw std::invalid_argument("benchmark warmup must leave measured ticks");
			setupCpu=processCpuNs()-setupCpuStart;
			engine.prepareRun(); engine.beginSession(SDL_GetTicks());
			if(benchmarkWarmup==0) measureStart=processCpuNs();
			while(engine.gui.isRunning)
			{
                const auto beforeTick=engine.gui.game.stepCounter;
                const auto tickStart=std::chrono::steady_clock::now();
				engine.stepSession(SDL_GetTicks()); engine.drawSession();
                if(beforeTick>=start && engine.gui.game.stepCounter>beforeTick) {
                    const auto duration=Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-tickStart).count());
                    ++tickHistogram[std::min<unsigned>(std::bit_width(duration),63)];
                    tickDurations.push_back(duration);
                }
				if(!measureStart && engine.gui.game.stepCounter>=start) measureStart=processCpuNs();
			}
			engine.finishSession();
			engine.gui.game.map.finishGradientPipeline(); engine.gui.game.map.finishResourceGrowth();
			if(!measureStart || engine.gui.game.stepCounter<=start) throw std::runtime_error("game ended before benchmark measurement");
			runCpu=processCpuNs()-measureStart;
			measuredTicks=engine.gui.game.stepCounter-start;
		}
		else { engine.run(); engine.gui.game.map.finishGradientPipeline(); engine.gui.game.map.finishResourceGrowth(); }
		if (engine.diagnostics) engine.diagnostics->finish();
		const auto runEnd = std::chrono::steady_clock::now();
		const auto saveCpuStart=benchmark?processCpuNs():0;
		if(final) engine.saveInitialGameStateOrExit((output/"final.game").string(),"final",engine.gui.game.mapHeader.getMapName());
		const auto saveCpu=benchmark?processCpuNs()-saveCpuStart:0;
		PerformanceTelemetry::collector().capture(engine.gui.game.stepCounter, true, true);
		PerformanceTelemetry::collector().reset();
		Game &game=engine.gui.game;
		const auto pipelineResult = game.map.gradientPipelineStatus();
		const auto buildingResult = game.map.buildingGradientPipelineStatus();
        std::sort(tickDurations.begin(),tickDurations.end());
        const auto percentile=[&](unsigned p)->Uint64 {return tickDurations.empty()?0:tickDurations[(tickDurations.size()-1)*p/100];};
		engine.trackTeamEliminations();
		std::ostringstream result;
		// A game the win probability model called is reported distinctly from one
		// the rules actually decided. The two are not the same evidence: analysis
		// may legitimately pool them or exclude them, but it must never mistake a
		// model's opinion for an engine-declared win.
		const char *termination="tick_cap";
		if(game.isGameEnded || game.totalPrestigeReached)
		{
			termination="engine_end";
			for(int t=0;t<game.teamsCount();++t)
				if(game.teams[t] && game.teams[t]->winCondition==WCWinProbability)
					termination="win_probability";
		}
		result << "{\"schema_version\":1,\"job_type\":\"game\",\"status\":\"completed\",\"ticks\":" << game.stepCounter
			<< ",\"initialChecksum\":" << initialChecksum
			<< ",\"finalChecksum\":" << game.checkSum(nullptr, nullptr, nullptr, true)
			<< ",\"benchmark_setup_cpu_ns\":" << setupCpu
			<< ",\"benchmark_run_cpu_ns\":" << runCpu
			<< ",\"benchmark_save_cpu_ns\":" << saveCpu
			<< ",\"benchmark_measured_ticks\":" << measuredTicks
			<< ",\"setup_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(runStart - setupStart).count()
			<< ",\"run_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(runEnd - runStart).count()
			<< ",\"growth_submitted\":" << game.map.resourceGrowthMetrics().submitted
			<< ",\"growth_published\":" << game.map.resourceGrowthMetrics().published
			<< ",\"growth_sampled\":" << game.map.resourceGrowthMetrics().sampled
			<< ",\"growth_proposals\":" << game.map.resourceGrowthMetrics().proposals
			<< ",\"growth_publishedProposals\":" << game.map.resourceGrowthMetrics().publishedProposals
			<< ",\"growth_accepted\":" << game.map.resourceGrowthMetrics().accepted
			<< ",\"growth_rejected\":" << game.map.resourceGrowthMetrics().rejected
			<< ",\"growth_clamped\":" << game.map.resourceGrowthMetrics().clamped
			<< ",\"growth_stockAdded\":" << game.map.resourceGrowthMetrics().stockAdded
			<< ",\"growth_tilesAdded\":" << game.map.resourceGrowthMetrics().tilesAdded
			<< ",\"growth_capacityGrowthBatches\":" << game.map.resourceGrowthMetrics().capacityGrowthBatches
			<< ",\"growth_maxProposals\":" << game.map.resourceGrowthMetrics().maxProposals
			<< ",\"growth_maxPending\":" << game.map.resourceGrowthMetrics().maxPending
			<< ",\"growth_maxProposalBytes\":" << game.map.resourceGrowthMetrics().maxProposalBytes
			<< ",\"growth_computeNs\":" << game.map.resourceGrowthMetrics().computeNs
			<< ",\"growth_queueNs\":" << game.map.resourceGrowthMetrics().queueNs
			<< ",\"growth_waitNs\":" << game.map.resourceGrowthMetrics().waitNs
			<< ",\"growth_publicationNs\":" << game.map.resourceGrowthMetrics().publicationNs
			<< ",\"growth_delay\":" << game.map.resourceGrowthDelay()
            << ",\"tick_p50_ns\":" << percentile(50)
            << ",\"tick_p95_ns\":" << percentile(95)
            << ",\"tick_p99_ns\":" << percentile(99)
			<< ",\"gradient_pipeline\":" << "true"
			<< ",\"gradient_workers\":" << pipelineResult.workers
			<< ",\"gradient_delay\":" << pipelineResult.delay
			<< ",\"gradient_jobs\":" << pipelineResult.jobs
			<< ",\"gradient_published\":" << pipelineResult.published
			<< ",\"gradient_discarded\":" << pipelineResult.discarded
			<< ",\"gradient_max_pending\":" << pipelineResult.maxPending
			<< ",\"gradient_wait_ns\":" << pipelineResult.waitNs
			<< ",\"gradient_preparation_ns\":" << pipelineResult.preparationNs
			<< ",\"gradient_active_elapsed_ns\":" << pipelineResult.activeElapsedNs
			<< ",\"compute_active_elapsed_ns\":" << game.map.computeExecutor().activeNs()
			<< ",\"building_gradient_jobs\":" << buildingResult.jobs
			<< ",\"building_gradient_published\":" << buildingResult.published
			<< ",\"building_gradient_discarded\":" << buildingResult.discarded
			<< ",\"building_gradient_synchronous\":" << buildingResult.synchronous
			<< ",\"building_gradient_max_pending\":" << buildingResult.maxPending
			<< ",\"building_gradient_wait_ns\":" << buildingResult.waitNs
			<< ",\"building_gradient_pending\":" << buildingResult.pending
			<< ",\"building_gradient_synchronous_by_reason\":{";
		for (std::size_t reason = 0; reason < buildingResult.synchronousByReason.size(); ++reason)
			result << (reason ? "," : "") << quote(Map::buildingSyncReasonName(Map::BuildingSyncReason(reason)))
				<< ':' << buildingResult.synchronousByReason[reason];
		result << '}';
		if (auto *stats = game.map.gradientStats.get())
		{
			// Diagnostics only: close the live field lifetimes and export them.
			stats->finish(game);
			result << ",\"building_gradient\":";
			stats->writeJson(result);
			std::ofstream csv(output/"gradient-stats.csv");
			stats->writeCsv(csv);
			if (!csv) throw std::runtime_error("cannot write gradient-stats.csv");
		}
		result
			<< ",\"compute_threads\":" << game.map.computeExecutor().threadCount()
			<< ",\"compute_requested_threads\":" << quote(requestedSizing)
			<< ",\"compute_resolved_threads\":" << computeThreads
			<< ",\"compute_workers\":" << game.map.computeExecutor().threadCount() - 1
			<< ",\"compute_batches\":" << game.map.computeExecutor().metrics().batches
			<< ",\"compute_jobs\":" << game.map.computeExecutor().metrics().jobs
			<< ",\"compute_parallel_batches\":" << game.map.computeExecutor().metrics().parallelBatches
			<< ",\"compute_batch_ns\":" << game.map.computeExecutor().metrics().batchNs
			<< ",\"compute_wait_ns\":" << game.map.computeExecutor().metrics().waitNs
			<< ",\"compute_deferred_batches\":" << game.map.computeExecutor().metrics().deferredBatches
			<< ",\"compute_deferred_jobs\":" << game.map.computeExecutor().metrics().deferredJobs
			<< ",\"compute_owner_jobs\":" << game.map.computeExecutor().metrics().ownerJobs
			<< ",\"compute_worker_jobs\":" << game.map.computeExecutor().metrics().workerJobs
			<< ",\"compute_join_wait_ns\":" << game.map.computeExecutor().metrics().joinWaitNs
			<< ",\"ai_pipeline\":{";
		bool metricComma=false;
		for(const auto& [name,value]:game.aiMetrics()) {if(metricComma)result<<',';metricComma=true;result<<quote(name)<<':'<<value;}
        result << "},\"benchmark_tick_histogram\":[";
        for(unsigned i=0;i<tickHistogram.size();++i) {
            if(i) result<<',';
            result<<"{\"upper_ns_exclusive\":";
            if(i==63)result<<"null";else result<<(Uint64(1)<<i);
            result<<",\"count\":"<<tickHistogram[i]<<'}';
        }
        result << ']'
			<< ",\"game_seed\":" << game.gameHeader.getRandomSeed() << ",\"termination\":"
			<< quote(termination)
			<< ",\"resolved\":{\"tick_limit\":" << globals.automaticEndingSteps << ",\"map\":" << quote(game.mapHeader.getMapName())
			<< ",\"save_version\":" << VERSION_MINOR << ",\"winning_conditions\":[";
		bool comma=false;
		for(const auto &c:game.gameHeader.getWinningConditions()) { if(comma)result<<',';comma=true;result<<int(c->getType()); }
		result << "],\"experiments\":[";
		comma=false;
		for(const auto &key:game.gameHeader.getExperiments().keys()) { if(comma)result<<',';comma=true;result<<quote(key); }
		// Results must describe the effective header so a variant can be reproduced
		// even when its initial state came from a saved game.
		result << "],\"rules\":{";
		comma=false;
		for(const auto& [name,value]:gameRuleValues(game.gameHeader))
		{ if(comma)result<<','; comma=true; result<<quote(name)<<':'<<value; }
		result << "}";
		if(!saved.empty())
		{
			result << ",\"fork\":[";
			comma=false;
			for(const auto& setting:forkSettings) { if(comma)result<<','; comma=true; result<<quote(setting); }
			result << ']';
		}
		result << "},";
		Headless::playersAndTeamsJson(result, game, engine.teamEliminatedTick);
		result << ",\"javascriptControllers\":[";
		bool scriptComma = false;
		for (int i = 0; i < game.gameHeader.getNumberOfPlayers(); ++i)
		{
			auto *player = game.players[i];
			if (!player || !player->ai || player->ai->implementationID != AI::JAVASCRIPT) continue;
			auto *ai = static_cast<AIJavaScript *>(player->ai->aiImplementation);
			if (scriptComma) result << ',';
			scriptComma = true;
			result << "{\"player\":" << i << ",\"disabled\":" << (ai->isDisabled() ? "true" : "false")
				<< ",\"rejectedDecision\":" << (ai->hasRejectedDecision() ? "true" : "false")
				<< ",\"diagnostic\":" << quote(ai->diagnostic()) << '}';
		}
		result << ']';
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

int runHeadlessCommand(const Cli::Request &request)
{
    const auto &command = request.command;
    if(command!="info catalog" && command!="game run" && command!="map study"
        && command!="match verify" && command!="info sim-version" && command!="online turn-client" && command!="assets compose-buildings") return -1;
	fs::path output;
	try
	{
		isolateEnvironment();
		if(command=="match verify") return runVerifyMatch(request);
		if(command=="online turn-client") return runTurnClient(request);
        if(command=="assets compose-buildings")
        {
            std::string base, artwork;
            bool hasArtwork=false;
            std::vector<std::string> packages;
            std::size_t packageBytes=0;
            for (const auto &[option, argument] : request.occurrences)
            {
                if(option=="--format") continue;
                if(option=="--base")
                {
                    if(!base.empty()) throw std::invalid_argument("duplicate --base");
                    base=argument;
                }
                else if(option=="--artwork-bundle")
                {
                    if(hasArtwork) throw std::invalid_argument("duplicate --artwork-bundle");
                    hasArtwork=true;
                    std::ifstream input(argument,std::ios::binary);
                    if(!input) throw std::invalid_argument("cannot read building artwork bundle");
                    char chunk[8192];
                    while(input) {
                        input.read(chunk,sizeof(chunk));artwork.append(chunk,input.gcount());
                        if(artwork.size()>BuildingArtwork::MaxBytes) throw std::invalid_argument("artwork bundle exceeds 72 MiB");
                    }
                    if(input.bad()) throw std::invalid_argument("artwork bundle read failed");
                }
                else if(option=="--package")
                {
                    std::ifstream input(argument, std::ios::binary);
                    if(!input) throw std::invalid_argument("cannot read building package");
                    std::string text;
                    char chunk[8192];
                    while(input)
                    {
                        input.read(chunk, sizeof(chunk));
                        text.append(chunk, input.gcount());
                        if(text.size()>8u*1024u*1024u) throw std::invalid_argument("building package exceeds 8 MiB");
                    }
                    if(input.bad()) throw std::invalid_argument("building package read failed");
                    packageBytes+=text.size();
                    if(packageBytes>8u*1024u*1024u || packages.size()>=4096)
                        throw std::invalid_argument("combined building packages exceed their limits");
                    packages.push_back(std::move(text));
                }
                else throw std::invalid_argument("unknown composition option: " + option);
            }
            GlobalContainer globals("glob2-building-composition", base);
            globalContainer=&globals; globals.runNoX=true;
            const auto baseHash=globals.buildingsTypes.fingerprint();
            const auto baseSize=globals.buildingsTypes.size();
            globals.buildingsTypes.composePackages(packages);
            for(std::size_t id=baseSize;id<globals.buildingsTypes.size();++id) {
                const auto& type=*globals.buildingsTypes.get(id);
                const auto installedFrames=[&](const std::string& path,int first,int count) {
                    if(!path.starts_with("data/gfx/"))return;
                    for(int frame=first;frame<first+count;++frame) {
                        std::unique_ptr<GAGCore::StreamBackend> input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path+std::to_string(frame)+".webp"));
                        if(!input || !input->isValid())throw std::invalid_argument("Installed building frame is missing: "+path+std::to_string(frame));
                    }
                };
                installedFrames(type.gameSprite,type.gameSpriteImage,type.crossConnectMultiImage?16:type.gameSpriteCount);
                if(type.miniSpriteImage>=0)installedFrames(type.miniSprite,type.miniSpriteImage,1);
            }
            nlohmann::json result={{"schemaVersion",1},{"baseHash",baseHash},
                {"catalog",{{"snapshot",globals.buildingsTypes.snapshotJson()},{"hash",globals.buildingsTypes.fingerprint()}}}};
            if(hasArtwork) {
                const auto hash=Online::Sha256::hex(artwork);
                BuildingArtwork::decode(std::move(artwork),globals.buildingsTypes);
                result["artworkHash"]=hash;
            }
            std::cout << result.dump() << std::endl;
            return 0;
        }
		if(command=="info sim-version")
		{
			GlobalContainer globals("glob2-sim-version");
			globalContainer=&globals;for(const auto &directory:request.all("--data-dir"))globals.fileManager->addDir(directory);globals.runNoX=true;
			std::cout << Online::currentSimVersion().toJson().dump() << std::endl;
			return 0;
		}
		if(command=="info catalog")
		{
			GlobalContainer globals("glob2-tournament-catalog");
			globalContainer=&globals;for(const auto &directory:request.all("--data-dir"))globals.fileManager->addDir(directory);globals.runNoX=true;
			std::cout << "{\"schema_version\":1,\"save_version\":" << VERSION_MINOR << ",\"protocol_version\":" << NET_PROTOCOL_VERSION
				<< ",\"building_catalog_hash\":" << quote(globals.buildingsTypes.fingerprint()) << ",\"map_report_version\":2,\"generation_telemetry_version\":1,\"gameplay_telemetry_version\":2,\"ai_telemetry_version\":1,\"performance_telemetry_version\":1,\"commands\":[\"game\",\"generate_map\",\"verify_match\",\"sim_version\",\"compose_buildings\",\"validate_set\"],\"sim_version\":" << Online::currentSimVersion().toJson().dump() << ",\"verify_match_version\":1,\"telemetry\":[\"checksums\",\"team-timeline\",\"maxima\",\"gradient-stats\"],\"ais\":[";

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
			std::cout << "}" << std::endl;
			return 0;
		}
		Options options = request.options;
		options.erase("--generator-package");
		if (command == "map study")
		{
			options["--generator"] = {request.positionals.at(0)};
			options["--map-seed"] = {request.get("--seed")};
			options.erase("--seed");
		}
		if (options.count("--set"))
		{
			options["--param"] = options.at("--set");
			options.erase("--set");
		}
		if (options.count("--write-replay"))
		{
			options["--replay"] = {"true"};
			options.erase("--write-replay");
		}
		if (command == "game run" && options.count("--building-artwork") &&
			!options.count("--generator"))
			throw std::invalid_argument(
				"--building-artwork requires --generator; loaded maps carry their own artwork");
		if (one(options, "--output-dir").empty())
			throw std::invalid_argument("--output-dir is required");
		output = fs::absolute(one(options, "--output-dir"));
		fs::create_directories(output);
		fs::create_directories(output / "profile");
		setHeadlessEnvironment("GLOB2_USER_DIR", (output / "profile").string().c_str());
		setHeadlessEnvironment("GLOB2_USER_DATA_DIR", (output / "profile").string().c_str());
		if (fs::exists(output / "result.json"))
			throw std::invalid_argument("output directory already contains a result");
		int code;
		if (command == "game run")
		{
			if (options.count("--generator"))
			{
				if (options.count("--map-file") || options.count("--load-game"))
					throw std::invalid_argument("generator conflicts with file input");
				Cli::Request generation;
				generation.command = "map study";
				generation.positionals = {one(options, "--generator")};
				generation.options["--output-dir"] = {(output / "generated").string()};
				generation.options["--write-map"] = {"true"};
				generation.options["--seed"] = {one(options, "--map-seed")};
				for (const auto &key : {"--param", "--candidates", "--building-catalog",
										"--building-artwork", "--data-dir"})
				{
					if (options.count(key))
						generation.options[std::string(key) == "--param" ? "--set" : key] =
							options.at(key);
					// Shared asset paths and catalogs are needed by both stages.
					if (std::string(key) == "--param" || std::string(key) == "--candidates")
						options.erase(key);
				}
				options.erase("--generator");
				options.erase("--map-seed");
				const int generated = runHeadlessCommand(generation);
				if (generated != 0)
				{
					if (fs::exists(output / "generated/result.json"))
						fs::copy_file(output / "generated/result.json", output / "result.json");
					manifest(output);
					return generated;
				}
				options["--map-file"] = {
					glob2GzipWritePath((output / "generated/map-r0.map").string())};
				setHeadlessEnvironment("GLOB2_USER_DIR", (output / "profile").string().c_str());
				setHeadlessEnvironment("GLOB2_USER_DATA_DIR",
									   (output / "profile").string().c_str());
			}
			else if (options.count("--map-seed") || options.count("--param") ||
					 options.count("--candidates"))
				throw std::invalid_argument("generator options require --generator");
			code = HeadlessRunner::game(options, output);
		}
		else
		{
			const auto generator = one(options, "--generator");
			int method = generator.find_first_not_of("0123456789") == std::string::npos
							 ? int(integer(generator, 0, INT32_MAX))
							 : GeneratorRegistry::active().idOf(generator);
			if (!GeneratorRegistry::active().find(method))
				throw std::invalid_argument("unknown generator");
			integer(one(options, "--map-seed"), 0, UINT32_MAX);
			std::vector<std::string> args = {"study",
											 std::to_string(method),
											 one(options, "--map-seed"),
											 one(options, "--profile", "glob2-tournament"),
											 "tuning",
											 "quality",
											 "result=" + (output / "result.json").string()};
			for (const auto &directory : many(options, "--data-dir"))
				args.push_back("data-dir=" + directory);
			if (options.count("--building-catalog"))
				args.push_back("building-catalog=" + one(options, "--building-catalog"));
			if (options.count("--building-artwork"))
				args.push_back("building-artwork=" + one(options, "--building-artwork"));
			std::set<std::string> seen;
			for (const auto &param : many(options, "--param"))
			{
				auto eq = param.find('=');
				if (eq == std::string::npos || !seen.insert(param.substr(0, eq)).second)
					throw std::invalid_argument("invalid or duplicate generator parameter");
				integer(param.substr(eq + 1), INT32_MIN, INT32_MAX);
				const auto key = param.substr(0, eq);
				bool known =
					key == "width" || key == "height" || key == "teams" || key == "workers";
				for (const auto &c : GenerationRequest::controls(method))
					known = known || c.id == key;
				if (!known)
					throw std::invalid_argument("unknown generator parameter: " + key);
				args.push_back(param);
			}
			args.push_back("candidates=" +
						   std::to_string(integer(one(options, "--candidates", "0"), 0, 10000)));
			args.push_back("rotations=" + std::to_string(integer(one(options, "--rotations", "1"),
																 1, Team::MAX_COUNT)));
			const auto write = one(options, "--write-map", "false");
			if (write != "true" && write != "false")
				throw std::invalid_argument("--write-map must be true or false");
			if (write == "true")
				args.push_back("save=" + (output / "map").string());
			for (const auto &spec : many(options, "--perturb"))
				args.push_back("perturb=" + spec);
			for (const auto &report : many(options, "--report"))
				if (report == "headroom" || report == "diagnostics" || report == "timing")
					args.push_back(report);
				else if (report == "terrain")
					args.push_back("dump=" + (output / "terrain.txt").string());
				else
					throw std::invalid_argument("unknown report: " + report);
			Headless::writeJson((output / "progress.json").string(),
								"{\"schema_version\":1,\"stage\":\"generation\"}");
			std::vector<char *> raw;
			for (auto &arg : args)
				raw.push_back(&arg[0]);
			code = runMapStudy(raw.size(), raw.data());
		}
		manifest(output);
		return code;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << std::endl;
		const bool invalid = dynamic_cast<const std::invalid_argument *>(&error) != nullptr;
		const std::string status = invalid ? "invalid_request" : "artifact_failure";
		if (!output.empty() && !fs::exists(output / "result.json"))
			try
			{
				Headless::writeJson((output / "result.json").string(),
									"{\"schema_version\":1,\"status\":" + quote(status) +
										",\"diagnostic\":" + quote(error.what()) + "}");
				manifest(output);
			}
			catch (...)
			{
			}
		return invalid ? 2 : 3;
	}
}
