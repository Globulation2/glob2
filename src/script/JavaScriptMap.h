// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptRuntime.h"
#include "MersenneTwister.h"
class GameGUI;
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
	void present(GameGUI &gui, bool publishHistory = true) const;
	void reset();
	void validate(const std::string &source) { runtime->validate(source); }
	void step(const std::string &source, GameGUI &gui);
	void save(GAGCore::OutputStream *stream) const;
	void load(GAGCore::InputStream *stream);
	unsigned checksum(GameGUI *gui) const;
	bool buildingAllowed(const std::string &name, bool flag) const;
};
} // namespace Script
