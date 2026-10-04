// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptObservations.h"
#include <map>
#include <memory>
class Game;
namespace Script
{
// Native calculations see only the same observations offered to JavaScript.
// Cached fields are exact functions of their source and obstacle masks. No
// cache timestamp, hit/miss, eviction or allocation order affects decisions.
class Spatial
{
	Game &game;
	int team, width, height;
	Observations &observations;
	unsigned tick = ~0u;
	bool incomplete = true;
	std::vector<Observations::Cell> cells;
	Value reservations = Value::array();
	struct Field
	{
		std::vector<unsigned char> sources, passable;
		std::vector<int> distances;
		std::string metric;
	};
	std::map<std::string, std::shared_ptr<Field>> cache;
	std::vector<std::shared_ptr<Field>> handles;
	std::vector<unsigned> frontier;
	std::vector<long long> prefix;
	int index(int x, int y) const;
	void snapshot();
	std::vector<int> sources(const Value &selector, const QueryBudget &budget);
	std::shared_ptr<Field> distanceField(const Value &spec, const QueryBudget &budget);
	Value placement(const Value &, const Value &staged, const QueryBudget &);
	void sums(const std::vector<int> &values);
	long long sum(int x, int y, int w, int h) const;

  public:
	Spatial(Game &, int, Observations &);
	void begin(const Value &records);
	Value query(const std::string &, const std::vector<Value> &, const QueryBudget &);
};
} // namespace Script
