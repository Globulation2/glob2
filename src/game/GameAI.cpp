// SPDX-License-Identifier: GPL-3.0-or-later
#include "Game.h"
#include "ai/engine/AIPipeline.h"
#include "AIJavaScript.h"
#include "Player.h"
std::vector<std::pair<unsigned,std::shared_ptr<Order>>> Game::prepareAIOrders(std::span<const unsigned> players,bool paused,const std::shared_ptr<GameDiagnostics::Session>& diagnostics) {
 if(!aiPipeline)aiPipeline=std::make_unique<AIEngine::Pipeline>();return aiPipeline->prepare(*this,players,paused,diagnostics);
}
std::shared_ptr<Order> Game::validateAIOrder(std::shared_ptr<Order> order,unsigned player) {return aiPipeline?aiPipeline->validate(*this,std::move(order),player):order;}
void Game::settleAIOrder(const std::shared_ptr<Order>& order,bool accepted) {if(aiPipeline)aiPipeline->settle(*this,order,accepted);}
void Game::cancelAI(unsigned player) {if(aiPipeline)aiPipeline->cancel(player);}
void Game::drainAI() {if(aiPipeline)aiPipeline->drain();}
void Game::clearAI() {aiPipeline.reset();}
void Game::saveAI(GAGCore::OutputStream* stream) {if(!aiPipeline){aiPipeline=std::make_unique<AIEngine::Pipeline>();aiPipeline->prepare(*this,{},true,nullptr);}aiPipeline->save(stream);}
bool Game::loadAI(GAGCore::InputStream* stream) {auto pipeline=std::make_unique<AIEngine::Pipeline>();if(!pipeline->load(*this,stream))return false;aiPipeline=std::move(pipeline);return true;}
std::vector<std::pair<std::string,Uint64>> Game::aiMetrics() const {
 if(!aiPipeline)return {};
 const auto& capture=aiPipeline->captureMetrics();const auto& scheduling=aiPipeline->schedulingMetrics();const auto memory=aiPipeline->snapshotMemoryMetrics();const auto queryMemory=aiPipeline->queryVectorMemory();
 return {{"captures",capture.captures},{"extraction_ns",capture.captureNs},{"preparation_ns",capture.preparationNs},
  {"bytes_copied",capture.bytesCopied},{"component_reuses",capture.reusedComponents},{"allocations",capture.allocations},
  {"computation_ns",aiPipeline->computationNs()},{"controller_query_vector_bytes",queryMemory.first},
  {"controller_query_vector_peak_bytes",aiPipeline->peakQueryVectorMemory()},
  {"controller_query_vector_samples",queryMemory.second},{"submitted",scheduling.submitted},{"delivered",scheduling.delivered},
  {"snapshot_allocated_buffers",memory.allocatedBuffers},{"snapshot_reusable_buffers",memory.reusableBuffers},
  {"snapshot_leased_buffers",memory.leasedBuffers},{"snapshot_retained_bytes",memory.retainedBytes},
  {"snapshot_capacity_bytes",memory.capacityBytes},{"snapshot_leased_bytes",memory.leasedBytes},
  {"snapshot_peak_allocated_buffers",memory.peakAllocatedBuffers},{"snapshot_peak_reusable_buffers",memory.peakReusableBuffers},{"snapshot_peak_leased_buffers",memory.peakLeasedBuffers},
  {"snapshot_peak_retained_bytes",memory.peakRetainedBytes},{"snapshot_peak_capacity_bytes",memory.peakCapacityBytes},
  {"snapshot_peak_leased_bytes",memory.peakLeasedBytes},
  {"deadline_misses",scheduling.deadlineMisses},{"deadline_wait_ns",scheduling.deadlineWaitNs},{"maximum_pending",scheduling.maximumPending},
  {"shared_batches",scheduling.sharedBatches}};
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
 // Resource memories read the registry's material yields, so the catalogs ride along.
 constexpr auto requirements=SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs)
  |SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain)
  |SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)
  |SimulationSnapshot::bit(SimulationSnapshot::Component::Visibility)
  |SimulationSnapshot::bit(SimulationSnapshot::Component::Teams);
 const AIEngine::AIWorldView world(aiPipeline ? aiPipeline->observe(*this,requirements)
  : SimulationSnapshot::capture(*this,AIEngine::AIWorldView::captureCatalog(*this),requirements));
 for(auto* controller:idle)controller->observe(world);
}
