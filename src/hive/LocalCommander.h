// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CommanderView.h"
#include "scripting/javascript/ScriptObservations.h"
#include <deque>
#include <future>
#include <functional>
class GameGUI;
namespace Hive
{
// Main-thread session service. Engine reads/order validation are parked explicitly;
// worker jobs own only filtered observations and never retain game pointers.
class LocalCommander : public CommanderView
{
	GameGUI &gui;
	Script::Observations observations;
	const int player, team;
	const std::string session;
	bool valid = true;
	unsigned tick = 0;
	struct Program
	{
		Json definition, state = nullptr, recent = nullptr;
		bool initialized = false, paused = false;
		unsigned nextTick = 0, random = 1;
		std::uint64_t generation = 0;
	};
	struct Operation
	{
		std::string key;
		Json request;
	};
	std::map<std::string, Program> programs;
	std::map<std::string, unsigned> revisions;
	std::map<std::string, Json> journal;
	std::map<std::string, Json> requests;
	std::deque<Operation> queued;
	std::future<Json> worker;
	std::function<std::future<Json>(Json)> launch;
	Operation current;
	std::string currentProgram;
	std::uint64_t currentGeneration = 0, generation = 0;
	unsigned observationTick = 0;
	bool contextCurrent() const;
	bool eligible() const;
	void boundary(const std::function<void()> &work);
	void finish(Json result);
	void remember(const std::string &key, Json result);

  public:
	explicit LocalCommander(GameGUI &gui, std::function<std::future<Json>(Json)> launch = {});
	~LocalCommander() override;
	void update();
	void clear();
	bool available() const override { return valid; }
	bool local() const override { return true; }
	Json status() const;
	Json call(const std::string &tool, const Json &arguments);
	Json standingOrders() const override;
	void stop() override;
	void change(const std::string &id, const std::string &action) override;
};
Json localCall(const std::string &tool, const Json &arguments);
Json playerAPI();
} // namespace Hive
