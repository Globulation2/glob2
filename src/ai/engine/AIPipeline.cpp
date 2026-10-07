// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIPipeline.h"
#include "AIReceiptSerialization.h"
#include "AI.h"
#include "Game.h"
#include "Player.h"
#include "Order.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "GameDiagnostics.h"
#include <algorithm>
#include <limits>
namespace AIEngine
{
namespace {
std::vector<Uint8> wire(Order& order) {
 std::vector<Uint8> result{order.getOrderType()};
 if(order.getDataLength()) { auto* p=order.getData(); result.insert(result.end(),p,p+order.getDataLength()); }
 return result;
}
void saveId(GAGCore::OutputStream* s,const RequestId& id) {
 s->writeUint32(id.player,"player"); s->writeUint32(id.generation,"generation"); s->writeUint32(id.observedTick,"observedTick");
 s->writeUint32(Uint32(id.pollSequence),"sequenceLow"); s->writeUint32(Uint32(id.pollSequence>>32),"sequenceHigh"); s->writeUint32(id.actionOrdinal,"ordinal");
}
RequestId loadId(GAGCore::InputStream* s) {
 RequestId id; id.player=s->readUint32("player"); id.generation=s->readUint32("generation"); id.observedTick=s->readUint32("observedTick");
 auto low=s->readUint32("sequenceLow"),high=s->readUint32("sequenceHigh"); id.pollSequence=Uint64(low)|(Uint64(high)<<32); id.actionOrdinal=s->readUint32("ordinal"); return id;
}
void saveBytes(GAGCore::OutputStream* s,const std::vector<Uint8>& bytes) {
 s->writeUint32(bytes.size(),"bytes"); for(unsigned i=0;i<bytes.size();++i) {s->writeEnterSection(i);s->writeUint8(bytes[i],"byte");s->writeLeaveSection();}
}
std::vector<Uint8> loadBytes(GAGCore::InputStream* s) {
 auto count=s->readCount("bytes",16*1024*1024); std::vector<Uint8> b(count);
 for(unsigned i=0;i<count;++i) {s->readEnterSection(i);b[i]=s->readUint8("byte");s->readLeaveSection();} return b;
}
}
void Pipeline::cancel(unsigned player) {
 if(player>=actors.size()) return;
 auto& a=actors[player]; scheduler.cancel(player,a.generation);
 if(auto session=diagnosticsSession.lock())session->cancelCaptures(player);
 if(a.generation==std::numeric_limits<Uint32>::max()) throw std::overflow_error("AI controller generation overflow");
 ++a.generation; a.sequence=0; a.controller=nullptr; a.suspended=false; a.feedback.clear(); a.admitted.reset(); a.published.reset();a.retainedQueryVectorBytes.reset();
}
bool Pipeline::wasPolled(unsigned player, Uint32 tick) const {
 return scheduler.wasSubmitted(player,actors.at(player).generation,tick);
}
std::vector<std::pair<unsigned,std::shared_ptr<Order>>> Pipeline::prepare(Game& game,
 std::span<const unsigned> eligible,bool paused,const std::shared_ptr<GameDiagnostics::Session>& diagnostics) {
 std::vector<std::pair<unsigned,std::shared_ptr<Order>>> result;
 std::array<bool,32> requested{},newlyDelivered{};
 if(diagnostics && diagnosticsSession.lock()!=diagnostics) scheduler.adoptDiagnostics(*diagnostics);
 diagnosticsSession=diagnostics;
 for(auto p:eligible) {
  if(p>=unsigned(game.gameHeader.getNumberOfPlayers())||p>=actors.size()) throw std::invalid_argument("Invalid AI poll player");
  if(requested[p])throw std::invalid_argument("Duplicate AI poll player");
  requested[p]=true;
 }
 // Decisions run on the map's compute executor; with the AI experiment on,
 // its workers share them and the owner joins at the deadline.
 const bool shared=game.map.computeEnabled(Map::ComputeAI);
 if(!configured) {scheduler.configure(game.gameHeader.getAIOrderDelay(),game.map.computeExecutor(),shared);configured=true;}
 else if(scheduler.delayTicks()!=game.gameHeader.getAIOrderDelay()) throw std::logic_error("AI delay cannot change during a match");
 else if(!scheduler.hasExecutor()||scheduler.sharedExecution()!=shared) scheduler.configureExecution(game.map.computeExecutor(),shared);
 if(paused) {
  const auto gradientRequirements=game.map.pendingGradientRequirements();
  if(gradientRequirements) game.map.preparePendingGradient(snapshots.captureBoundary(game,gradientRequirements));
  for(auto p:eligible) result.emplace_back(p,std::make_shared<NullOrder>());
  return result;
 }
 for(unsigned p=0;p<actors.size();++p) {
  auto* player=p<unsigned(game.gameHeader.getNumberOfPlayers())?game.players[p]:nullptr;
  auto* current=player?player->ai:nullptr;
  auto& actor=actors[p];
  if(actor.controller && actor.controller!=current) cancel(p);
  if(actor.controller && player->team && !player->team->isAlive && !actor.suspended) {
   // Death may be reversed by a scenario. Keep receipts in this controller's
   // ordered stream so canceled intentions settle on its next eligible poll.
   auto canceled=scheduler.cancel(p,actor.generation);
   if(auto session=diagnosticsSession.lock())session->cancelCaptures(p);
   auto remember=[&](Delivery& delivery) {
    if(delivery.command.bytes.front()!=ORDER_NULL)
     actor.feedback.push_back({delivery.request,ExecutionStatus::Canceled,game.stepCounter,
       delivery.dueTick,std::move(delivery.command.bytes),delivery.command.target});
   };
   if(actor.admitted) remember(*actor.admitted);
   for(auto& delivery:canceled) remember(delivery);
   actor.admitted.reset();actor.published.reset();actor.suspended=true;
  }
  if(current && player->team && player->team->isAlive) actor.suspended=false;
 }
 if(!boundary || *boundary!=game.stepCounter) {
  if(boundary && game.stepCounter<*boundary) throw std::logic_error("AI observation clock moved backwards");
  if(diagnostics) diagnostics->beginTick(game);
  std::vector<unsigned> polls(eligible.begin(),eligible.end());
  std::sort(polls.begin(),polls.end());
  if(std::adjacent_find(polls.begin(),polls.end())!=polls.end()) throw std::invalid_argument("Duplicate AI poll");
  // Controller requirements determine the shared capture union. The union
  // is declared before capture; each consumer receives its own projection.
  const auto gradientRequirements=game.map.pendingGradientRequirements();
  SimulationSnapshot::Requirements requirements=gradientRequirements;
  for(auto p:polls) if(game.players[p]&&game.players[p]->ai&&game.players[p]->team->isAlive) requirements|=game.players[p]->ai->observationRequirements();
  const auto captured=snapshots.captureBoundary(game,requirements);
  if(gradientRequirements)game.map.preparePendingGradient(captured.project(gradientRequirements));
  for(auto p:polls) {
   auto* player=game.players[p]; auto& actor=actors[p]; actor.published.reset();
   if(!player||!player->ai||!player->team->isAlive) continue;
   actor.controller=player->ai; actor.suspended=false; auto* ai=actor.controller; ai->prepareDecision();
   if(actor.sequence==std::numeric_limits<Uint64>::max()) throw std::overflow_error("AI poll sequence overflow");
   auto world=std::make_shared<const AIWorldView>(captured.project(ai->observationRequirements()));
   RequestId request{p,actor.generation,game.stepCounter,actor.sequence++,0};
   auto feedback=std::move(actor.feedback); actor.feedback.clear();
   auto fields=diagnostics?diagnostics->reserveCapture(p,game.stepCounter):nullptr;
   scheduler.submit(request,world,[ai,world,request,feedback=std::move(feedback),fields,team=unsigned(player->teamNumber),delay=scheduler.delayTicks()](const AIWorldView& input) {
    std::vector<ResourceEnrollmentRequest> enrollments;
    DecisionContext context{input,request.player,team,feedback,world,request.observedTick+delay,request.pollSequence,&enrollments,fields,request.generation};
    auto output=ai->decide(context); output.resourceEnrollments=std::move(enrollments); return output;
   });
  }
  scheduler.dispatch();
  for(auto& delivery:scheduler.takeDue(game.stepCounter)) {
   newlyDelivered[delivery.request.player]=true;
   auto& actor=actors[delivery.request.player];
   if(actor.generation!=delivery.request.generation||!actor.controller) throw std::logic_error("Canceled AI output escaped lifecycle barrier");
   actor.controller->publishDecision(delivery.command);
   actor.retainedQueryVectorBytes=delivery.command.retainedQueryVectorBytes;
   peakRetainedQueryVectorBytes=std::max(peakRetainedQueryVectorBytes,queryVectorMemory().first);
   if(diagnostics&&delivery.command.fieldDiagnostics) diagnostics->publishCapture(*delivery.command.fieldDiagnostics);
   for(const auto& e:delivery.command.resourceEnrollments) {
    if(!e.initialField) throw std::logic_error("AI resource enrollment lacks frozen inputs");
    game.map.installObservedResourceField(e.team,e.resource,e.swim,*e.initialField);
   }
   auto order=delivery.command.decode();order->sender=delivery.request.player;
   order->aiGeneration=delivery.request.generation;order->aiPollSequence=delivery.request.pollSequence;
   actor.published=order;
   // Null outputs have no command admission or incarnation to await. The
   // local network intentionally filters nulls, so settle them at publication.
   if(order->getOrderType()==ORDER_NULL) actor.feedback.push_back({delivery.request,ExecutionStatus::Accepted,game.stepCounter,delivery.dueTick,delivery.command.bytes});
   else {
    if(actor.admitted) throw std::logic_error("Previous AI order has not executed at its deadline");
    delivery.command.resourceEnrollments.clear(); delivery.command.diagnostics.clear();delivery.command.telemetry.reset();delivery.command.namedTelemetry.clear();delivery.command.fieldDiagnostics.reset();
    actor.admitted=std::move(delivery);
   }
  }
  if(diagnostics) diagnostics->completeTick(game);
  boundary=game.stepCounter;
 }
 // Poll eligibility controls new observations, never a submitted deadline.
 // Repeated stalled calls return only requested actors, avoiding re-enqueueing
 // an output that was already handed to the network on the first call.
 for(unsigned p=0;p<actors.size();++p)if(requested[p]||newlyDelivered[p])
  result.emplace_back(p,actors[p].published?actors[p].published:std::make_shared<NullOrder>());
 return result;
}
std::shared_ptr<Order> Pipeline::validate(Game& game,std::shared_ptr<Order> order,unsigned player) {
 if(!order||player>=actors.size()) return order;
 auto& a=actors[player];
 if(order->aiGeneration && order->aiGeneration!=a.generation) {
  auto null=std::make_shared<NullOrder>();null->sender=player;return null;
 }
 if(order->aiGeneration && (!a.admitted || order->aiPollSequence!=a.admitted->request.pollSequence)) {
  auto null=std::make_shared<NullOrder>();null->sender=player;return null;
 }
 if(!a.admitted) return order;
 if((order->aiGeneration && order->aiPollSequence!=a.admitted->request.pollSequence)||wire(*order)!=a.admitted->command.bytes) throw std::logic_error("AI delivered order does not match admission ledger");
 if(a.admitted->command.target&&!game.resolveBuilding(*a.admitted->command.target)) {
  settle(game,order,false);auto null=std::make_shared<NullOrder>();null->sender=player;return null;
 }
 return order;
}
void Pipeline::settle(Game& game,const std::shared_ptr<Order>& order,bool accepted) {
 if(!order||order->sender<0||unsigned(order->sender)>=actors.size()) return;
 auto& a=actors[order->sender]; if(!a.admitted||wire(*order)!=a.admitted->command.bytes
  ||(order->aiGeneration&&(order->aiGeneration!=a.admitted->request.generation||order->aiPollSequence!=a.admitted->request.pollSequence))) return;
 a.feedback.push_back({a.admitted->request,accepted?ExecutionStatus::Accepted:ExecutionStatus::Rejected,game.stepCounter,a.admitted->dueTick,a.admitted->command.bytes,a.admitted->command.target});a.admitted.reset();a.published.reset();
}
void Pipeline::save(GAGCore::OutputStream* s) {
 drain();s->writeEnterSection("AIPipeline");
 s->writeUint8(boundary.has_value(),"hasBoundary");if(boundary)s->writeUint32(*boundary,"boundary");
 scheduler.save(s);
 for(unsigned p=0;p<actors.size();++p) {
  const auto& a=actors[p];s->writeEnterSection(p);s->writeUint32(a.generation,"generation");s->writeUint32(Uint32(a.sequence),"sequenceLow");s->writeUint32(Uint32(a.sequence>>32),"sequenceHigh");
  s->writeUint32(a.feedback.size(),"feedback");
  for(unsigned i=0;i<a.feedback.size();++i) {
   s->writeEnterSection(i);saveExecutionReceipt(*s,a.feedback[i]);s->writeLeaveSection();
  }
  s->writeUint8(a.admitted.has_value(),"hasAdmitted");if(a.admitted) {saveId(s,a.admitted->request);s->writeUint32(a.admitted->dueTick,"dueTick");saveBytes(s,a.admitted->command.bytes);s->writeUint8(a.admitted->command.target.has_value(),"hasTarget");if(a.admitted->command.target){s->writeUint16(a.admitted->command.target->gid,"gid");s->writeUint32(a.admitted->command.target->generation,"targetGeneration");}}
  s->writeUint8(bool(a.published),"hasPublished");if(a.published)saveBytes(s,wire(*a.published));s->writeLeaveSection();
 }
 s->writeLeaveSection();
}
bool Pipeline::load(Game& game,GAGCore::InputStream* s)
{
 GAGCore::BinaryInputStream::CheckedReads checked(s);
 auto boolean=[&](const char* name) {
  const auto value=s->readUint8(name);
  if(value>1)throw std::runtime_error("Invalid saved AI pipeline boolean");
  return value!=0;
 };
 auto validRequest=[&](const RequestId& id,unsigned player,const Actor& actor) {
  return id.player==player && id.generation==actor.generation && id.pollSequence<actor.sequence
   && id.actionOrdinal==0 && id.observedTick<=game.stepCounter
   && id.observedTick<=std::numeric_limits<Uint32>::max()-scheduler.delayTicks();
 };
 s->readEnterSection("AIPipeline");
 if(boolean("hasBoundary"))boundary=s->readUint32("boundary");else boundary.reset();
 if(boundary && *boundary>game.stepCounter)return false;
 if(!scheduler.load(s)||scheduler.delayTicks()!=game.gameHeader.getAIOrderDelay())return false;
 configured=true;
 for(unsigned p=0;p<actors.size();++p) {
  auto& actor=actors[p];s->readEnterSection(p);
  actor.generation=s->readUint32("generation");
  const auto low=s->readUint32("sequenceLow"),high=s->readUint32("sequenceHigh");
  actor.sequence=Uint64(low)|(Uint64(high)<<32);
  if(!actor.generation)return false;
  const auto count=s->readCount("feedback",9);
  for(unsigned i=0;i<count;++i) {
   s->readEnterSection(i);auto receipt=loadExecutionReceipt(*s);
   if(!validRequest(receipt.request,p,actor))return false;
   if(receipt.executionTick>game.stepCounter || receipt.scheduledTick!=receipt.request.observedTick+scheduler.delayTicks()
     ||(receipt.status==ExecutionStatus::Canceled ? receipt.executionTick>receipt.scheduledTick : receipt.executionTick!=receipt.scheduledTick) || (!actor.feedback.empty() && actor.feedback.back().request.pollSequence>=receipt.request.pollSequence))return false;
   actor.feedback.push_back(std::move(receipt));s->readLeaveSection();
  }
  if(boolean("hasAdmitted")) {
   Delivery delivery;delivery.request=loadId(s);delivery.dueTick=s->readUint32("dueTick");delivery.command.bytes=loadBytes(s);
   if(boolean("hasTarget"))delivery.command.target=BuildingRef{s->readUint16("gid"),s->readUint32("targetGeneration")};
   const auto order=delivery.command.decode();
   const auto target=Command::targetGid(*order);
   if(target.has_value()!=delivery.command.target.has_value()
     ||(target && *target!=delivery.command.target->gid))return false;
   if(!validRequest(delivery.request,p,actor)||delivery.dueTick!=delivery.request.observedTick+scheduler.delayTicks()
     ||delivery.dueTick!=game.stepCounter||!boundary||*boundary!=delivery.dueTick||order->getOrderType()==ORDER_NULL)return false;
   actor.admitted=std::move(delivery);
  }
  if(boolean("hasPublished")) {
   Command command;command.bytes=loadBytes(s);actor.published=command.decode();actor.published->sender=p;
   if(actor.admitted) {
    if(command.bytes!=actor.admitted->command.bytes)return false;
    actor.published->aiGeneration=actor.admitted->request.generation;
    actor.published->aiPollSequence=actor.admitted->request.pollSequence;
   } else if(actor.published->getOrderType()!=ORDER_NULL)return false;
  } else if(actor.admitted)return false;
  if(p<unsigned(game.gameHeader.getNumberOfPlayers())&&game.players[p])actor.controller=game.players[p]->ai;
  if(!actor.controller && (actor.sequence || !actor.feedback.empty() || actor.admitted))return false;
  actor.suspended=actor.controller && game.players[p]->team && !game.players[p]->team->isAlive;
  s->readLeaveSection();
 }
 std::array<std::pair<Uint32,Uint64>,32> streams;
 for(unsigned p=0;p<actors.size();++p)streams[p]={actors[p].generation,actors[p].sequence};
 if(!scheduler.validateRestoredState(game.gameHeader.getNumberOfPlayers(),game.stepCounter,streams,
   game.map.getW(),game.map.getH(),game.mapHeader.getNumberOfTeams()))return false;
 s->readLeaveSection();return true;
}
}
