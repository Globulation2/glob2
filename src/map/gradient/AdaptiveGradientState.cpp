// SPDX-License-Identifier: GPL-3.0-or-later
#include "AdaptiveGradientState.h"
#include <Stream.h>
#include <stdexcept>

namespace {
void write64(GAGCore::OutputStream* s,std::uint64_t value,const char* name) {
    s->writeEnterSection(name);s->writeUint32(value>>32,"high");s->writeUint32(value,"low");s->writeLeaveSection();
}
std::uint64_t read64(GAGCore::InputStream* s,const char* name) {
    s->readEnterSection(name);const std::uint64_t high=s->readUint32("high"),low=s->readUint32("low");s->readLeaveSection();return (high<<32)|low;
}
void saveTargets(GAGCore::OutputStream* s,const AdaptiveGradientState::Targets& value) {
    write64(s,value.added,"added");write64(s,value.removed,"removed");write64(s,value.routeEpoch,"routeEpoch");write64(s,value.seedCostEpoch,"seedCostEpoch");
}
AdaptiveGradientState::Targets loadTargets(GAGCore::InputStream* s) {
    return {read64(s,"added"),read64(s,"removed"),read64(s,"routeEpoch"),read64(s,"seedCostEpoch")};
}
void saveCounts(GAGCore::OutputStream* s,const AdaptiveGradientState::Counts& values) {
    s->writeUint8(AdaptiveGradientFeatures::CounterMask,"counterMask");
    for (unsigned i=0;i<values.size();++i) {s->writeEnterSection(AdaptiveGradientFeatures::CounterIds[i]);write64(s,values[i],"count");s->writeLeaveSection();}
}
bool loadCounts(GAGCore::InputStream* s,AdaptiveGradientState::Counts& values) {
    const unsigned mask=s->readUint8("counterMask");
    if (mask&~63u) throw std::runtime_error("Invalid adaptive gradient counter mask");
    for (unsigned id=0;id<6;++id) if(mask&(1u<<id)) {
        s->readEnterSection(id);const auto value=read64(s,"count");s->readLeaveSection();
        for(unsigned i=0;i<values.size();++i) if(AdaptiveGradientFeatures::CounterIds[i]==id) values[i]=value;
    }
    return (mask&AdaptiveGradientFeatures::CounterMask)==AdaptiveGradientFeatures::CounterMask;
}
bool loadFlag(GAGCore::InputStream* s,const char* name) {
    const auto value=s->readUint8(name);if(value>1)throw std::runtime_error("Invalid adaptive gradient flag");return value;
}
}
void AdaptiveGradientState::Field::advance(std::uint32_t tick) {
    if(!queryKnown || std::uint32_t(tick-queryTick)>=128) {
        queries.fill(0);count32=count128=historyHash=0;queryTick=tick;queryKnown=true;return;
    }
    while(queryTick!=tick) {++queryTick;count32-=queries[(queryTick-32)&127];count128-=queries[queryTick&127];setBucket(queryTick&127,0);}
}
bool AdaptiveGradientState::Field::validCapture(const Capture& capture) const {
    if(!capture.known)return true;
    for(unsigned i=0;i<total.size();++i) if(capture.baseline[i]>total[i])return false;
    const auto queries=total[2]-capture.baseline[2];
    return total[0]-capture.baseline[0]<=queries && total[1]-capture.baseline[1]<=queries;
}
AdaptiveGradientState::Inputs AdaptiveGradientState::Field::inputs(std::uint32_t tick,const Targets& now) {
    advance(tick);
    const auto bound=[](std::uint64_t value){return std::min<std::uint64_t>(value,1000000);};
    const auto added=bound(now.added-serving.targets.added),removed=bound(now.removed-serving.targets.removed);
    const auto denominator=std::max<std::uint64_t>(1,serving.goalCount);
    return {std::uint32_t(tick-serving.tick),count32,count128,std::int64_t(added),std::int64_t(removed),
        std::int64_t(bound(added*4096/denominator)),std::int64_t(bound(removed*4096/denominator))};
}
AdaptiveGradientState::Evaluation AdaptiveGradientState::Field::evaluate(std::uint32_t tick,const Targets& now,bool hasServing,const Prediction& predict,unsigned fixedInterval) {
    if(!demand&&!reactive&&!deferred)return {};
    if(queued)return {};
    if(inflight){demand=false;return {};}
    const auto accept=[&](Reason reason){acceptedReason=reason;return Evaluation{reason};};
    if(!hasServing)return accept(Reason::Cold);
    if(deferred && acceptedReason!=Reason::None)return {acceptedReason};
    // The minimum remains a correctness guard. Warm ordinary admissions have
    // no maximum-age fallback: the model must accept, or explicit recovery applies.
    const auto age=std::uint32_t(tick-submitted);
    if(age<MinimumInterval){demand=false;return {};}
    if(reactive)return accept(Reason::Reactive);
    // Legacy fields get one demanded capture to establish honest new semantics.
    if(!serving.known)return accept(Reason::HistoryInitialization);
    // Fixed intervals are explicit research behavior, never a production cap.
    if(fixedInterval && age>=std::max(fixedInterval,MinimumInterval))return accept(Reason::Bootstrap);
    if(!fixedInterval && predict && predict(inputs(tick,now),contradictions())>=Threshold)return accept(Reason::Model);
    demand=false;acceptedReason=Reason::None;return {};
}
void AdaptiveGradientState::Capture::save(GAGCore::OutputStream* s) const {
    s->writeEnterSection("adaptiveCapture");s->writeUint32(tick,"tick");saveTargets(s,targets);write64(s,goalCount,"goalCount");s->writeUint8(known,"known");saveCounts(s,baseline);s->writeLeaveSection();
}
AdaptiveGradientState::Capture AdaptiveGradientState::Capture::load(GAGCore::InputStream* s) {
    s->readEnterSection("adaptiveCapture");Capture value;value.tick=s->readUint32("tick");value.targets=loadTargets(s);value.goalCount=read64(s,"goalCount");value.known=loadFlag(s,"known");value.known=loadCounts(s,value.baseline)&&value.known;s->readLeaveSection();return value;
}
void AdaptiveGradientState::eraseTeam(unsigned team) {
    if(team<guardSeedCostEpoch.size())guardSeedCostEpoch[team]=0;
    std::erase_if(fields,[&](const auto& item){return item.first.team==team;});
    std::erase_if(targets,[&](const auto& item){return item.first.team==team;});
}
void AdaptiveGradientState::save(GAGCore::OutputStream* s) const {
    s->writeEnterSection("adaptiveGradients");write64(s,routeEpoch,"routeEpoch");
    s->writeEnterSection("guardCostEpochs");for(unsigned team=0;team<guardSeedCostEpoch.size();++team){s->writeEnterSection(team);write64(s,guardSeedCostEpoch[team],"epoch");s->writeLeaveSection();}s->writeLeaveSection();
    s->writeUint32(targets.size(),"targetCount");s->writeEnterSection("targets");unsigned i=0;
    for(const auto& [key,value]:targets) {s->writeEnterSection(i++);s->writeUint32(key.packed(),"key");saveTargets(s,value);s->writeLeaveSection();}
    s->writeLeaveSection();s->writeUint32(fields.size(),"fieldCount");s->writeEnterSection("fields");i=0;
    for(const auto& [key,f]:fields) {
        s->writeEnterSection(i++);s->writeUint32(key.packed(),"key");f.serving.save(s);s->writeEnterSection("totals");saveCounts(s,f.total);s->writeLeaveSection();
        s->writeUint32(f.submitted,"submitted");s->writeUint32(f.queryTick,"queryTick");
        s->writeUint8(f.demand,"demand");s->writeUint8(f.reactive,"reactive");s->writeUint8(f.queued,"queued");s->writeUint8(f.inflight,"inflight");s->writeUint8(f.deferred,"deferred");s->writeUint8(unsigned(f.acceptedReason),"acceptedReason");s->writeUint8(f.queryKnown,"queryKnown");
        for(unsigned j=0;j<128;++j){s->writeEnterSection(j);s->writeUint32(f.queries[j],"queries");s->writeLeaveSection();}
        s->writeLeaveSection();
    }
    s->writeLeaveSection();s->writeLeaveSection();
}
void AdaptiveGradientState::load(GAGCore::InputStream* s,unsigned teams,unsigned movementClasses,unsigned materials) {
    *this={};s->readEnterSection("adaptiveGradients");routeEpoch=read64(s,"routeEpoch");
    s->readEnterSection("guardCostEpochs");for(unsigned team=0;team<guardSeedCostEpoch.size();++team){s->readEnterSection(team);guardSeedCostEpoch[team]=read64(s,"epoch");s->readLeaveSection();}s->readLeaveSection();
    const unsigned limit=teams*movementClasses*(2*materials+3),targetCount=s->readUint32("targetCount");
    if(targetCount>limit)throw std::runtime_error("Invalid adaptive gradient target count");
    s->readEnterSection("targets");
    for(unsigned i=0;i<targetCount;++i) {
        s->readEnterSection(i);const auto key=GradientFieldKey::unpack(s->readUint32("key"));
        if(!key.valid(teams,movementClasses,materials))throw std::runtime_error("Invalid adaptive target key");
        const auto targetKey=GradientTargetKey::of(key);if(targetKey.packed()!=key.packed())throw std::runtime_error("Noncanonical adaptive target key");if(targets.contains(targetKey))throw std::runtime_error("Duplicate adaptive target key");
        auto value=loadTargets(s);if(value.routeEpoch>routeEpoch)throw std::runtime_error("Invalid adaptive route epoch");targets.emplace(targetKey,value);s->readLeaveSection();
    }
    s->readLeaveSection();const unsigned count=s->readUint32("fieldCount");if(count>limit)throw std::runtime_error("Invalid adaptive gradient field count");s->readEnterSection("fields");
    for(unsigned i=0;i<count;++i) {
        s->readEnterSection(i);const auto key=GradientFieldKey::unpack(s->readUint32("key"));
        if(!key.valid(teams,movementClasses,materials)||fields.contains(key)||!targets.contains(GradientTargetKey::of(key)))throw std::runtime_error("Invalid adaptive gradient field key");
        auto& f=fields[key];f.serving=Capture::load(s);s->readEnterSection("totals");if(!loadCounts(s,f.total))f.serving.known=false;s->readLeaveSection();
        f.submitted=s->readUint32("submitted");f.queryTick=s->readUint32("queryTick");
        f.demand=loadFlag(s,"demand");f.reactive=loadFlag(s,"reactive");f.queued=loadFlag(s,"queued");f.inflight=loadFlag(s,"inflight");f.deferred=loadFlag(s,"deferred");const auto accepted=s->readUint8("acceptedReason");if(accepted>unsigned(Reason::Bootstrap)||accepted==unsigned(Reason::AgeCap))throw std::runtime_error("Invalid adaptive accepted reason");f.acceptedReason=Reason(accepted);f.queryKnown=loadFlag(s,"queryKnown");
        if((f.queued&&f.inflight)||(f.deferred&&(f.inflight||f.acceptedReason==Reason::None)))throw std::runtime_error("Conflicting adaptive gradient association");
        if(!f.validCapture(f.serving))throw std::runtime_error("Invalid adaptive movement baseline");
        const auto now=currentTargets(key);
        if(f.serving.known&&(f.serving.targets.added>now.added||f.serving.targets.removed>now.removed||f.serving.targets.routeEpoch>routeEpoch||f.serving.targets.seedCostEpoch>now.seedCostEpoch))throw std::runtime_error("Invalid adaptive capture epoch");
        for(unsigned j=0;j<128;++j){s->readEnterSection(j);const auto value=s->readUint32("queries");s->readLeaveSection();if(value>1000000||(!f.queryKnown&&value))throw std::runtime_error("Invalid adaptive query history");f.queries[j]=value;f.historyHash^=Field::bucketHash(j,value);f.count128+=value;if(((f.queryTick-j)&127)<32)f.count32+=value;}
        s->readLeaveSection();
    }
    s->readLeaveSection();s->readLeaveSection();
}
std::uint32_t AdaptiveGradientState::checksum() const {
    std::uint32_t hash=0;const auto add=[&](std::uint64_t value){hash=std::rotl(hash,1)^std::uint32_t(value)^std::uint32_t(value>>32);};
    add(routeEpoch);for(auto value:guardSeedCostEpoch)add(value);for(const auto& [key,value]:targets){add(key.packed());add(value.added);add(value.removed);add(value.routeEpoch);add(value.seedCostEpoch);}
    for(const auto& [key,f]:fields){add(key.packed());add(f.serving.tick);add(f.serving.goalCount);add(f.serving.known);add(f.serving.targets.added);add(f.serving.targets.removed);add(f.serving.targets.routeEpoch);add(f.serving.targets.seedCostEpoch);for(auto value:f.serving.baseline)add(value);for(auto value:f.total)add(value);add(f.submitted);add(f.queryTick);add(f.demand);add(f.reactive);add(f.queued);add(f.inflight);add(f.deferred);add(unsigned(f.acceptedReason));add(f.queryKnown);add(f.historyHash);add(f.count32);add(f.count128);}
    return hash;
}
