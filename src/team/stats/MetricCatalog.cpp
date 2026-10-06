// SPDX-License-Identifier: GPL-3.0-or-later
#include "MetricCatalog.h"
#include "MetricSeries.h"
#include "BuildingType.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <map>

namespace Stats
{
namespace
{
using M = GameplayMeasurements;

template <class T> double total(const T &value)
{
	return double(value);
}
template <class T, size_t N> double total(const T (&values)[N])
{
	double sum = 0;
	for (const auto &v : values)
		sum += total(v);
	return sum;
}

double deathsBy(const M &m, int cause)
{
	double sum = 0;
	for (const auto &row : m.deaths)
		sum += double(row[cause]);
	return sum;
}

double damageBy(const Uint64 (&damage)[M::DAMAGE_SOURCES][M::TARGETS], int source)
{
	return total(damage[source]);
}

std::vector<Band> perUnitType(std::function<double(const M &, int)> value)
{
	const char *keys[NB_UNIT_TYPE] = {"[stat band workers]", "[stat band explorers]", "[stat band warriors]"};
	std::vector<Band> bands;
	for (int type = 0; type < NB_UNIT_TYPE; ++type)
		bands.push_back({keys[type], [value, type](const M &m) { return value(m, type); }});
	return bands;
}

std::vector<Band> perResource(std::function<double(const M &, int)> value)
{
	const char *keys[MAX_RESOURCES] = {"[Wood]", "[Wheat]", "[Papyrus]", "[Stone]", "[Alga]", "[Cherry]", "[Orange]", "[Prune]"};
	std::vector<Band> bands;
	for (int resource = 0; resource < MAX_RESOURCES; ++resource)
		bands.push_back({keys[resource], [value, resource](const M &m) { return value(m, resource); }});
	return bands;
}

std::vector<Band> perWeapon(bool dealt)
{
	const char *keys[M::DAMAGE_SOURCES] = {"[stat band melee]", "[stat band magic]", "[stat band towers]"};
	std::vector<Band> bands;
	for (int source = 0; source < M::DAMAGE_SOURCES; ++source)
		bands.push_back({keys[source], [dealt, source](const M &m) { return damageBy(dealt ? m.damageDealt : m.damageReceived, source); }});
	return bands;
}

std::vector<Metric> build()
{
	std::vector<Metric> all;
	auto add = [&all](Metric metric) -> Metric &
	{
		all.push_back(std::move(metric));
		return all.back();
	};
	auto births = [](const M &m) { return total(m.births); };
	auto deaths = [](const M &m) { return total(m.deaths); };
	auto dealt = [](const M &m) { return total(m.damageDealt); };
	auto received = [](const M &m) { return total(m.damageReceived); };

	// Population
	{
		Metric &m = add({.id = "population", .group = Group::Population, .sampled = EndOfGameStat::TYPE_UNITS});
		m.shareable = true;
	}
	{
		Metric &m = add({.id = "births", .group = Group::Population, .kind = Metric::Counter, .value = births});
		m.splitKey = "[stat view by unit type]";
		m.bands = perUnitType([](const M &s, int type) { return double(s.births[type]); });
	}
	{
		Metric &m = add({.id = "deaths", .group = Group::Population, .kind = Metric::Counter, .value = deaths});
		m.splitKey = "[stat view by unit type]";
		m.bands = perUnitType([](const M &s, int type) { return total(s.deaths[type]); });
		m.perUnits = true;
	}
	add({.id = "net growth", .group = Group::Population, .kind = Metric::Counter, .value = births, .minus = deaths});
	{
		Metric &m = add({.id = "death causes", .group = Group::Population, .kind = Metric::Counter, .value = deaths});
		m.unitKey = "[stat unit deaths]";
		const std::pair<const char *, int> causes[] = {{"[stat band combat]", M::COMBAT},
													   {"[stat band starvation]", M::STARVATION},
													   {"[stat band clearing]", M::CLEARING},
													   {"[stat band trapped]", M::TRAPPED},
													   {"[stat band other]", M::UNKNOWN}};
		for (const auto &[key, cause] : causes)
			m.bands.push_back({key, [cause](const M &s) { return deathsBy(s, cause); }});
		m.composition = true;
	}
	{
		Metric &m = add({.id = "conversions", .group = Group::Population, .kind = Metric::Counter,
						 .value = [](const M &m) { return total(m.conversionsIn); },
						 .minus = [](const M &m) { return total(m.conversionsOut); }});
		m.rare = true;
	}

	// Food and wellbeing
	{
		Metric &m = add({.id = "hunger", .group = Group::Food, .value = [](const M &s) { return total(s.lowFood[1]); }});
		const char *keys[] = {"[stat band starving]", "[stat band hungry]", "[stat band peckish]"};
		for (int band = 0; band < 3; ++band)
			m.bands.push_back({keys[band], [band](const M &s) { return total(s.lowFood[band]); }});
		m.remainderKey = "[stat band fed]";
		m.composition = m.perUnits = m.perUnitsByDefault = m.extended = true;
	}
	{
		Metric &m = add({.id = "health", .group = Group::Food, .value = [](const M &s) { return total(s.lowHP[1]); }});
		const char *keys[] = {"[stat band critical]", "[stat band wounded]", "[stat band hurt]"};
		for (int band = 0; band < 3; ++band)
			m.bands.push_back({keys[band], [band](const M &s) { return total(s.lowHP[band]); }});
		m.remainderKey = "[stat band healthy]";
		m.composition = m.perUnits = m.perUnitsByDefault = m.extended = true;
	}
	{
		Metric &m = add({.id = "seeking food", .group = Group::Food, .value = [](const M &s) { return double(s.hungry); }});
		m.smoothed = true;
		m.splitKey = "[stat view by state]";
		m.bands = {{"[stat band weakening]", [](const M &s) { return double(s.critical); }},
				   {"[stat band looking]", [](const M &s) { return double(s.hungry) - double(std::min(s.hungry, s.critical)); }}};
		m.perUnits = m.perUnitsByDefault = true;
	}
	{
		Metric &m = add({.id = "in care", .group = Group::Food, .value = [](const M &s) { return double(s.feeding + s.healing); }});
		m.smoothed = true;
		m.splitKey = "[stat view by state]";
		m.bands = {{"[stat band eating]", [](const M &s) { return double(s.feeding); }},
				   {"[stat band healing]", [](const M &s) { return double(s.healing); }}};
		m.perUnits = m.perUnitsByDefault = true;
	}
	add({.id = "wheat harvested", .group = Group::Food, .kind = Metric::Counter, .unitKey = "[stat unit wheat]",
		 .value = [](const M &m) { return double(m.harvested[WHEAT]); }});
	add({.id = "wheat stored", .group = Group::Food, .unitKey = "[stat unit wheat]",
		 .value = [](const M &m) { return double(m.stock[WHEAT]); }});
	{
		Metric &m = add({.id = "meals", .group = Group::Food, .kind = Metric::Counter, .unitKey = "[stat unit meals]",
						 .value = [](const M &s) { return double(s.meals); }});
		m.perUnits = true;
	}
	{
		Metric &m = add({.id = "starvation", .group = Group::Food, .kind = Metric::Counter,
						 .value = [](const M &s) { return deathsBy(s, M::STARVATION); }});
		m.rare = true;
		m.perUnits = true;
	}
	add({.id = "healing", .group = Group::Food, .kind = Metric::Counter, .unitKey = "[stat unit hit points]",
		 .value = [](const M &m) { return double(m.hpRestored); }});

	// Workers' time. Every worker counts once a tick in one activity, so a rate
	// per minute scaled by workerTicks is the workers doing it, on average.
	const double workerTicks = 1 / TICKS_PER_MINUTE;
	auto filling = [](const M &m) { return total(m.filling); };
	auto workerTime = [](const M &m) { return total(m.labour) + total(m.filling); };
	{
		// Idle is stacked last, so it reads down from the 100% line. Time spent
		// hungry or hurt with nowhere to go is its own band: it is waiting, and
		// would otherwise pass for eating or healing.
		Metric &m = add({.id = "worker time", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit workers]",
						 .value = filling, .ratioOf = workerTime});
		m.bands = {{"[stat band working]", filling},
				   {"[stat band flag work]", [](const M &s) { return double(s.labour[M::FLAG_WORK]); }},
				   {"[stat band training]", [](const M &s) { return double(s.labour[M::TRAIN_WALKING] + s.labour[M::TRAIN_INSIDE]); }},
				   {"[stat band eating]", [](const M &s) { return double(s.labour[M::EAT_WALKING] + s.labour[M::EAT_INSIDE]); }},
				   {"[stat band healing]", [](const M &s) { return double(s.labour[M::HEAL_WALKING] + s.labour[M::HEAL_INSIDE]); }},
				   {"[stat band unserved]", [](const M &s) { return double(s.labour[M::EAT_NO_INN] + s.labour[M::HEAL_NO_HOSPITAL]); }},
				   {"[stat band other]", [](const M &s) { return double(s.labour[M::OTHER_ACTIVITY]); }},
				   {"[stat band idle]", [](const M &s) { return double(s.labour[M::IDLE]); }}};
		m.scale = workerTicks;
		m.composition = m.percentByDefault = m.labour = true;
	}
	{
		Metric &m = add({.id = "idle workers", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit worker time]",
						 .value = [](const M &s) { return double(s.labour[M::IDLE]); }, .ratioOf = workerTime});
		m.labour = true;
	}
	{
		Metric &m = add({.id = "work by job", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit workers]", .value = filling});
		const char *keys[M::LABOUR_JOBS] = {"[stat band swarms]", "[stat band inns]", "[stat band sites]", "[stat band other buildings]"};
		for (int job = 0; job < M::LABOUR_JOBS; ++job)
			m.bands.push_back({keys[job], [job](const M &s) { return total(s.filling[job]); }});
		m.scale = workerTicks;
		m.composition = m.labour = true;
	}
	{
		Metric &m = add({.id = "haul distance", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit squares]",
						 .value = [](const M &s) { return total(s.harvestDistance); }, .ratioOf = [](const M &s) { return total(s.harvestSamples); }});
		m.mean = m.labour = true;
	}
	{
		// Its headline is the share of working time spent walking either way.
		Metric &m = add({.id = "hauling", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit working workers]",
						 .value = [](const M &s)
						 {
							 double walking = 0;
							 for (const auto &job : s.filling)
								 walking += double(job[M::TO_RESOURCE] + job[M::TO_BUILDING]);
							 return walking;
						 },
						 .ratioOf = filling});
		const char *keys[M::LABOUR_PHASES] = {"[stat band to resource]", "[stat band harvesting]", "[stat band carrying]", "[stat band delivering]"};
		for (int phase = 0; phase < M::LABOUR_PHASES; ++phase)
			m.bands.push_back({keys[phase], [phase](const M &s)
							   {
								   double sum = 0;
								   for (const auto &job : s.filling)
									   sum += double(job[phase]);
								   return sum;
							   }});
		m.scale = workerTicks;
		m.composition = m.labour = true;
	}
	{
		// Its headline is the share of workers' time spent hungry with no inn to go to.
		Metric &m = add({.id = "eating time", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit workers]",
						 .value = [](const M &s) { return double(s.labour[M::EAT_NO_INN]); }, .ratioOf = workerTime});
		m.bands = {{"[stat band walking to inn]", [](const M &s) { return double(s.labour[M::EAT_WALKING]); }},
				   {"[stat band eating]", [](const M &s) { return double(s.labour[M::EAT_INSIDE]); }},
				   {"[stat band no inn]", [](const M &s) { return double(s.labour[M::EAT_NO_INN]); }}};
		m.scale = workerTicks;
		m.composition = m.labour = true;
	}
	{
		Metric &m = add({.id = "inn distance", .group = Group::Work, .kind = Metric::Counter, .unitKey = "[stat unit squares]",
						 .value = [](const M &s) { return double(s.eatWalkDistance); }, .ratioOf = [](const M &s) { return double(s.eatWalkSamples); }});
		m.mean = m.labour = true;
	}

	// Resources
	{
		Metric &m = add({.id = "gathered", .group = Group::Resources, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.harvested); }});
		m.splitKey = "[stat view by resource]";
		m.bands = perResource([](const M &s, int r) { return double(s.harvested[r]); });
	}
	{
		Metric &m = add({.id = "stored", .group = Group::Resources, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.stock); }});
		m.splitKey = "[stat view by resource]";
		m.bands = perResource([](const M &s, int r) { return double(s.stock[r]); });
	}
	{
		Metric &m = add({.id = "spending", .group = Group::Resources, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.consumed); }});
		const char *keys[M::PURPOSES] = {"[stat band meals]", "[stat band new units]", "[stat band ammunition]",
										 "[stat band construction]", "[stat band upgrades]", "[stat band healing]", "[stat band training]"};
		for (int purpose = 0; purpose < M::PURPOSES; ++purpose)
			m.bands.push_back({keys[purpose], [purpose](const M &s) { return total(s.consumed[purpose]); }});
		m.composition = true;
	}
	{
		Metric &m = add({.id = "delivered", .group = Group::Resources, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.delivered); }});
		m.splitKey = "[stat view by resource]";
		m.bands = perResource([](const M &s, int r) { return double(s.delivered[r]); });
	}
	add({.id = "traded", .group = Group::Resources, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
		 .value = [](const M &m) { return total(m.transferredIn); },
		 .minus = [](const M &m) { return total(m.transferredOut); }});

	// Buildings
	{
		Metric &m = add({.id = "buildings", .group = Group::Buildings, .unitKey = "[stat unit buildings]",
						 .sampled = EndOfGameStat::TYPE_BUILDINGS});
		m.splitKey = "[stat view by building]";

		m.shareable = true;
	}
	{
		Metric &m = add({.id = "construction", .group = Group::Buildings, .kind = Metric::Counter, .unitKey = "[stat unit buildings]",
						 .value = [](const M &s) { if(s.variants.empty()) return total(s.completed); double sum=0; for(const auto& v:s.variants) sum+=total(v.completed); return sum; }});
		m.rare = true;
		m.splitKey = "[stat view by work]";
		const char *keys[M::COMPLETIONS] = {"[stat band built]", "[stat band upgraded]", "[stat band repaired]"};
		for (int kind = 0; kind < M::COMPLETIONS; ++kind)
			m.bands.push_back({keys[kind], [kind](const M &s) { if(s.variants.empty()) return total(s.completed[kind]); double sum=0; for(const auto& v:s.variants) sum+=v.completed[kind]; return sum; }});
	}
	{
		Metric &m = add({.id = "buildings lost", .group = Group::Buildings, .kind = Metric::Counter, .unitKey = "[stat unit buildings]",
						 .value = [](const M &s) { if(s.variants.empty()) return total(s.removed); double sum=0; for(const auto& v:s.variants) sum+=total(v.removed); return sum; }});
		m.rare = true;
		m.splitKey = "[stat view by cause]";
		const char *keys[M::REMOVALS] = {"[stat band destroyed]", "[stat band demolished]", "[stat band other]"};
		for (int kind = 0; kind < M::REMOVALS; ++kind)
			m.bands.push_back({keys[kind], [kind](const M &s) { if(s.variants.empty()) return total(s.removed[kind]); double sum=0; for(const auto& v:s.variants) sum+=v.removed[kind]; return sum; }});
	}
	{
		Metric &m = add({.id = "blocked buildings", .group = Group::Buildings, .unitKey = "[stat unit buildings]",
						 .value = [](const M &s) { if(s.variants.empty()) return total(s.trappedBuildings[1][0]); double sum=0; for(const auto& v:s.variants) sum+=v.trapped[1][0]; return sum; }});
		m.extended = true;
	}

	// Military
	{
		Metric &m = add({.id = "attack", .group = Group::Military, .unitKey = "[stat unit strength]", .sampled = EndOfGameStat::TYPE_ATTACK});
		m.shareable = true;
	}
	{
		Metric &m = add({.id = "defence", .group = Group::Military, .unitKey = "[stat unit strength]", .sampled = EndOfGameStat::TYPE_DEFENSE});
		m.shareable = true;
	}
	add({.id = "hit points", .group = Group::Military, .unitKey = "[stat unit hit points]", .sampled = EndOfGameStat::TYPE_HP});
	{
		Metric &m = add({.id = "damage dealt", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit damage]", .value = dealt});
		m.splitKey = "[stat view by weapon]";
		m.bands = perWeapon(true);
		m.shareable = true;
	}
	{
		Metric &m = add({.id = "damage taken", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit damage]", .value = received});
		m.splitKey = "[stat view by weapon]";
		m.bands = perWeapon(false);
	}
	add({.id = "damage traded", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit damage]", .value = dealt, .minus = received});
	{
		Metric &m = add({.id = "combat deaths", .group = Group::Military, .kind = Metric::Counter,
						 .value = [](const M &s) { return deathsBy(s, M::COMBAT); }});
		m.bands = perUnitType([](const M &s, int type) { return double(s.deaths[type][M::COMBAT]); });
		m.splitKey = "[stat view by unit type]";
		m.perUnits = true;
	}
	add({.id = "accuracy", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit shots]",
		 .value = [](const M &m) { return total(m.impacts); }, .ratioOf = [](const M &m) { return total(m.shots); }});
	{
		Metric &m = add({.id = "training", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit visits]",
						 .value = [](const M &s) { return total(s.trainingVisits); }});
		m.splitKey = "[stat view by unit type]";
		m.bands = perUnitType([](const M &s, int type) { return double(s.trainingVisits[type]); });
	}
	add({.id = "skills", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit skills]",
		 .value = [](const M &m) { return total(m.abilityGains); }});

	auto warriors = [](const M &m) { return total(m.warriors); };
	{
		Metric &m = add({.id = "warriors", .group = Group::Military, .unitKey = "[stat unit warriors]", .value = warriors});
		const char *keys[M::PLACES] = {"[stat band at home]", "[stat band at enemy]", "[stat band in the open]"};
		for (int place = 0; place < M::PLACES; ++place)
			m.bands.push_back({keys[place], [place](const M &s) { return double(s.warriors[place]); }});
		m.splitKey = "[stat view by place]";
		m.shareable = m.labour = true;
	}
	{
		// Recorded as the sum of two skills counted from 0; shown as their average
		// on the 1 to 4 scale the game shows a unit's levels on.
		Metric &m = add({.id = "warrior level", .group = Group::Military, .unitKey = "[stat unit attack level]",
						 .value = [](const M &s) { return total(s.warriorLevels) + 2 * total(s.warriors); },
						 .ratioOf = [](const M &s) { return 2 * total(s.warriors); }});
		m.mean = m.labour = true;
	}
	{
		// A hurt warrior's assignment becomes its hospital, so the two are disjoint.
		// Its headline is the share sent to war flags.
		Metric &m = add({.id = "warrior duties", .group = Group::Military, .unitKey = "[stat unit warriors]",
						 .value = [](const M &s) { return double(s.warriorsFlagged); }, .ratioOf = warriors});
		m.bands = {{"[stat band war flag]", [](const M &s) { return double(s.warriorsFlagged); }},
				   {"[stat band seeking healing]", [](const M &s) { return double(s.warriorsHurt); }},
				   {"[stat band free]", [](const M &s) { return std::max(0.0, total(s.warriors) - double(s.warriorsFlagged) - double(s.warriorsHurt)); }}};
		m.composition = m.percentByDefault = m.smoothed = m.labour = true;
	}
	{
		Metric &m = add({.id = "intruders", .group = Group::Military, .unitKey = "[stat unit enemy warriors]",
						 .value = [](const M &s) { return double(s.intruders); }});
		m.smoothed = m.labour = true;
	}
	{
		// Strength counts a warrior and each of its attack levels as one.
		Metric &m = add({.id = "home defence", .group = Group::Military, .unitKey = "[stat unit strength count]",
						 .value = [](const M &s) { return double(s.warriors[M::HOME] + s.warriorLevels[M::HOME]); },
						 .minus = [](const M &s) { return double(s.intruders + s.intruderLevels); }});
		m.smoothed = m.labour = true;
	}
	{
		Metric &m = add({.id = "combat death places", .group = Group::Military, .kind = Metric::Counter, .unitKey = "[stat unit deaths]",
						 .value = [](const M &s) { return total(s.combatDeathPlace); }});
		const char *keys[M::PLACES] = {"[stat band at home]", "[stat band at enemy]", "[stat band in the open]"};
		for (int place = 0; place < M::PLACES; ++place)
			m.bands.push_back({keys[place], [place](const M &s)
							   {
								   double sum = 0;
								   for (const auto &type : s.combatDeathPlace)
									   sum += double(type[place]);
								   return sum;
							   }});
		m.composition = m.rare = m.labour = true;
	}

	// Map
	{
		Metric &m = add({.id = "growth nearby", .group = Group::Map, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.growthAmount[2]); }});
		m.unitKey = "[stat unit growth]";
		m.splitKey = "[stat view by distance]";
		// The recorded ranges are nested (within 8, 16 and 32 tiles).
		m.bands = {{"[stat band within 8]", [](const M &s) { return total(s.growthAmount[0]); }},
				   {"[stat band 8 to 16]", [](const M &s) { return std::max(0.0, total(s.growthAmount[1]) - total(s.growthAmount[0])); }},
				   {"[stat band 16 to 32]", [](const M &s) { return std::max(0.0, total(s.growthAmount[2]) - total(s.growthAmount[1])); }}};
		m.extended = true;
	}
	{
		Metric &m = add({.id = "growth map", .group = Group::Map, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.growthGlobal[1]); }});
		m.unitKey = "[stat unit growth]";
		m.splitKey = "[stat view by resource]";
		m.bands = perResource([](const M &s, int r) { return double(s.growthGlobal[1][r]); });
		m.extended = m.global = true;
	}
	{
		Metric &m = add({.id = "stranded", .group = Group::Map, .value = [](const M &s) { return total(s.trappedUnits[1]); }});
		m.smoothed = true;
		m.perUnits = m.perUnitsByDefault = m.extended = true;
	}
	{
		Metric &m = add({.id = "cleared", .group = Group::Map, .kind = Metric::Counter, .unitKey = "[stat unit resources]",
						 .value = [](const M &s) { return total(s.cleared); }});
		m.splitKey = "[stat view by resource]";
		m.bands = perResource([](const M &s, int r) { return double(s.cleared[r]); });
	}

	// Score
	add({.id = "prestige", .group = Group::Score, .unitKey = "[stat unit prestige]", .sampled = EndOfGameStat::TYPE_PRESTIGE});
	return all;
}

