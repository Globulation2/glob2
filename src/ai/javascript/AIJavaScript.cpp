// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIJavaScript.h"
#include "Player.h"
#include "Game.h"
#include "FileFormatVersions.h"
#include "Order.h"
#include "scripting/javascript/ScriptOrders.h"
#include "Stream.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <stdexcept>
AIJavaScript::AIJavaScript(Player *p)
	: player(p), observations(*p->game, p->teamNumber), runtime(Script::makeRuntime())
{
	source = Script::sourceFromConfig(player->game->gameHeader.getAIConfig(player->number));
	profile = Script::profileFromConfig(player->game->gameHeader.getAIConfig(player->number));
	runtime->validate(source);
	if (profile == 2)
	{
		const auto metadata = Script::inspectAI(source);
		if (metadata.apiVersion != 2)
			throw std::runtime_error("Profile 2 AI requires metadata declaring apiVersion 2");
		displayName = metadata.name;
	}
	observations.setProfile(profile);
	if (profile == 2)
		services =
			std::make_unique<Script::Services>(*player->game, player->teamNumber, observations);
}
bool AIJavaScript::load(GAGCore::InputStream *s, Player *, Sint32 version)
{
	if (version < FILE_FORMAT_VERSION_JAVASCRIPT ||
		(profile == 2 && version < FILE_FORMAT_VERSION_CUSTOM_AI))
		return false;
	s->readEnterSection("JavaScriptAI");
	if (s->readUint32("profile") != profile)
		throw std::runtime_error("Unsupported JavaScript AI profile");
	runtime->discard();
	state = Script::Value::decode(s->readText("state"));
	initialized = s->readUint8("initialized");
	disabled = s->readUint8("disabled");
	error = s->readText("error");
	if (error.size() > 16384)
		throw std::runtime_error("Oversized script diagnostic");
	observations.load(s, version);
	if (profile == 2)
		services->load(Script::Value::decode(s->readText("services")));
	s->readLeaveSection();
	return true;
}
void AIJavaScript::save(GAGCore::OutputStream *s)
{
	s->writeEnterSection("JavaScriptAI");
	s->writeUint32(profile, "profile");
	s->writeText(state.encode(), "state");
	s->writeUint8(initialized, "initialized");
	s->writeUint8(disabled, "disabled");
	s->writeText(error, "error");
	observations.save(s);
	if (profile == 2)
		s->writeText(services->save().encode(), "services");
	s->writeLeaveSection();
}
std::shared_ptr<Order> AIJavaScript::getOrder()
{
	if (disabled)
		return std::make_shared<NullOrder>();
	auto checkpoint = snapshotRandom();
	try
	{
		observations.observe();
		Script::Host host;
		host.profile = profile;
		if (services)
		{
			services->begin();
			host.nextAction = services->nextAction();
		}
		host.tick = player->game->stepCounter;
		host.team = player->teamNumber;
		host.width = player->game->map.getW();
		host.height = player->game->map.getH();
		host.random = [this] { return random(); };
		host.query = [this](const auto &name, const auto &args, const Script::QueryBudget &budget)
		{
			return services ? services->query(name, args, budget)
							: observations.query(name, args, budget);
		};
		auto result = runtime->invoke(source, state, !initialized, host);
		std::shared_ptr<Order> accepted;
		if (services)
		{
			if (result.effects.kind != Script::Value::Null)
				throw std::runtime_error(
					"Profile 2 step must return nothing; use properties or ctx.actions");
			services->commit(result.commands, result.telemetry);
			accepted = services->dispatch();
		}
		else
			accepted = Script::order(*player->game, player->teamNumber, result.effects);
		state = std::move(result.state);
		initialized = true;
		return accepted;
	}
	catch (const Script::HostFailure &)
	{
		runtime->discard();
		restoreRandom(checkpoint);
		throw;
	}
	catch (const std::bad_alloc &)
	{
		runtime->discard();
		restoreRandom(checkpoint);
		throw Script::HostFailure("Native allocation failed in JavaScript controller");
	}
	catch (const std::exception &ex)
	{
		runtime->discard();
		restoreRandom(checkpoint);
		// Prepare the diagnostic and fallback before publishing disabled state.
		try
		{
			std::string diagnostic =
				displayName + " at tick " + std::to_string(player->game->stepCounter) + ": " +
				std::string(ex.what(), std::min(std::strlen(ex.what()), std::size_t(16000)));
			auto fallback = std::make_shared<NullOrder>();
			error.swap(diagnostic);
			disabled = true;
			std::cerr << "JavaScript AI player " << player->number << ": " << error << '\n';
			return fallback;
		}
		catch (const std::bad_alloc &)
		{
			throw Script::HostFailure("Cannot retain the JavaScript controller diagnostic");
		}
	}
}

void AIJavaScript::observe()
{
	if (!disabled && player->game->teams[player->teamNumber]->isAlive)
		observations.observe();
}

void AIJavaScript::captureTelemetry()
{
	if (!telemetry.series)
		return;
	auto &values = telemetry.series->named;
	values.clear();
	const unsigned tick = player->game->stepCounter;
	values.push_back(
		{"runtime.status", disabled ? "Disabled" : "Running", "", error.substr(0, 4096), tick});
	if (!services)
		return;
	unsigned pending = 0;
	for (const auto &r : services->actions().items)
		if (r.get("status").text == "pending")
			++pending;
	values.push_back({"runtime.pendingActions", std::to_string(pending), "orders",
					  "Orders waiting for dispatch", tick});
	for (const auto &[name, sample] : services->telemetry().fields)
	{
		const auto &value = sample.get("value");
		std::string formatted;
		if (value.kind == Script::Value::String)
			formatted = value.text;
		else if (value.kind == Script::Value::Boolean)
			formatted = value.number ? "true" : "false";
		else
		{
			AITelemetry::Value bits;
			std::memcpy(&bits.bits, &value.number, sizeof(double));
			bits.valid = true;
			AITelemetry::Field field;
			field.type = AITelemetry::Real;
			formatted = AITelemetry::displayValue(field, bits);
		}
		const auto &description = sample.get("description");
		values.push_back({name, formatted, description.get("unit").text,
						  description.kind == Script::Value::String
							  ? description.text
							  : description.get("description").text,
						  unsigned(sample.get("updated").number)});
	}
}
