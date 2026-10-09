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
// The game owns shared snapshot storage; this pipeline owns controller state.
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
 OrderScheduler scheduler;
 std::optional<Uint32> boundary;
 bool configured = false;
 Uint64 peakRetainedQueryVectorBytes = 0;
 std::weak_ptr<GameDiagnostics::Session> diagnosticsSession;
public:
 std::vector<std::pair<unsigned,std::shared_ptr<Order>>> prepare(Game& game,
  std::span<const unsigned> eligible, bool paused, const std::shared_ptr<GameDiagnostics::Session>& diagnostics,
  const SimulationSnapshot::Handle& captured);
 std::shared_ptr<Order> validate(Game& game, std::shared_ptr<Order> order, unsigned player);
 void settle(Game& game, const std::shared_ptr<Order>& order, bool accepted);
 void cancel(unsigned player);
 void drain() { scheduler.drain(); }
 void save(GAGCore::OutputStream* stream);
 bool load(Game& game, GAGCore::InputStream* stream);
 const auto& schedulingMetrics() const { return scheduler.metrics; }
 Uint64 computationNs() const { return scheduler.activeNs(); }
 bool wasPolled(unsigned player, Uint32 tick) const;
 std::pair<Uint64,unsigned> queryVectorMemory() const {
  Uint64 bytes=0; unsigned available=0;
  for(const auto& actor:actors)if(actor.retainedQueryVectorBytes){bytes+=*actor.retainedQueryVectorBytes;++available;}
  return {bytes,available};
 }
 Uint64 peakQueryVectorMemory() const {return peakRetainedQueryVectorBytes;}
};
}