//! Whether `point` holds everything `metric` needs under `view`.
bool covered(const Metric &metric, const View &view, const TeamHistory &history, const Point &point)
{
	const bool split = view.split;
	const bool needsMeasurements = split || metric.sampled < 0;
	// The population comes from the sampled values: as the metric itself, as the
	// whole a remainder band is taken from, or as what a value is relative to.
	const bool needsSampled = !needsMeasurements || (split && metric.remainderKey) || (!split && view.relative);
	if (needsSampled && !point.sampled)
		return false;
	if (!needsMeasurements)
		return true;
	if (!point.measurements)
		return false;
	// Later additions are zero in samples taken before a save gained them.
	if (metric.extended && history.extendedCoverageStartTick != 0 && point.tick <= history.extendedCoverageStartTick)
		return false;
	return !metric.labour || history.labourCoverageStartTick == 0 || point.tick > history.labourCoverageStartTick;
}

//! The samples of one team that cover a metric, read a column at a time.
struct Samples
{
	std::vector<const Point *> points;
	std::vector<Uint32> ticks;

	Samples(const Metric &metric, const View &view, const TeamHistory &history)
	{
		for (const auto &point : history.points)
			if (covered(metric, view, history, point))
			{
				points.push_back(&point);
				ticks.push_back(point.tick);
			}
	}
	std::vector<double> measured(const Extract &extract) const
	{
		std::vector<double> values;
		for (const Point *point : points)
			values.push_back(extract(*point->measurements));
		return values;
	}
	std::vector<double> sampled(int type) const
	{
		std::vector<double> values;
		for (const Point *point : points)
			values.push_back(point->sampled[type]);
		return values;
	}
	//! A value as the view shows it: a counter as its rate per minute or as
	//! recorded, a level as sampled or averaged.
	std::vector<double> rateOrTotal(const Metric &metric, const View &view, std::vector<double> raw) const
	{
		if (metric.kind == Metric::Counter)
		{
			if (view.total)
				return raw;
			auto rate = ratePerMinute(ticks, raw, view.window);
			for (double &value : rate)
				value *= metric.scale;
			return rate;
		}
		return metric.smoothed ? movingAverage(raw, view.window) : raw;
	}
};

