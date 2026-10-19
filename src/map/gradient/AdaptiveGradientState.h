// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AdaptiveGradientFeatures.h"
#include "GradientFieldIdentity.h"
#include "TeamCapacity.h"
#include "ResourceGradientContradictions.h"
#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <map>
#include <utility>

namespace GAGCore { class InputStream; class OutputStream; }

// Owner-thread scheduling state, sparse in fields that have actually been read.
// World mutations update shared summaries. Query histories never allocate per
// unit, and worker completion never changes scheduling or capture chronology.
struct AdaptiveGradientState
{
    using Counts=AdaptiveGradientFeatures::Counts;
    using Inputs=AdaptiveGradientFeatures::Base;
    static constexpr unsigned MinimumInterval=8, QueryHistoryTicks=128;
    // The generated deployment model owns its benefit cutoff. Define this in
    // the implementation so coefficient updates do not widen header rebuilds.
    static const std::int64_t Threshold;
    struct Targets {
        std::uint64_t added=0,removed=0,routeEpoch=0,seedCostEpoch=0;
        auto operator<=>(const Targets&) const = default;
    };
    struct Capture {
        std::uint32_t tick=0;
        Targets targets;
        std::uint64_t goalCount=0;
        Counts baseline{};
        // Old resource telemetry measured different target semantics. A false
        // flag means unknown evidence, never an observed history of zero events.
        bool known=false;
        void save(GAGCore::OutputStream*) const;
        static Capture load(GAGCore::InputStream*);
    };
    enum class Reason : std::uint8_t { None,Cold,Reactive,HistoryInitialization,Model,AgeCap/* reserved wire ID; never admitted */,Bootstrap };
    struct Evaluation { Reason reason=Reason::None; bool requested() const { return reason!=Reason::None; } };
    using Prediction=std::function<std::int64_t(const Inputs&,const Counts&)>;
    struct Field {
        Capture serving;
        Counts total{};
        std::uint32_t submitted=0,queryTick=0;
        bool demand=false,reactive=false,queued=false,inflight=false,deferred=false,queryKnown=false;
        Reason acceptedReason=Reason::None;
        std::array<std::uint32_t,128> queries{};
        std::uint32_t count32=0,count128=0,historyHash=0;
        static std::uint32_t bucketHash(unsigned i,std::uint32_t value) {
            return std::rotl(value*0x9e3779b9u,int(i&31))^value*(i+1);
        }
        void setBucket(unsigned i,std::uint32_t value) {
            historyHash^=bucketHash(i,queries[i])^bucketHash(i,value);queries[i]=value;
        }
        void advance(std::uint32_t tick);
        void queried(std::uint32_t tick) {
            advance(tick);
            if (queries[tick&127]<1000000) {setBucket(tick&127,queries[tick&127]+1);++count32;++count128;}
            demand=true;
        }
        // Call once after the family selector has classified its actual result.
        // Here emptyGoal is the shared evidence slot for family-invalid goals.
        void recordMovement(const GradientMovementEvidence& evidence) {
            total[0]+=evidence.emptyGoal;total[1]+=evidence.reachableNoMove;++total[2];
        }
        Counts contradictions() const {
            Counts result=total;for (unsigned i=0;i<result.size();++i) result[i]-=serving.baseline[i];return result;
        }
        Capture capture(std::uint32_t tick,const Targets& targets,std::uint64_t goalCount) const {
            return {tick,targets,goalCount,total,true};
        }
        bool validCapture(const Capture&) const;
        Inputs inputs(std::uint32_t tick,const Targets&);
        // Rejected ordinary demand expires at this boundary: an idle field must
        // not refresh later merely because an old query aged. Accepted demand
        // survives until admission, including explicit capacity deferral.
        Evaluation evaluate(std::uint32_t tick,const Targets&,bool hasServing,const Prediction&,unsigned fixedInterval=0);
        void defer(Reason reason) {deferred=demand=true;acceptedReason=reason;}
        void admitted(std::uint32_t tick) {submitted=tick;demand=reactive=queued=deferred=false;acceptedReason=Reason::None;inflight=true;}
        void published(const Capture& capture) {serving=capture;inflight=false;}
    };
    std::map<GradientFieldKey,Field> fields;
    std::map<GradientTargetKey,Targets> targets;
    // Shared route changes are coarse evidence; there is no loop over fields.
    std::uint64_t routeEpoch=0;
    // Conservative cost-affecting event epoch, separate from target membership.
    // It is diagnostic metadata; no extra fitted inference input is implied.
    std::array<std::uint64_t,TEAM_MAX_COUNT> guardSeedCostEpoch{};
    Targets currentTargets(GradientFieldKey key) const {
        const auto found=targets.find(GradientTargetKey::of(key));
        auto value=found==targets.end()?Targets{}:found->second;value.routeEpoch=routeEpoch;if(key.family==GradientFieldFamily::Guard)value.seedCostEpoch+=guardSeedCostEpoch[key.team];return value;
    }
    Field& enroll(GradientFieldKey key) {targets.try_emplace(GradientTargetKey::of(key));return fields[key];}
    Field& queried(GradientFieldKey key,std::uint32_t tick) {auto& field=enroll(key);field.queried(tick);return field;}
    void targetChanged(GradientTargetKey key,bool added) {const auto found=targets.find(key);if(found==targets.end())return;auto& value=found->second;if(added)++value.added;else ++value.removed;}
    void seedCostsChanged(GradientTargetKey key) {const auto found=targets.find(key);if(found!=targets.end())++found->second.seedCostEpoch;}
    void routesChanged() {++routeEpoch;}
    void guardCostsChanged(unsigned team) {if(team<guardSeedCostEpoch.size())++guardSeedCostEpoch[team];}
    void eraseTeam(unsigned team);
    void clearAssociations() {for(auto& [key,field]:fields){field.queued=field.inflight=field.deferred=false;field.acceptedReason=Reason::None;}}
    void save(GAGCore::OutputStream*) const;
    void load(GAGCore::InputStream*,unsigned teams,unsigned movementClasses,unsigned materials);
    std::uint32_t checksum() const;
};
