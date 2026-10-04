// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameDiagnostics.h"
#include "Game.h"
#include <FileManager.h>
#include <Toolkit.h>
#include "AI.h"
#include "AIMaxima.h"
#include "Player.h"
#include "Team.h"
#include "Unit.h"
#include "Sector.h"
#include "render/scene/SceneExtract.h"
#include "render/GameAnimations.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace GameDiagnostics
{
namespace
{
struct Named { const char* name; const char* units; int r,g,b; };
constexpr Named names[] = {
	{"threat", "placement score", 255,40,40},
	{"protectedness", "placement score", 40,120,255},
	{"foodOpportunity", "micro-wheat per tick", 40,220,120},
	{"farmCapacity", "micro-wheat per tick", 220,200,40},
	{"protectedYield", "micro-wheat per tick", 220,120,220}
};
// Upper bound for a fresh Scene, including vector growth during extraction.
// Panels and selected-building diagnostics are not requested by this tool.
size_t sceneBytes(const Game& game)
{
	size_t bytes = sizeof(Scene) + size_t(game.map.getW())*game.map.getH()*32
		+ 2*SceneEntities::Teams*SceneEntities::SlotsPerTeam*sizeof(int);
	for (int t=0; t<game.teamsCount(); ++t)
	{
		const Team& team = *game.teams[t];
		for (int i=0; i<Unit::MAX_COUNT; ++i) if (team.myUnits[i]) bytes += 4*sizeof(SceneUnit);
		for (int i=0; i<Building::MAX_COUNT; ++i) if (team.myBuildings[i]) bytes += 4*sizeof(SceneBuilding);
		bytes += 4*team.virtualBuildings.size()*sizeof(Uint16);
	}
	Map& map = const_cast<Map&>(game.map);
	const int sectors = map.getSectorW()*map.getSectorH();
	bytes += size_t(sectors)*sizeof(SceneSectorEffects);
	for (int i=0; i<sectors; ++i)
	{
		bytes += 4*map.getSector(i)->bullets.size()*sizeof(SceneBullet);
		if (game.animations)
		{
			bytes += 4*game.animations->getExplosions(i).size()*sizeof(SceneExplosion);
			bytes += 4*game.animations->getDeathAnimations(i).size()*sizeof(SceneDeathAnimation);
		}
	}
	for (int p=0; p<game.gameHeader.getNumberOfPlayers(); ++p)
		if (game.players[p]) bytes += 4*game.players[p]->name.size();
	return bytes;
}
void checkedText(const fs::path& path, const std::string& text)
{
	std::ofstream out(path, std::ios::binary); out << text; out.flush(); out.close();
	if (!out) throw std::runtime_error("Cannot write " + path.string());
}
}
void FieldSink::capture(const AIMaximaPlacement::WorldState& world) noexcept
{
	if (!enabled || captured || tick < nextTick) return;
	nextTick = tick + interval;
	try
	{
		const size_t count = fields[0].values.size();
		if (world.width != fields[0].width || world.height != fields[0].height || world.tiles.size() != count)
			throw std::runtime_error("Placement world dimensions changed");
		for (size_t i=0; i<count; ++i)
		{
			const auto& tile = world.tiles[i];
			fields[0].values[i] = tile.threat; fields[1].values[i] = tile.protectedness;
			fields[2].values[i] = tile.foodOpportunity; fields[3].values[i] = tile.farmCapacity;
			fields[4].values[i] = tile.protectedYield;
		}
		plannerTick = world.tick; captured = true;
	}
	catch (...) { failed = true; }
}
Session::Session(Game& game, std::string path, unsigned interval, bool paint, size_t budget)
	: directory(fs::absolute(path).string()), png(paint), byteBudget(budget)
{
	if (!budget || budget > CaptureBudget) throw std::invalid_argument("Invalid diagnostic capture budget");
	if (!interval) throw std::invalid_argument("Diagnostic interval must be positive");
	for (int p=0; p<game.gameHeader.getNumberOfPlayers(); ++p)
		if (game.players[p] && game.players[p]->ai)
			if (auto* maxima = dynamic_cast<AIMaxima::Maxima*>(game.players[p]->ai->aiImplementation))
			{
				auto sink = std::make_shared<FieldSink>();
				sink->player = p; sink->team = game.players[p]->teamNumber; sink->interval = interval;
				maxima->fieldDiagnostics = sink; sinks.push_back(std::move(sink));
			}
	if (sinks.empty()) throw std::invalid_argument("Maxima field diagnostics require a Maxima player");
}
Session::~Session() { for (auto& sink : sinks) sink->enabled = false; }
void Session::beginTick(const Game& game) noexcept
{
	if (pending()) return;
	try
	{
		sceneBudget = png ? sceneBytes(game) : 0;
		size_t remaining = sceneBudget <= byteBudget ? byteBudget-sceneBudget : 0;
		const int w=game.map.getW(), h=game.map.getH();
		const size_t count=size_t(w)*h, bytes=count*5*sizeof(std::int64_t);
		for (auto& sink : sinks)
		{
			sink->enabled = false; sink->tick = game.stepCounter;
			if (sink->tick < sink->nextTick) continue;
			if (w <= 0 || h <= 0 || count > MapRender::MaximumFieldValues || bytes > remaining)
			{
				sink->skipped = true; sink->nextTick = sink->tick + sink->interval; continue;
			}
			remaining -= bytes;
			try
			{
				for (auto& field : sink->fields) { field.width=w; field.height=h; field.values.resize(count); }
				sink->enabled = true;
			}
			catch (...) { sink->failed = true; sink->nextTick = sink->tick + sink->interval; }
		}
	}
	catch (...) { ++failures; }
}
void Session::completeTick(const Game& game) noexcept
{
	if (pending()) return;
	bool any = false;
	for (const auto& sink : sinks) any |= sink->captured || sink->failed || sink->skipped;
	if (!any) return;
	bool captured = false;
	for (const auto& sink : sinks) captured |= sink->captured;
	if (png && captured)
	{
		try
		{
			scene = std::make_unique<Scene>(); SceneRequest request; request.includePanels = false;
			extractScene(game, request, *scene);
		}
		catch (...) { scene.reset(); for (auto& sink : sinks) if (sink->captured) sink->failed = true; }
	}
	ready.store(true);
}
void Session::issue(const FieldSink& sink, const std::string& reason)
{
	// Keep failure reporting bounded even if a long run cannot write its directory.
	if (issues.size() < 32) issues.push_back({sink.tick, sink.player, reason.substr(0,512)});
}
void Session::drain() noexcept
{
	if (!pending()) return;
	for (auto& sink : sinks)
	{
		try
		{
			if (sink->skipped) { ++skipped; issue(*sink,"Capture budget exceeded"); std::cerr << "Diagnostics: capture budget exceeded for player " << sink->player << '\n'; }
			if (sink->failed) throw std::runtime_error("Capture failed");
			if (!sink->captured) continue;
			fs::create_directories(directory);
			std::ostringstream name;
			name << "tick-" << std::setw(7) << std::setfill('0') << sink->tick << ".player" << sink->player << ".team" << sink->team;
			const fs::path destination=fs::path(directory)/name.str(), staging=destination.string()+".tmp";
			if (!fs::create_directory(staging)) throw std::runtime_error("Capture staging directory already exists");
			struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path,error); } } cleanup{staging};
			nlohmann::json metadata={{"schema_version",1},{"simulation_tick",sink->tick},{"planner_tick",sink->plannerTick},
				{"player",sink->player},{"team",sink->team},{"width",sink->fields[0].width},{"height",sink->fields[0].height}};
			for (size_t f=0; f<sink->fields.size(); ++f)
			{
				auto& field=sink->fields[f]; const auto& named=names[f];
				std::ofstream out(staging/(std::string(named.name)+".field"));
				out << field.width << ' ' << field.height << '\n';
				for (size_t i=0; i<field.values.size(); ++i) out << field.values[i] << ((i+1)%field.width ? ' ' : '\n');
				out.flush(); out.close(); if (!out) throw std::runtime_error("Field write failed");
				metadata["fields"][named.name]={{"units",named.units},{"colour",{named.r,named.g,named.b}}};
				if (png) { field.red=named.r; field.green=named.g; field.blue=named.b; MapRender::toPng(*scene,(staging/(std::string(named.name)+".png")).string(),2048,&field); }
			}
			if (scene) metadata["scene_tick"]=scene->tick;
			metadata["complete"]=true;
			checkedText(staging/"capture.json",metadata.dump(2)+"\n");
			fs::rename(staging,destination); ++completed;
		}
		catch (const std::exception& error) { ++failures; try { issue(*sink,error.what()); } catch (...) {} std::cerr << "Diagnostics player " << sink->player << ": " << error.what() << '\n'; }
		catch (...) { ++failures; }
		sink->enabled = sink->captured = sink->failed = sink->skipped = false;
		for (auto& field : sink->fields) std::vector<std::int64_t>().swap(field.values);
	}
	scene.reset(); ready.store(false);
}
void Session::finish() noexcept
{
	drain();
	try
	{
		fs::create_directories(directory);
		nlohmann::json summary={{"schema_version",1},{"completed",completed},{"failed",failures},{"skipped",skipped},{"issue_samples",nlohmann::json::array()}};
		for (const auto& issue : issues) summary["issue_samples"].push_back({{"simulation_tick",issue.tick},{"player",issue.player},{"reason",issue.reason}});
		if (!GAGCore::Toolkit::getFileManager()->writeFileAtomic((fs::path(directory)/"summary.json").string(),summary.dump(2)+"\n"))
			throw std::runtime_error("Cannot write diagnostic summary");
	}
	catch (const std::exception& error) { std::cerr << "Diagnostics summary: " << error.what() << '\n'; }
	std::cerr << "Diagnostics: " << completed << " completed, " << failures << " failed, " << skipped << " skipped\n";
}
}