//! The bands of a split metric, one series each.
std::vector<std::vector<double>> bandSeries(const Metric &metric, const View &view, const Samples &samples)
{
	std::vector<std::vector<double>> bands;
	for (const auto &band : metric.bands)
		bands.push_back(samples.measured(band.value));
	if (metric.remainderKey)
		bands = disjointBands(bands, samples.sampled(EndOfGameStat::TYPE_UNITS));
	for (auto &band : bands)
		band = samples.rateOrTotal(metric, view, std::move(band));
	if (view.relative)
		normaliseToPercent(bands);
	return bands;
}

//! The single series of a metric that is not split.
std::vector<double> lineSeries(const Metric &metric, const View &view, const Samples &samples)
{
	std::vector<double> values = metric.sampled >= 0 ? samples.sampled(metric.sampled)
													 : samples.rateOrTotal(metric, view, samples.measured(metric.value));
	if (metric.minus)
	{
		const auto minus = samples.rateOrTotal(metric, view, samples.measured(metric.minus));
		for (std::size_t i = 0; i < values.size(); ++i)
			values[i] -= minus[i];
	}
	if (metric.ratioOf)
	{
		const auto whole = samples.rateOrTotal(metric, view, samples.measured(metric.ratioOf));
		values = metric.mean ? meanOf(values, whole) : ratioPercent(values, whole);
	}
	if (view.relative)
	{
		const auto population = samples.sampled(EndOfGameStat::TYPE_UNITS);
		if (metric.kind == Metric::Gauge)
			// The count was averaged above; the population is averaged alike, so
			// one hungry unit of an early handful does not read as a tenth of it.
			values = percentOf(values, metric.smoothed ? movingAverage(population, view.window) : population);
		else
			// Per 100 units: not a share of anything, so it may exceed 100.
			for (std::size_t i = 0; i < values.size(); ++i)
				values[i] = population[i] > 0 ? values[i] * 100.0 / population[i] : 0.0;
	}
	return values;
}

