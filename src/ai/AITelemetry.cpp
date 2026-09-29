// SPDX-License-Identifier: GPL-3.0-or-later
#include <PerformanceTelemetry.h>
#include "AITelemetry.h"
#include "AI.h"
#include "Order.h"
#include "Game.h"
#include "Player.h"
#include "Team.h"
#include "TeamStat.h"
#include "GlobalContainer.h"
#include <BinaryStream.h>
#include <Stream.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <typeinfo>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <sstream>

namespace AITelemetry
{
std::shared_ptr<Order> Sink::returnedOrder(unsigned result, std::shared_ptr<Order> value) const
{
	set(result, value ? value->getOrderType() : -1);
	return value;
}

namespace
{
std::string quoted(const std::string &text)
{
	std::ostringstream out;
	out << '\"';
	for (unsigned char c : text)
	{
		if (c == '\"' || c == '\\')
			out << '\\' << c;
		else if (c < 32)
			out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
		else
			out << c;
	}
	out << '\"';
	return out.str();
}
void require(bool condition)
{
	if (!condition)
		throw std::runtime_error("Invalid AI telemetry");
}
void writeString(GAGCore::OutputStream *s, const std::string &v, const char *name)
{
	s->writeEnterSection(name);
	s->writeUint32(v.size(), "length");
	s->write(v.data(), v.size(), "bytes");
	s->writeLeaveSection();
}
std::string readString(GAGCore::InputStream *s, const char *name)
{
	s->readEnterSection(name);
	auto n = s->readUint32("length");
	require(n <= 4096);
	std::string v(n, '\0');
	s->read(v.data(), n, "bytes");
	s->readLeaveSection();
	return v;
}
void writeSample(GAGCore::OutputStream *s, const Sample &a)
{
	s->writeUint32(a.tick, "tick");
	s->writeUint32(a.available, "available");
	// The binary format has no section bytes. Pack the identical four network-order
	// words per value, avoiding thousands of tiny virtual writes on every autosave.
	// Derived streams may observe field boundaries, so retain their scalar path.
	if (typeid(*s) == typeid(GAGCore::BinaryOutputStream))
	{
		Uint8 bytes[4096];
		for (size_t first = 0; first < a.values.size(); first += sizeof(bytes) / 16)
		{
			const auto count = std::min(a.values.size() - first, sizeof(bytes) / 16);
			for (size_t i = 0; i < count; ++i)
			{
				const auto &v = a.values[first + i];
				const Uint32 words[] = {Uint32(v.bits), Uint32(v.bits >> 32), v.updated,
										Uint32(v.valid)};
				for (unsigned w = 0; w < 4; ++w)
					for (unsigned b = 0; b < 4; ++b)
						bytes[i * 16 + w * 4 + b] = Uint8(words[w] >> (24 - b * 8));
			}
			s->write(bytes, count * 16, "values");
		}
		return;
	}
	for (unsigned i = 0; i < a.values.size(); ++i)
	{
		s->writeEnterSection(i);
		const auto &v = a.values[i];
		s->writeUint32(v.bits, "low");
		s->writeUint32(v.bits >> 32, "high");
		s->writeUint32(v.updated, "updated");
		s->writeUint32(v.valid, "valid");
		s->writeLeaveSection();
	}
}
Sample readSample(GAGCore::InputStream *s, size_t fields)
{
	Sample a;
	a.tick = s->readUint32("tick");
	auto available = s->readUint32("available");
	require(available <= 1);
	a.available = available;
	a.values.resize(fields);
	for (unsigned i = 0; i < fields; ++i)
	{
		s->readEnterSection(i);
		auto &v = a.values[i];
		v.bits = s->readUint32("low");
		v.bits |= Uint64(s->readUint32("high")) << 32;
		v.updated = s->readUint32("updated");
		auto valid = s->readUint32("valid");
		require(valid <= 1 && v.updated <= a.tick);
		v.valid = valid;
		s->readLeaveSection();
	}
	return a;
}
void identity(const Series &r, int team)
{
	std::cout << " team=" << team << " player=" << r.player << " ai=" << r.implementation
			  << " generation=" << r.generation << " schema=" << r.schemaVersion
			  << " coverage_start=" << r.coverage;
}
void printSample(const Series &r, int team, const Sample &a, const char *prefix)
{
	std::cout << prefix;
	identity(r, team);
	std::cout << " tick=" << a.tick << " available=" << a.available << " active=" << r.active;
	if (a.available)
		for (unsigned i = 0; i < a.values.size(); ++i)
		{
			const auto &v = a.values[i];
			std::cout << ' ' << r.fields[i].name << '=';
			if (!v.valid)
				std::cout << "na";
			else if (r.fields[i].type == Real)
			{
				double d;
				std::memcpy(&d, &v.bits, sizeof(d));
				if (std::isfinite(d))
					std::cout << std::setprecision(std::numeric_limits<double>::max_digits10) << d;
				else
					std::cout << "na";
			}
			else if (r.fields[i].type == Signed)
				std::cout << static_cast<Sint64>(v.bits);
			else
				std::cout << v.bits;
			if (v.valid)
				std::cout << ' ' << r.fields[i].name << "@tick=" << v.updated;
		}
	std::cout << '\n';
}
} // namespace
void save(GAGCore::OutputStream *s, const std::vector<std::shared_ptr<Series>> &records)
{
	s->writeEnterSection("aiTelemetry");
	s->writeUint32(records.size(), "count");
	for (unsigned i = 0; i < records.size(); ++i)
	{
		const auto &r = *records[i];
		s->writeEnterSection(i);
		s->writeSint32(r.player, "player");
		s->writeSint32(r.implementation, "implementation");
		s->writeUint32(r.generation, "generation");
		s->writeUint32(r.coverage, "coverage");
		s->writeUint32(r.schemaVersion, "schemaVersion");
		s->writeUint32(r.active, "active");
		writeString(s, r.playerName, "playerName");
		s->writeUint32(r.fields.size(), "fields");
		for (unsigned f = 0; f < r.fields.size(); ++f)
		{
			s->writeEnterSection(f);
			const auto &d = r.fields[f];
			writeString(s, d.name, "name");
			writeString(s, d.unit, "unit");
			writeString(s, d.meaning, "meaning");
			s->writeUint32(d.type, "type");
			s->writeUint32(d.kind, "kind");
			s->writeLeaveSection();
		}
		s->writeEnterSection("current");
		writeSample(s, r.current);
		s->writeLeaveSection();
		s->writeUint32(r.history.size(), "samples");
		for (unsigned n = 0; n < r.history.size(); ++n)
		{
			s->writeEnterSection(n);
			writeSample(s, r.history[n]);
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
void load(GAGCore::InputStream *s, std::vector<std::shared_ptr<Series>> &records)
{
	GAGCore::BinaryInputStream::CheckedReads checked(s);
	s->readEnterSection("aiTelemetry");
	const auto count = s->readUint32("count");
	require(count <= 4096);
	std::vector<std::shared_ptr<Series>> loaded;
	for (unsigned i = 0; i < count; ++i)
	{
		auto p = std::make_shared<Series>();
		auto &r = *p;
		s->readEnterSection(i);
		r.player = s->readSint32("player");
		r.implementation = s->readSint32("implementation");
		r.generation = s->readUint32("generation");
		r.coverage = s->readUint32("coverage");
		r.schemaVersion = s->readUint32("schemaVersion");
		auto active = s->readUint32("active");
		require(r.player >= 0 && r.player < Team::MAX_COUNT && r.implementation >= 0 &&
				r.implementation < AI::SIZE && active <= 1 && r.schemaVersion > 0);
		r.active = active;
		r.playerName = readString(s, "playerName");
		const auto fields = s->readUint32("fields");
		require(fields >= Specific && fields <= 4096);
		for (unsigned f = 0; f < fields; ++f)
		{
			s->readEnterSection(f);
			Field d;
			d.name = readString(s, "name");
			d.unit = readString(s, "unit");
			d.meaning = readString(s, "meaning");
			auto type = s->readUint32("type"), kind = s->readUint32("kind");
			require(type <= Real && kind <= Mask && !d.name.empty());
			require(d.name.find_first_not_of(
						"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") ==
					std::string::npos);
			for (const auto &prior : r.fields)
				require(prior.name != d.name);
			d.type = static_cast<Type>(type);
			d.kind = static_cast<Kind>(kind);
			r.fields.push_back(d);
			s->readLeaveSection();
		}
		s->readEnterSection("current");
		r.current = readSample(s, fields);
		s->readLeaveSection();
		require(r.current.tick >= r.coverage);
		const auto samples = s->readUint32("samples");
		require(samples <= Uint64(r.current.tick) / 512 + 1);
		for (unsigned n = 0; n < samples; ++n)
		{
			s->readEnterSection(n);
			auto a = readSample(s, fields);
			s->readLeaveSection();
			require(a.tick >= r.coverage && a.tick <= r.current.tick && !(a.tick & 511) &&
					(r.history.empty() || a.tick > r.history.back().tick));
			r.history.push_back(std::move(a));
		}
		for (const auto &prior : loaded)
			require(prior->player != r.player || prior->generation != r.generation);
		loaded.push_back(p);
		s->readLeaveSection();
	}
	s->readLeaveSection();
	records.swap(loaded);
}
void emit(Series &r, int team, bool final, bool describe)
{
	PERF_SCOPE_TIME(Output);
	const auto precision = std::cout.precision();
	const auto flags = std::cout.flags();
	if (!r.schemaPrinted)
	{
		std::cout << "GLOB2_AI_PLAYER";
		identity(r, team);
		std::cout << " name=" << quoted(r.playerName) << '\n';
		for (unsigned f = 0; describe && f < r.fields.size(); ++f)
		{
			const auto &d = r.fields[f];
			std::cout << "GLOB2_AI_SCHEMA";
			identity(r, team);
			std::cout << " field=" << d.name << " type=" << d.type << " kind=" << d.kind
					  << " unit=" << quoted(d.unit) << " meaning=" << quoted(d.meaning) << '\n';
		}
		r.schemaPrinted = true;
	}
	if (final)
		for (const auto &a : r.history)
			printSample(r, team, a, "GLOB2_AI_HISTORY");
	printSample(r, team, r.current, final ? "GLOB2_AI_FINAL" : "GLOB2_AI_SAMPLE");
	std::cout.precision(precision);
	std::cout.flags(flags);
}
void capture(Team *team, bool retain, bool output, bool final)
{
	const bool replay = globalContainer && globalContainer->replaying;
	for (int p = 0; p < team->game->gameHeader.getNumberOfPlayers(); ++p)
	{
		auto *player = team->game->players[p];
		if (player && player->team == team && player->ai)
		{
			if (!replay)
				player->ai->captureTelemetry();
		}
	}
	for (auto &r : team->stats.aiTelemetry)
	{
		auto *player = team->game->players[r->player];
		if (r->active &&
			(!player || !player->ai || player->team != team || player->ai->telemetrySeries != r))
			r->active = false;
		if (replay)
		{
			r->current.available = false;
			r->current.tick = team->game->stepCounter;
		}
		if (retain && r->active && r->current.available &&
			(r->history.empty() || r->history.back().tick != r->current.tick))
			r->history.push_back(r->current);
		if (output && (r->active || final || replay))
		{
			bool describe = true;
			if (!r->schemaPrinted)
				for (int t = 0; t < team->game->mapHeader.getNumberOfTeams(); ++t)
					if (team->game->teams[t])
						for (const auto &other : team->game->teams[t]->stats.aiTelemetry)
							if (other != r && other->schemaPrinted &&
								other->implementation == r->implementation &&
								other->schemaVersion == r->schemaVersion &&
								other->fields == r->fields)
								describe = false;
			emit(*r, team->teamNumber, final, describe);
		}
	}
}
} // namespace AITelemetry
