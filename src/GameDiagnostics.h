// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapRender.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
class Game;
struct Scene;
namespace AIMaximaPlacement { struct WorldState; }
namespace GameDiagnostics
{
inline constexpr size_t CaptureBudget = 128 * 1024 * 1024;
// One controller owns each sink; AI workers never share mutable sink storage.
struct FieldSink
{
	std::uint64_t tick = 0, nextTick = 0;
	unsigned interval = 2500;
	int player = 0, team = 0, plannerTick = 0;
	bool enabled = false, captured = false, failed = false, skipped = false;
	std::array<MapRender::Field, 5> fields;
	void capture(const AIMaximaPlacement::WorldState& world) noexcept;
};
class Session
{
public:
	Session(Game& game, std::string directory, unsigned interval, bool png, size_t byteBudget = CaptureBudget);
	~Session();
	// Simulation owner: before dispatching AI workers, then after joining them.
	void beginTick(const Game& game) noexcept;
	void completeTick(const Game& game) noexcept;
	bool pending() const { return ready.load(); }
	// Graphics owner, while the simulation is parked (or stopped).
	void drain() noexcept;
	void finish() noexcept;
private:
	std::string directory;
	bool png;
	std::vector<std::shared_ptr<FieldSink>> sinks;
	size_t byteBudget;
	std::unique_ptr<Scene> scene;
	std::atomic<bool> ready{false};
	std::uint64_t completed = 0, failures = 0, skipped = 0;
	size_t sceneBudget = 0;
	struct Issue { std::uint64_t tick; int player; std::string reason; };
	std::vector<Issue> issues;
	void issue(const FieldSink& sink, const std::string& reason);
};
}