//! Turns each team's line into its percentage of all teams at the same tick.
void shareAcrossTeams(std::vector<TeamSeries> &teams)
{
	// Summed by tick, not by index: teams loaded from different saves may not
	// share every sample.
	std::map<Uint32, double> totals;
	for (const auto &team : teams)
		for (std::size_t i = 0; i < team.ticks.size(); ++i)
			totals[team.ticks[i]] += std::max(0.0, team.values[0][i]);
	for (auto &team : teams)
		for (std::size_t i = 0; i < team.ticks.size(); ++i)
		{
			const double sum = totals[team.ticks[i]];
			team.values[0][i] = sum > 0 ? std::max(0.0, team.values[0][i]) * 100.0 / sum : 0.0;
		}
}

//! validView(), except that a composition may be left unsplit: text panels read
//! its headline value.
View restrictedView(const Metric &metric, View view, bool compositionStaysSplit)
{
	view.window = std::clamp(view.window, 1, MAX_RATE_WINDOW);
	view.total = view.total && canTotal(metric);
	view.split = (compositionStaysSplit && metric.composition) || (view.split && canSplit(metric));
	view.share = view.share && canShare(metric, view);
	view.relative = view.relative && canRelative(metric, view);
	return view;
}

//! buildChart() for a view that is already restricted to what the metric offers.
Chart chartOf(const Metric &metric, const View &view, const std::vector<TeamHistory> &teams)
{
	Chart chart;
	chart.stacked = view.split;
	chart.global = metric.global;
	chart.ordered = view.split && metric.remainderKey;
	// A split metric's ratio is its headline in text panels, not what is charted.
	const bool ratio = metric.ratioOf && !view.split;
	chart.percent = view.share || (ratio && !metric.mean) || (view.relative && (view.split || metric.kind == Metric::Gauge));
	chart.decimals = chart.percent || ratio || view.relative || metric.smoothed || (metric.kind == Metric::Counter && !view.total);
	if (view.split)
	{
		for (const auto &band : metric.bands)
        {
			chart.bandKeys.push_back(band.labelKey);
            chart.bandLabelLiteral.push_back(band.literalLabel);
        }
		if (metric.remainderKey)
			chart.bandKeys.push_back(metric.remainderKey);
	}
	for (const auto &history : teams)
	{
		const Samples samples(metric, view, history);
		TeamSeries series;
		series.team = history.team;
		series.ticks = samples.ticks;
		if (view.split)
			series.values = bandSeries(metric, view, samples);
		else
			series.values.push_back(lineSeries(metric, view, samples));
		// The sample at tick 0 is taken before anything is counted: population 0,
		// and no interval behind a rate. The series starts at the first real one.
		if (series.ticks.size() > 1 && series.ticks.front() == 0)
		{
			series.ticks.erase(series.ticks.begin());
			for (auto &band : series.values)
				band.erase(band.begin());
		}
		chart.teams.push_back(std::move(series));
		// Every team recorded the same map-wide values: the first stands for all.
		if (metric.global)
			break;
	}
	if (view.share)
		shareAcrossTeams(chart.teams);
	for (const auto &team : chart.teams)
		for (std::size_t i = 0; i < team.ticks.size(); ++i)
		{
			double value = 0;
			for (const auto &band : team.values)
				value += band[i];
			// A gap (a ratio with nothing to measure) has no value to range over.
			if (std::isnan(value))
				continue;
			chart.any |= value != 0;
			chart.low = std::min(chart.low, value);
			chart.high = std::max(chart.high, value);
		}
	return chart;
}
} // namespace

