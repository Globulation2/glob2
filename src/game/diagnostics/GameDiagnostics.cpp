// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameDiagnostics.h"
#include "Game.h"
#include "AIStateSerialization.h"
#include <bit>
#include <algorithm>
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
	sceneBudget = png ? sceneBytes(game) : 0;
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
		size_t remaining = sceneBudget <= byteBudget && reservedBytes <= byteBudget-sceneBudget
            ? byteBudget-sceneBudget-reservedBytes : 0;
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
std::shared_ptr<FieldSink> Session::reserveCapture(int player, std::uint64_t observedTick) noexcept
{
    if(pending()) return {};
    for(auto& sink:sinks) if(sink->player==player) {
        if(!sink->enabled || sink->tick!=observedTick || observedTick<sink->nextTick) return {};
        try {
            size_t prepared=0,bytes=0;
            for(const auto& owner:sinks) for(const auto& field:owner->fields)
                prepared+=field.values.capacity()*sizeof(std::int64_t);
            for(const auto& field:sink->fields) bytes+=field.values.capacity()*sizeof(std::int64_t);
            // Retain headroom for both the private output and its immutable
            // owner-publication copy. Outstanding jobs count across all players
            // and the entire delay horizon, not just the current tick.
            const auto remaining=sceneBudget<=byteBudget ? byteBudget-sceneBudget : 0;
            if(reservedBytes>remaining || prepared>remaining-reservedBytes || bytes>remaining-reservedBytes-prepared) {
                sink->enabled=false;sink->skipped=true;sink->nextTick=observedTick+sink->interval;
                for(auto& field:sink->fields) std::vector<std::int64_t>().swap(field.values);
                return {};
            }
            auto result=std::make_shared<FieldSink>();
            const auto key=std::pair{player,observedTick};
            if(!reservations.emplace(key,2*bytes).second) return {};
            reservedBytes+=2*bytes;
            result->tick=observedTick;result->nextTick=sink->nextTick;result->interval=sink->interval;
            result->player=sink->player;result->team=sink->team;result->enabled=true;
            // The owner prepared these buffers within its deterministic budget.
            // Transfer their storage; no worker shares the stable Session sink.
            result->fields=std::move(sink->fields);
            sink->enabled=false;sink->nextTick=observedTick+sink->interval;
            return result;
        } catch(...) {sink->enabled=false;sink->failed=true;sink->nextTick=observedTick+sink->interval;}
        return {};
    }
    return {};
}
bool Session::adoptCapture(const FieldSink& completed) noexcept
{
    try {
        auto found=std::find_if(sinks.begin(),sinks.end(),[&](const auto& sink) {
            return sink->player==completed.player && sink->team==completed.team;
        });
        if(found==sinks.end()) return false;
        auto& sink=**found;
        const auto key=std::pair{completed.player,completed.tick};
        if(reservations.contains(key)) return true;
        size_t bytes=0,prepared=0;
        for(const auto& field:completed.fields) {
            if(field.values.capacity()>(CaptureBudget-bytes)/sizeof(std::int64_t)) {
                ++skipped;issue(completed,"Saved capture budget exceeded");return false;
            }
            bytes+=field.values.capacity()*sizeof(std::int64_t);
        }
        for(const auto& owner:sinks) for(const auto& field:owner->fields)
            prepared+=field.values.capacity()*sizeof(std::int64_t);
        const auto remaining=sceneBudget<=byteBudget ? byteBudget-sceneBudget : 0;
        // Account for the retained saved output and its later owner copy before
        // any fresh capture is prepared. This never changes simulation state.
        if(reservedBytes>remaining || prepared>remaining-reservedBytes || bytes>(remaining-reservedBytes-prepared)/2) {
            ++skipped;issue(completed,"Saved capture budget exceeded");return false;
        }
        reservations.emplace(key,2*bytes);reservedBytes+=2*bytes;
        sink.nextTick=std::max(sink.nextTick,completed.nextTick);
        return true;
    } catch(...) {++failures;return false;}
}
void Session::cancelCaptures(int player) noexcept
{
    for(auto at=reservations.begin();at!=reservations.end();) {
        if(at->first.first==player) {
            reservedBytes-=at->second;
            at=reservations.erase(at);
        } else ++at;
    }
    for(auto& sink:sinks) if(sink->player==player) {
        sink->enabled=false;
        // Published output remains available for the graphics owner to drain.
        // Unpublished prepared storage can be released after the worker barrier.
        if(!sink->captured) for(auto& field:sink->fields)
            std::vector<std::int64_t>().swap(field.values);
    }
}
void Session::publishCapture(const FieldSink& completed) noexcept
{
    struct Release {
        Session& session; std::pair<int,std::uint64_t> key;
        ~Release(){const auto found=session.reservations.find(key);if(found!=session.reservations.end()){
            session.reservedBytes-=found->second;session.reservations.erase(found);
        }}
    } release{*this,{completed.player,completed.tick}};
    for(auto& sink:sinks) if(sink->player==completed.player && sink->team==completed.team) {
        if(!completed.captured && !completed.failed && !completed.skipped) {
            sink->nextTick=completed.nextTick;return;
        }
        try {
            const auto scheduled=sink->nextTick;
            *sink=completed;
            sink->enabled=false;sink->nextTick=std::max(scheduled,completed.nextTick);
        } catch(...) {sink->enabled=false;sink->failed=true;}
        return;
    }
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

namespace GameDiagnostics
{
namespace
{
void writeWide(GAGCore::OutputStream* stream,std::uint64_t value,const char* name)
{
    stream->writeEnterSection(name);
    stream->writeUint32(Uint32(value),"low");stream->writeUint32(Uint32(value>>32),"high");
    stream->writeLeaveSection();
}
std::uint64_t readWide(GAGCore::InputStream* stream,const char* name)
{
    stream->readEnterSection(name);
    const auto low=stream->readUint32("low"),high=stream->readUint32("high");
    stream->readLeaveSection();return std::uint64_t(low)|(std::uint64_t(high)<<32);
}
}
void FieldSink::save(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("DiagnosticFieldSink");
    writeWide(stream,tick,"tick");writeWide(stream,nextTick,"nextTick");
    stream->writeUint32(interval,"interval");stream->writeSint32(player,"player");
    stream->writeSint32(team,"team");stream->writeSint32(plannerTick,"plannerTick");
    stream->writeUint8(enabled,"enabled");stream->writeUint8(captured,"captured");
    stream->writeUint8(failed,"failed");stream->writeUint8(skipped,"skipped");
    for(unsigned f=0;f<fields.size();++f) {
        stream->writeEnterSection(f);const auto& field=fields[f];
        stream->writeSint32(field.width,"width");stream->writeSint32(field.height,"height");
        stream->writeSint32(field.red,"red");stream->writeSint32(field.green,"green");stream->writeSint32(field.blue,"blue");
        stream->writeUint32(field.values.size(),"count");
        for(Uint32 i=0;i<field.values.size();++i) {
            stream->writeEnterSection(i);writeWide(stream,std::bit_cast<std::uint64_t>(field.values[i]),"value");stream->writeLeaveSection();
        }
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
}
bool FieldSink::load(GAGCore::InputStream* stream)
{
    try {
        GAGCore::BinaryInputStream::CheckedReads checked(stream);
        FieldSink value;size_t bytes=0;
        stream->readEnterSection("DiagnosticFieldSink");
        value.tick=readWide(stream,"tick");value.nextTick=readWide(stream,"nextTick");
        value.interval=stream->readUint32("interval");
        value.player=AIStateSerialization::readSint32(stream,"player");value.team=AIStateSerialization::readSint32(stream,"team");
        value.plannerTick=AIStateSerialization::readSint32(stream,"plannerTick");
        if(!value.interval || value.player<0 || value.player>=Team::MAX_COUNT || value.team<0 || value.team>=Team::MAX_COUNT) return false;
        auto flag=[&](const char* name){const auto n=stream->readUint8(name);if(n>1)throw std::invalid_argument("Invalid diagnostic flag");return n!=0;};
        value.enabled=flag("enabled");value.captured=flag("captured");value.failed=flag("failed");value.skipped=flag("skipped");
        for(unsigned f=0;f<value.fields.size();++f) {
            stream->readEnterSection(f);auto& field=value.fields[f];
            field.width=AIStateSerialization::readSint32(stream,"width");field.height=AIStateSerialization::readSint32(stream,"height");
            field.red=AIStateSerialization::readSint32(stream,"red");field.green=AIStateSerialization::readSint32(stream,"green");field.blue=AIStateSerialization::readSint32(stream,"blue");
            const auto count=stream->readUint32("count");
            if(field.width<0 || field.height<0 || count>MapRender::MaximumFieldValues || size_t(count)> (CaptureBudget-bytes)/sizeof(std::int64_t)) return false;
            if(count && (field.width<=0 || field.height<=0 || std::uint64_t(field.width)*field.height!=count)) return false;
            if(value.captured && (!count || field.width!=value.fields[0].width || field.height!=value.fields[0].height))return false;
            if(field.red<0 || field.red>255 || field.green<0 || field.green>255 || field.blue<0 || field.blue>255)return false;
            bytes+=size_t(count)*sizeof(std::int64_t);field.values.resize(count);
            for(Uint32 i=0;i<count;++i){stream->readEnterSection(i);field.values[i]=std::bit_cast<std::int64_t>(readWide(stream,"value"));stream->readLeaveSection();}
            stream->readLeaveSection();
        }
        stream->readLeaveSection();if(!stream->isValid())return false;
        *this=std::move(value);return true;
    } catch(...) {return false;}
}
}
