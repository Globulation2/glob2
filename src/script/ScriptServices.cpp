// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptServices.h"
#include "ScriptSpatial.h"
#include "ScriptOrders.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include "Building.h"
#include "Order.h"
#include <algorithm>
#include <stdexcept>

namespace Script
{
namespace
{
bool pending(const Value &r)
{
	return r.get("status").text == "pending";
}
bool terminal(const Value &r)
{
	const auto s = r.get("status").text;
	return s == "completed" || s == "failed" || s == "cancelled";
}
std::string key(const Value &c)
{
	if (c.get("building").kind != Value::Object)
		return {};
	const auto type = c.get("type").text;
	if (type == "construction" || type == "delete" || type == "cancelConstruction" ||
		type == "cancelDelete")
		return {};
	return type + c.get("building").encode();
}
} // namespace
Services::Services(Game &g, int t, Observations &o)
	: game(g), team(t), observations(o), spatial(std::make_unique<Spatial>(g, t, o))
{
}
Services::~Services() = default;
void Services::begin()
{
	reconcile();
	spatial->begin(records);
}
Value Services::query(const std::string &name, const std::vector<Value> &args,
					  const QueryBudget &budget)
{
	if (name.rfind("spatial.", 0) == 0)
		return spatial->query(name.substr(8), args, budget);
	if (name == "desired")
	{
		Value desired = Value::object();
		if (args.size() != 1)
			throw std::runtime_error("desired requires a reference");
		for (const auto &r : records.items)
			if (pending(r) || r.get("status").text == "issued")
			{
				const auto &c = r.get("command");
				if (c.get("building").encode() != args[0].encode())
					continue;
				for (const auto &[name, value] : c.fields)
				{
					if (name == "type" || name == "building" || name == "actionId" ||
						name == "intent")
						continue;
					desired.set(name == "ratios"      ? "production"
								: name == "level"     ? "minimumLevel"
								: name == "resources" ? "clearingResources"
													  : name,
								value);
				}
			}
		return desired;
	}
	if (name == "actionStatus")
	{
		if (args.size() != 1)
			throw std::runtime_error("status requires an action ID");
		int id = Value::object().set("id", args[0]).integer("id", 1, 0x7fffffff);
		for (const auto &r : records.items)
			if (r.get("id").number == id)
				return r;
		return {};
	}
	if (name == "validateAction")
	{
		if (args.size() != 1)
			throw std::runtime_error("An action descriptor is required");
		const auto &c = args[0];
		if (c.get("type").text == "cancel")
			c.integer("target", 1, 0x7fffffff);
		else
			order(game, team, c);
		return {};
	}
	return observations.query(name, args, budget);
}
void Services::commit(const Value &commands, const Value &telemetry)
{
	if (commands.kind != Value::Array || commands.items.size() > 256)
		throw std::runtime_error("Too many AI actions");
	auto candidate = records;
	for (const auto &c : commands.items)
	{
		query("validateAction", {c}, {});
		if (c.get("type").text == "cancel")
		{
			bool cancelled = false;
			for (auto &r : candidate.items)
				if (r.get("id").number == c.get("target").number && pending(r))
				{
					r.set("status", "cancelled");
					cancelled = true;
				}
			candidate.items.push_back(Value::object()
										  .set("id", c.get("actionId"))
										  .set("command", c)
										  .set("status", cancelled ? "completed" : "failed")
										  .set("tick", game.stepCounter)
										  .set("reason", cancelled
															 ? "Pending action cancelled"
															 : "Action was no longer pending"));
			continue;
		}
		const auto coalesce = key(c);
		auto previous = candidate.items.end();
		if (!coalesce.empty())
			previous =
				std::find_if(candidate.items.begin(), candidate.items.end(), [&](const auto &r)
							 { return pending(r) && key(r.get("command")) == coalesce; });
		if (previous != candidate.items.end())
		{
			auto replacement = c;
			replacement.set("actionId", previous->get("id"));
			previous->set("command", replacement);
		}
		else
			candidate.items.push_back(Value::object()
										  .set("id", c.get("actionId"))
										  .set("command", c)
										  .set("status", "pending")
										  .set("tick", game.stepCounter));
	}
	if (std::count_if(candidate.items.begin(), candidate.items.end(), pending) > 256)
		throw std::runtime_error("AI action queue is full");
	while (candidate.items.size() > 1024)
	{
		auto old = std::find_if(candidate.items.begin(), candidate.items.end(), terminal);
		if (old == candidate.items.end())
			throw std::runtime_error("AI action history is full");
		candidate.items.erase(old);
	}
	auto values = diagnostics;
	for (const auto &[name, value] : telemetry.fields)
		values.set(name, value);
	if (values.fields.size() > 130)
		throw std::runtime_error("AI telemetry field limit exceeded");
	candidate.encode();
	values.encode();
	records = std::move(candidate);
	diagnostics = std::move(values);
	next += unsigned(commands.items.size());
}
void Services::reconcile()
{
	Value owned;
	for (auto &r : records.items)
	{
		const auto status = r.get("status").text;
		if (status != "issued" && status != "constructing")
			continue;
		if (r.get("tick").number == game.stepCounter)
			continue;
		const auto &c = r.get("command");
		if (c.get("type").text == "create")
		{
			Value ref = r.get("building");
			Value building;
			if (ref.kind == Value::Object)
				building = observations.query("building", {ref});
			else
			{
				if (owned.kind == Value::Null)
					owned = observations.query("buildings", {Value::object().set("team", team)});
				for (const auto &b : owned.items)
					if (b.get("x").number == c.get("x").number &&
						b.get("y").number == c.get("y").number &&
						b.get("shortType").number ==
							globalContainer->buildingsTypes.get(int(c.get("buildingType").number))
								->shortTypeNum &&
						b.get("generation").number != r.get("previousGeneration").number)
					{
						building = b;
						break;
					}
			}
			if (building.kind == Value::Object)
			{
				r.set("building", Value::object()
									  .set("id", building.get("id"))
									  .set("generation", building.get("generation")));
				r.set("status", building.get("construction").number ? "constructing" : "completed");
			}
			else if (status == "constructing" ||
					 game.stepCounter - unsigned(r.get("tick").number) > 32)
				r.set("status", "failed")
					.set("reason", "Construction did not appear or was destroyed");
		}
		else
		{
			const auto b = c.get("building").kind == Value::Object
							   ? observations.query("building", {c.get("building")})
							   : Value();
			bool applied = c.get("building").kind != Value::Object;
			const auto type = c.get("type").text;
			if (type == "delete")
				applied = b.kind == Value::Null;
			else if (b.kind != Value::Null)
			{
				if (type == "construction")
				{
					if (b.get("construction").number)
					{
						r.set("status", "constructing");
						continue;
					}
					applied = status == "constructing" ||
							  b.get("level").number > r.get("beforeLevel").number ||
							  (c.get("intent").text == "repair" &&
							   b.get("hp").number >= b.get("maxHp").number);
				}
				else
				{
					applied = true;
					for (const auto &[field, v] : c.fields)
					{
						if (field == "type" || field == "building" || field == "actionId")
							continue;
						const auto name = field == "ratios"      ? "production"
										  : field == "level"     ? "minimumLevel"
										  : field == "resources" ? "clearingResources"
																 : field;
						applied &= b.get(name).encode() == v.encode();
					}
				}
			}
			if (applied)
				r.set("status", "completed");
			else if (b.kind == Value::Null ||
					 game.stepCounter - unsigned(r.get("tick").number) > 32)
				r.set("status", "failed")
					.set("reason", "Order no longer applies or was not applied");
		}
	}
}
std::shared_ptr<Order> Services::dispatch()
{
	for (auto &r : records.items)
		if (pending(r))
		{
			try
			{
				// Tracking fields below can grow r.fields and move its child Values.
				// Keep the command independent until the order has been constructed.
				const auto command = r.get("command");
				if (command.get("type").text == "construction")
				{
					const auto b = observations.query("building", {command.get("building")});
					if (b.kind != Value::Null)
					{
						if (command.get("intent").text == "repair" &&
							b.get("hp").number >= b.get("maxHp").number)
						{
							r.set("status", "completed").set("reason", "Already repaired");
							continue;
						}
						if (command.get("intent").text == "upgrade" &&
							b.get("hp").number < b.get("maxHp").number)
						{
							r.set("status", "failed")
								.set("reason", "Repair needed before upgrading");
							continue;
						}
						r.set("beforeLevel", b.get("level"));
					}
				}
				auto result = order(game, team, command);
				if (r.get("command").get("type").text == "create")
				{
					const auto &c = command;
					for (const auto &b :
						 observations.query("buildings", {Value::object().set("team", team)}).items)
						if (b.get("x").number == c.get("x").number &&
							b.get("y").number == c.get("y").number)
							r.set("previousGeneration", b.get("generation"));
				}
				r.set("status", "issued").set("tick", game.stepCounter);
				return result;
			}
			catch (const std::bad_alloc &)
			{
				throw;
			}
			catch (const std::exception &)
			{
				r.set("status", "failed").set("reason", "Target no longer accepts this order");
			}
		}
	return std::make_shared<NullOrder>();
}
Value Services::save() const
{
	return Value::object().set("next", next).set("records", records).set("telemetry", diagnostics);
}
void Services::load(const Value &v)
{
	next = v.integer("next", 1, 0x7fffffff);
	if (v.get("records").kind != Value::Array || v.get("records").items.size() > 1024 ||
		v.get("telemetry").kind != Value::Object || v.get("telemetry").fields.size() > 130)
		throw std::runtime_error("Invalid saved AI services");
	unsigned previous = 0;
	for (const auto &r : v.get("records").items)
	{
		unsigned id = r.integer("id", 1, int(next) - 1);
		if (id <= previous || r.get("command").kind != Value::Object)
			throw std::runtime_error("Invalid saved action identity");
		if (r.get("command").get("actionId").number != id ||
			r.get("command").get("type").kind != Value::String ||
			r.get("tick").kind != Value::Number || r.get("tick").number > game.stepCounter)
			throw std::runtime_error("Invalid saved action descriptor");
		const auto &command = r.get("command");
		const auto type = command.string("type");
		if (type == "create" || type == "forbidden" || type == "guardArea" || type == "clearArea")
			order(game, team, command); // Validate types and bounds without executing it.
		else if (type == "cancel")
			command.integer("target", 1, 0x7fffffff);
		else
		{
			// A valid pending target may have died before the save. Validate its
			// identity here; dispatch handles its current existence and ownership.
			const auto &ref = command.get("building");
			ref.integer("id", 0, Building::MAX_COUNT * Team::MAX_COUNT - 1);
			if (ref.get("generation").kind != Value::Number || ref.get("generation").number < 1)
				throw std::runtime_error("Invalid saved action reference");
		}
		previous = id;
		const auto status = r.string("status");
		if (status != "pending" && status != "issued" && status != "constructing" &&
			status != "completed" && status != "failed" && status != "cancelled")
			throw std::runtime_error("Invalid saved action status");
	}
	records = v.get("records");
	diagnostics = v.get("telemetry");
}
} // namespace Script
