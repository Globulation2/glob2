// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AITelemetry.h"
#include <map>
class Game;
namespace ReplayTelemetry
{
struct Row
{
	int team = 0, player = 0;
	std::string name;
	bool available = false;
	std::vector<AITelemetry::NamedValue> values;
	bool operator==(const Row &) const = default;
};
struct Frame
{
	Uint32 step = 0;
	std::vector<Row> changes;
};
// Diagnostic data is bounded independently of the gameplay order stream.
class Stream
{
	std::map<int, Row> current;
	std::vector<Frame> frames;
	size_t bytes = 0, cursor = 0;
	bool present = false, truncated = false;
	Uint32 until = 0;

  public:
	void capture(const Game &, Uint32 step);
	void write(GAGCore::OutputStream *) const;
	void read(GAGCore::InputStream *);
	void apply(Game &, Uint32 step);
	bool available() const { return present; }
};
} // namespace ReplayTelemetry
