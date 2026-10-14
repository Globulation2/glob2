// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReplayTelemetry.h"
#include "Game.h"
#include "Team.h"
#include <Stream.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <functional>
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
bool Stream::SchemaLess::operator()(const SchemaPtr &a, const SchemaPtr &b) const
{
	if (a == b)
		return false;
	return std::lexicographical_compare(a->begin(), a->end(), b->begin(), b->end(),
		[](const Metadata &x, const Metadata &y) { return x.view() < y.view(); });
}
bool Stream::SchemaLess::operator()(const SchemaPtr &a, const SchemaView &b) const
{
	return std::ranges::lexicographical_compare(*a, b, {}, &Metadata::view);
}
bool Stream::SchemaLess::operator()(const SchemaView &a, const SchemaPtr &b) const
{
	return std::ranges::lexicographical_compare(a, *b, {}, std::identity{}, &Metadata::view);
}
Stream::SchemaPtr Stream::schemaFor(const SchemaView &view, const SchemaPtr *hint) const
{
	if (hint && std::ranges::equal(**hint, view, {}, &Metadata::view))
		return *hint;
	if (const auto found = schemas.find(view); found != schemas.end())
		return *found;
	auto schema = std::make_shared<Schema>();
	schema->reserve(view.size());
	for (const auto &[name, unit, meaning] : view)
		schema->push_back({std::string(name), std::string(unit), std::string(meaning)});
	return schema;
}
void Stream::retainSchemas(StoredFrame &frame, std::map<int, StoredRow> *latest)
{
	// A rejected capture or incomplete decoded frame must not retain descriptors.
	auto retain = [&](StoredRow &row) { row.schema = *schemas.insert(row.schema).first; };
	for (auto &row : frame.changes)
		retain(row);
	if (latest)
		for (auto &[player, row] : *latest)
			retain(row);
}
void Stream::capture(const Game &game, Uint32 step)
{
	if (truncated)
		return;
	present = true;
	until = step;
	StoredFrame frame;
	frame.step = step;
	std::map<int, StoredRow> latest;
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		if (game.teams[t])
			for (const auto &s : game.teams[t]->stats.aiTelemetry)
				if (s->active)
				{
					StoredRow row;
					SchemaView descriptors;
					row.team = t;
					row.player = s->player;
					row.name = s->playerName;
					row.available = s->current.available;
					if (row.available)
					{
						const auto fieldCount = std::min(s->fields.size(), s->current.values.size());
						size_t count = s->named.size();
						for (size_t f = 0; f < fieldCount; ++f)
							count += s->current.values[f].valid;
						row.values.reserve(count);
						descriptors.reserve(count);
						for (size_t f = 0; f < fieldCount; ++f)
						{
							const auto &v = s->current.values[f];
							const auto &d = s->fields[f];
							if (v.valid)
							{
								descriptors.emplace_back(d.name, d.unit, d.meaning);
								row.values.push_back({display(d, v), v.updated});
							}
						}
						for (const auto &v : s->named)
						{
							descriptors.emplace_back(v.name, v.unit, v.meaning);
							row.values.push_back({v.value, v.updated});
						}
					}
					const auto prior = current.find(row.player);
					row.schema = schemaFor(descriptors, prior == current.end() ? nullptr : &prior->second.schema);
					latest[row.player] = row;
					if (prior == current.end() || !(prior->second == row))
						frame.changes.push_back(std::move(row));
				}
	for (const auto &[id, row] : current)
		if (!latest.count(id))
		{
			auto gone = row;
			gone.available = false;
			gone.values.clear();
			gone.schema = schemaFor({});
			frame.changes.push_back(std::move(gone));
		}
	if (frame.changes.empty())
		return;
	size_t cost = 8;
	for (const auto &r : frame.changes)
	{
		cost += 32 + r.name.size();
		for (size_t f = 0; f < r.values.size(); ++f)
		{
			const auto &d = (*r.schema)[f];
			cost += 24 + d.name.size() + r.values[f].value.size() + d.unit.size() + d.meaning.size();
		}
	}
	if (cost > Limit - bytes || frames.size() >= 65536)
	{
		truncated = true;
		until = step ? step - 1 : 0;
		return;
	}
	bytes += cost;
	retainSchemas(frame, &latest);
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
			for (size_t f = 0; f < r.values.size(); ++f)
			{
				const auto &v = r.values[f];
				const auto &d = (*r.schema)[f];
				writeText(s, d.name);
				writeText(s, v.value);
				writeText(s, d.unit);
				writeText(s, d.meaning);
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
		StoredFrame frame;
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
			StoredRow row;
			SchemaView descriptors;
			Schema decoded;
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
			row.values.reserve(fields);
			decoded.reserve(fields);
			descriptors.reserve(fields);
			for (unsigned f = 0; f < fields; ++f)
			{
				Metadata d;
				StoredValue v;
				d.name = readText(s, budget);
				v.value = readText(s, budget);
				d.unit = readText(s, budget);
				d.meaning = readText(s, budget);
				v.updated = s->readUint32("updated");
				decoded.push_back(std::move(d));
				row.values.push_back(std::move(v));
			}
			for (const auto &d : decoded)
				descriptors.push_back(d.view());
			if (const auto found = schemas.find(descriptors); found != schemas.end())
				row.schema = *found;
			else
				row.schema = std::make_shared<Schema>(std::move(decoded));
			frame.changes.push_back(std::move(row));
		}
		retainSchemas(frame);
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
					{
						s->named.reserve(row.values.size());
						for (size_t f = 0; f < row.values.size(); ++f)
						{
							const auto &d = (*row.schema)[f];
							const auto &v = row.values[f];
							s->named.push_back({d.name, v.value, d.unit, d.meaning, v.updated});
						}
					}
					series.push_back(std::move(s));
				}
		}
}
} // namespace ReplayTelemetry
