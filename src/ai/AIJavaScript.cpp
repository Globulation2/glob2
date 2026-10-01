// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIJavaScript.h"
#include "Player.h"
#include "Game.h"
#include "Order.h"
#include "script/ScriptOrders.h"
#include "Stream.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <stdexcept>
AIJavaScript::AIJavaScript(Player *p)
	: player(p), observations(*p->game, p->teamNumber), runtime(Script::makeRuntime())
{
	source = Script::sourceFromConfig(player->game->gameHeader.getAIConfig(player->number));
	runtime->validate(source);
}
bool AIJavaScript::load(GAGCore::InputStream *s, Player *, Sint32 version)
{
	if (version < 124)
		return false;
	s->readEnterSection("JavaScriptAI");
	if (s->readUint32("profile") != Script::ProfileVersion)
		throw std::runtime_error("Unsupported JavaScript AI profile");
	state = Script::Value::decode(s->readText("state"));
	initialized = s->readUint8("initialized");
	disabled = s->readUint8("disabled");
	error = s->readText("error");
	if (error.size() > 16384)
		throw std::runtime_error("Oversized script diagnostic");
	observations.load(s);
	s->readLeaveSection();
	return true;
}
void AIJavaScript::save(GAGCore::OutputStream *s)
{
	s->writeEnterSection("JavaScriptAI");
	s->writeUint32(Script::ProfileVersion, "profile");
	s->writeText(state.encode(), "state");
	s->writeUint8(initialized, "initialized");
	s->writeUint8(disabled, "disabled");
	s->writeText(error, "error");
	observations.save(s);
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
		host.tick = player->game->stepCounter;
		host.team = player->teamNumber;
		host.width = player->game->map.getW();
		host.height = player->game->map.getH();
		host.random = [this] { return random(); };
		host.query = [this](const auto &name, const auto &args, const Script::QueryBudget &budget)
		{ return observations.query(name, args, budget); };
		auto result = runtime->invoke(source, state, !initialized, host);
		auto accepted = Script::order(*player->game, player->teamNumber, result.effects);
		state = std::move(result.state);
		initialized = true;
		return accepted;
	}
	catch (const Script::HostFailure &)
	{
		restoreRandom(checkpoint);
		throw;
	}
	catch (const std::bad_alloc &)
	{
		restoreRandom(checkpoint);
		throw Script::HostFailure("Native allocation failed in JavaScript controller");
	}
	catch (const std::exception &ex)
	{
		restoreRandom(checkpoint);
		// Prepare the diagnostic and fallback before publishing disabled state.
		try
		{
			std::string diagnostic(ex.what(), std::min(std::strlen(ex.what()), std::size_t(16384)));
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