const char *groupKey(Group group)
{
	static const char *const keys[] = {"[stat group population]", "[stat group food]", "[stat group work]", "[stat group resources]", "[stat group buildings]",
									   "[stat group military]", "[stat group map]", "[stat group score]"};
	static_assert(std::size(keys) == std::size_t(Group::Count), "one text key per group");
	assert(group < Group::Count);
	return keys[int(group)];
}

const std::vector<Metric> &catalog()
{
	static const std::vector<Metric> metrics = build();
	return metrics;
}

std::vector<Metric> catalogForBuildings(const BuildingsTypes& buildings)
{
    auto metrics=catalog();
    auto& metric=metrics[findMetric("buildings")];
    for (size_t id=0; id<buildings.size(); ++id)
    {
        const auto& type=*buildings.get(id);
        if (type.isBuildingSite || !type.semantics.occupiesGround) continue;
        // The key disambiguates same-name stages and survives translation changes.
        const std::string label=type.presentation.displayName.empty() ? type.key :
            type.presentation.displayName+" ("+type.key+")";
        metric.bands.push_back({label,[id](const M& sample) {
            return id<sample.variants.size() ? double(sample.variants[id].count) : 0.;
        },true});
    }
    return metrics;
}

int findMetric(const std::string &id)
{
	const auto &metrics = catalog();
	for (std::size_t i = 0; i < metrics.size(); ++i)
		if (id == metrics[i].id)
			return int(i);
	return -1;
}

