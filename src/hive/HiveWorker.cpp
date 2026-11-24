// SPDX-License-Identifier: GPL-3.0-or-later
#include <nlohmann/json.hpp>
#include "PowerOfTwo.h"
#include "HiveWorker.h"
#include "scripting/javascript/ScriptRuntime.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/syscall.h>
#include <cstddef>
#include <cerrno>
#endif
namespace Hive
{
Json json(const Script::Value &v)
{
	switch (v.kind)
	{
	case Script::Value::Null:
		return nullptr;
	case Script::Value::Boolean:
		return v.number != 0;
	case Script::Value::Number:
		// JavaScript has one number type. Integral values (IDs, ticks, counts)
		// are published as JSON integers so results read as 3, not 3.0.
		if (std::isfinite(v.number) && std::trunc(v.number) == v.number &&
			std::fabs(v.number) <= 9007199254740992.0)
			return std::int64_t(v.number);
		return v.number;
	case Script::Value::String:
		return v.text;
	case Script::Value::Array:
	{
		Json a = Json::array();
		for (auto &x : v.items)
			a.push_back(json(x));
		return a;
	}
	case Script::Value::Object:
	{
		Json o = Json::object();
		for (auto &[k, x] : v.fields)
			o[k] = json(x);
		return o;
	}
	}
	throw std::runtime_error("Invalid script data");
}
Script::Value value(const Json &j, unsigned depth)
{
	if (depth > 64)
		throw std::runtime_error("Data is too deeply nested");
	if (j.is_null())
		return {};
	if (j.is_boolean())
		return Script::Value(j.get<bool>());
	if (j.is_number())
	{
		double n = j.get<double>();
		if (!std::isfinite(n))
			throw std::runtime_error("Invalid number");
		return Script::Value(n);
	}
	if (j.is_string())
		return Script::Value(j.get<std::string>());
	auto v = j.is_array() ? Script::Value::array() : Script::Value::object();
	if (j.is_array())
		for (auto &x : j)
			v.items.push_back(value(x, depth + 1));
	else
		for (auto it = j.begin(); it != j.end(); ++it)
			v.set(it.key(), value(it.value(), depth + 1));
	return v;
}
namespace
{
// Per run: 250x the simulation scripts' work units (a whole-map scan with per-tile
// logic, about 2 s of compute), and a 4x JavaScript heap.
constexpr std::uint64_t WorkBudget = 250 * Script::FuelLimit;
constexpr std::size_t HeapLimit = 128 * 1024 * 1024;
} // namespace
namespace
{
// A whole number in minimum..maximum; what names the argument in the error, which also
// gives the range and the value received.
int integer(const Json &j, int minimum, int maximum, const std::string &what)
{
	const auto fail = [&]
	{
		auto got = j.is_null() ? std::string("nothing") : j.dump();
		if (got.size() > 40)
			got = got.substr(0, 37) + "...";
		return std::runtime_error(what + " must be a whole number " + std::to_string(minimum) + ".." +
								  std::to_string(maximum) + " (got " + got + ")");
	};
	if (!j.is_number())
		throw fail();
	double v = j.get<double>();
	if (!std::isfinite(v) || std::floor(v) != v || v < minimum || v > maximum)
		throw fail();
	return int(v);
}
// An object's field that must be present (what names it in the error).
const Json &field(const Json &o, const char *key, const std::string &what)
{
	if (!o.is_object() || !o.contains(key))
		throw std::runtime_error(what + " needs " + key);
	return o.at(key);
}
constexpr int NoResource = 65535;
int wrap(int v, int size)
{
	return dimensionRemainder(dimensionRemainder(v, size) + size, size);
}
// A team filter: a number, or "mine", "enemies" or "allies" relative to this colony.
std::function<bool(int)> teamFilter(const Json &s, const Json &f)
{
	if (!f.contains("team") || f["team"].is_null())
		return [](int) { return true; };
	const int self = s.at("team");
	if (f["team"].is_string())
	{
		const auto which = f["team"].get<std::string>();
		std::uint32_t allies = 0;
		for (const auto &t : s.at("teams"))
			if (t.value("id", -1) == self)
				allies = t.value("allies", 0u);
		if (which == "mine")
			return [self](int t) { return t == self; };
		if (which == "allies")
			return [self, allies](int t) { return t != self && t >= 0 && (allies & (1u << t)); };
		if (which == "enemies")
			return [self, allies](int t) { return t != self && t >= 0 && !(allies & (1u << t)); };
		throw std::runtime_error("team must be a team number, \"mine\", \"enemies\" or \"allies\"");
	}
	const int team = integer(f["team"], 0, int(s.at("teams").size()) - 1, "team");
	return [team](int t) { return t == team; };
}
const Json *typeOf(const Json &s, int id)
{
	for (const auto &t : s.at("buildingTypes"))
		if (t.value("id", -1) == id)
			return &t;
	return nullptr;
}
// The variant a player places for a building or flag name: level 0's construction
// site, or the flag itself (unless level/site say otherwise).
const Json *placeable(const Json &s, const std::string &name, int level = 0, int site = -1)
{
	const Json *found = nullptr;
	for (const auto &t : s.at("buildingTypes"))
		if (t.value("name", std::string()) == name && t.value("level", -1) == level)
		{
			const bool isSite = t.value("site", false), isFlag = t.value("virtual", false);
			if (site >= 0 ? isSite == bool(site) : (isSite || isFlag))
				return &t;
			if (!found)
				found = &t;
		}
	return site >= 0 ? nullptr : found;
}
// The names a player can place (level 0's site, or a flag), for errors about others.
std::string placeableNames(const Json &s)
{
	std::vector<std::string> names;
	for (const auto &t : s.at("buildingTypes"))
		if (t.value("level", -1) == 0 && (t.value("site", false) || t.value("virtual", false)))
			if (const auto name = t.value("name", std::string());
				std::find(names.begin(), names.end(), name) == names.end())
				names.push_back(name);
	std::string out;
	for (const auto &name : names)
		out += (out.empty() ? "" : ", ") + name;
	return out;
}
bool knownName(const Json &s, const std::string &name)
{
	for (const auto &t : s.at("buildingTypes"))
		if (t.value("name", std::string()) == name)
			return true;
	return false;
}
// Tiles covered by buildings this colony knows of (its own, others in sight), by its
// pending placements, and by spots findPlacement already gave out in this run. Each
// covered tile says by what: Claimed values.
enum Claimed : char
{
	ClaimNone = 0,
	ClaimOther = 1,
	ClaimOwn = 2,
	ClaimPending = 3
};
std::vector<char> occupied(const Json &s, const Json &reserved = Json::array())
{
	const int width = s.at("width"), height = s.at("height");
	std::vector<char> mask(std::size_t(width) * height, ClaimNone);
	const auto claim = [&](const Json &f)
	{
		for (int dy = 0; dy < f.value("height", 1); ++dy)
			for (int dx = 0; dx < f.value("width", 1); ++dx)
				mask[std::size_t(wrap(f.value("y", 0) + dy, height)) * width + wrap(f.value("x", 0) + dx, width)] = ClaimPending;
	};
	for (const auto &f : s.value("pending", Json::array()))
		claim(f);
	for (const auto &f : reserved)
		claim(f);
	const int self = s.at("team");
	for (const auto &b : s.at("buildings"))
		if (const auto *t = typeOf(s, b.value("type", -1)); t && !t->value("virtual", false))
			for (int dy = 0; dy < t->value("height", 1); ++dy)
				for (int dx = 0; dx < t->value("width", 1); ++dx)
					mask[std::size_t(wrap(b.value("y", 0) + dy, height)) * width + wrap(b.value("x", 0) + dx, width)] =
						b.value("team", -1) == self ? ClaimOwn : ClaimOther;
	return mask;
}
// As far as this colony can tell: explored, one buildable terrain on all four corners,
// no resource and no known building. Empty when it is; otherwise what is in the way.
// The game still decides when the order arrives.
std::string blocked(const Json &s, const Json &tile, const std::vector<char> &taken, int x, int y)
{
	if (!tile.value("explored", false))
		return "unexplored";
	switch (taken[std::size_t(y) * int(s.at("width")) + x])
	{
	case ClaimOther:
		return "another team's building";
	case ClaimOwn:
		return "one of your buildings";
	case ClaimPending:
		return "a pending placement (yours, or one findPlacement gave out in this run)";
	default:
		break;
	}
	const auto &corners = tile.at("corners");
	const int terrain = corners.at(0);
	for (const auto &c : corners)
		if (c != terrain)
			return "mixed terrain (its corners differ)";
	const auto &terrains = s.at("terrainTypes");
	if (terrain < 0 || terrain >= int(terrains.size()) || !terrains[terrain].value("buildable", false))
		return "unbuildable terrain" + (terrain >= 0 && terrain < int(terrains.size())
											? " (" + terrains[terrain].value("name", std::string("?")) + ")"
											: std::string());
	if (const int resource = tile.at("resource").value("type", NoResource); resource != NoResource)
	{
		const auto &resources = s.at("resourceTypes");
		return "a resource" + (resource >= 0 && resource < int(resources.size())
								   ? " (" + resources[resource].value("key", std::string("?")) + ")"
								   : std::string());
	}
	if (tile.contains("building") && tile.value("building", NoResource) != NoResource)
		return "a building";
	return {};
}
bool buildable(const Json &s, const Json &tile, const std::vector<char> &taken, int x, int y)
{
	return blocked(s, tile, taken, x, y).empty();
}
std::string canPlace(const Json &s, const Json &type, int x, int y, const std::vector<char> &taken)
{
	const int width = s.at("width"), height = s.at("height");
	if (type.value("virtual", false))
	{
		for (const auto &b : s.at("buildings"))
			if (b.value("team", -1) == s.at("team").get<int>() && b.value("x", -1) == wrap(x, width) &&
				b.value("y", -1) == wrap(y, height))
				if (const auto *t = typeOf(s, b.value("type", -1)); t && t->value("virtual", false))
					return "another of your flags is on that tile";
		return {};
	}
	for (int dy = 0; dy < type.value("height", 1); ++dy)
		for (int dx = 0; dx < type.value("width", 1); ++dx)
		{
			const int tx = wrap(x + dx, width), ty = wrap(y + dy, height);
			if (const auto why = blocked(s, s.at("tiles").at(std::size_t(ty) * width + tx), taken, tx, ty); !why.empty())
				return "tile (" + std::to_string(tx) + ", " + std::to_string(ty) + "): " + why;
		}
	return {};
}
const Json &placeableNamed(const Json &s, const Json &name)
{
	if (!name.is_string())
		throw std::runtime_error("Expected a building or flag name such as \"inn\" or \"warflag\"");
	const auto *type = placeable(s, name.get<std::string>());
	if (!type)
		throw std::runtime_error("Unknown building or flag \"" + name.get<std::string>() + "\"; names: " +
								 placeableNames(s));
	return *type;
}
Json query(const Json &s, const std::string &name, const Json &args, Json &reserved)
{
	const int width = s.at("width"), height = s.at("height");
	if (name == "teams" || name == "buildingTypes" || name == "terrainTypes" || name == "resourceTypes")
		return s.at(name);
	if (name == "units" || name == "buildings")
	{
		Json f = args.empty() ? Json::object() : args.at(0);
		if (!f.is_object())
			throw std::runtime_error("Expected query options");
		// A misspelled key would otherwise widen the query silently.
		const std::vector<std::string> keys = name == "units"
												  ? std::vector<std::string>{"team", "type", "offset", "limit"}
												  : std::vector<std::string>{"team", "type", "offset", "limit", "remembered"};
		for (const auto &[key, value] : f.items())
			if (std::find(keys.begin(), keys.end(), key) == keys.end())
			{
				std::string valid;
				for (const auto &k : keys)
					valid += (valid.empty() ? "" : ", ") + k;
				throw std::runtime_error(name + " filter has an unknown key \"" + key + "\"; valid keys: " + valid);
			}
		bool remembered = false;
		if (f.contains("remembered") && !f["remembered"].is_null())
		{
			if (!f["remembered"].is_boolean())
				throw std::runtime_error("buildings remembered must be true or false");
			remembered = f["remembered"].get<bool>();
		}
		const auto team = teamFilter(s, f);
		// type: a number, or a name ("warrior"; "inn", "warflag").
		std::function<bool(const Json &)> typeMatch = [](const Json &) { return true; };
		if (f.contains("type") && !f["type"].is_null())
		{
			if (f["type"].is_number() && remembered)
				throw std::runtime_error("remembered buildings filter type by family name (\"inn\"), not number");
			if (f["type"].is_number())
			{
				const int wanted = integer(f["type"], 0, 65535, name + " type");
				const std::string field = name == "units" ? "type" : "shortType";
				typeMatch = [wanted, field](const Json &v) { return v.value(field, -1) == wanted; };
			}
			else if (name == "units")
			{
				static const std::vector<std::string> kinds = {"worker", "explorer", "warrior"};
				const auto it = std::find(kinds.begin(), kinds.end(), f["type"].get<std::string>());
				if (it == kinds.end())
					throw std::runtime_error("Unit type must be worker, explorer or warrior");
				const int wanted = int(it - kinds.begin());
				typeMatch = [wanted](const Json &v) { return v.value("type", -1) == wanted; };
			}
			else
			{
				const auto wanted = f["type"].get<std::string>();
				typeMatch = [wanted, &s](const Json &v)
				{
					if (v.contains("familyName"))
						return v.value("familyName", std::string()) == wanted;
					const auto *t = typeOf(s, v.value("type", -1));
					return t && t->value("name", std::string()) == wanted;
				};
			}
		}
		int offset =
			f.contains("offset") && !f["offset"].is_null() ? integer(f["offset"], 0, 32768, name + " offset") : 0;
		int limit =
			f.contains("limit") && !f["limit"].is_null() ? integer(f["limit"], 0, 32768, name + " limit") : 32768;
		Json out = Json::array();
		static const Json none = Json::array();
		const Json &source = remembered ? (s.contains("remembered") ? s.at("remembered") : none) : s.at(name);
		for (auto &v : source)
			if (team(v.value("team", -1)) && typeMatch(v))
			{
				if (offset)
				{
					--offset;
					continue;
				}
				if (int(out.size()) >= limit)
					break;
				out.push_back(v);
			}
		return out;
	}
	if (name == "unit" || name == "building")
	{
		if (args.empty() || !args[0].is_object())
			throw std::runtime_error("Expected entity reference");
		int id = integer(field(args[0], "id", name + " reference"), 0, 65535, name + " id");
		if (!args[0].contains("generation") || !args[0]["generation"].is_number())
			return nullptr;
		for (auto &v : s.at(name == "unit" ? "units" : "buildings"))
			if (v.at("id") == id && v.at("generation") == args[0]["generation"])
				return v;
		return nullptr;
	}
	if (name == "tile" || name == "region")
	{
		if (args.size() < 2)
			throw std::runtime_error(name + "(x, y" + (name == "region" ? ", width, height" : "") + ") needs a tile");
		int x = integer(args[0], -32768, 32767, name + " x"), y = integer(args[1], -32768, 32767, name + " y");
		const auto &resources = s.at("resourceTypes");
		auto tile = [&](int tx, int ty) -> Json
		{
			Json t = s.at("tiles").at(std::size_t(wrap(ty, height)) * width + wrap(tx, width));
			// The resource's key ("wheat", "rice"), so scripts need not decode map-local IDs.
			if (t.contains("resource"))
			{
				const int r = t["resource"].value("type", NoResource);
				if (r != NoResource && r >= 0 && r < int(resources.size()))
					t["resource"]["key"] = resources[r].value("key", std::string());
			}
			return t;
		};
		if (name == "tile")
			return tile(x, y);
		int w = integer(args.size() > 2 ? args[2] : Json(), 0, 256, "region width"), h = integer(args.size() > 3 ? args[3] : Json(), 0, 256, "region height");
		Json out = Json::array();
		for (int yy = 0; yy < h; yy++)
			for (int xx = 0; xx < w; xx++)
				out.push_back(tile(x + xx, y + yy));
		return out;
	}
	// Hive helpers (Host::gameExtensions / mapExtensions), all from this snapshot.
	if (name == "buildingType")
	{
		if (args.empty() || !args[0].is_string())
			throw std::runtime_error("buildingType(name, {level, site}) needs a name such as \"inn\"");
		const Json o = args.size() > 1 && args[1].is_object() ? args[1] : Json::object();
		// An unknown name is an error, as for findPlacement; a known name without that
		// level or site is null.
		if (!knownName(s, args[0].get<std::string>()))
			throw std::runtime_error("Unknown building or flag \"" + args[0].get<std::string>() + "\"; names: " +
									 placeableNames(s));
		const auto *t = placeable(s, args[0].get<std::string>(), o.contains("level") ? integer(o["level"], 0, 3, "buildingType level") : 0,
								  o.contains("site") ? int(o["site"].get<bool>()) : -1);
		return t ? *t : Json(nullptr);
	}
	if (name == "events")
	{
		const unsigned since = args.empty() || args[0].is_null() ? 0u : unsigned(integer(args[0], 0, 0x7fffffff, "events since"));
		Json out = Json::array();
		for (const auto &e : s.value("events", Json::array()))
			if (e.value("tick", 0u) > since)
				out.push_back(e);
		return out;
	}
	if (name == "scan")
	{
		if (args.size() < 4)
			throw std::runtime_error("scan(x, y, width, height, fields) needs a rectangle");
		const int x = integer(args[0], -32768, 32767, "scan x"), y = integer(args[1], -32768, 32767, "scan y");
		const int w = integer(args[2], 1, 256, "scan width"), h = integer(args[3], 1, 256, "scan height");
		static const std::vector<std::string> known = {"explored", "visible", "resource", "amount",
													   "terrain", "buildable", "building"};
		std::vector<std::string> fields = {"resource", "buildable"};
		if (args.size() > 4 && !args[4].is_null())
		{
			fields.clear();
			for (const auto &field : args[4])
			{
				if (!field.is_string() || std::find(known.begin(), known.end(), field.get<std::string>()) == known.end())
					throw std::runtime_error("scan fields are explored, visible, resource, amount, terrain, "
											 "buildable and building");
				fields.push_back(field);
			}
		}
		const auto taken = occupied(s);
		Json out = {{"x", wrap(x, width)}, {"y", wrap(y, height)}, {"width", w}, {"height", h}};
		for (const auto &field : fields)
			out[field] = Json::array();
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx)
			{
				const int tx = wrap(x + xx, width), ty = wrap(y + yy, height);
				const auto &t = s.at("tiles").at(std::size_t(ty) * width + tx);
				const bool explored = t.value("explored", false);
				for (const auto &field : fields)
				{
					int v = 0;
					if (field == "explored")
						v = explored;
					else if (field == "visible")
						v = t.value("visible", false);
					else if (field == "resource")
						v = explored ? t.at("resource").value("type", NoResource) : NoResource;
					else if (field == "amount")
						v = explored ? t.at("resource").value("amount", 0) : 0;
					else if (field == "terrain")
					{
						v = -1;
						if (explored)
						{
							const auto &c = t.at("corners");
							if (c[0] == c[1] && c[1] == c[2] && c[2] == c[3])
								v = c[0];
						}
					}
					else if (field == "buildable")
						v = buildable(s, t, taken, tx, ty);
					else
						v = t.value("building", NoResource);
					out[field].push_back(v);
				}
			}
		return out;
	}
	if (name == "canPlace")
	{
		if (args.size() < 3)
			throw std::runtime_error("canPlace(name, x, y) needs a building or flag name and a tile");
		const auto &type = placeableNamed(s, args[0]);
		const auto why = canPlace(s, type, integer(args[1], -32768, 32767, "canPlace x"), integer(args[2], -32768, 32767, "canPlace y"), occupied(s, reserved));
		return why.empty() ? Json{{"ok", true}} : Json{{"ok", false}, {"reason", why}};
	}
	if (name == "findPlacement")
	{
		if (args.size() < 2 || !args[1].is_object())
			throw std::runtime_error("findPlacement(name, {x, y, radius}) needs a name and where to look");
		const auto &type = placeableNamed(s, args[0]);
		const auto &o = args[1];
		const int x = integer(field(o, "x", "findPlacement"), -32768, 32767, "findPlacement x"),
				  y = integer(field(o, "y", "findPlacement"), -32768, 32767, "findPlacement y");
		const int radius = o.contains("radius") ? integer(o["radius"], 0, 64, "findPlacement radius") : 12;
		const auto taken = occupied(s, reserved);
		// Nearest ring first, as game_place near does. The spot is reserved for the rest of
		// this run, so a second call finds another one.
		for (int r = 0; r <= radius; ++r)
			for (int dy = -r; dy <= r; ++dy)
				for (int dx = -r; dx <= r; ++dx)
					if (std::max(std::abs(dx), std::abs(dy)) == r && canPlace(s, type, x + dx, y + dy, taken).empty())
					{
						Json spot = {{"x", wrap(x + dx, width)}, {"y", wrap(y + dy, height)}};
						reserved.push_back({{"x", spot["x"]}, {"y", spot["y"]}, {"width", type.value("width", 1)},
											{"height", type.value("height", 1)}});
						return spot;
					}
		return nullptr;
	}
	throw std::runtime_error("Unavailable colony query");
}
} // namespace
Json invoke(const Json &request)
{
	try
	{
		const auto source = request.at("source").get<std::string>();
		auto runtime = Script::makeRuntime();
		runtime->validate(source);
		if (request.value("validate", false))
			return {{"ok", true}};
		const auto &snapshot = request.at("snapshot");
		Script::Host host;
		host.commander = true;
		// Colony scripts run outside the simulation, in this disposable worker: they get
		// room for whole-map scans with real logic. The worker's 5 s deadline and 4 s CPU
		// limit still bound every run.
		host.workBudget = WorkBudget;
		host.memoryLimit = HeapLimit;
		host.gameExtensions = {"buildingType", "events"};
		host.mapExtensions = {"scan", "canPlace", "findPlacement"};
		host.team = snapshot.at("team");
		host.tick = snapshot.at("tick");
		host.width = snapshot.at("width");
		host.height = snapshot.at("height");
		if (host.team < 0 || host.team >= 32 || !host.width || !host.height)
			throw std::runtime_error("Invalid colony context");
		Json wakes = Json::array(), reserved = Json::array();
		std::uint32_t rng = request.value("random", 1u);
		host.random = [&]
		{
			rng = rng * 1664525u + 1013904223u;
			return rng;
		};
		host.query = [&](const std::string &name, const std::vector<Script::Value> &args,
						 const Script::QueryBudget &budget)
		{
			Json a = Json::array();
			for (auto &v : args)
				a.push_back(json(v));
			if (name == "wakeAgent")
			{
				if (a.size() != 1 || !a[0].is_object() || wakes.size() >= 4)
					throw std::runtime_error("Invalid commander wake request");
				const auto &w = a[0];
				const auto key = w.at("key").get<std::string>(),
						   reason = w.at("reason").get<std::string>();
				if (key.empty() || key.size() > 80 || reason.empty() || reason.size() > 512 ||
					w.dump().size() > 4096)
					throw std::runtime_error("Wake request too large");
				wakes.push_back(w);
				return Script::Value();
			}
			Json output = query(snapshot, name, a, reserved);
			const auto size = output.dump().size();
			budget(size / 8 + 1, size * 4);
			return value(output);
		};
		Json initialState = request.value("state", Json(nullptr));
		bool initialized = request.value("initialized", false);
		if (request.contains("migration"))
		{
			auto migrated = invoke({{"source", request.at("migration")},
									{"snapshot", snapshot},
									{"state", {{"previous", request.at("previousState")}}},
									{"initialized", false}});
			if (!migrated.value("ok", false) || !migrated.value("orders", Json::array()).empty() ||
				!migrated.value("wakes", Json::array()).empty())
				throw std::runtime_error("Migration must only return a checkpoint");
			initialState = migrated.at("output");
			initialized = !initialState.is_null();
		}
		auto result = runtime->invoke(source, value(initialState), !initialized, host);
		Json effects = json(result.effects);
		if (!effects.is_object())
			throw std::runtime_error("Return an object containing output and orders");
		auto orders = effects.value("orders", Json::array());
		if (!orders.is_array())
			throw std::runtime_error("orders must be an array of order descriptors");
		if (orders.size() > 32)
			throw std::runtime_error("Return at most 32 orders per invocation (returned " +
									 std::to_string(orders.size()) + ")");
		if (effects.dump().size() > 65536)
			throw std::runtime_error("Returned output and orders exceed 64 KiB");
		return {{"ok", true},
				{"state", json(result.state)},
				{"installedState", initialState},
				{"installedInitialized", initialized},
				{"output", effects.value("output", Json(nullptr))},
				{"orders", orders},
				{"wakes", wakes},
				{"random", rng}};
	}
	catch (const std::exception &e)
	{
		return {{"ok", false}, {"diagnostic", std::string(e.what()).substr(0, 4096)}};
	}
}
int workerMain()
{
#ifdef __linux__
	// The dynamic loader has finished. Admit only anonymous memory, pipe I/O,
	// clocks, signals and process exit. No path lookup, sockets, process creation,
	// ptrace or descriptor acquisition is permitted, even after an engine exploit.
#if defined(__x86_64__)
	constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
	constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#else
	return 2; // Fail closed until this architecture has a reviewed syscall profile.
#endif
#if defined(__x86_64__) || defined(__aarch64__)
#define HIVE_ALLOW(n)                                                                              \
	BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_##n, 0, 1),                                           \
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
	sock_filter filter[] = {BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
							BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
							BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
							BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
							HIVE_ALLOW(read),
							HIVE_ALLOW(write),
							HIVE_ALLOW(close),
							HIVE_ALLOW(fstat),
							HIVE_ALLOW(brk),
							HIVE_ALLOW(mmap),
							HIVE_ALLOW(munmap),
							HIVE_ALLOW(mremap),
							HIVE_ALLOW(mprotect),
							HIVE_ALLOW(madvise),
							HIVE_ALLOW(futex),
							HIVE_ALLOW(clock_gettime),
							HIVE_ALLOW(gettimeofday),
							HIVE_ALLOW(getrandom),
							HIVE_ALLOW(rt_sigaction),
							HIVE_ALLOW(rt_sigprocmask),
							HIVE_ALLOW(rt_sigreturn),
							HIVE_ALLOW(sigaltstack),
							HIVE_ALLOW(getpid),
							HIVE_ALLOW(gettid),
							HIVE_ALLOW(exit),
							HIVE_ALLOW(exit_group),
							BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)};
#undef HIVE_ALLOW
	sock_fprog program{static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])), filter};
#endif
#endif
#ifdef __linux__
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
		return 2;
	// Address space for the process, its snapshot and a HeapLimit JavaScript heap.
	rlimit limit{384 * 1024 * 1024, 384 * 1024 * 1024};
	setrlimit(RLIMIT_AS, &limit);
	rlimit files{0, 0};
	setrlimit(RLIMIT_FSIZE, &files);
	rlimit cpu{4, 4};
	setrlimit(RLIMIT_CPU, &cpu);
#if defined(__x86_64__) || defined(__aarch64__)
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
		return 2;
#endif
#endif
	std::string line;
	line.reserve(65536);
	// A fresh process per invocation has no engine world, credentials, or script host I/O bindings.
	char c;
	while (std::cin.get(c) && c != '\n')
	{
		if (line.size() >= 64 * 1024 * 1024)
			return 2;
		line += c;
	}
	try
	{
		std::cout << invoke(Json::parse(line)).dump() << '\n';
		return 0;
	}
	catch (...)
	{
		return 2;
	}
}
} // namespace Hive
