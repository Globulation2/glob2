// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClaimPolicyConfig.h"
#include "ClaimRecovery.h"
#include "ClaimMixedPatterns.h"
#include "ResourceClaimPilotPatterns.h"
#include "online/Sha256.h"
#include <nlohmann/json.hpp>
#include <array>
#include <stdexcept>

namespace ResourceClaims
{
namespace
{
using Json=nlohmann::json;
constexpr std::array<unsigned,15> Intervals{0,1,2,3,4,6,8,12,16,24,32,48,64,96,128};
bool interval(unsigned v){return std::find(Intervals.begin(),Intervals.end(),v)!=Intervals.end();}
const Experiment::PilotPattern& patternByName(std::string_view name)
{
    for(const auto& pattern:Experiment::pilotPatterns())if(name==pattern.name)return pattern;
    throw std::invalid_argument("unknown resource claim experiment pattern");
}
std::string patternDigest(std::string_view name)
{
    const auto& pattern=patternByName(name); Online::Sha256 hash;
    const auto integer=[&](std::uint32_t value,unsigned bytes) {
        std::array<std::uint8_t,4> encoded{};
        for(unsigned i=0;i<bytes;++i)encoded[i]=std::uint8_t(value>>(8*i));
        hash.update(encoded.data(),bytes);
    };
    for(unsigned direction=0;direction<8;++direction) {
        const auto view=pattern.viewForDirection(direction);integer(view.x.size(),4);
        for(std::size_t i=0;i<view.x.size();++i) {
            integer(std::uint16_t(view.x[i]),2);integer(std::uint16_t(view.y[i]),2);integer(view.origin[i],1);
        }
    }
    return Online::Sha256::toHex(hash.finish());
}
std::string mixedDigest(const ClaimPolicyConfig& config)
{
    const auto view=Mixed::view(config.pattern,0);
    if(view.x.empty())throw std::invalid_argument("unknown mixed resource claim pattern");
    Online::Sha256 hash;
    const std::string identity=std::string(Mixed::Format)+":"+
        (config.patternFrame==ClaimPolicyConfig::PatternFrame::MapAxes?"mapaxes":"nearest-d4-v1")+":";
    hash.update(identity.data(),identity.size());
    for(std::size_t i=0;i<view.x.size();++i) {
        const auto x=std::uint16_t(view.x[i]),y=std::uint16_t(view.y[i]);
        const std::array<std::uint8_t,5> bytes{std::uint8_t(x),std::uint8_t(x>>8),std::uint8_t(y),std::uint8_t(y>>8),view.tag[i]};
        hash.update(bytes.data(),bytes.size());
    }
    return Online::Sha256::toHex(hash.finish());
}
std::string assetDigest(const ClaimPolicyConfig& config)
{return config.sourceVersion==5?mixedDigest(config):patternDigest(config.pattern);}
template<class E,std::size_t N> E enumeration(const Json& value,const std::array<const char*,N>& names)
{
    if(!value.is_string())throw std::invalid_argument("resource claim policy enum must be a string");
    for(unsigned i=0;i<N;++i)if(value==names[i])return E(i);
    throw std::invalid_argument("unknown resource claim policy enum");
}
constexpr std::array SelectionNames{"first_available","closest_geometric"};
constexpr std::array FallbackNames{"wait","roam","release_job","bounded_wait_release","gradient"};
constexpr std::array RouteNames{"claimed_access","probe_then_gradient"};
constexpr std::array PendingNames{"wait","direct_heading","gradient"};
constexpr std::array TriggerNames{"always","contention"};
unsigned number(const Json& value)
{
    if(!(value.is_number_integer() || value.is_number_unsigned()) || value.is_number_float()
        || value<0 || value>UINT32_MAX)throw std::invalid_argument("resource claim policy needs a bounded unsigned integer");
    return value.get<unsigned>();
}
}
unsigned ClaimPolicyConfig::recoveryCandidateCount(std::size_t coordinates) const
{
    const unsigned factor=sourceVersion==4 && directResourceProbes?2:1;
    if(coordinates>UINT32_MAX/factor)throw std::overflow_error("claim candidate count exceeds cursor width");
    return unsigned(coordinates)*factor;
}
unsigned ClaimPolicyConfig::maximumRecoveryCandidateCount() const
{
    if(sourceVersion==5)return unsigned(Mixed::view(pattern,0).x.size());
    const auto& selected=patternByName(pattern);unsigned maximum=0;
    for(unsigned direction=0;direction<8;++direction)
        maximum=std::max(maximum,recoveryCandidateCount(selected.viewForDirection(direction).x.size()));
    return maximum;
}
void ClaimPolicyConfig::validate() const
{
    if(sourceVersion==5) {
        if(Mixed::view(pattern,0).x.empty() || directResourceProbes || selection!=Selection::FirstAvailable || unsigned(patternFrame)>1)
            throw std::invalid_argument("invalid mixed claim policy");
    } else {
        patternByName(pattern);
        if(patternFrame!=PatternFrame::MapAxes)throw std::invalid_argument("legacy claim frame must be mapaxes");
    }
    if(sourceVersion<1 || sourceVersion>LatestSourceVersion || (sourceVersion<3 && opportunisticAdjacentPickup) || (sourceVersion<4 && directResourceProbes)
        || unsigned(selection)>=SelectionNames.size() || unsigned(fallback)>=FallbackNames.size()
        || unsigned(routeTarget)>=RouteNames.size() || unsigned(pendingStep)>=PendingNames.size()
        || !interval(destinationRefreshJ) || !interval(routeRefreshJ)
        || !fallbackAfterDecisions || fallbackAfterDecisions>65535
        || !maximumExpansions || maximumExpansions>4096
        || refreshSwitchMarginPermille>MaximumRefreshSwitchMarginPermille
        || trafficWaitLimit>MaximumTrafficWaitLimit
        || (maxClaimDistanceFactorPermille && (maxClaimDistanceFactorPermille<MinimumClaimDistanceFactorPermille
            || maxClaimDistanceFactorPermille>MaximumClaimDistanceFactorPermille))
        || (claimOversubscriptionPermille!=UnlimitedClaimOversubscription
            && (claimOversubscriptionPermille<ExactClaimOversubscriptionPermille
                || claimOversubscriptionPermille>MaximumClaimOversubscriptionPermille))
        || unsigned(claimTrigger)>=TriggerNames.size()
        || contentionMinWorkers<MinimumContentionMinWorkers || contentionMinWorkers>MaximumContentionMinWorkers
        || contentionRadius>MaximumContentionRadius
        || (!contentionTriggered() && (contentionMinWorkers!=DefaultContentionMinWorkers || contentionRadius!=DefaultContentionRadius)))
        throw std::invalid_argument("invalid resource claim policy parameters");
}
std::string ClaimPolicyConfig::canonicalJson() const
{
    validate();
    Json result{{"source_version",sourceVersion},{"pattern",pattern},{"pattern_digest",assetDigest(*this)},
        {"selection",SelectionNames[unsigned(selection)]},{"fallback",FallbackNames[unsigned(fallback)]},
        {"fallback_after_decisions",fallbackAfterDecisions},{"destination_refresh_j",destinationRefreshJ},
        {"route_refresh_j",routeRefreshJ},{"route_target",RouteNames[unsigned(routeTarget)]},
        {"pending_step",PendingNames[unsigned(pendingStep)]},{"maximum_expansions",maximumExpansions},
        {"use_primary_gradient",usePrimaryGradient}};
    if(usesRecovery()) {
        result["recovery_source_version"]=ClaimRecovery::SourceVersion;
        result["recovery_cache_entries"]=ClaimRecovery::CacheEntries;
        result["recovery_ttl_ticks"]=ClaimRecovery::TtlTicks;
        result["recovery_attempt_window_ticks"]=ClaimRecovery::AttemptWindowTicks;
        result["recovery_max_attempts"]=ClaimRecovery::MaxAttempts;
        result["recovery_backoff_ticks"]=ClaimRecovery::BackoffTicks;
        result["recovery_selection"]="first_available_cursor";
    }
    if(sourceVersion>=3)result["opportunistic_adjacent_pickup"]=opportunisticAdjacentPickup;
    if(sourceVersion==4)result["direct_resource_probes"]=directResourceProbes;
    if(refreshSwitchMarginPermille)result["refresh_switch_margin_permille"]=refreshSwitchMarginPermille;
    if(terrainComponentPrefilter)result["terrain_component_prefilter"]=true;
    if(claimGradientDemand)result["claim_gradient_demand"]=true;
    if(trafficSidestep)result["traffic_sidestep"]=true;
    if(trafficWaitLimit)result["traffic_wait_limit"]=trafficWaitLimit;
    if(maxClaimDistanceFactorPermille)result["max_claim_distance_factor_permille"]=maxClaimDistanceFactorPermille;
    if(claimOversubscriptionPermille==UnlimitedClaimOversubscription)result["claim_oversubscription_permille"]="unlimited";
    else if(claimOversubscriptionPermille!=ExactClaimOversubscriptionPermille)
        result["claim_oversubscription_permille"]=claimOversubscriptionPermille;
    if(contentionTriggered())result["claim_trigger"]=TriggerNames[unsigned(claimTrigger)];
    if(contentionMinWorkers!=DefaultContentionMinWorkers)result["contention_min_workers"]=contentionMinWorkers;
    if(contentionRadius!=DefaultContentionRadius)result["contention_radius"]=contentionRadius;
    if(sourceVersion==5) {result["pattern_format"]="mixed-v1";result["pattern_frame"]=patternFrame==PatternFrame::MapAxes?"mapaxes":"nearest-d4-v1";}
    return result.dump();
}
std::string ClaimPolicyConfig::digest() const {return Online::Sha256::hex(canonicalJson());}
ClaimPolicyConfig ClaimPolicyConfig::parse(std::string_view text)
{
    if(text.size()>8192)throw std::invalid_argument("resource claim policy JSON exceeds allowance");
    const auto input=Json::parse(text);if(!input.is_object())throw std::invalid_argument("resource claim policy must be an object");
    if(!input.contains("source_version") || number(input.at("source_version"))<1 || number(input.at("source_version"))>LatestSourceVersion)
        throw std::invalid_argument("unsupported resource claim policy source version");
    ClaimPolicyConfig result;result.sourceVersion=number(input.at("source_version"));
    const Json recovery{{"recovery_source_version",ClaimRecovery::SourceVersion},{"recovery_cache_entries",ClaimRecovery::CacheEntries},
        {"recovery_ttl_ticks",ClaimRecovery::TtlTicks},{"recovery_attempt_window_ticks",ClaimRecovery::AttemptWindowTicks},
        {"recovery_max_attempts",ClaimRecovery::MaxAttempts},{"recovery_backoff_ticks",ClaimRecovery::BackoffTicks},
        {"recovery_selection","first_available_cursor"}};
    if(result.usesRecovery()) {
        for(const auto* key:{"pattern","pattern_digest","selection","fallback","fallback_after_decisions","destination_refresh_j",
            "route_refresh_j","route_target","pending_step","maximum_expansions","use_primary_gradient"})
            if(!input.contains(key))throw std::invalid_argument("incomplete recovery resource claim policy");
        for(const auto& [key,value]:recovery.items())
            if(!input.contains(key) || (value.is_string() ? !input[key].is_string() : !(input[key].is_number_integer() || input[key].is_number_unsigned()))
                || input[key]!=value || input[key].is_boolean())
                throw std::invalid_argument("resource claim recovery contract differs");
    }
    if(result.sourceVersion>=3 && !input.contains("opportunistic_adjacent_pickup"))
        throw std::invalid_argument("version 3 requires an explicit adjacent pickup policy");
    if(result.sourceVersion==4 && !input.contains("direct_resource_probes"))
        throw std::invalid_argument("version 4 requires an explicit direct resource probe policy");
    if(result.sourceVersion==5 && (!input.contains("pattern_format") || input["pattern_format"]!="mixed-v1" || !input.contains("pattern_frame")))
        throw std::invalid_argument("version5 requires explicit mixed format/frame");
    for(const auto& [key,value]:input.items()) {
        if(key=="source_version" || key=="pattern_digest" || (result.usesRecovery() && recovery.contains(key)))continue;
        if(key=="pattern") {if(!value.is_string())throw std::invalid_argument("invalid claim pattern");result.pattern=value.get<std::string>();}
        else if(key=="selection")result.selection=enumeration<Selection>(value,SelectionNames);
        else if(key=="fallback")result.fallback=enumeration<Fallback>(value,FallbackNames);
        else if(key=="route_target")result.routeTarget=enumeration<RouteTarget>(value,RouteNames);
        else if(key=="pending_step")result.pendingStep=enumeration<PendingStep>(value,PendingNames);
        else if(key=="fallback_after_decisions")result.fallbackAfterDecisions=number(value);
        else if(key=="destination_refresh_j")result.destinationRefreshJ=number(value);
        else if(key=="route_refresh_j")result.routeRefreshJ=number(value);
        else if(key=="maximum_expansions")result.maximumExpansions=number(value);
        else if(key=="use_primary_gradient") {if(!value.is_boolean())throw std::invalid_argument("invalid primary gradient flag");result.usePrimaryGradient=value.get<bool>();}
        else if(key=="opportunistic_adjacent_pickup" && result.sourceVersion>=3) {
            if(!value.is_boolean())throw std::invalid_argument("invalid adjacent pickup flag");
            result.opportunisticAdjacentPickup=value.get<bool>();
        }
        else if(key=="refresh_switch_margin_permille")result.refreshSwitchMarginPermille=number(value);
        else if(key=="terrain_component_prefilter") {
            if(!value.is_boolean())throw std::invalid_argument("invalid terrain component prefilter flag");
            result.terrainComponentPrefilter=value.get<bool>();
        }
        else if(key=="max_claim_distance_factor_permille") {
            const auto permille=number(value);
            if(permille && (permille<MinimumClaimDistanceFactorPermille || permille>MaximumClaimDistanceFactorPermille))
                throw std::invalid_argument("claim distance factor permille out of range");
            result.maxClaimDistanceFactorPermille=permille;
        }
        else if(key=="traffic_sidestep") {
            if(!value.is_boolean())throw std::invalid_argument("invalid traffic sidestep flag");
            result.trafficSidestep=value.get<bool>();
        }
        else if(key=="traffic_wait_limit") {
            const auto limit=number(value);
            if(limit>MaximumTrafficWaitLimit)throw std::invalid_argument("traffic wait limit out of range");
            result.trafficWaitLimit=limit;
        }
        else if(key=="claim_gradient_demand") {
            if(!value.is_boolean())throw std::invalid_argument("invalid claim gradient demand flag");
            result.claimGradientDemand=value.get<bool>();
        }
        else if(key=="claim_oversubscription_permille") {
            if(value.is_string()) {
                if(value!="unlimited")throw std::invalid_argument("invalid claim oversubscription");
                result.claimOversubscriptionPermille=UnlimitedClaimOversubscription;
            } else {
                // Bounded before assignment: the sentinel is never an integer.
                const auto permille=number(value);
                if(permille<ExactClaimOversubscriptionPermille || permille>MaximumClaimOversubscriptionPermille)
                    throw std::invalid_argument("claim oversubscription permille out of range");
                result.claimOversubscriptionPermille=permille;
            }
        }
        else if(key=="claim_trigger")result.claimTrigger=enumeration<ClaimTrigger>(value,TriggerNames);
        else if(key=="contention_min_workers") {
            // Bounded before assignment so the domain error names the key.
            const auto count=number(value);
            if(count<MinimumContentionMinWorkers || count>MaximumContentionMinWorkers)
                throw std::invalid_argument("contention minimum workers out of range");
            result.contentionMinWorkers=count;
        }
        else if(key=="contention_radius") {
            const auto radius=number(value);
            if(radius>MaximumContentionRadius)throw std::invalid_argument("contention radius out of range");
            result.contentionRadius=radius;
        }
        else if(key=="pattern_format" && result.sourceVersion==5) {}
        else if(key=="pattern_frame" && result.sourceVersion==5) {
            result.patternFrame=enumeration<PatternFrame>(value,std::array{"mapaxes","nearest-d4-v1"});
        }
        else if(key=="direct_resource_probes" && result.sourceVersion==4) {
            if(!value.is_boolean())throw std::invalid_argument("invalid direct resource probe flag");
            result.directResourceProbes=value.get<bool>();
        }
        else throw std::invalid_argument("unknown resource claim policy parameter: "+key);
    }
    result.validate();
    if(input.contains("pattern_digest") && input["pattern_digest"]!=assetDigest(result))
        throw std::invalid_argument("resource claim pattern asset digest differs");
    return result;
}
}