const Metric &metricById(const std::string &id)
{
	const int index = findMetric(id);
	assert(index >= 0);
	return catalog()[std::size_t(index)];
}

int windowMinutes(int samples)
{
	return std::max(1, int(std::lround(samples * SAMPLE_INTERVAL_TICKS / TICKS_PER_MINUTE)));
}

bool canTotal(const Metric &metric)
{
	// A ratio of two counters has no running total, and a total of worker-ticks
	// means nothing to a player.
	return metric.kind == Metric::Counter && !metric.ratioOf && metric.scale == 1;
}
bool canSplit(const Metric &metric)
{
	// A composition is always split, so it does not offer the choice.
	return !metric.bands.empty() && !metric.composition;
}
bool canRelative(const Metric &metric, const View &view)
{
	return view.split ? true : metric.perUnits && !view.share;
}
bool canShare(const Metric &metric, const View &view)
{
	return metric.shareable && !view.split;
}

View validView(const Metric &metric, View view)
{
	return restrictedView(metric, view, true);
}

View defaultView(const Metric &metric)
{
	View view;
	view.total = metric.rare;
	view.split = metric.composition;
	view.relative = (metric.composition && (metric.remainderKey || metric.percentByDefault)) || metric.perUnitsByDefault;
	return validView(metric, view);
}

