// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIImplementation.h"
#include "scripting/javascript/ScriptRuntime.h"
#include "scripting/javascript/ScriptObservations.h"
#include "scripting/javascript/ScriptServices.h"
class AIJavaScript : public AIImplementation
{
	Player *player;
	unsigned teamNumber = 0, observationTick = 0;
	std::shared_ptr<Order> decide();
	std::string source;
	std::string displayName = "JavaScript AI";
	unsigned profile = 1;
	Script::Value state = Script::Value::object();
	bool initialized = false, disabled = false;
	std::string error;
	Script::Observations observations;
	std::unique_ptr<Script::Runtime> runtime;
	std::unique_ptr<Script::Services> services;

  public:
	explicit AIJavaScript(Player *player);
	bool load(GAGCore::InputStream *, Player *, Sint32) override;
	void save(GAGCore::OutputStream *) override;
	std::shared_ptr<Order> getOrder() override;
	bool supportsObservation() const override { return true; }
    std::optional<Uint64> retainedQueryVectorBytes() const override;
	std::shared_ptr<Order> getOrder(const AIEngine::DecisionContext&) override;
	bool isDisabled() const { return disabled; }
	void enableValidationReporting()
	{
		if (services)
			services->enableValidationReporting();
	}
	bool hasRejectedDecision() const { return services && services->hasRejectedDecision(); }
	const std::string &diagnostic() const { return error; }
	// Ordered worker-stream observation for boundaries without an AI poll.
	void observe(const AIEngine::AIWorldView&);
	void captureTelemetry() override;
};
