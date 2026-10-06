// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptObservations.h"
#include "ScriptRuntime.h"
#include "ai/engine/AIDecision.h"
#include <map>
#include <memory>
#include <vector>
class Game;
class Order;
namespace Script
{
class Spatial;
class Services
{
	int team;
	Observations &observations;
	Value records = Value::array();
	Value diagnostics = Value::object();
	unsigned next = 1;
	// Validation diagnostics are observational: never serialized or checksummed.
	bool validationReporting = false, rejectedDecision = false;
	bool waitForExecution = false;
	std::unique_ptr<Spatial> spatial;
	void reconcile();

  public:
	Services(Game &, int team, Observations &);
	~Services();
    std::uint64_t retainedQueryVectorBytes() const;
	unsigned nextAction() const { return next; }
	void begin();
	void applyExecutionReceipts(const std::vector<AIEngine::ExecutionReceipt>&);
	void useObservedExecution() {
        waitForExecution = false;
        for (auto& record : records.items) record.set("awaitingExecution", false);
    }
	void enableValidationReporting() { validationReporting = true; }
	bool hasRejectedDecision() const { return rejectedDecision; }
	Value query(const std::string &, const std::vector<Value> &, const QueryBudget &);
	void commit(const Value &commands, const Value &telemetry);
	std::shared_ptr<Order> dispatch();
	Value save() const;
	void load(const Value &);
	const Value &telemetry() const { return diagnostics; }
	const Value &actions() const { return records; }
};
} // namespace Script
