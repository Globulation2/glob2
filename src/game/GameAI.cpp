// SPDX-License-Identifier: GPL-3.0-or-later
#include "Game.h"
#include "TurretTargeting.h"
#include "TeamStatsPipeline.h"
#include "HiringPipeline.h"
#include "TeamStatsChecksum.h"
#include "Team.h"
#include "GameDiagnostics.h"
#include "ai/engine/AIPipeline.h"
#include "AIJavaScript.h"
#include "Player.h"
SimulationSnapshot::Handle Game::captureReadBoundary(SimulationSnapshot::CaptureOptions options) const
{
 return worldSnapshots.captureBoundary(*this, options);
}
SimulationSnapshot::Handle Game::prepareReadBoundary(bool paused, SimulationSnapshot::CaptureOptions options)
{
 ensureTurretTargeting();
 const bool statisticsActive = !paused && !anyPlayerWaited;
 // Hiring observes every completed boundary before matched orders, even while
 // a replica waits or is paused, so every replica admits the same jobs.
 if (!hiringPipeline) hiringPipeline = std::make_unique<HiringPipeline>(map.computeExecutor());
 hiringPipeline->publishDue(stepCounter);
 if (statisticsActive) {
  if (statsPipeline && !statsPipeline->matchesWorld(map.identity())) statsPipeline.reset();
  if (!statsPipeline) statsPipeline = std::make_unique<TeamStatsPipeline>(map.computeExecutor());
  for (int t = 0; t < teamsCount(); ++t) teams[t]->stats.enableCollection();
  if (statsPipeline->publishDue(*this)) worldSnapshots.invalidateBoundary();
 }
 auto world = worldSnapshots.captureBoundary(*this, options);
 if (statisticsActive && statsPipeline->needsSample(stepCounter)) {
  statsPipeline->prepareBoundary(*this, world);
  if (statsPipeline->publishDue(*this)) {
   worldSnapshots.invalidateBoundary();
   world = worldSnapshots.captureBoundary(*this, options);
  }
 }
 turretTargetingPipeline->admit(*this,world,paused || anyPlayerWaited);
 hiringPipeline->observe(*this, world);
 return world;
}
std::vector<std::pair<unsigned,std::shared_ptr<Order>>> Game::prepareAIOrders(std::span<const unsigned> players,
 bool paused,const std::shared_ptr<GameDiagnostics::Session>& diagnostics,const SimulationSnapshot::Handle* captured)
{
 if(!aiPipeline) aiPipeline=std::make_unique<AIEngine::Pipeline>();
 // Standalone simulation callers use the same boundary service; engine callers
 // supply the standard snapshot already published at this boundary.
 if (captured) return aiPipeline->prepare(*this,players,paused,diagnostics,*captured);
 const auto world=prepareReadBoundary(paused, diagnostics ? diagnostics->captureOptions(stepCounter) : SimulationSnapshot::CaptureOptions{});
 return aiPipeline->prepare(*this,players,paused,diagnostics,world);
}
std::shared_ptr<Order> Game::validateAIOrder(std::shared_ptr<Order> order,unsigned player) {return aiPipeline?aiPipeline->validate(*this,std::move(order),player):order;}
void Game::settleAIOrder(const std::shared_ptr<Order>& order,bool accepted) {if(aiPipeline)aiPipeline->settle(*this,order,accepted);}
void Game::cancelAI(unsigned player) {if(aiPipeline)aiPipeline->cancel(player);}
void Game::drainAI() {if(aiPipeline)aiPipeline->drain();if(turretTargetingPipeline)turretTargetingPipeline->drain();}
void Game::clearAI() { hiringPipeline.reset(); if(turretTargetingPipeline)turretTargetingPipeline->drain(); statsPipeline.reset(); map.finishGradientPipeline({JoinReason::Teardown, ComputeTelemetry::NoTick}); map.finishResourceGrowth({JoinReason::Teardown, ComputeTelemetry::NoTick}); aiPipeline.reset(); worldSnapshots.reset(); }
void Game::saveAI(GAGCore::OutputStream* stream) {if(!aiPipeline){aiPipeline=std::make_unique<AIEngine::Pipeline>();aiPipeline->prepare(*this,{},true,nullptr,captureReadBoundary());}aiPipeline->save(stream);}
bool Game::loadAI(GAGCore::InputStream* stream) {auto pipeline=std::make_unique<AIEngine::Pipeline>();if(!pipeline->load(*this,stream))return false;aiPipeline=std::move(pipeline);return true;}
std::vector<std::pair<std::string,Uint64>> Game::aiMetrics() const {
 auto values=teamStatsMetrics();
 const auto hiring=hiringMetrics();values.insert(values.end(),hiring.begin(),hiring.end());
 if(!aiPipeline)return values;
 const auto compute=map.computeExecutor().telemetrySnapshot();
 const auto& waits=compute.summary.joins[unsigned(ComputeProducer::AI)][unsigned(JoinReason::TickDeadline)];
 const auto& capture=worldSnapshots.metrics;const auto& scheduling=aiPipeline->schedulingMetrics();const auto memory=worldSnapshots.memoryMetrics();const auto queryMemory=aiPipeline->queryVectorMemory();
 const std::vector<std::pair<std::string,Uint64>> aiValues={{"captures",capture.captures},
  {"map_bytes_copied",capture.mapBytesCopied},{"detailed_captures",capture.detailedCaptures},{"bytes_copied",capture.bytesCopied},
  {"entity_bytes_copied",capture.entityBytesCopied},{"entity_sidecar_bytes_copied",capture.entitySidecarBytesCopied},{"component_reuses",capture.reusedComponents},{"allocations",capture.allocations},
  {"controller_query_vector_bytes",queryMemory.first},
  {"controller_query_vector_peak_bytes",aiPipeline->peakQueryVectorMemory()},
  {"controller_query_vector_samples",queryMemory.second},{"submitted",scheduling.submitted},{"delivered",scheduling.delivered},
  {"snapshot_allocated_buffers",memory.allocatedBuffers},{"snapshot_reusable_buffers",memory.reusableBuffers},
  {"snapshot_leased_buffers",memory.leasedBuffers},{"snapshot_retained_bytes",memory.retainedBytes},
  {"snapshot_capacity_bytes",memory.capacityBytes},{"snapshot_leased_bytes",memory.leasedBytes},
  {"snapshot_peak_allocated_buffers",memory.peakAllocatedBuffers},{"snapshot_peak_reusable_buffers",memory.peakReusableBuffers},{"snapshot_peak_leased_buffers",memory.peakLeasedBuffers},
  {"snapshot_peak_retained_bytes",memory.peakRetainedBytes},{"snapshot_peak_capacity_bytes",memory.peakCapacityBytes},
  {"snapshot_peak_leased_bytes",memory.peakLeasedBytes},
  {"deadline_misses",waits.incomplete},{"deadline_wait_ns",waits.background.totalNs},{"maximum_pending",scheduling.maximumPending},
  {"shared_batches",scheduling.sharedBatches}};
 values.insert(values.end(),aiValues.begin(),aiValues.end());return values;
}
void Game::observeUnpolledAI() {
 std::vector<AIJavaScript*> idle;
 for(unsigned p=0;p<unsigned(gameHeader.getNumberOfPlayers());++p)
  if(players[p]&&players[p]->ai&&players[p]->ai->implementationID==AI::JAVASCRIPT
    &&(!aiPipeline||!aiPipeline->wasPolled(p,stepCounter)))
   idle.push_back(static_cast<AIJavaScript*>(players[p]->ai->aiImplementation));
 if(idle.empty())return;
 // Replay/inactive replicas still update their historical visibility at the
 // legacy phase. Serialize this exceptional observation against private jobs.
 drainAI();
 // Replica-only observations historically capture fresh inputs at this mid-tick phase.
 if(!aiPipeline) worldSnapshots.invalidateBoundary();
 const AIEngine::AIWorldView world(worldSnapshots.captureBoundary(*this).simulationView());
 for(auto* controller:idle)controller->observe(world);
}

