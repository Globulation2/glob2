// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIOrderScheduler.h"
#include "sim/snapshot/SnapshotStore.h"
#include <array>
#include <span>
class AI;
class Game;
namespace GameDiagnostics { class Session; }
namespace AIEngine
{
// Simulation-owner state. Worker callbacks borrow immutable inputs and one
// controller; receipts and completed publication never enter a running lane.
// Snapshot and memory-accounting storage share this owner lifetime.
class Pipeline
{
 struct Actor {
  AI* controller = nullptr;
  bool suspended = false;
  Uint32 generation = 1;
  Uint64 sequence = 0;
  std::vector<ExecutionReceipt> feedback;
  std::optional<Delivery> admitted;
  std::shared_ptr<Order> published;
  std::optional<Uint64> retainedQueryVectorBytes;
 };
 std::array<Actor,32> actors;
 SimulationSnapshot::Store snapshots;
 OrderScheduler scheduler;
 std::optional<Uint32> boundary;
 bool configured = false;
 Uint64 peakRetainedQueryVectorBytes = 0;
 std::weak_ptr<GameDiagnostics::Session> diagnosticsSession;
public:
 std::vector<std::pair<unsigned,std::shared_ptr<Order>>> prepare(Game& game,
  std::span<const unsigned> eligible, bool paused, const std::shared_ptr<GameDiagnostics::Session>& diagnostics);
 std::shared_ptr<Order> validate(Game& game, std::shared_ptr<Order> order, unsigned player);
 void settle(Game& game, const std::shared_ptr<Order>& order, bool accepted);
 void cancel(unsigned player);
 void drain() { scheduler.drain(); }
 void save(GAGCore::OutputStream* stream);
 bool load(Game& game, GAGCore::InputStream* stream);
 const auto& captureMetrics() const { return snapshots.metrics; }
 SimulationSnapshot::MemoryMetrics snapshotMemoryMetrics() const { return snapshots.memoryMetrics(); }
 const auto& schedulingMetrics() const { return scheduler.metrics; }
 Uint64 computationNs() const { return scheduler.activeNs(); }
 bool wasPolled(unsigned player, Uint32 tick) const;
 // The tick's shared capture (reused components, cached catalog) for engine
 // consumers that observe outside a decision poll.
 SimulationSnapshot::Handle observe(const Game& game, SimulationSnapshot::Requirements requirements)
 { return snapshots.captureBoundary(game, requirements); }
 std::pair<Uint64,unsigned> queryVectorMemory() const {
  Uint64 bytes=0; unsigned available=0;
  for(const auto& actor:actors)if(actor.retainedQueryVectorBytes){bytes+=*actor.retainedQueryVectorBytes;++available;}
  return {bytes,available};
 }
 Uint64 peakQueryVectorMemory() const {return peakRetainedQueryVectorBytes;}
};
}
