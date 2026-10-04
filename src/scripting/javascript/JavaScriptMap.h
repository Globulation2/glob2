// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptRuntime.h"
#include "MersenneTwister.h"
class Game;
class ClientCommandSink;
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore
namespace Script
{
class JavaScriptMap
{
	Value state = Value::object(), presentation = Value::object();
	bool initialized = false, seeded = false;
	MersenneTwister random;
	std::unique_ptr<Runtime> runtime = makeRuntime();

  public:
	// Apply saved interface state without invoking JavaScript or changing simulation state.
	void present(ClientCommandSink &client, bool publishHistory = true) const;
	void reset();
	void validate(const std::string &source) { runtime->validate(source); }
	void step(const std::string &source, Game &game, ClientCommandSink &client);
	void save(GAGCore::OutputStream *stream) const;
	void load(GAGCore::InputStream *stream);
	// Objectives and hints are folded in only when `game` is given (historically:
	// only when a GUI was attached).
	unsigned checksum(Game *game) const;
	bool buildingAllowed(const std::string &name, bool flag) const;
};
} // namespace Script