void Game::configureTurretTargeting(unsigned delay,unsigned refresh,unsigned capacity,unsigned margin)
{
 auto replacement=std::make_unique<TurretTargeting::Pipeline>(*this,TurretTargeting::Configuration{delay,refresh,capacity,margin});
 turretTargetingPipeline=std::move(replacement);
}

void Game::ensureTurretTargeting() const
{
 if (!turretTargetingPipeline) turretTargetingPipeline=std::make_unique<TurretTargeting::Pipeline>(const_cast<Game&>(*this),TurretTargeting::Configuration{});
}

void Game::forgetTurret(BuildingRef identity) { if (turretTargetingPipeline) turretTargetingPipeline->forget(identity); }
void Game::drainTeamStats(){if(statsPipeline)statsPipeline->drain(JoinReason::Reconfigure);}
void Game::saveTeamStats(GAGCore::OutputStream* s){if(!statsPipeline)statsPipeline=std::make_unique<TeamStatsPipeline>(map.computeExecutor());statsPipeline->save(s);}
void Game::loadTeamStats(GAGCore::InputStream* s, int versionMinor){auto p=std::make_unique<TeamStatsPipeline>(map.computeExecutor());p->load(*this,s,versionMinor);statsPipeline=std::move(p);}
Uint32 Game::teamStatsChecksum(bool heavy) const {
 const auto queue=statsPipeline ? statsPipeline->checkSum(heavy) : 0;
 Uint32 cs=(queue^gameHeader.getTeamStatsDelay())*16777619u;
 for(int t=0;t<teamsCount();++t) {
  const auto& stats=teams[t]->stats;
  const auto value=heavy ? stats.checkSum() : teamStatsCoreChecksum(stats,stats.smoothedStats,stats.statsIndex,stats.smoothedIndex);
  cs=(cs^value)*16777619u;
 }
 return cs;
}

std::vector<std::pair<std::string,Uint64>> Game::teamStatsMetrics() const {
 if(!statsPipeline)return {};
 const auto& m=statsPipeline->metrics;
 return {{"stats_submitted",m.submitted},{"stats_published",m.published},{"stats_peak_pending",m.peakPending}};
}

void Game::drainHiring(){if(hiringPipeline)hiringPipeline->drain(JoinReason::Reconfigure);}
void Game::saveHiring(GAGCore::OutputStream* s){if(!hiringPipeline)hiringPipeline=std::make_unique<HiringPipeline>(map.computeExecutor());hiringPipeline->save(s);}
void Game::loadHiring(GAGCore::InputStream* s){auto p=std::make_unique<HiringPipeline>(map.computeExecutor());p->load(*this,s);hiringPipeline=std::move(p);}
Uint32 Game::hiringChecksum() const {return hiringPipeline ? hiringPipeline->checkSum() : 0;}
const HiringPlanner::BuildingLists* Game::hiringCandidates(const Building& b) const {return hiringPipeline ? hiringPipeline->suggestions(b) : nullptr;}
std::vector<std::pair<std::string,Uint64>> Game::hiringMetrics() const {
 if(!hiringPipeline)return {};
 const auto& m=hiringPipeline->metrics;
 return {{"hiring_submitted",m.submitted},{"hiring_published",m.published}};
}