TeamHistory historyOf(int team, const TeamStats &stats)
{
	TeamHistory history;
	history.team = team;
	history.extendedCoverageStartTick = stats.extendedCoverageStartTick;
	history.labourCoverageStartTick = stats.labourCoverageStartTick;
	const auto &sampled = stats.getEndOfGameStats();
	const auto &measured = stats.measurementHistory;
	// Both are appended every sample interval, but measurements may start later
	// (a save from before they existed). Sampled values carry no tick of their
	// own: the n-th was taken at the n-th interval.
	const Uint64 never = ~Uint64(0);
	std::size_t s = 0, m = 0;
	while (s < sampled.size() || m < measured.size())
	{
		const Uint64 sampledTick = s < sampled.size() ? Uint64(s) * SAMPLE_INTERVAL_TICKS : never;
		const Uint64 measuredTick = m < measured.size() ? measured[m].tick : never;
		Point point;
		point.tick = Uint32(std::min(sampledTick, measuredTick));
		if (sampledTick <= measuredTick)
			point.sampled = sampled[s++].value;
		if (measuredTick <= sampledTick)
			point.measurements = &measured[m++];
		history.points.push_back(point);
	}
	return history;
}

Chart buildChart(const Metric &metric, const View &view, const std::vector<TeamHistory> &teams)
{
	return chartOf(metric, validView(metric, view), teams);
}

