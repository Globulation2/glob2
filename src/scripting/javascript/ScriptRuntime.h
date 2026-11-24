// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include <functional>
#include <vector>
#include <memory>
#include <stdexcept>
namespace Script
{
class SessionFailure : public std::runtime_error
{
  public:
	using std::runtime_error::runtime_error;
};
// Fatal machine-resource failures and deterministic scenario errors both end
// the session, but remain distinguishable to diagnostics and tests.
class HostFailure : public SessionFailure
{
  public:
	using SessionFailure::SessionFailure;
};
// The game session ran out of native memory (std::bad_alloc), whatever was
// allocating; not a script fault. In the browser this is the Wasm heap's cap.
class MemoryFailure : public HostFailure
{
  public:
	using HostFailure::HostFailure;
};
class ScenarioFailure : public SessionFailure
{
  public:
	using SessionFailure::SessionFailure;
};
struct Host
{
	// Separate local-assistant host; never set by deterministic AI or map scripts.
	bool commander = false;
	// Work units and JavaScript heap for one invocation. Simulation scripts (AI and
	// map) must keep the defaults: their budget is part of what the match computes.
	// The local Hive worker runs outside the simulation and raises both.
	std::uint64_t workBudget = FuelLimit;
	std::size_t memoryLimit = MemoryLimit;
	// Extra ctx.game / ctx.game.map methods this host serves through query (by name),
	// beside the shared ones. Simulation hosts leave both empty.
	std::vector<std::string> gameExtensions, mapExtensions;

	unsigned profile = 1;
	unsigned nextAction = 1;
	unsigned tick = 0;
	unsigned width = 0, height = 0;
	int team = -1;
	std::function<Value(const std::string &, const std::vector<Value> &, const QueryBudget &)>
		query;
	std::function<unsigned()> random;
};
struct Result
{
	Value state, effects;
	Value commands = Value::array(), telemetry = Value::object();
};
struct Metadata
{
	unsigned apiVersion = 1;
	std::string name, description, version, author;
};
class Runtime
{
  public:
	virtual ~Runtime() = default;
	virtual Result invoke(const std::string &source, const Value &state, bool initialize,
						  Host &host) = 0;
	virtual void validate(const std::string &source) = 0;
	virtual void discard() noexcept {}
	// Diagnostic/test view of ordinary global data; snapshots preserve graphs.
	virtual Value inspectGlobals() { return Value::object(); }
};
std::unique_ptr<Runtime> makeRuntime();
// Evaluates in a disposable runtime, with neither world access nor randomness.
// Also checks callback resolution and the initial automatic globals snapshot.
Metadata inspectAI(const std::string &source, std::string *stage = nullptr);
} // namespace Script
