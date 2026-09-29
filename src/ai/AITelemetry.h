// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL_stdinc.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore
class Team;
class AI;
class Order;

// Numeric diagnostic storage only. Never read by a gameplay decision.
namespace AITelemetry
{
enum Type : Uint32
{
	Signed,
	Unsigned,
	Real
};
enum Kind : Uint32
{
	Gauge,
	Counter,
	Enumeration,
	Mask
};
struct Field
{
	std::string name, unit, meaning;
	Type type = Signed;
	Kind kind = Gauge;
	bool operator==(const Field &) const = default;
};
struct Value
{
	Uint64 bits = 0;
	Uint32 updated = 0;
	bool valid = false;
	bool operator==(const Value &) const = default;
};
struct Sample
{
	Uint32 tick = 0;
	bool available = false;
	std::vector<Value> values;
	bool operator==(const Sample &) const = default;
};
struct Series
{
	int player = 0, implementation = 0;
	Uint32 generation = 0, coverage = 0, schemaVersion = 1;
	std::string playerName;
	bool active = true;
	bool schemaPrinted = false; // output state only; not saved
	std::vector<Field> fields;
	Sample current;
	std::vector<Sample> history;
};
// Common columns are stable and independent of any implementation schema.
constexpr unsigned Polls = 0, NullOrders = 1, Orders = 2, OrderTypes = 3;
constexpr unsigned Specific = OrderTypes + 256;
const std::vector<Field> &schema(int implementation);
class Sink
{
  public:
	bool returnedBool(unsigned result, unsigned yes, bool value) const
	{
		set(result, value);
		if (value)
			count(yes);
		return value;
	}
	int returnedInt(unsigned result, int value) const
	{
		set(result, value);
		return value;
	}
	std::shared_ptr<Order> returnedOrder(unsigned result, std::shared_ptr<Order> value) const;
	Series *series = nullptr;
	Uint32 tick = 0;
	void set(unsigned field, Sint64 value) const
	{
		if (series)
			series->current.values[field] = {static_cast<Uint64>(value), tick, true};
	}
	void setUnsigned(unsigned field, Uint64 value) const
	{
		if (series)
			series->current.values[field] = {value, tick, true};
	}
	void setReal(unsigned field, double value) const
	{
		Uint64 bits;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&bits, &value, sizeof(bits));
		setUnsigned(field, bits);
	}
	void count(unsigned field, Uint64 amount = 1) const
	{
		if (series)
		{
			auto &v = series->current.values[field];
			v.bits += amount;
			v.updated = tick;
			v.valid = true;
		}
	}
};
void capture(Team *team, bool retain, bool output, bool final = false);
void save(GAGCore::OutputStream *stream, const std::vector<std::shared_ptr<Series>> &series);
void load(GAGCore::InputStream *stream, std::vector<std::shared_ptr<Series>> &series);
void emit(Series &series, int team, bool final, bool describe = true);
} // namespace AITelemetry
