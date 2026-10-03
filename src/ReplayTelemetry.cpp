// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReplayTelemetry.h"
#include "Game.h"
#include "Team.h"
#include <Stream.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cmath>
namespace ReplayTelemetry
{
namespace
{
std::string display(const AITelemetry::Field &field, const AITelemetry::Value &value)
{
	if (field.type == AITelemetry::Signed)
		return std::to_string(static_cast<Sint64>(value.bits));
	if (field.type == AITelemetry::Unsigned)
		return std::to_string(value.bits);
	double n;
	std::memcpy(&n, &value.bits, sizeof(n));
	if (!std::isfinite(n))
		return "Unavailable";
	std::ostringstream s;
	s << std::setprecision(8) << n;
	return s.str();
}
constexpr size_t Limit = 16 * 1024 * 1024;
std::string readText(GAGCore::InputStream *s, size_t &budget)
{
	auto n = s->readUint32("length");
	if (n > 4096 || n > budget)
		throw std::runtime_error("Invalid replay telemetry text");
	budget -= n;
	std::string text(n, '\0');
	if (n)
		s->read(text.data(), n, "text");
	return text;
}
void writeText(GAGCore::OutputStream *s, const std::string &v)
{
	s->writeUint32(v.size(), "length");
	if (!v.empty())
		s->write(v.data(), v.size(), "text");
}
} // namespace
void Stream::capture(const Game &game, Uint32 step)
{
	if (truncated)
		return;
	present = true;
	until = step;
	Frame frame;
	frame.step = step;
	std::map<int, Row> latest;
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		if (game.teams[t])
			for (const auto &s : game.teams[t]->stats.aiTelemetry)
				if (s->active)
				{
					Row row;
					row.team = t;
					row.player = s->player;
					row.name = s->playerName;
					row.available = s->current.available;
					if (row.available)
					{
						for (size_t f = 0; f < s->fields.size() && f < s->current.values.size();
							 ++f)
						{
							const auto &v = s->current.values[f];
							const auto &d = s->fields[f];
							if (v.valid)
								row.values.push_back(
									{d.name, display(d, v), d.unit, d.meaning, v.updated});
						}
						row.values.insert(row.values.end(), s->named.begin(), s->named.end());
					}
					latest[row.player] = row;
					if (!current.count(row.player) || !(current.at(row.player) == row))
						frame.changes.push_back(std::move(row));
				}
	for (const auto &[id, row] : current)
		if (!latest.count(id))
		{
			auto gone = row;
			gone.available = false;
			gone.values.clear();
			frame.changes.push_back(std::move(gone));
		}
	if (frame.changes.empty())
		return;
	size_t cost = 8;
	for (const auto &r : frame.changes)
	{
		cost += 32 + r.name.size();
		for (const auto &v : r.values)
			cost += 24 + v.name.size() + v.value.size() + v.unit.size() + v.meaning.size();
	}
	if (cost > Limit - bytes || frames.size() >= 65536)
	{
		truncated = true;
		until = step ? step - 1 : 0;
		return;
	}
	bytes += cost;
	current = std::move(latest);
	frames.push_back(std::move(frame));
}
void Stream::write(GAGCore::OutputStream *s) const
{
	s->writeUint32(0x41495431, "telemetryMagic");
	s->writeUint32(1, "telemetryVersion");
	s->writeUint32(until, "telemetryUntil");
	s->writeUint8(truncated, "telemetryTruncated");
	s->writeUint32(frames.size(), "telemetryFrames");
	for (const auto &frame : frames)
	{
		s->writeUint32(frame.step, "step");
		s->writeUint32(frame.changes.size(), "changes");
		for (const auto &r : frame.changes)
		{
			s->writeUint32(r.team, "team");
			s->writeUint32(r.player, "player");
			writeText(s, r.name);
			s->writeUint8(r.available, "available");
			s->writeUint32(r.values.size(), "fields");
			for (const auto &v : r.values)
			{
				writeText(s, v.name);
				writeText(s, v.value);
				writeText(s, v.unit);
				writeText(s, v.meaning);
				s->writeUint32(v.updated, "updated");
			}
		}
	}
}
void Stream::read(GAGCore::InputStream *s)
{
	if (s->readUint32("telemetryMagic") != 0x41495431 || s->readUint32("telemetryVersion") != 1)
		throw std::runtime_error("Unsupported replay telemetry");
	until = s->readUint32("telemetryUntil");
	truncated = s->readUint8("telemetryTruncated") != 0;
	auto count = s->readUint32("telemetryFrames");
	if (count > 65536)
		throw std::runtime_error("Oversized replay telemetry");
	size_t budget = Limit;
	for (unsigned i = 0; i < count; ++i)
	{
		if (budget < 8)
			throw std::runtime_error("Oversized replay telemetry frames");
		budget -= 8;
		Frame frame;
		frame.step = s->readUint32("step");
		auto rows = s->readUint32("changes");
		if (frame.step > until || (i && frame.step <= frames.back().step) ||
			rows > Team::MAX_COUNT * 2)
			throw std::runtime_error("Invalid replay telemetry frame");
		for (unsigned j = 0; j < rows; ++j)
		{
			if (budget < 32)
				throw std::runtime_error("Oversized replay telemetry rows");
			budget -= 32;
			Row row;
			row.team = s->readUint32("team");
			row.player = s->readUint32("player");
			row.name = readText(s, budget);
			if (row.team < 0 || row.team >= Team::MAX_COUNT || row.player < 0 ||
				row.player >= Team::MAX_COUNT)
				throw std::runtime_error("Invalid telemetry controller");
			row.available = s->readUint8("available") != 0;
			auto fields = s->readUint32("fields");
			if (fields > AITelemetry::MaximumPresentationValues || size_t(fields) * 24 > budget)
				throw std::runtime_error("Oversized telemetry fields");
			budget -= size_t(fields) * 24;
			for (unsigned f = 0; f < fields; ++f)
			{
				AITelemetry::NamedValue v;
				v.name = readText(s, budget);
				v.value = readText(s, budget);
				v.unit = readText(s, budget);
				v.meaning = readText(s, budget);
				v.updated = s->readUint32("updated");
				row.values.push_back(std::move(v));
			}
			frame.changes.push_back(std::move(row));
		}
		frames.push_back(std::move(frame));
	}
	current.clear();
	cursor = 0;
	present = true;
}
void Stream::apply(Game &game, Uint32 step)
{
	while (cursor < frames.size() && frames[cursor].step <= step)
	{
		for (const auto &r : frames[cursor].changes)
			current[r.player] = r;
		++cursor;
	}
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		if (game.teams[t])
		{
			auto &series = game.teams[t]->stats.aiTelemetry;
			if (!present)
			{
				for (auto &s : series)
					s->current.available = false;
				continue;
			}
			series.clear();
			for (const auto &[id, row] : current)
				if (row.team == t)
				{
					auto s = std::make_shared<AITelemetry::Series>();
					s->schemaVersion = AITelemetry::ReplayPresentationSchema;
					s->player = id;
					s->playerName = row.name;
					s->current.tick = game.stepCounter;
					s->current.available = row.available && !(truncated && step > until);
					if (s->current.available)
						s->named = row.values;
					series.push_back(std::move(s));
				}
		}
}
} // namespace ReplayTelemetry
