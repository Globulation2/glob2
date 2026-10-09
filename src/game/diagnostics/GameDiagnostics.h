// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapRender.h"
#include "sim/snapshot/Requirements.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <vector>
class Game;
namespace GAGCore { class InputStream; class OutputStream; }
struct PresentationFrame;
struct SceneInputs;
namespace SimulationSnapshot { struct Handle; }
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
    void save(GAGCore::OutputStream*) const;
    bool load(GAGCore::InputStream*);
};
class Session
{
public:
	Session(Game& game, std::string directory, unsigned interval, bool png, size_t byteBudget = CaptureBudget);
	~Session();
	// Simulation-owner API. The pipeline keeps a weak Session reference;
	// workers receive private FieldSink values, never the Session.
	void beginTick(const Game& game) noexcept;
    SimulationSnapshot::Requirements observationRequirements(Uint32 tick) const;
	void completeTick(const SimulationSnapshot::Handle& world) noexcept;
    std::shared_ptr<FieldSink> reserveCapture(int player, std::uint64_t observedTick) noexcept;
    void publishCapture(const FieldSink&) noexcept;
    // Registers an already completed saved output before admitting new captures.
    // False discards diagnostic output only and records a bounded skipped issue.
    bool adoptCapture(const FieldSink&) noexcept;
    // Call only after the controller stream has joined and discarded its outputs.
    void cancelCaptures(int player) noexcept;
	bool pending() const { return ready.load(); }
	// Graphics owner consumes an immutable bounded publication; the simulation keeps running.
	void drain() noexcept;
	void finish() noexcept;
private:
	std::string directory;
	bool png;
	std::vector<std::shared_ptr<FieldSink>> sinks;
	size_t byteBudget;
    size_t reservedBytes = 0;
    std::map<int,std::uint64_t> publishedNextTicks;
    std::map<std::pair<int,std::uint64_t>,size_t> reservations;
    struct Publication {
        std::shared_ptr<const SceneInputs> inputs;
        std::vector<FieldSink> sinks;
    };
    // Single producer/consumer: ready publishes ownership and prevents reuse
    // until the graphics owner releases the completed batch.
    std::shared_ptr<Publication> publication;
    std::atomic<bool> ready{false};
    std::atomic<std::uint64_t> completed{0}, failures{0}, skipped{0};
    std::mutex issueMutex;

	size_t sceneBudget = 0;
	struct Issue { std::uint64_t tick; int player; std::string reason; };
	std::vector<Issue> issues;
	void issue(const FieldSink& sink, const std::string& reason);
};
}
