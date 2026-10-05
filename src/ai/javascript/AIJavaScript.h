// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIImplementation.h"
#include "scripting/javascript/ScriptRuntime.h"
#include "scripting/javascript/ScriptObservations.h"
#include "scripting/javascript/ScriptServices.h"
class AIJavaScript : public AIImplementation
{
	Player *player;
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
	bool isDisabled() const { return disabled; }
	const std::string &diagnostic() const { return error; }
	void observe();
	void captureTelemetry() override;
};
