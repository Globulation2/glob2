// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include <functional>
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
class ScenarioFailure : public SessionFailure
{
  public:
	using SessionFailure::SessionFailure;
};
struct Host
{
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
Metadata inspectAI(const std::string &source);
} // namespace Script