std::vector<Marker> markers(const TeamHistory &history)
{
	std::vector<Marker> found;
	bool populated = false, wiped = false, fought = false, razed = false;
	for (const auto &point : history.points)
	{
		if (point.sampled)
		{
			const int units = point.sampled[EndOfGameStat::TYPE_UNITS];
			if (populated && units == 0 && !wiped)
				found.push_back({point.tick, history.team, "[stat marker wiped out]"});
			wiped = populated && units == 0;
			populated |= units > 0;
		}
		if (!point.measurements)
			continue;
		if (!fought && deathsBy(*point.measurements, M::COMBAT) > 0)
		{
			fought = true;
			found.push_back({point.tick, history.team, "[stat marker first combat death]"});
		}
		if (!razed && total(point.measurements->removed[M::DESTROYED]) > 0)
		{
			razed = true;
			found.push_back({point.tick, history.team, "[stat marker first building destroyed]"});
		}
	}
	return found;
}

Reading latestReading(const Metric &metric, const View &requested, const TeamHistory &history)
{
	View view = requested;
	view.split = view.share = false;
	view = restrictedView(metric, view, false);
	const Chart chart = chartOf(metric, view, {history});
	Reading reading;
	reading.percent = chart.percent;
	reading.decimals = chart.decimals;
	const auto &series = chart.teams[0];
	// A rate needs two samples: the first alone has no interval behind it.
	const bool rate = metric.kind == Metric::Counter && !view.total;
	if (!series.ticks.empty() && !(rate && series.ticks.size() == 1 && series.ticks.front() == 0) && !std::isnan(series.values[0].back()))
	{
		reading.available = true;
		reading.value = series.values[0].back();
	}
	return reading;
}
} // namespace Stats
