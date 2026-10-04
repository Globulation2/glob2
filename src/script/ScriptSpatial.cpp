// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptSpatial.h"
#include "Game.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace Script
{
namespace
{
int number(const Value &v, const char *key, int fallback, int lo = -32768, int hi = 32767)
{
	return v.get(key).kind == Value::Null ? fallback : v.integer(key, lo, hi);
}
std::string text(const Value &v, const char *key, const char *fallback)
{
	return v.get(key).kind == Value::Null ? fallback : v.string(key);
}
bool boolean(const Value &v, const char *key, bool fallback)
{
	const auto &value = v.get(key);
	if (value.kind == Value::Null)
		return fallback;
	if (value.kind != Value::Boolean)
		throw std::runtime_error(std::string(key) + " must be boolean");
	return value.number != 0;
}
int resource(const Value &v)
{
	if (v.kind == Value::Number)
		return Value::object().set("type", v).integer("type", 0, 7);
	const char *names[] = {"wood", "wheat",  "papyrus", "stone",
						   "alga", "cherry", "orange",  "prune"};
	for (int i = 0; i < 8; ++i)
		if (v.kind == Value::String && v.text == names[i])
			return i;
	throw std::runtime_error("Unknown resource name");
}
int unitType(const Value &v)
{
	if (v.kind == Value::Number)
		return Value::object().set("type", v).integer("type", 0, 2);
	const char *names[] = {"worker", "explorer", "warrior"};
	for (int i = 0; i < 3; ++i)
		if (v.kind == Value::String && v.text == names[i])
			return i;
	throw std::runtime_error("Unknown unit type");
}
int buildingFamily(const Value &v)
{
	if (v.kind == Value::Number)
		return Value::object().set("type", v).integer("type", 0, 12);
	if (v.kind == Value::String)
	{
		auto found = IntBuildingType::conversionMap.find(v.text);
		if (found != IntBuildingType::conversionMap.end())
			return found->second;
	}
	throw std::runtime_error("Unknown building family");
}
void charge(const QueryBudget &b, std::size_t work, std::size_t bytes = 0)
{
	if (b)
		b(work, bytes);
}
int displacement(int a, int b, int size)
{
	int d = (b - a) & (size - 1);
	return d > size / 2 ? d - size : d;
}
struct Box
{
	int x, y, w, h;
};
Box box(const Value &v)
{
	return {v.integer("x", -32768, 32767), v.integer("y", -32768, 32767),
			number(v, "width", 1, 1, 512), number(v, "height", 1, 1, 512)};
}
int gap(int a, int aw, int b, int bw, int size)
{
	int best = size;
	for (int shift = -1; shift <= 1; ++shift)
	{
		int c = b + shift * size;
		best = std::min(best, std::max({0, c - (a + aw), a - (c + bw)}));
	}
	return best;
}
} // namespace
Spatial::Spatial(Game &g, int t, Observations &o)
	: game(g), team(t), width(g.map.getW()), height(g.map.getH()), observations(o)
{
}
int Spatial::index(int x, int y) const
{
	return (y & (height - 1)) * width + (x & (width - 1));
}
void Spatial::begin(const Value &records)
{
	handles.clear();
	reservations = Value::array();
	for (const auto &r : records.items)
		if (r.get("command").get("type").text == "create" &&
			(r.get("status").text == "pending" || r.get("status").text == "issued"))
			reservations.items.push_back(r.get("command"));
}
void Spatial::snapshot()
{
	if (tick == game.stepCounter)
		return;
	cells.resize(std::size_t(width) * height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			cells[index(x, y)] = observations.cell(x, y);
	incomplete = std::any_of(cells.begin(), cells.end(), [](const auto &c) { return !c.known; });
	tick = game.stepCounter;
}
std::vector<int> Spatial::sources(const Value &selector, const QueryBudget &budget)
{
	snapshot();
	charge(budget, cells.size(), cells.size() * sizeof(int));
	std::vector<int> out(cells.size());
	if (selector.get("resource").kind != Value::Null)
	{
		int type = resource(selector.get("resource"));
		bool harvestable = boolean(selector, "harvestable", false);
		for (std::size_t i = 0; i < cells.size(); ++i)
			if (cells[i].known && cells[i].resource == type &&
				(!harvestable || (!cells[i].forbidden && cells[i].amount > 0)))
				out[i] = text(selector, "weight", "count") == "amount" ? cells[i].amount : 1;
	}
	if (selector.get("points").kind != Value::Null)
	{
		const auto &points = selector.get("points");
		if (points.kind != Value::Array || points.items.size() > 4096)
			throw std::runtime_error("Source points exceed limit");
		for (const auto &p : points.items)
			++out[index(p.integer("x", -32768, 32767), p.integer("y", -32768, 32767))];
	}
	for (const char *kind : {"units", "buildings"})
		if (selector.get(kind).kind != Value::Null)
		{
			const auto &filter = selector.get(kind);
			const bool units = std::string(kind) == "units";
			const int ownerFilter = number(filter, "team", -1, 0, Team::MAX_COUNT - 1);
			const int typeFilter =
				filter.get("type").kind == Value::Null
					? -1
					: (units ? unitType(filter.get("type")) : buildingFamily(filter.get("type")));
			const auto relation = text(filter, "relation", "any");
			if (relation != "any" && relation != "own" && relation != "ally" && relation != "enemy")
				throw std::runtime_error("Unknown team relation");
			const bool weighted = units && text(selector, "weight", "count") == "strength";
			const auto &strength = selector.get("strength");
			const int multipliers[] = {number(strength, "worker", 1, 0, 1000),
									   number(strength, "explorer", 1, 0, 1000),
									   number(strength, "warrior", 1, 0, 1000)};
			observations.visitSpatialEntities(
				units, ownerFilter,
				[&](const Observations::SpatialEntity &e)
				{
					if (!units && filter.get("virtual").kind != Value::Null &&
						e.isVirtual != boolean(filter, "virtual", false))
						return;
					const bool allied = (game.teams[team]->allies & (1u << e.team)) != 0;
					if ((relation == "own" && e.team != team) || (relation == "ally" && !allied) ||
						(relation == "enemy" && (e.team == team || allied)))
						return;
					if (typeFilter >= 0 && e.type != typeFilter)
						return;
					int weight = 1;
					if (weighted)
						weight = int(std::min(1000000LL, static_cast<long long>(std::max(1, e.hp)) *
															 std::max(1, e.attack) *
															 multipliers[e.type]));
					auto &cell = out[index(e.x, e.y)];
					cell = int(std::min(2147483647LL, static_cast<long long>(cell) + weight));
				},
				budget);
		}

	return out;
}
std::shared_ptr<Spatial::Field> Spatial::distanceField(const Value &spec, const QueryBudget &budget)
{
	auto source = sources(spec.get("sources"), budget);
	const auto mode = text(spec, "movement", "walk"), metric = text(spec, "metric", "path");
	if (mode != "walk" && mode != "swim" && mode != "fly")
		throw std::runtime_error("movement must be walk, swim or fly");
	if (metric != "path" && metric != "manhattan" && metric != "chebyshev")
		throw std::runtime_error("Unknown distance metric");
	charge(budget, cells.size() * 10, cells.size() * 10);
	std::vector<unsigned char> seeds(cells.size()), passable(cells.size());
	for (std::size_t i = 0; i < cells.size(); ++i)
	{
		seeds[i] = source[i] > 0;
		const auto &c = cells[i];
		const bool water = c.terrain >= 256 && c.terrain < 272;
		passable[i] =
			metric != "path" ||
			(c.known && (mode == "fly" || (!c.building && !c.forbidden &&
										   (mode == "swim" || !water) && c.resource == 255)));
	}
	const auto key = spec.encode();
	auto it = cache.find(key);
	if (it != cache.end() && it->second->sources == seeds && it->second->passable == passable)
		return it->second;
	auto field = std::make_shared<Field>();
	field->metric = metric;
	field->sources = std::move(seeds);
	field->passable = std::move(passable);
	field->distances.assign(cells.size(), -1);
	frontier.clear();
	frontier.reserve(cells.size());
	for (unsigned i = 0; i < cells.size(); ++i)
		if (field->sources[i])
		{
			field->distances[i] = 0;
			frontier.push_back(i);
		}
	for (std::size_t head = 0; head < frontier.size(); ++head)
	{
		const auto from = frontier[head];
		int x = from % width, y = from / width;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if ((!dx && !dy) || (metric == "manhattan" && dx && dy))
					continue;
				int to = index(x + dx, y + dy);
				if (field->distances[to] < 0 && field->passable[to])
				{
					field->distances[to] = field->distances[from] + 1;
					frontier.push_back(to);
				}
			}
	}
	if (cache.size() >= 8 && !cache.contains(key))
		cache.erase(cache.begin());
	cache[key] = field;
	return field;
}
void Spatial::sums(const std::vector<int> &values)
{
	prefix.assign(std::size_t(width + 1) * (height + 1), 0);
	for (int y = 0; y < height; ++y)
	{
		long long row = 0;
		for (int x = 0; x < width; ++x)
		{
			row += values[index(x, y)];
			prefix[std::size_t(y + 1) * (width + 1) + x + 1] =
				prefix[std::size_t(y) * (width + 1) + x + 1] + row;
		}
	}
}
long long Spatial::sum(int x, int y, int w, int h) const
{
	x &= width - 1;
	y &= height - 1;
	w = std::min(w, width);
	h = std::min(h, height);
	long long total = 0;
	for (auto [left, right] :
		 {std::pair{x, std::min(width, x + w)}, std::pair{0, std::max(0, x + w - width)}})
		for (auto [top, bottom] :
			 {std::pair{y, std::min(height, y + h)}, std::pair{0, std::max(0, y + h - height)}})
			total += prefix[std::size_t(bottom) * (width + 1) + right] -
					 prefix[std::size_t(top) * (width + 1) + right] -
					 prefix[std::size_t(bottom) * (width + 1) + left] +
					 prefix[std::size_t(top) * (width + 1) + left];
	return total;
}
Value Spatial::query(const std::string &name, const std::vector<Value> &args,
					 const QueryBudget &budget)
{
	if (args.empty())
		throw std::runtime_error("Spatial query requires arguments");
	const auto &a = args[0];
	if (name == "distance" || name == "displacement" || name == "footprintDistance" ||
		name == "overlap")
	{
		if (args.size() < 2 || args.size() > 3)
			throw std::runtime_error("Geometry query requires two positions");
		auto p = box(a), q = box(args[1]);
		p.x &= width - 1;
		p.y &= height - 1;
		q.x &= width - 1;
		q.y &= height - 1;
		int dx = displacement(p.x, q.x, width), dy = displacement(p.y, q.y, height);
		if (name == "displacement")
			return Value::object().set("x", dx).set("y", dy);
		if (name == "distance")
		{
			const auto metric = args.size() == 3 ? args[2].text : "chebyshev";
			if (metric != "chebyshev" && metric != "manhattan")
				throw std::runtime_error("Geometric metric must be chebyshev or manhattan");
			return Value(metric == "manhattan" ? std::abs(dx) + std::abs(dy)
											   : std::max(std::abs(dx), std::abs(dy)));
		}
		int xgap = gap(p.x, p.w, q.x, q.w, width), ygap = gap(p.y, p.h, q.y, q.h, height);
		if (name == "footprintDistance")
			return Value(std::max(xgap, ygap));
		bool overlap = false;
		for (int oy = -1; oy <= 1; ++oy)
			for (int ox = -1; ox <= 1; ++ox)
				overlap |= p.x < q.x + ox * width + q.w && q.x + ox * width < p.x + p.w &&
						   p.y < q.y + oy * height + q.h && q.y + oy * height < p.y + p.h;
		return Value(overlap);
	}
	if (name != "passable" && name != "summary")
		snapshot();
	if (name == "distanceField")
	{
		if (handles.size() >= 8)
			throw std::runtime_error("At most eight spatial fields per callback");
		auto field = distanceField(a, budget);
		handles.push_back(field);
		return Value::object()
			.set("id", unsigned(handles.size() - 1))
			.set("tick", game.stepCounter);
	}
	if (name == "fieldValue")
	{
		if (args.size() != 3 || a.get("tick").number != game.stepCounter)
			throw std::runtime_error("Field handle expired; reacquire the field each callback");
		unsigned id = a.integer("id", 0, 7);
		if (id >= handles.size())
			throw std::runtime_error("Unknown spatial field");
		int x = Value::object().set("x", args[1]).integer("x", -32768, 32767),
			y = Value::object().set("y", args[2]).integer("y", -32768, 32767);
		int i = index(x, y);
		const auto &c = cells[i];
		int distance = handles[id]->distances[i];
		charge(budget, 1);
		return Value::object()
			.set("known", c.known)
			.set("reachable",
				 c.known && (distance >= 0 || !incomplete) ? Value(distance >= 0) : Value())
			.set("distance", c.known && distance >= 0 ? Value(distance) : Value())
			.set("observedTick", c.tick);
	}
	if (name == "passable")
	{
		auto p = box(a);
		const auto c = observations.cell(p.x, p.y);
		charge(budget, 1);
		const auto mode = text(a, "movement", "walk");
		if (mode != "walk" && mode != "swim" && mode != "fly")
			throw std::runtime_error("Unknown movement mode");
		return c.known ? Value(mode == "fly" ||
							   (!c.building && !c.forbidden && c.resource == 255 &&
								(mode == "swim" || c.terrain < 256 || c.terrain >= 272)))
					   : Value();
	}
	if (name == "summary")
	{
		auto b = box(a);
		if (b.w > width || b.h > height)
			throw std::runtime_error("Region exceeds map dimensions");
		charge(budget, std::size_t(b.w) * b.h);
		int known = 0, visible = 0, tiles = 0, amount = 0;
		double fertility = 0;
		int type = a.get("resource").kind == Value::Null ? -1 : resource(a.get("resource"));
		for (int dy = 0; dy < b.h; ++dy)
			for (int dx = 0; dx < b.w; ++dx)
			{
				const auto c = observations.cell(b.x + dx, b.y + dy);
				if (!c.known)
					continue;
				++known;
				visible += c.visible;
				fertility += c.fertility;
				if ((type < 0 ? c.resource != 255 : c.resource == type) &&
					(!boolean(a, "harvestable", false) || (!c.forbidden && c.amount > 0)))
				{
					++tiles;
					amount += c.amount;
				}
			}
		return Value::object()
			.set("knownTiles", known)
			.set("visibleTiles", visible)
			.set("resourceTiles", tiles)
			.set("amount", amount)
			.set("fertility", known ? Value(fertility / known) : Value());
	}
	if (name == "hotspots")
	{
		auto values = sources(a.get("sources"), budget);
		sums(values);
		int radius = number(a, "radius", 8, 0, 64), limit = number(a, "limit", 8, 0, 64);
		unsigned bits = 0;
		for (size_t n = cells.size(); n > 1; n >>= 1)
			++bits;
		charge(budget, cells.size() * (bits + limit + 6), cells.size() * 24);
		std::vector<std::pair<long long, int>> spots;
		spots.reserve(cells.size());
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				if (cells[index(x, y)].known)
				{
					auto score = sum(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1);
					if (score > 0)
						spots.emplace_back(-score, index(x, y));
				}
		std::sort(spots.begin(), spots.end());
		Value result = Value::array();
		for (const auto &[negative, i] : spots)
		{
			bool nearby = false;
			for (const auto &old : result.items)
				nearby |=
					std::max(std::abs(displacement(i % width, int(old.get("x").number), width)),
							 std::abs(displacement(i / width, int(old.get("y").number), height))) <=
					radius;
			if (!nearby && int(result.items.size()) < limit)
				result.items.push_back(Value::object()
										   .set("x", i % width)
										   .set("y", i / width)
										   .set("score", double(-negative)));
			if (int(result.items.size()) >= limit)
				break;
		}
		return result;
	}
	if (name == "components")
	{
		charge(budget, cells.size() * 10, cells.size() * 8);
		std::vector<int> labels(cells.size(), -1);
		Value components = Value::array();
		const bool pointQuery = a.get("x").kind != Value::Null;
		int nextLabel = 0;
		const auto mode = text(a, "movement", "walk");
		if (mode != "walk" && mode != "swim" && mode != "fly")
			throw std::runtime_error("Unknown movement mode");
		auto open = [&](int i)
		{
			const auto &c = cells[i];
			return c.known &&
				   (mode == "fly" || (!c.building && !c.forbidden && c.resource == 255 &&
									  (mode == "swim" || c.terrain < 256 || c.terrain >= 272)));
		};
		for (unsigned i = 0; i < cells.size(); ++i)
			if (labels[i] < 0 && open(i))
			{
				int label = nextLabel++;
				frontier.clear();
				frontier.push_back(i);
				labels[i] = label;
				for (std::size_t head = 0; head < frontier.size(); ++head)
				{
					int f = frontier[head];
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							int to = index(f % width + dx, f / width + dy);
							if (labels[to] < 0 && open(to))
							{
								labels[to] = label;
								frontier.push_back(to);
							}
						}
				}
				if (!pointQuery)
					components.items.push_back(Value::object()
												   .set("id", label)
												   .set("x", int(i) % width)
												   .set("y", int(i) / width)
												   .set("tiles", unsigned(frontier.size())));
				if (components.items.size() > 1024)
					throw std::runtime_error(
						"Component result exceeds limit; request a component at a point");
			}
		if (pointQuery)
		{
			int at = index(a.integer("x", -32768, 32767), a.integer("y", -32768, 32767));
			return cells[at].known ? Value(labels[at]) : Value();
		}
		return components;
	}
	if (name == "placement")
		return placement(a, args.size() > 1 ? args[1] : Value::array(), budget);
	throw std::runtime_error("Unknown spatial query: " + name);
}
Value Spatial::placement(const Value &spec, const Value &staged, const QueryBudget &budget)
{
	const auto name = spec.string("building");
	int type = globalContainer->buildingsTypes.getTypeNum(name, 0, true);
	if (type < 0)
		type = globalContainer->buildingsTypes.getTypeNum(name, 0, false);
	if (type < 0)
		throw std::runtime_error("Unknown building family");
	const auto *bt = globalContainer->buildingsTypes.get(type);
	int workers = number(spec, "workers", 2, 0, 20),
		future = number(spec, "futureWorkers", workers, 0, 20);
	int ox = 0, oy = 0, bw = bt->width, bh = bt->height;
	if (boolean(spec, "reserveUpgrade", true) && !bt->isVirtual)
	{
		const auto *next = bt;
		int left = 0, top = 0, right = bw, bottom = bh;
		for (int n = 0; n < 16 && next->nextLevel >= 0; ++n)
		{
			next = globalContainer->buildingsTypes.get(next->nextLevel);
			int x = next->decLeft - bt->decLeft, y = next->decTop - bt->decTop;
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x + next->width);
			bottom = std::max(bottom, y + next->height);
		}
		ox = left;
		oy = top;
		bw = right - left;
		bh = bottom - top;
	}
	int clearance = number(spec, "clearance", 1, 0, 8), limit = number(spec, "limit", 1, 1, 16);
	Box area{0, 0, width, height};
	if (spec.get("region").kind != Value::Null)
		area = box(spec.get("region"));
	if (area.w > width || area.h > height)
		throw std::runtime_error("Placement region exceeds map");
	struct Metric
	{
		Value spec;
		std::vector<int> values;
		bool hard;
		int weight, minimum, maximum;
	};
	std::vector<Metric> metrics;
	for (const auto *list : {"constraints", "preferences"})
		if (spec.get(list).kind != Value::Null)
		{
			const auto &items = spec.get(list);
			if (items.kind != Value::Array || items.items.size() > 8)
				throw std::runtime_error("At most eight constraints/preferences per list");
			for (const auto &term : items.items)
			{
				Metric m{term,
						 {},
						 std::string(list) == "constraints",
						 number(term, "weight", 1, -1000000, 1000000),
						 0,
						 0x7fffffff};
				// Validate once, even when no candidate has a buildable footprint.
				// Malformed API arguments must not depend on the observed map.
				if (m.hard)
				{
					m.minimum = number(term, "min", 0, -0x7fffffff, 0x7fffffff);
					m.maximum = number(term, "max", 0x7fffffff, -0x7fffffff, 0x7fffffff);
				}
				const auto kind = text(term, "metric", "distance");
				if (kind == "distance")
				{
					Value query = term;
					query.set("metric", text(term, "distanceMetric", "path"));
					m.values = distanceField(query, budget)->distances;
				}
				else if (kind == "fertility" || kind == "resourceDensity" || kind == "threat")
				{
					std::vector<int> raw(cells.size());
					if (kind == "fertility")
					{
						for (std::size_t i = 0; i < cells.size(); ++i)
							raw[i] = cells[i].known ? cells[i].fertility : 0;
					}
					else
					{
						Value selector = term.get("sources");
						if (kind == "resourceDensity")
							selector = Value::object()
										   .set("resource", term.get("resource"))
										   .set("harvestable", boolean(term, "harvestable", true));
						raw = sources(selector, budget);
					}
					sums(raw);
					int radius = number(term, "radius", 8, 0, 64);
					charge(budget, cells.size() * 5, cells.size() * 12);
					m.values.resize(cells.size());
					for (int y = 0; y < height; ++y)
						for (int x = 0; x < width; ++x)
						{
							auto value =
								sum(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1);
							if (kind == "fertility")
								value /= std::min(width, 2 * radius + 1) *
										 std::min(height, 2 * radius + 1);
							m.values[index(x, y)] =
								int(std::min(value, static_cast<long long>(0x7fffffff)));
						}
				}
				else
					throw std::runtime_error("Unknown placement metric");
				metrics.push_back(std::move(m));
			}
		}
	charge(budget,
		   std::size_t(area.w) * area.h *
			   (std::size_t(bw + 2 * clearance) * (bh + 2 * clearance) + metrics.size() + 1),
		   cells.size());
	std::vector<unsigned char> reserved(cells.size());
	auto reserve = [&](const Value &command)
	{
		if (command.get("type").text != "create")
			return;
		int t = command.integer("buildingType", 0, int(globalContainer->buildingsTypes.size()) - 1);
		const auto *b = globalContainer->buildingsTypes.get(t);
		if (b->isVirtual)
			return;
		int x = command.integer("x", 0, width - 1), y = command.integer("y", 0, height - 1),
			rw = number(command, "reservedWidth", b->width, 1, 32),
			rh = number(command, "reservedHeight", b->height, 1, 32),
			rx = number(command, "reservedX", x), ry = number(command, "reservedY", y);
		for (int dy = 0; dy < rh; ++dy)
			for (int dx = 0; dx < rw; ++dx)
				reserved[index(rx + dx, ry + dy)] = 1;
	};
	for (const auto &c : reservations.items)
		reserve(c);
	for (const auto &c : staged.items)
		reserve(c);
	// Preserve upgrade footprints around existing owned buildings as well.
	if (boolean(spec, "reserveUpgrade", true))
	{
		auto buildings =
			observations.query("buildings", {Value::object().set("team", team)}, budget);
		for (const auto &b : buildings.items)
		{
			if (b.get("virtual").number)
				continue;
			const auto *base = globalContainer->buildingsTypes.get(int(b.get("type").number));
			const auto *next = base;
			for (int n = 0; n < 16; ++n)
			{
				int x = int(b.get("x").number) + next->decLeft - base->decLeft,
					y = int(b.get("y").number) + next->decTop - base->decTop;
				for (int dy = 0; dy < next->height; ++dy)
					for (int dx = 0; dx < next->width; ++dx)
						reserved[index(x + dx, y + dy)] = 1;
				if (next->nextLevel < 0)
					break;
				next = globalContainer->buildingsTypes.get(next->nextLevel);
			}
		}
	}
	std::shared_ptr<Field> access;
	if (!bt->isVirtual && boolean(spec, "reachable", true))
	{
		Value selector = Value::object().set(
			"buildings", Value::object().set("relation", "own").set("virtual", false));
		if (spec.get("anchor").kind != Value::Null)
			selector = Value::object().set("points", Value::array());
		if (spec.get("anchor").kind != Value::Null)
		{
			Value points = Value::array();
			points.items.push_back(spec.get("anchor"));
			selector.set("points", points);
		}
		access = distanceField(Value::object()
								   .set("sources", selector)
								   .set("movement", text(spec, "movement", "walk")),
							   budget);
	}
	std::vector<std::pair<long long, int>> best;
	int rejectedSpace = 0, rejectedConstraints = 0;
	for (int dy = 0; dy < area.h; ++dy)
		for (int dx = 0; dx < area.w; ++dx)
		{
			int x = (area.x + dx) & (width - 1), y = (area.y + dy) & (height - 1), i = index(x, y);
			bool valid = true;
			if (bt->isVirtual)
				valid = cells[i].known;
			else
				for (int yy = -clearance; yy < bh + clearance && valid; ++yy)
					for (int xx = -clearance; xx < bw + clearance; ++xx)
					{
						int at = index(x + ox + xx, y + oy + yy);
						const auto &c = cells[at];
						bool inside = xx >= 0 && xx < bw && yy >= 0 && yy < bh;
						if (!c.known || c.building || reserved[at] ||
							(inside && (!c.visible || c.terrain >= 16 || c.resource != 255)))
						{
							valid = false;
							break;
						}
					}
			if (valid && access)
			{
				bool reachable = false;
				for (int yy = -1; yy <= bt->height; ++yy)
					for (int xx = -1; xx <= bt->width; ++xx)
						if (xx == -1 || yy == -1 || xx == bt->width || yy == bt->height)
							reachable |= access->distances[index(x + xx, y + yy)] >= 0;
				valid = reachable;
			}
			if (!valid)
			{
				++rejectedSpace;
				continue;
			}
			long long score = 0;
			for (const auto &m : metrics)
			{
				int value = m.values[i];
				if (m.hard)
				{
					if (value < 0 || value < m.minimum || value > m.maximum)
					{
						valid = false;
						break;
					}
				}
				else
				{
					if (value < 0)
					{
						valid = false;
						break;
					}
					score += static_cast<long long>(value) * m.weight;
				}
			}
			if (!valid)
			{
				++rejectedConstraints;
				continue;
			}
			best.emplace_back(-score, i);
			std::sort(best.begin(), best.end());
			if (int(best.size()) > limit)
				best.pop_back();
		}
	Value result = Value::object()
					   .set("found", !best.empty())
					   .set("rejectedSpace", rejectedSpace)
					   .set("rejectedConstraints", rejectedConstraints);
	Value candidates = Value::array();
	for (const auto &[negative, i] : best)
	{
		Value scores = Value::array();
		for (const auto &m : metrics)
			scores.items.push_back(Value::object()
									   .set("metric", text(m.spec, "metric", "distance"))
									   .set("value", m.values[i])
									   .set("weight", m.weight));
		candidates.items.push_back(Value::object()
									   .set("x", i % width)
									   .set("y", i / width)
									   .set("score", double(-negative))
									   .set("scores", scores));
	}
	result.set("candidates", candidates);
	if (!best.empty())
	{
		int x = best[0].second % width, y = best[0].second / width;
		Value order = Value::object()
						  .set("type", "create")
						  .set("buildingType", type)
						  .set("x", x)
						  .set("y", y)
						  .set("workers", workers)
						  .set("futureWorkers", future);
		if (bt->isVirtual)
			order.set("range", number(spec, "range", 8, 0, 255));
		else
			order.set("reservedX", (x + ox) & (width - 1))
				.set("reservedY", (y + oy) & (height - 1))
				.set("reservedWidth", bw)
				.set("reservedHeight", bh);
		result.set("order", order);
	}
	else
		result.set("reason", "No observed location satisfies the footprint and constraints");
	return result;
}
} // namespace Script
