// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimProbe.h"
#include "ResourceClaimOracle.h"
#include "ResourceClaimDeepEvent.h"
#include "ResourceClaimStratifiedDeep.h"
#include "ResourceClaimTopKDeep.h"
#include "ResourceClaimMissingTopKDeep.h"
#include "ResourceClaimMissingDeep.h"
#include "ClaimPolicyConfig.h"
#include "ResourceClaimPatternEvaluation.h"
#include "ResourceClaimPilotPatterns.h"
#include "ResourceClaimRouteExperiment.h"
#include "ClaimRoutePipeline.h"
#include "Game.h"
#include "Map.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include "Utilities.h"
#include "engine/sim/snapshot/SnapshotStorage.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <chrono>
#include <algorithm>
#include <deque>
#include <map>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace ResourceClaims
{
namespace {
Uint64 mix(Uint64 value)
{
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;
    return value^(value>>31);
}
using Json=nlohmann::json;
Json distanceJson(Uint64 cost) { return cost==Oracle::Unreachable ? Json(nullptr) : Json(cost); }
}
struct Probe::Impl
{
    static constexpr unsigned MaximumClaimantExamples=16,MaximumTrackedClaimants=Unit::MAX_COUNT*Team::MAX_COUNT;
    struct ClaimAge {Claim claim;Uint64 task=0;Uint32 since=0;bool exact=false;};
    struct Claimant {
        Owner owner=0;Claim claim;Uint32 position=Oracle::NoTile,team=0;
        Uint64 task=0;Uint32 age=0;bool identityValid=false,claimMatches=false,ageAvailable=false,ageExact=false;
    };
    std::map<Owner,ClaimAge> claimAges;
    Uint64 claimAgeTrackingExcluded=0;
    // Fixed material/phase/counter dimensions; no per-owner retention.
    enum OutcomeCounter { Eligible, Attempted, Acquired, HeldSame, RetargetAccess, RetargetDonor,
        NoCandidate, NoAttempt, Cooldown, MissingField, Linked, UnlinkedHeldExchange,
        Ready, Pending, Failed, Arrived, Wait, Roam, Release, CooldownWait, CounterCount };
    std::array<std::array<std::array<Uint64,CounterCount>,2>,MaterialCount> outcomes{};
    Uint64 outcomeRows=0;
    std::array<std::array<Uint64,6>,MaterialCount> rawProbes{};
    Json outcomeSummary() const {
        return {{"version",1},{"material_phase_counters",outcomes},{"phase_names",{"movement","pickup"}},
            {"counter_names",{"eligible","attempted","acquired","held_same","retarget_access","retarget_donor_same_access",
                "no_candidate","not_attempted","cooldown_not_attempted","missing_field_not_attempted","linked_rows","unlinked_held_exchange",
                "route_ready","route_pending","route_failed","route_arrived","fallback_wait","fallback_roam","fallback_release","cooldown_wait"}},
            {"rows",outcomeRows},{"submitted_without_outcome",metrics.submitted-outcomeRows},{"missing_contract","legacy/shadow and direct diagnostic calls may lack outcomes; missing is unavailable, never failure"},{"nearest_classification","sampled linked rows only; join original decision nearest access"},
            {"scope","active natural-resource movement calls and complete adjacent pickup scans; attempted means selector/prepare invoked, even during cooldown adjacent search; no_candidate means attempted without successful selection and may retain a held claim; cooldown_wait records suppressed fallback independently; counters overlap; no delivery causality"}};
    }
    struct Work {
        SimulationSnapshot::Handle world;
        std::vector<std::pair<Owner,Claim>> owners;
        std::vector<Claimant> claimants;
        Uint32 selectedSource=Oracle::NoTile;
        bool pooledGoal=false,desiredExclusive=false,desiredInfinite=false;
        Uint64 claimAgeTrackingExcluded=0;
        std::vector<Uint16> field;
        std::shared_ptr<const gradient_kernel::OwnedDestinations> destinations;
        bool deepSelected=false;
        Uint64 deepPriority=0;
        unsigned deepStratum=Experiment::DeepStratumCount;
        unsigned deepRetainedSlot=0;bool deepTopFour=false;unsigned deepTopK=0;
        Uint64 inputReservation=0;
        Json deepContext;
        std::vector<Uint8> deepBytes;
        bool metadataEnabled=false;
        bool claimsEnforced=false,experimental=false,goalRefresh=false;
        Json policyConfig;std::string policyDigest;
        Uint32 incomingAttackers=0,outgoingEnemies=0,observerVisibility=0;
        const char* reason="fully_claimed";
        Oracle::Query query;
        Uint32 tick=0, seed=0, nearest=0, capacity=0, claimed=0;
        Uint64 ordinal=0,preTask=0;
        bool pickup=false;
        Json features;
        std::string line;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
    };
    struct MissingWork:Work {Json missingMetadata;};
    // Payload bounds, not allocator/RSS bounds. Five captured chunk-stamp
    // planes are bounded by cell count; stock sidecars have at most one row/cell.
    static constexpr Uint64 CellInputAllowance=128,OwnerInputAllowance=128;
    static_assert(sizeof(MapState::ResourceCell)+sizeof(Uint32)+sizeof(std::array<Uint16,MaterialCount>)+
        sizeof(MapState::OccupancyCell)+sizeof(MapState::AreaCell)+2*sizeof(Uint32)+
        sizeof(Uint16)+sizeof(Uint16)+2*sizeof(Uint16)+sizeof(TerrainType)+5*sizeof(Uint64)<=CellInputAllowance);
    static_assert(sizeof(std::pair<Owner,Claim>)+sizeof(Claimant)<=OwnerInputAllowance);
    static Uint64 inputPayloadBytes(const Work& work)
    {
        const auto vectorBytes=[]<class T>(const std::vector<T>& v){return Uint64(v.capacity())*sizeof(T);};
        const auto& w=work.world;
        Uint64 bytes=sizeof(Work)+vectorBytes(work.owners)+vectorBytes(work.claimants)+vectorBytes(work.field);
        if(w.terrain) {
            bytes+=sizeof(*w.terrain)+vectorBytes(w.terrain->cellRules)+vectorBytes(w.terrain->stamps.chunks);
            if(w.terrain->vertices)bytes+=vectorBytes(*w.terrain->vertices);
        }
        if(w.resources)bytes+=sizeof(*w.resources)+vectorBytes(w.resources->cells)+vectorBytes(w.resources->stockIndices)+vectorBytes(w.resources->stocks)+vectorBytes(w.resources->stamps.chunks);
        if(w.occupancy)bytes+=sizeof(*w.occupancy)+vectorBytes(w.occupancy->cells)+vectorBytes(w.occupancy->stamps.chunks);
        if(w.areas)bytes+=sizeof(*w.areas)+vectorBytes(w.areas->cells)+vectorBytes(w.areas->stamps.chunks);
        if(w.visibility)bytes+=sizeof(*w.visibility)+vectorBytes(w.visibility->discovered)+vectorBytes(w.visibility->visible)+vectorBytes(w.visibility->stamps.chunks);
        if(w.rules) {
            bytes+=sizeof(*w.rules)+vectorBytes(w.rules->named)+vectorBytes(w.rules->experiments);
            for(const auto& row:w.rules->named)bytes+=row.first.capacity();
            for(const auto& row:w.rules->experiments)bytes+=row.capacity();
        }
        const auto selectionBytes=[&](const BuildingUnitSelection& selection) {
            Uint64 n=vectorBytes(selection.keys)+vectorBytes(selection.resolved);
            for(const auto& key:selection.keys)n+=key.capacity();
            return n;
        };
        const auto mapBytes=[](const auto& entries) {
            Uint64 n=0;for(const auto& entry:entries)n+=sizeof(entry)+entry.first.capacity();
            return n; // Tree-node links/allocator overhead explicitly excluded.
        };
        const auto semanticsBytes=[&](const BuildingSemantics& semantics) {
            Uint64 n=selectionBytes(semantics.admittedUnits)+selectionBytes(semantics.feeding.units)+selectionBytes(semantics.healing.units);
            for(const auto& units:semantics.attractionUnits)n+=selectionBytes(units);
            for(const auto& training:semantics.training)n+=selectionBytes(training.units);
            const auto& production=semantics.production;
            return n+vectorBytes(production.recipes)+vectorBytes(production.enabledUnits)+vectorBytes(production.initialRatios)+
                mapBytes(production.additionalRecipes)+mapBytes(production.additionalInitialRatios)+
                mapBytes(semantics.additionalProjectileDamage)+vectorBytes(semantics.resolvedProjectileDamage)+
                semantics.market.suppliesStockExperiment.capacity()+semantics.market.fetchesStockExperiment.capacity();
        };
        const auto typeBytes=[&](const BuildingType& type) {
            return type.type.capacity()+type.gameSprite.capacity()+type.miniSprite.capacity()+type.key.capacity()+
                type.previousKey.capacity()+type.nextKey.capacity()+type.requiredExperiment.capacity()+
                semanticsBytes(type.semantics)+type.presentation.displayName.capacity()+
                type.presentation.skinSlot.capacity()+type.presentation.connectionGroup.capacity();
        };
        if(w.catalogs) {
            bytes+=sizeof(*w.catalogs)+w.catalogs->buildingFingerprint.capacity();
            if(w.catalogs->buildings) {
                bytes+=vectorBytes(*w.catalogs->buildings);
                for(const auto& kind:*w.catalogs->buildings)
                    bytes+=kind.key.capacity()+kind.legacyType.capacity()+typeBytes(kind.resolvedType)+semanticsBytes(kind.semantics);
            }
            if(w.catalogs->typeDefinitions) {
                bytes+=vectorBytes(*w.catalogs->typeDefinitions);
                for(const auto& type:*w.catalogs->typeDefinitions)bytes+=typeBytes(type);
            }
        }
        if(w.rules && w.rules->configuration) {
            const auto& config=*w.rules->configuration;
            bytes+=sizeof(config)+config.getBuildingCatalogSnapshot().capacity()+vectorBytes(config.buildingExperimentKeys())+vectorBytes(config.resourceExperiments());
            for(const auto& key:config.buildingExperimentKeys())bytes+=key.capacity();
            for(const auto& experiment:config.resourceExperiments())bytes+=experiment.key.capacity()+experiment.label.capacity()+experiment.help.capacity();
            for(int team=0;team<Team::MAX_COUNT;++team)bytes+=config.getAIConfig(team).capacity()+config.getBasePlayer(team).name.capacity();
        }
        if(work.destinations)bytes+=Uint64(work.destinations->cells)*2*sizeof(Uint16);
        // Bound serialized metadata separately; JSON nodes/allocator bookkeeping,
        // shared registries/catalog assets and snapshot-pool retention are outside payload accounting.
        const auto metadata=work.features.dump().size()+work.deepContext.dump().size()+work.policyConfig.dump().size()+work.policyDigest.size();
        if(metadata>64*1024)throw std::runtime_error("stratified input metadata exceeds allowance");
        return bytes+metadata;
    }
    struct Baseline {
        SimulationSnapshot::Handle world;
        RouteProfile profile;
        Uint32 origin=0,target=0;Uint64 due=0;
        RouteStatus status=RouteStatus::Unreachable;
        Uint64 cost=0,expansions=0,elapsedNs=0;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
        bool published=false;
    };
    static Json blockerCensus(const Work& work,const Oracle::Result& oracle,int width,int height) {
        if(!work.claimsEnforced)return Json{{"available",false},{"reason","shadow-or-disabled-ledger; authoritative blocker census unavailable"},{"direct",nullptr},{"pooled",nullptr},{"nearest_access_reservable",nullptr},{"nearest_access_static_reachable",nullptr}};
        std::vector<Uint32> components(std::size_t(width)*height,Oracle::NoTile);
        std::vector<bool> infinite(components.size(),false);
        const Oracle::Candidate* nearestCandidate=nullptr;
        for(const auto& c:oracle.candidates){components[c.source]=c.component;infinite[c.source]=c.infinite;if(c.source==work.nearest)nearestCandidate=&c;}
        const auto component=components[work.nearest];
        const bool nearestEligible=nearestCandidate && nearestCandidate->accessEligible;
        const bool nearestReservable=nearestEligible && (work.pooledGoal
            ? component!=Oracle::NoTile && std::any_of(oracle.candidates.begin(),oracle.candidates.end(),[&](const auto& c){return c.component==component && c.available;})
            : nearestCandidate->available);
        struct Histograms {
            Uint32 owners=0,ageUnavailable=0;
            std::array<Uint32,11> sourceDistance{},accessDistance{}; // Last bin is unknown.
            std::array<Uint32,9> exactAge{},lowerBoundAge{};
        };
        const auto bin=[](Uint32 value,const auto& upperBounds) {
            std::size_t i=0;while(i+1<upperBounds.size() && value>upperBounds[i])++i;return i;
        };
        constexpr std::array<Uint32,10> distanceBounds{0,1,3,7,15,31,63,127,255,UINT32_MAX};
        constexpr std::array<Uint32,9> ageBounds{0,15,63,255,1023,4095,16383,65535,UINT32_MAX};
        const auto group=[&](bool pooled) {
            Json examples=Json::array();Uint32 allOwners=0,requesterOwners=0,total=0,conflicts=0,sameTeam=0,otherTeam=0,unknownIdentity=0,exactAge=0,lowerAge=0,unknownAge=0;
            std::array<Uint32,Team::MAX_COUNT> byTeam{},conflictingByTeam{};
            std::array<Histograms,3> histograms{};
            const auto dx=[&](Uint32 a,Uint32 b){const auto d=std::abs(int(a%width)-int(b%width));return std::min(d,width-d);};
            const auto dy=[&](Uint32 a,Uint32 b){const auto d=std::abs(int(a/width)-int(b/width));return std::min(d,height-d);};
            for(const auto& c:work.claimants) {
                if(pooled ? components[c.claim.source]!=component : c.claim.source!=work.selectedSource)continue;
                ++allOwners;if(c.owner==work.query.owner){++requesterOwners;continue;}
                ++total;
                const bool sourceInfinite=pooled?infinite[c.claim.source]:work.desiredInfinite;
                const bool conflict=(!sourceInfinite && c.claim.material==work.query.material) || c.claim.exclusive || work.desiredExclusive;
                conflicts+=conflict;
                if(c.identityValid){++byTeam[c.team];conflictingByTeam[c.team]+=conflict;}
                if(!c.identityValid)++unknownIdentity;
                else if((Uint32(1)<<c.team)==work.query.route.teamMask)++sameTeam;else ++otherTeam;
                if(!c.ageAvailable)++unknownAge;else if(c.ageExact)++exactAge;else ++lowerAge;
                const auto distance=[&](Uint32 tile)->Uint32 {return c.position==Oracle::NoTile?Oracle::NoTile:Uint32(std::max(dx(c.position,tile),dy(c.position,tile)));};
                // Full frozen census, before deterministic example truncation.
                // Other-team does not imply enemy: diplomacy is not inferred.
                if(conflict) {
                    const unsigned teamGroup=!c.identityValid?2:(Uint32(1)<<c.team)==work.query.route.teamMask?0:1;
                    auto& h=histograms[teamGroup];++h.owners;
                    const auto sourceDistance=distance(c.claim.source),accessDistance=distance(c.claim.access);
                    ++h.sourceDistance[sourceDistance==Oracle::NoTile?10:bin(sourceDistance,distanceBounds)];
                    ++h.accessDistance[accessDistance==Oracle::NoTile?10:bin(accessDistance,distanceBounds)];
                    if(!c.ageAvailable)++h.ageUnavailable;
                    else if(c.ageExact)++h.exactAge[bin(c.age,ageBounds)];
                    else ++h.lowerBoundAge[bin(c.age,ageBounds)];
                }
                if(examples.size()>=MaximumClaimantExamples)continue;
                const auto distanceValue=[&](Uint32 tile)->Json {const auto d=distance(tile);return d==Oracle::NoTile?Json(nullptr):Json(d);};
                examples.push_back({{"owner",c.owner},{"team",c.identityValid?Json(c.team):Json(nullptr)},
                    {"identity_valid",c.identityValid},{"claim_matches_unit",c.claimMatches},{"material",c.claim.material},{"source",c.claim.source},{"access",c.claim.access},
                    {"exclusive",c.claim.exclusive},{"requested_yield_infinite",sourceInfinite},{"reserve",c.claim.reserve},{"conflicts_requested_material_or_exclusive",conflict},
                    {"current_position",c.position==Oracle::NoTile?Json(nullptr):Json(c.position)},
                    {"source_chebyshev_tiles",distanceValue(c.claim.source)},{"access_chebyshev_tiles",distanceValue(c.claim.access)},
                    {"claim_age_ticks",c.ageAvailable?Json(c.age):Json(nullptr)},
                    {"claim_age_kind",!c.ageAvailable?"unavailable":c.ageExact?"observed-acquisition-exact":"first-observed-lower-bound"},
                    {"task",c.claimMatches?Json(c.task):Json(nullptr)},{"path_distance",nullptr},{"route_state",nullptr}});
            }
            Json distributions=Json::array();
            constexpr std::array<const char*,3> teamGroups{"same_team","other_team","unresolved"};
            for(unsigned i=0;i<histograms.size();++i) {
                const auto& h=histograms[i];
                distributions.push_back({{"team_group",teamGroups[i]},{"potential_conflicting_owners",h.owners},
                    {"source_distance_tiles",h.sourceDistance},{"access_distance_tiles",h.accessDistance},
                    {"exact_age_ticks",h.exactAge},{"lower_bound_age_ticks",h.lowerBoundAge},{"age_unavailable",h.ageUnavailable}});
            }
            return Json{{"histograms",std::move(distributions)},{"all_selected_goal_owners",allOwners},{"requester_owners",requesterOwners},{"owners",total},{"conflicting_owners",conflicts},{"same_team_owners",sameTeam},{"other_team_owners",otherTeam},
                {"known_team_owners",byTeam},{"conflicting_team_owners",conflictingByTeam},{"identity_unavailable",unknownIdentity},{"exact_age_owners",exactAge},{"lower_bound_age_owners",lowerAge},{"age_unavailable_owners",unknownAge},
                {"examples",std::move(examples)},{"examples_excluded",total>MaximumClaimantExamples?total-MaximumClaimantExamples:0},
                {"examples_censored",total>MaximumClaimantExamples}};
        };
        return Json{{"available",true},{"version",1},
            {"histogram_schema",{{"version",1},{"scope","all potential conflicting owners excluding requester; independent of example truncation"},
                {"distance_bins",{"0","1","2-3","4-7","8-15","16-31","32-63","64-127","128-255",">=256","unknown"}},
                {"age_bins_ticks",{"0","1-15","16-63","64-255","256-1023","1024-4095","4096-16383","16384-65535",">=65536"}},
                {"age_unknown_bin","age_unavailable; separate from exact and lower-bound histograms"},
                {"sum_contract","each distance histogram sums to potential_conflicting_owners; exact_age+lower_bound_age+age_unavailable sums to that same count; three team groups sum to conflicting_owners"},
                {"interpretation","lower-bound bins describe observed minima, not actual-age distribution; geometric distances are not path distances"}}},
            {"direct_source",work.selectedSource},{"nearest_access",work.nearest},
            {"nearest_access_reservable",nearestReservable},
            {"nearest_access_static_reachable",nearestEligible && nearestCandidate->accessDistance!=Oracle::Unreachable},
            {"nearest_access_reservable_contract","owner-aware oracle access eligibility and available physical donor (any donor in nearest pool); independent of path reachability; raw donor failure need not require an alternative access"},
            {"pooled_goal",work.pooledGoal},{"pool_membership_available",work.pooledGoal && component!=Oracle::NoTile},
            {"pool_component",component==Oracle::NoTile?Json(nullptr):Json(component)},
            {"direct",group(false)},{"pooled",work.pooledGoal && component!=Oracle::NoTile?group(true):Json(nullptr)},
            {"age_tracking_excluded_total",work.claimAgeTrackingExcluded},{"captured_ledger_owners",work.claimants.size()},{"example_limit_per_group",MaximumClaimantExamples},
            {"example_selection","first owners in deterministic ledger order; censored examples are not a random sample"},
            {"group_relationship","direct-source and nearest-pool groups may overlap; do not add their counts"},
            {"distance_contract","current owner-to-own-source/access toroidal Chebyshev tiles; geometric only, not path distance"},
            {"count_contract","exact frozen ledger owner counts; owners/conflicting_owners exclude requester, all_selected_goal_owners includes it; potential conflicting reservations do not prove cause of shortage; pool groups physical sources in nearest query-material farm component"},
            {"age_contract","ledger acquisition/release observations, independent of journey/harvesting lifetime; inherited ages are lower bounds; no persisted ages"}};
    }
    static void runBaseline(void* context,std::size_t) noexcept {
        auto& job=*static_cast<Baseline*>(context);
        try {
            const auto begin=std::chrono::steady_clock::now();const auto view=job.world.view();
            const field::Grid grid(view.width,view.height);const int tx=job.target%view.width,ty=job.target/view.width;
            thread_local RouteSearch search;
            const auto route=search.find(grid,job.origin,UINT32_MAX,
                [&](Uint32 cell){return routePassable(view,cell,job.profile);},
                [&](Uint32 cell,bool diagonal){return routeEntryCost(view,cell,job.profile,diagonal);},
                [&](Uint32 cell){const int dx=std::abs(int(cell%view.width)-tx),dy=std::abs(int(cell/view.width)-ty);
                    return std::min(dx,view.width-dx)<=1 && std::min(dy,view.height-dy)<=1;},
                [](Uint32){return Uint64(0);});
            job.status=route.status;job.cost=route.cost;job.expansions=route.expansions;
            job.elapsedNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
        } catch(...) {job.error=std::current_exception();}
        job.world={};
    }
    struct Journey {
        Uint64 task=0;Uint32 acquired=0,origin=0,source=0,access=0,material=0;
        Uint64 steps=0,cost=0;
        bool harvesting=false;
        std::unique_ptr<Baseline> baseline;
        std::string policyDigest;
        std::array<Uint64,unsigned(LiveEvent::Count)> events{};
        std::optional<ReleaseReason> releaseReason;
    };
    std::optional<ReleaseReason> pendingReleaseReason;
    LiveMetrics live;
    MovementMetrics movement;
    std::optional<Uint32> movementEndTick;
    std::optional<Uint32> lastTransportEventTick;
    std::map<Owner,Journey> journeys;
    struct ClosedJourney {Owner owner;Journey journey;const char* terminal;Uint32 tick;};
    std::vector<ClosedJourney> closedJourneys;
    Uint32 latestLiveTick=0;
    std::array<Uint64,16> journeyEligiblePhase{},journeySampledPhase{},journeyAdmittedPhase{},journeyExcludedPhase{};
    Uint64 baselineUnpublishedChecks=0,baselineUnpublishedAtFinish=0,baselineBlockingFinishJoins=0,baselineLatePublications=0,baselineMaxPublishDelay=0;
    Uint64 censoredJourneyOutcomes=0;

    static constexpr Uint32 JourneyTimeout=65536;
    static constexpr unsigned MaximumJourneys=8,MaximumJourneyAdmissions=512;
    void writeJourney(Owner owner,const Journey& j,const char* terminal,Uint32 tick) {
        const auto* baseline=j.baseline.get();
        const bool ready=baseline && baseline->published;
        const char* status=!ready?"not_ready":baseline->status==RouteStatus::Found?"reached":baseline->status==RouteStatus::Unreachable?"unreachable":"limit";
        write(Json{{"kind","journey"},{"schema_version",1},{"owner",owner},{"task",j.task},
            {"acquired_tick",j.acquired},{"end_tick",tick},{"origin",j.origin},{"source",j.source},{"access",j.access},{"material",j.material},
            {"terminal",terminal},{"steps",j.steps},{"live_cost",j.cost},{"events",j.events},
            {"baseline_cost",ready && baseline->status==RouteStatus::Found?Json(baseline->cost):Json(nullptr)},
            {"baseline_status",status},{"baseline_expansions",ready?Json(baseline->expansions):Json(nullptr)},
            {"baseline_elapsed_ns",ready?Json(baseline->elapsedNs):Json(nullptr)},
            {"baseline_contract","acquisition static occupancy; exact held-access 3x3 stance; not a dynamic-world optimum"},
            {"policy_digest",j.policyDigest},
            {"release_reason",j.releaseReason?Json(ReleaseReasonNames[unsigned(*j.releaseReason)]):Json(nullptr)}}.dump()+"\n");
        ++live.journeyCompleted;
        if(std::string_view(terminal)=="timeout" || std::string_view(terminal)=="end_of_segment")++censoredJourneyOutcomes;
    }
    void closeJourney(std::map<Owner,Journey>::iterator at,const char* terminal,Uint32 tick) {
        if(at->second.baseline && !at->second.baseline->published)
            closedJourneys.push_back({at->first,std::move(at->second),terminal,tick});
        else writeJourney(at->first,at->second,terminal,tick);
        journeys.erase(at);
    }
    void publishBaselines(Uint32 tick,bool finish=false) {
        const auto publish=[&](Journey& journey) {
            auto* baseline=journey.baseline.get();
            if(!baseline || baseline->published || (!finish && baseline->due>tick))return;
            // Diagnostic work has no simulation deadline: only retire completed
            // batches here. Explicit finish is the sole blocking observation join.
            const bool finished=executor.finished(baseline->batch);
            if(!finish && !finished){++baselineUnpublishedChecks;return;}
            if(finish){++baselineUnpublishedAtFinish;baselineBlockingFinishJoins+=!finished;}
            else if(tick>baseline->due){++baselineLatePublications;baselineMaxPublishDelay=std::max(baselineMaxPublishDelay,Uint64(tick)-baseline->due);}
            executor.join(baseline->batch,{finish?JoinReason::Save:JoinReason::TickDeadline,tick});
            if(baseline->error)std::rethrow_exception(baseline->error);
            baseline->published=true;
        };
        for(auto& [owner,journey]:journeys)publish(journey);
        for(auto at=closedJourneys.begin();at!=closedJourneys.end();) {
            publish(at->journey);
            if(!at->journey.baseline || at->journey.baseline->published) {
                writeJourney(at->owner,at->journey,at->terminal,at->tick);at=closedJourneys.erase(at);
            } else ++at;
        }
    }
    void expireJourneys(Uint32 tick) {
        latestLiveTick=std::max(latestLiveTick,tick);
        for(auto at=journeys.begin();at!=journeys.end();) {
            if(Uint64(tick)>=Uint64(at->second.acquired)+JourneyTimeout) {auto expired=at++;closeJourney(expired,"timeout",tick);}
            else ++at;
        }
    }
    Json liveSummary() const {
        return {{"version",1},{"event_counts",live.events},{"actual_moves",live.moves},{"actual_weighted_cost",live.liveCost},
            {"scope","held-material-claim-pursuit-only"},{"unclaimed_movement_available",false},
            {"movement_availability","actual_moves and actual_weighted_cost count committed moves only while a claim is held; disabled or shadow claims do not observe ordinary pursuit movement; zero is not zero total travel"},
            {"inherited_claim_coverage","held-claim moves counted from segment start; acquisition baseline and full journey unavailable until a new sampled acquisition"},
            {"journey_eligible",live.journeyEligible},{"journey_sampled",live.journeySampled},{"journey_admitted",live.journeyAdmitted},
            {"journey_completed",live.journeyCompleted},{"excluded_capacity",live.excludedCapacity},{"excluded_limit",live.excludedLimit},
            {"untracked_claim_moves",live.untrackedMoves},{"sample_every",config.sampleEvery},
            {"route_completed",live.routeCompleted},{"route_canceled",live.routeCanceled},{"route_expansions",live.routeExpansions},
            {"route_worker_elapsed_ns",live.routeElapsedNs},{"route_unobserved_work",live.routeUnobserved},{"route_restored_outputs",live.routeRestored},
            {"route_status_counts",live.routeStatuses},
            {"release_reason_version",1},{"release_reason_names",ReleaseReasonNames},{"release_reason_counts",live.releaseReasons},
            {"selection_filter_counts",{{"terrain_prefilter_rejections",live.terrainPrefilterRejections},
                {"refresh_margin_rejections",live.refreshMarginRejections},{"refresh_margin_kept",live.refreshMarginKept}}},{"route_status_names",Json::array({"found","unreachable","cost_limit","expansion_limit"})},
            {"max_active",MaximumJourneys},{"max_admissions",std::min<unsigned>(MaximumJourneyAdmissions,config.maxSamples)},
            {"timeout_ticks",JourneyTimeout},{"continuation_policy","new-segment; inherited claims are not sampled acquisitions"},
            {"sampling_censored",bool(live.excludedCapacity || live.excludedLimit)},
            {"admission_uncensored",!live.excludedCapacity && !live.excludedLimit},
            {"censored_journey_outcomes",censoredJourneyOutcomes},
            {"sampling_warning","admission under capacity pressure is workload-dependent; cluster by game, never treat journeys as independent games"},
            {"phase_bin_ticks",4096},{"phase_last_bin_starts",61440},
            {"eligible_by_phase",journeyEligiblePhase},{"sampled_by_phase",journeySampledPhase},
            {"admitted_by_phase",journeyAdmittedPhase},{"excluded_by_phase",journeyExcludedPhase},
            {"baseline_unpublished_checks",baselineUnpublishedChecks},{"baseline_unpublished_at_finish",baselineUnpublishedAtFinish},
            {"baseline_blocking_finish_joins",baselineBlockingFinishJoins},{"baseline_late_publications",baselineLatePublications},
            {"baseline_max_publish_delay_ticks",baselineMaxPublishDelay},
            {"baseline_contract","sampled off-owner Dijkstra at acquisition; static held-access stance"}};
    }
    ComputeExecutor& executor;
    Config config;
    Metrics metrics;
    std::array<Uint64,MaterialCount> materialDecisions{}, materialSampled{}, materialContested{};
    Uint64 sampledInfinite=0, sampledPooled=0, sampledExhaustedPools=0;
    Uint64 staleScalarIneligible=0, staleMixedFieldIneligible=0, staleInvalidXY=0;
    std::string path;
    gzFile file=nullptr;
    SimulationSnapshot::Storage storage;
    std::deque<std::unique_ptr<Work>> pending;
    std::deque<std::unique_ptr<MissingWork>> missingPending;
    SimulationSnapshot::Storage missingStorage;
    std::array<Experiment::MissingSlot,Experiment::MissingStrata> missingSlots;
    std::array<Experiment::TopKDeepSlot,Experiment::MissingStrata> missingTopSlots;
    std::filesystem::path missingDirectory;
    Uint64 missingCalls=0,missingSampled=0,missingMixed=0,missingValid=0,missingAdmissions=0,missingCompleted=0,missingObsolete=0;
    Uint64 missingInputReservations=0,missingPeakInput=0,missingPeakPayload=0,missingOracleNs=0;
    Uint64 missingPressureCount=0,missingTickEligible=0,missingTickSelected=0,missingTickAdmitted=0;
    Uint32 missingAdmissionTick=UINT32_MAX;
    Json missingPressureExamples=Json::array();
    unsigned missingPendingCapacity() const {return config.deepMissingTopK?Experiment::MissingTopKPendingLimit:Experiment::MissingPendingLimit;}
    void missingPressure(Uint64 ordinal,Uint32 tick,unsigned stratum) {
        if(!config.deepMissingTopK || missingPending.size()<Experiment::MissingPendingLimit)return;
        ++missingPressureCount;if(missingPressureExamples.size()>=16)return;
        Json pending=Json::array();for(const auto& w:missingPending)pending.push_back({{"ordinal",w->ordinal},{"tick",w->tick},{"stratum",w->deepStratum},{"slot",w->deepRetainedSlot},{"input_bytes",w->inputReservation}});
        missingPressureExamples.push_back({{"ordinal",ordinal},{"tick",tick},{"stratum",stratum},{"pending_count",missingPending.size()},{"input_bytes",missingInputReservations},{"eligible_this_tick",missingTickEligible},{"selected_this_tick",missingTickSelected},{"admitted_this_tick",missingTickAdmitted},{"pending",std::move(pending)}});
    }
    void missingTopManifest(bool complete) {
        Json slots=Json::array();bool censored=false;
        for(unsigned i=0;i<missingTopSlots.size();++i) {
            const auto& s=missingTopSlots[i];censored|=s.censored();Json records=Json::array();
            for(unsigned j=0;j<s.entries.size();++j) {const auto& e=s.entries[j];if(e.ordinal)records.push_back({{"slot",j},{"priority",e.priority},{"ordinal",e.ordinal},{"captured_ordinal",e.writtenOrdinal},{"replacements",e.replacements},{"file",Experiment::missingTopKFilename(i,j,config.deepMissingTopK)},{"file_sha256",e.digest}});}
            slots.push_back({{"id",i},{"eligible",s.eligible},{"last_ordinal",s.lastOrdinal},{"retained",s.retained()},{"reasons",s.reasons},{"ties",s.currentPriorityTies},{"excluded_sample_cap",s.excludedSampleCap},{"excluded_queue_cap",s.excludedQueueCap},{"excluded_input_bytes",s.excludedInputBytes},{"censored",s.censored()},{"weight_numerator",s.eligible?Json(s.eligible):Json(nullptr)},{"weight_denominator",s.eligible?Json(s.retained()):Json(nullptr)},{"records",std::move(records)}});
        }
        const auto value=Json{{"version",2},{"design",Experiment::MissingTopKDesign},{"top_k",config.deepMissingTopK},{"complete",complete},{"censored",censored},{"context",deepContext},
            {"sample_every",config.missingSampleEvery},{"calls",missingCalls},{"sampled",missingSampled},{"excluded_mixed",missingMixed},{"valid_destination",missingValid},
            {"admitted",missingAdmissions},{"completed",missingCompleted},{"obsolete_completed",missingObsolete},{"oracle_elapsed_ns",missingOracleNs},{"strata",std::move(slots)},
            {"input_pending_limit",Experiment::MissingInputLimit},{"peak_input_reservations",missingPeakInput},{"peak_input_payload",missingPeakPayload},{"pending_limit",Experiment::MissingTopKPendingLimit},{"encoded_pending_limit",Experiment::MissingTopKPendingLimit*Experiment::DeepRecordLimit},
            {"record_limit",Experiment::DeepRecordLimit},{"final_limit",Experiment::missingTopKFinalLimit(config.deepMissingTopK)},{"transient_limit",Experiment::MissingTransientLimit},{"free_reserve",Experiment::DeepFreeReserve},{"max_admissions",Experiment::MissingAdmissionLimit},
            {"capture_timing","pre-legacy-reconciliation; immutable observed ledger, no extra reconciliation"},{"population","independent hash over existing decision hooks plus exclusive active missing-field acquisition hook; not all worker ticks"},
            {"input_accounting","same vector-capacity/metadata accounting as legacy; excludes allocator/node/shared-registry/pool/oracle RSS"},{"admission_pressure_count",missingPressureCount},{"admission_pressure_examples",missingPressureExamples},{"admission_pressure_examples_censored",missingPressureCount>missingPressureExamples.size()},
            {"weight_contract","Ns/min(k,Ns) only for complete uncensored exact selected sets; hash priority then ordinal"}}.dump();
        Experiment::replaceMissingTopKFile(missingDirectory/"manifest.json",std::span(reinterpret_cast<const Uint8*>(value.data()),value.size()),config.deepMissingTopK);
    }
    void missingManifest(bool complete) {
        if(!config.deepMissing)return;
        if(config.deepMissingTopK){missingTopManifest(complete);return;}
        Json slots=Json::array();bool censored=false;
        for(unsigned i=0;i<missingSlots.size();++i) {
            const auto& s=missingSlots[i];const auto& r=s.reservoir;censored|=s.censored();
            slots.push_back({{"id",i},{"eligible",r.eligible},{"priority",r.priority},{"ordinal",r.ordinal},{"captured_ordinal",r.writtenOrdinal},
                {"reasons",s.reasons},{"ties",s.ties},{"excluded_sample_cap",r.excludedSampleCap},{"excluded_queue_cap",r.excludedQueueCap},
                {"excluded_input_bytes",s.inputExcluded},{"replacements",r.replacements},{"censored",s.censored()},
                {"file",r.eligible?Json(Experiment::missingFilename(i)):Json(nullptr)},{"file_sha256",r.eligible?Json(s.digest):Json(nullptr)}});
        }
        const auto value=Json{{"version",1},{"design",Experiment::MissingDesign},{"complete",complete},{"censored",censored},{"context",deepContext},
            {"sample_every",config.missingSampleEvery},{"calls",missingCalls},{"sampled",missingSampled},{"excluded_mixed",missingMixed},{"valid_destination",missingValid},
            {"admitted",missingAdmissions},{"completed",missingCompleted},{"obsolete_completed",missingObsolete},{"oracle_elapsed_ns",missingOracleNs},{"strata",std::move(slots)},
            {"input_pending_limit",Experiment::MissingInputLimit},{"peak_input_reservations",missingPeakInput},{"peak_input_payload",missingPeakPayload},
            {"pending_limit",Experiment::MissingPendingLimit},{"encoded_pending_limit",Experiment::MissingPendingLimit*Experiment::DeepRecordLimit},
            {"record_limit",Experiment::DeepRecordLimit},{"final_limit",Experiment::MissingFinalLimit},{"transient_limit",Experiment::MissingTransientLimit},
            {"free_reserve",Experiment::DeepFreeReserve},{"max_admissions",Experiment::MissingAdmissionLimit},
            {"capture_timing","pre-legacy-reconciliation; immutable observed ledger, no extra reconciliation"},
            {"population","independent hash over existing decision hooks plus exclusive active missing-field acquisition hook; not all worker ticks"},
            {"input_accounting","same vector-capacity/metadata accounting as legacy; excludes allocator/node/shared-registry/pool/oracle RSS"}}.dump();
        Experiment::replaceMissingFile(missingDirectory/"manifest.json",std::span(reinterpret_cast<const Uint8*>(value.data()),value.size()));
    }
    static void runMissing(void* context,std::size_t) noexcept {
        auto& w=*static_cast<MissingWork*>(context);
        try {
            const auto start=std::chrono::steady_clock::now();const auto result=Oracle::evaluate(w.world,w.query,w.owners);
            w.missingMetadata["oracle_elapsed_ns"]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            w.missingMetadata["oracle_expanded"]=result.expanded;
            w.missingMetadata["optimum"]=result.best?Json(result.candidates[*result.best].distance):Json(nullptr);
            const auto view=w.world.view();std::vector<Uint32> plane;
            if(w.destinations) {
                plane.resize(view.width*view.height);
                for(Uint32 i=0;i<plane.size();++i) {auto x=w.destinations->x[i],y=w.destinations->y[i];plane[i]=x<view.width && y<view.height?Uint32(y*view.width+x):Oracle::NoTile;}
            }
            w.deepBytes=Experiment::encodeMissingEvent(w.missingMetadata,Uint32(view.width*view.height),plane,result.candidates);
        }catch(...){w.error=std::current_exception();}
        w.world={};w.owners.clear();w.destinations.reset();
    }
    void publishMissing(Uint64 tick,bool all=false) {
        while(!missingPending.empty() && (all || Uint64(missingPending.front()->tick)+1<=tick)) {
            auto& w=*missingPending.front();executor.join(w.batch,{all?JoinReason::Save:JoinReason::TickDeadline,Uint32(tick)});
            if(w.error)std::rethrow_exception(w.error);
            if(config.deepMissingTopK) {
                auto& slot=missingTopSlots.at(w.deepStratum);
                if(slot.current(w.deepRetainedSlot,w.ordinal)) {
                    Experiment::replaceMissingTopKFile(missingDirectory/Experiment::missingTopKFilename(w.deepStratum,w.deepRetainedSlot,config.deepMissingTopK),w.deepBytes,config.deepMissingTopK);
                    slot.written(w.deepRetainedSlot,w.ordinal,Online::Sha256::toHex(Online::Sha256::of(w.deepBytes)));
                } else ++missingObsolete;
            } else {
            auto& slot=missingSlots.at(w.deepStratum);
            if(slot.reservoir.ordinal==w.ordinal) {
                Experiment::replaceMissingFile(missingDirectory/Experiment::missingFilename(w.deepStratum),w.deepBytes);
                slot.digest=Online::Sha256::toHex(Online::Sha256::of(w.deepBytes));slot.reservoir.written(w.ordinal);
            } else ++missingObsolete;
            }
            missingOracleNs+=w.missingMetadata.at("oracle_elapsed_ns").get<Uint64>();
            missingInputReservations-=w.inputReservation;++missingCompleted;missingPending.pop_front();
        }
    }
    bool finished=false;
    Json deepContext;
    Experiment::DeepReservoir deep;
    std::array<Experiment::StratifiedDeepSlot,Experiment::DeepStratumCount> deepSlots;
    std::array<Experiment::TopKDeepSlot,Experiment::DeepStratumCount> topSlots;
    static constexpr Uint64 InputPendingLimit=128ull*1024*1024;
    Uint64 pendingInputReservations=0,peakInputReservations=0,inputAdmissionExclusions=0,inputIneligibleExclusions=0,peakInputPayload=0;
    Uint64 deepIneligibleNoXY=0;
    Uint64 captureOnlySkipped=0;
    std::string deepDigest;
    std::optional<ClaimPolicyConfig> cachedPolicy;
    Json cachedPolicyJson;
    std::string cachedPolicyDigest;
    std::filesystem::path deepDirectory;

    static std::string deepFilename(unsigned slot) {return "event-"+std::string(slot<10?"0":"")+std::to_string(slot)+".bin";}

    unsigned topKCapacity() const {return config.deepTopK?config.deepTopK:Experiment::DeepTopK;}
    const char* topKDesign() const {return config.deepTopK>16?Experiment::Retention64DeepDesign:(config.deepTopK?Experiment::ScalableTopKDeepDesign:Experiment::TopKDeepDesign);}
    Uint64 topKManifestLimit() const {return Experiment::topKManifestLimit(topKCapacity(),config.deepTopK!=0);}

    void deepManifest(bool complete)
    {
        if(config.deepContext.empty())return;
        if(config.deepTopFour) {
            Json strata=Json::array();bool censored=false;
            for(unsigned i=0;i<topSlots.size();++i) {
                const auto& r=topSlots[i];Json records=Json::array();const bool missing=r.censored();censored|=missing;
                for(unsigned k=0;k<r.entries.size();++k) {
                    const auto& e=r.entries[k];if(!e.ordinal)continue;
                    records.push_back(Json{{"slot",k},{"file",Experiment::topKFilename(i,k,topKCapacity())},
                        {"priority",e.priority},{"ordinal",e.ordinal},{"captured_ordinal",e.writtenOrdinal},
                        {"replacements",e.replacements},{"file_sha256",e.digest}});
                }
                strata.push_back(Json{{"id",i},{"name",Experiment::DeepStratumNames[i]},
                    {"eligible",r.eligible},{"last_ordinal",r.lastOrdinal},{"retained",r.retained()},
                    {"records",std::move(records)},{"censored",missing},
                    {"excluded_eligible_admission",r.excludedSampleCap+r.excludedQueueCap},
                    {"excluded_eligible_sample_cap",r.excludedSampleCap},{"excluded_eligible_queue_cap",r.excludedQueueCap},
                    {"excluded_input_bytes",r.excludedInputBytes},{"reasons",r.reasons},{"unknown_pool",r.unknownPool},
                    {"current_priority_ties",r.currentPriorityTies},
                    {"weight_numerator",!missing && r.eligible?Json(r.eligible):Json(nullptr)},
                    {"weight_denominator",!missing && r.eligible?Json(r.retained()):Json(nullptr)}});
            }
            auto document=Json{{"version",config.deepTopK>16?5:(config.deepTopK?4:3)},{"top_k",topKCapacity()},{"complete",complete},{"censored",censored},
                {"design",topKDesign()},{"context",deepContext},{"strata",std::move(strata)},
                {"weight_contract",config.deepTopK?"Ns/min(k,Ns) only for complete uncensored exact selected sets; nominal independent uniform hash priorities, ordinal tie-break; observed current-priority ties exposed":"Ns/min(4,Ns) only for complete uncensored exact selected sets; nominal independent uniform hash priorities, ordinal tie-break; observed current-priority ties exposed"},
                {"ineligible_contested_no_xy",deepIneligibleNoXY},{"ordinary_missing_field",metrics.excludedMissingField},
                {"encoded_pending_limit",Experiment::StratifiedEncodedPendingLimit},{"input_pending_limit",InputPendingLimit},
                {"peak_input_reservations",peakInputReservations},{"input_admission_exclusions",inputAdmissionExclusions},
                {"input_ineligible_exclusions",inputIneligibleExclusions},{"peak_input_payload",peakInputPayload},
                {"input_accounting","vector-capacity-payload-plus-serialized-metadata; excludes allocator/json-node/map/experiment-set-node/shared-registry/pool/oracle-scratch RSS"},
                {"final_limit",Experiment::topKFinalLimit(topKCapacity())},{"transient_limit",Experiment::StratifiedTransientLimit}};
            if(config.captureOnly)document["work_scope"]="retained-replacements-only-v1";
            const auto value=document.dump();
            if(value.size()>topKManifestLimit())throw std::runtime_error("top-k manifest exceeds allowance");
            Experiment::replaceTopKDeepFile(deepDirectory/"manifest.json",std::span(reinterpret_cast<const Uint8*>(value.data()),value.size()),topKCapacity(),config.deepTopK!=0);return;
        }
        if(config.deepStratified) {
            Json slots=Json::array();bool censored=false;
            for(unsigned i=0;i<deepSlots.size();++i) {
                const auto& slot=deepSlots[i];const auto& r=slot.reservoir;
                censored|=slot.censored();
                slots.push_back(Json{{"id",i},{"name",Experiment::DeepStratumNames[i]},
                    {"file",deepFilename(i)},{"censored",slot.censored()},{"eligible",r.eligible},
                    {"excluded_eligible_admission",r.excludedSampleCap+r.excludedQueueCap},
                    {"excluded_eligible_sample_cap",r.excludedSampleCap},{"excluded_eligible_queue_cap",r.excludedQueueCap},
                    {"excluded_input_bytes",slot.excludedInputBytes},{"reasons",slot.reasons},{"unknown_pool",slot.unknownPool},
                    {"minimum_priority_ties",slot.minimumPriorityTies},{"priority",r.priority},{"ordinal",r.ordinal},
                    {"captured_ordinal",r.writtenOrdinal},{"replacements",r.replacements},{"file_sha256",slot.digest}});
            }
            const auto value=Json{{"version",2},{"complete",complete},{"censored",censored},
                {"design",Experiment::StratifiedDeepDesign},{"context",deepContext},{"strata",std::move(slots)},
                {"ineligible_contested_no_xy",deepIneligibleNoXY},{"ordinary_missing_field",metrics.excludedMissingField},
                {"encoded_pending_limit",Experiment::StratifiedEncodedPendingLimit},{"input_pending_limit",InputPendingLimit},{"peak_input_reservations",peakInputReservations},{"input_admission_exclusions",inputAdmissionExclusions},{"input_ineligible_exclusions",inputIneligibleExclusions},{"peak_input_payload",peakInputPayload},{"input_accounting","vector-capacity-payload-plus-serialized-metadata; excludes allocator/json-node/map/experiment-set-node/shared-registry/pool/oracle-scratch RSS"},
                {"final_limit",Experiment::StratifiedFinalLimit},{"transient_limit",Experiment::StratifiedTransientLimit}}.dump();
            if(value.size()>64*1024)throw std::runtime_error("stratified manifest exceeds allowance");
            Experiment::replaceStratifiedDeepFile(deepDirectory/"manifest.json",std::span(reinterpret_cast<const Uint8*>(value.data()),value.size()));
            return;
        }
        const bool censored=deep.censored();
        const auto value=Json{{"version",1},{"complete",complete},{"censored",bool(censored)},{"design",Experiment::DeepDesign},
            {"eligible",deep.eligible},{"excluded_eligible_admission",deep.excludedSampleCap+deep.excludedQueueCap},{"excluded_eligible_sample_cap",deep.excludedSampleCap},{"excluded_eligible_queue_cap",deep.excludedQueueCap},{"ineligible_contested_no_xy",deepIneligibleNoXY},{"ordinary_missing_field",metrics.excludedMissingField},{"priority",deep.priority},{"ordinal",deep.ordinal},{"captured_ordinal",deep.writtenOrdinal},
            {"replacements",deep.replacements},{"file_sha256",deepDigest},{"context",deepContext}}.dump();
        Experiment::replaceDeepFile(deepDirectory/"manifest.json",std::span(reinterpret_cast<const Uint8*>(value.data()),value.size()));
    }

    Impl(ComputeExecutor& executor,std::string path,Config config):executor(executor),config(config),path(std::move(path))
    {
        if (!config.sampleEvery || !config.maxSamples || !config.maxOutputBytes || !config.maxPending || config.maxPending>8)
            throw std::invalid_argument("invalid resource claim probe limits");
        if(config.deepTopK && (!config.deepTopFour || config.deepTopK>Experiment::DeepTopKMaximum))throw std::invalid_argument("invalid scalable capture configuration");
        if(config.captureOnly && (!config.deepTopK || !config.deepStratified))throw std::invalid_argument("capture-only requires configured stratified top-k");
        if(config.deepMissingTopK && (!config.deepMissing || config.deepMissingTopK>64))throw std::invalid_argument("invalid missing top-k config");
        if(config.deepMissingTopK)for(auto& slot:missingTopSlots)slot=Experiment::TopKDeepSlot(config.deepMissingTopK);
        if(config.deepTopK)for(auto& slot:topSlots)slot=Experiment::TopKDeepSlot(config.deepTopK);
        if(config.deepMissing && (config.deepContext.empty() || !config.missingSampleEvery || config.movementOnly))throw std::invalid_argument("missing capture requires full observer/context/sample rate");
        if(config.deepTopFour && !config.deepStratified)throw std::invalid_argument("top-four capture requires stratified capture");
        if(config.deepStratified && config.deepContext.empty())throw std::invalid_argument("stratified capture requires deep context");
        if (std::filesystem::exists(this->path)) throw std::runtime_error("resource claim probe output already exists");
        if (std::filesystem::space(std::filesystem::path(this->path).parent_path()).available<16ull*1024*1024)
            throw std::runtime_error("insufficient free space for resource claim probe");
        if(!config.deepContext.empty()) {
            deepContext=Json::parse(config.deepContext);
            for(const auto* key:{"source_revision","bundle_id","cohort_id","game_id"})
                if(!deepContext.contains(key) || !deepContext[key].is_string() || deepContext[key].get<std::string>().empty() || deepContext[key].get<std::string>().size()>256)
                    throw std::invalid_argument("deep context requires bounded source/bundle/cohort/game identities");
            deepDirectory=std::filesystem::path(this->path).parent_path()/"resource-claims-deep";
            if(!std::filesystem::create_directory(deepDirectory))throw std::runtime_error("deep output already exists");
            deepManifest(false);
            if(config.deepMissing) {
                missingDirectory=std::filesystem::path(this->path).parent_path()/"resource-claims-missing";
                if(!std::filesystem::create_directory(missingDirectory))throw std::runtime_error("missing output already exists");
                missingManifest(false);
            }
        }
        file=gzopen(this->path.c_str(),"wb6");
        if (!file) throw std::runtime_error("cannot open resource claim probe output");
    }
    ~Impl()
    {
        // Explicit finish reports errors; teardown still waits for every borrower.
        for (auto& work:pending) { try { executor.join(work->batch,{JoinReason::Teardown,ComputeTelemetry::NoTick}); } catch (...) {} }
        for(auto& work:missingPending){try{executor.join(work->batch,{JoinReason::Teardown,ComputeTelemetry::NoTick});}catch(...) {}}
        const auto joinBaseline=[&](Journey& journey){if(journey.baseline)try{executor.join(journey.baseline->batch,{JoinReason::Teardown,ComputeTelemetry::NoTick});}catch(...) {}};
        for(auto& [owner,journey]:journeys)joinBaseline(journey);
        for(auto& closed:closedJourneys)joinBaseline(closed.journey);
        if(file) gzclose(file);
    }
    void write(const std::string& text)
    {
        const auto allowance=Uint64(compressBound(text.size()))+64;
        if (allowance>config.maxOutputBytes || metrics.outputBytes>config.maxOutputBytes-allowance)
            throw std::runtime_error("resource claim probe byte budget exhausted; dataset is incomplete");
        if (std::filesystem::space(std::filesystem::path(path).parent_path()).available<allowance+16ull*1024*1024)
            throw std::runtime_error("resource claim probe free-space reserve exhausted; dataset is incomplete");
        if (text.size()>std::numeric_limits<unsigned>::max() || gzwrite(file,text.data(),unsigned(text.size()))!=int(text.size())
            || gzflush(file,Z_SYNC_FLUSH)!=Z_OK)
            throw std::runtime_error("resource claim probe write failed; dataset is incomplete");
        const auto bytes=gzoffset(file);
        if (bytes<0) throw std::runtime_error("resource claim probe cannot measure output size");
        metrics.outputBytes=Uint64(bytes);
    }
    static void run(void* context,std::size_t) noexcept
    {
        auto& work=*static_cast<Work*>(context);
        try {
            const auto start=std::chrono::steady_clock::now();
            const auto oracle=Oracle::evaluate(work.world,work.query,work.owners);
            const auto oracleNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            const auto view=work.world.view();
            // The immutable XY lease and copied cost plane come from the same
            // publication. Older/warming fields explicitly use scalar fallback.
            const auto destination=[&](Uint32 cell) {
                if(work.destinations) {
                    const auto x=work.destinations->x[cell],y=work.destinations->y[cell];
                    if(x>=view.width || y>=view.height)return Oracle::NoTile;
                    return Uint32(y*view.width+x);
                }
                Uint16 here=work.field[cell];
                if (here<=GRADIENT_UNREACHABLE) return Oracle::NoTile;
                while (here!=GRADIENT_AT_GOAL) {
                    const int x=int(cell%view.width),y=int(cell/view.width);
                    int best=-1; Uint32 selected=Oracle::NoTile;
                    for (int d=0;d<8;++d) {
                        const int dx=tabClose[d][0],dy=tabClose[d][1];
                        const auto next=Uint32(view.index(x+dx,y+dy));
                        if (work.field[next]<=here) continue;
                        const int score=int(work.field[next])-int(routeEntryCost(view,next,work.query.route,dx&&dy));
                        if (score>best) { best=score; selected=next; }
                    }
                    if (selected==Oracle::NoTile) break;
                    cell=selected; here=work.field[cell];
                }
                return cell;
            };
            const auto exposureStart=std::chrono::steady_clock::now();
            std::unique_ptr<Experiment::ExposureProxy> exposure;
            if(work.deepSelected)exposure=std::make_unique<Experiment::ExposureProxy>(view.width,view.height,work.world.occupancy->cells,work.world.visibility->visible,
                work.incomingAttackers,work.observerVisibility,unsigned(Unit::MAX_COUNT),unsigned(Building::MAX_COUNT));
            const auto exposureNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-exposureStart).count();
            Experiment::PatternEvaluator evaluator(oracle,view.width,view.height,work.query.origin,work.nearest);
            Json outcomes=Json::array();
            std::map<std::tuple<Uint32,Uint32,Uint32>,Experiment::PatternOutcome> uniqueRoutes;
            unsigned patternIndex=0;
            for (const auto& pattern:Experiment::pilotPatterns()) {
                for (unsigned policy=0;policy<2;++policy) {
                    const auto outcome=evaluator.evaluate(pattern.viewForDirection(evaluator.closestOctant()),policy?Experiment::PatternSelection::ClosestGeometric:Experiment::PatternSelection::FirstAvailable,destination);
                    if(!outcome.fallback)uniqueRoutes.try_emplace(std::tuple{outcome.source,outcome.access,outcome.probe},outcome);
                    outcomes.push_back({patternIndex,policy,outcome.source,outcome.access,outcome.probe,distanceJson(outcome.distance),outcome.probes,outcome.destinationReads,outcome.fallback,distanceJson(outcome.accessDistance)});
                }
                ++patternIndex;
            }
            // Hash-rank distinct routes, independent of pattern enumeration order.
            // This is an explicit bounded subset, not a per-pattern performance census.
            std::vector<std::pair<Uint64,Experiment::PatternOutcome>> ranked;
            for(const auto& [key,outcome]:uniqueRoutes) {
                const auto hash=mix(mix(work.ordinal ^ (Uint64(work.seed)<<32)) ^ mix(outcome.source) ^
                    mix(Uint64(outcome.access)+0x100000000ull) ^ mix(Uint64(outcome.probe)+0x200000000ull));
                ranked.emplace_back(hash,outcome);
            }
            std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
                return std::tie(a.first,a.second.source,a.second.access,a.second.probe)<
                    std::tie(b.first,b.second.source,b.second.access,b.second.probe);});
            Experiment::StaticRoutes routes(view,work.query,oracle,work.field,exposure.get());
            Json routeRows=Json::array();
            const auto measurement=[&](const Experiment::RouteMeasurement& r) {
                Json proxy=nullptr;
                if(exposure && r.exposure.endpoint!=UINT32_MAX)proxy={{"complete",std::string_view(r.status)=="reached"},{"endpoint",r.exposure.endpoint},{"endpoint_distance",exposure->at(r.exposure.endpoint)},
                    {"entered_steps",r.exposure.steps},{"entered_cost",r.exposure.cost},{"min_entered_distance",r.exposure.minimum},
                    {"near_steps",r.exposure.nearSteps},{"near_cost",r.exposure.nearCost}};
                return Json{{"status",r.status},{"cost",distanceJson(r.cost)},{"expansions",r.expansions},
                    {"elapsed_ns",r.ns},{"continuation_steps",r.continuationSteps},{"exposure_proxy",std::move(proxy)}};
            };
            const auto routeStart=std::chrono::steady_clock::now();
            for(std::size_t i=0;i<std::min(ranked.size(),std::size_t(Experiment::StaticRoutes::OutcomeLimit));++i) {
                const auto& outcome=ranked[i].second;
                routeRows.push_back({{"source",outcome.source},{"access",outcome.access},{"probe",outcome.probe},
                    {"source_optimal_cost",distanceJson(outcome.distance)},{"access_optimal_cost",distanceJson(outcome.accessDistance)},
                    {"direct_source_component",measurement(routes.direct(outcome.source))},
                    {"via_probe_selected_access",measurement(routes.via(outcome.access,outcome.probe))}});
            }
            const auto routeNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-routeStart).count();
            Json accessExposure=nullptr;std::set<Uint32> measuredAccess;
            if(exposure) {
                accessExposure=Json::array();
                for(const auto& [key,outcome]:uniqueRoutes)if(measuredAccess.insert(outcome.access).second)
                    accessExposure.push_back({outcome.access,exposure->at(outcome.access)});
            }
            Json optimalExposure=nullptr;
            if(exposure && oracle.best) {
                const auto& candidate=oracle.candidates[*oracle.best];
                optimalExposure={{"access",candidate.access},{"access_distance",exposure->at(candidate.access)},
                    {"stance",candidate.stance},{"stance_distance",exposure->at(candidate.stance)}};
            }
            Json best=nullptr;
            if (oracle.best) {
                const auto& chosen=oracle.candidates[*oracle.best];
                best={{"source",chosen.source},{"access",chosen.access},{"stance",chosen.stance},{"distance",chosen.distance}};
            }
            Json exposureMetadata=nullptr;
            if(exposure)exposureMetadata={{"incoming_attackable_team_mask",work.incomingAttackers},{"outgoing_enemy_team_mask",work.outgoingEnemies},
                {"observer_visibility_mask",work.observerVisibility},{"seed_cells",exposure->seedCells()},
                {"origin_distance",exposure->at(work.query.origin)},{"nearest_access_distance",exposure->at(work.nearest)},
                {"build_elapsed_ns",exposureNs}};
            Json row={{"kind","decision"},{"claims_enforced",work.claimsEnforced},{"active_policy",work.experimental?"experimental":work.claimsEnforced?"prototype-stationary-v1":"disabled"},
                {"policy_config",work.policyConfig},{"policy_digest",work.policyDigest},
                {"pre_task",work.preTask},{"decision_kind",work.goalRefresh?"goal_refresh":work.claimsEnforced?"new_claim_acquisition":"legacy_resource_decision"},{"reason",work.reason},{"phase",work.pickup?"pickup":"movement"},{"tick",work.tick},{"ordinal",work.ordinal},{"seed",work.seed},
                {"width",view.width},{"height",view.height},{"owner",work.query.owner},{"gid",work.query.route.unitGid},{"team_mask",work.query.route.teamMask},
                {"movement",work.query.route.movement},{"material",work.query.material},{"origin",work.query.origin},
                {"nearest",work.nearest},{"capacity",work.capacity},{"claimed",work.claimed},
                {"destination_lookup",work.destinations?"published-xy":"weighted-scalar-projection"},
                {"material_destinations_enabled",work.metadataEnabled},{"destination_xy_missing",!work.destinations},
                {"exposure_missing",!exposure},{"exposure_proxy",std::move(exposureMetadata)},
                {"access_exposure",std::move(accessExposure)},{"optimal_exposure",std::move(optimalExposure)},
                {"occupancy","static"},{"nearest_octant",evaluator.closestOctant()},{"oracle_expanded",oracle.expanded},{"oracle_elapsed_ns",oracleNs},
                {"blocker_census",blockerCensus(work,oracle,view.width,view.height)},{"static_routes",std::move(routeRows)},{"static_route_unique",ranked.size()},
                {"static_route_excluded_limit",ranked.size()>Experiment::StaticRoutes::OutcomeLimit?ranked.size()-Experiment::StaticRoutes::OutcomeLimit:0},
                {"static_route_elapsed_ns",routeNs},{"source_count",oracle.candidates.size()},{"optimal",best},{"features",work.features},{"patterns",std::move(outcomes)}};
            if(work.deepSelected) {
                auto metadata=row;
                metadata.erase("static_routes");
                metadata.erase("blocker_census"); // Bounded ordinary trace, joined by event ordinal; deep wire unchanged.
                metadata.erase("access_exposure"); // Compact trace holds selected-access proxies; full offline risk needs a future wire revision.
                metadata.erase("features"); // Feature row remains in ordinary trace, joined by ordinal.
                metadata["game_id"]=work.deepContext.at("game_id");
                metadata["event_id"]=std::to_string(work.seed)+":"+std::to_string(work.ordinal);
                metadata["priority"]=work.deepPriority;
                if(work.deepStratum<Experiment::DeepStratumCount) {
                    metadata["deep_design"]=work.deepTopFour?(work.deepTopK>16?Experiment::Retention64DeepDesign:(work.deepTopK?Experiment::ScalableTopKDeepDesign:Experiment::TopKDeepDesign)):Experiment::StratifiedDeepDesign;
                    if(work.deepTopFour)metadata["deep_slot"]=work.deepRetainedSlot;
                    metadata["deep_stratum"]=work.deepStratum;
                }
                metadata["optimum"]=oracle.best?distanceJson(oracle.candidates[*oracle.best].distance):Json(nullptr);
                auto provenance=work.deepContext;provenance.erase("game_id");
                provenance["metadata_mode"]="published-xy";provenance["censored"]=false;
                metadata["provenance"]=std::move(provenance);
                std::vector<Uint32> plane(view.width*view.height);
                for(Uint32 tile=0;tile<plane.size();++tile)plane[tile]=destination(tile);
                work.deepBytes=Experiment::encodeDeepEvent(metadata,plane,oracle.candidates);
            }
            work.line=row.dump()+"\n";
        } catch (...) { work.error=std::current_exception(); }
        work.world={}; work.owners.clear(); work.field.clear(); work.destinations.reset();
    }
    void publish(Uint64 tick,bool all=false)
    {
        while (!pending.empty() && (all || Uint64(pending.front()->tick)+1<=tick)) {
            auto& work=*pending.front();
            executor.join(work.batch,{all?JoinReason::Save:JoinReason::TickDeadline,Uint32(tick)});
            if(work.error) std::rethrow_exception(work.error);
            write(work.line);
            if(work.deepSelected) {
                const auto digest=Online::Sha256::toHex(Online::Sha256::of(work.deepBytes));
                if(config.deepTopFour) {
                    auto& slot=topSlots.at(work.deepStratum);
                    if(slot.current(work.deepRetainedSlot,work.ordinal)) {
                        Experiment::replaceTopKDeepFile(deepDirectory/Experiment::topKFilename(work.deepStratum,work.deepRetainedSlot,topKCapacity()),work.deepBytes,topKCapacity(),config.deepTopK!=0);
                        slot.written(work.deepRetainedSlot,work.ordinal,digest);
                    }
                } else if(config.deepStratified) {
                    Experiment::replaceStratifiedDeepFile(deepDirectory/deepFilename(work.deepStratum),work.deepBytes);
                    auto& slot=deepSlots.at(work.deepStratum);slot.digest=digest;slot.reservoir.written(work.ordinal);
                } else {
                    Experiment::replaceDeepFile(deepDirectory/"event.bin",work.deepBytes);
                    deepDigest=digest;deep.written(work.ordinal);
                }
            }
            pendingInputReservations-=work.inputReservation;
            ++metrics.completed; pending.pop_front();
        }
    }
};

Probe::Probe(ComputeExecutor& executor,std::string path,Config config):impl(std::make_unique<Impl>(executor,std::move(path),config))
{
    Json patterns=Json::array();
    for (const auto& pattern:Experiment::pilotPatterns()) patterns.push_back(pattern.name);
    impl->write(Json{{"kind","schema"},{"version",1},{"timing_clock","worker-steady-clock-elapsed"},{"cohort","sampled-material-movement-and-pickup-decisions-before-choice"},
        {"deep_capture",config.deepContext.empty()?Json(nullptr):Json{{"design",config.deepTopFour?impl->topKDesign():config.deepStratified?Experiment::StratifiedDeepDesign:Experiment::DeepDesign},{"context",impl->deepContext},{"record_limit",Experiment::DeepRecordLimit},{"directory_limit",config.deepTopFour?Experiment::topKFinalLimit(impl->topKCapacity())+Experiment::StratifiedTransientLimit:config.deepStratified?Experiment::StratifiedFinalLimit+Experiment::StratifiedTransientLimit:Experiment::DeepDirectoryLimit},{"free_reserve",Experiment::DeepFreeReserve}}},
        {"capture_only_version",config.captureOnly?1:0},{"decision_work_scope",config.captureOnly?"retained-replacements-only-v1":"all-admitted-contested-samples"},
        {"sample_every",config.sampleEvery},{"max_samples",config.maxSamples},{"max_pending",config.maxPending},
        {"movement_audit_version",1},
        {"movement_only",config.movementOnly},{"decision_sampling_enabled",!config.movementOnly},
        {"live_journey_version",1},{"live_event_names",Json::array({"acquired","released","retargeted","harvest_begin","harvested","harvest_failed",
            "goal_refresh_same","goal_refresh_changed","goal_refresh_failed","fallback_wait","fallback_roam","fallback_release",
            "route_initial","route_refresh","route_repair","route_probe_fallback","pending_path","traffic_wait","geometry_contradiction",
            "reanchor_joined","reanchor_failed","probe_tail_failed","obsolete_probe"})},
        {"max_output_bytes",config.maxOutputBytes},{"claims_enforced",nullptr},{"mode_source","decision.claims_enforced and decision.active_policy"},{"destination_lookup","per-event-published-xy-or-scalar-fallback"},
        {"continuation_policy","diagnostic-ordinal-resets-after-load"},{"patterns",patterns},
        {"new_destination_reads_semantics","marginal shared event-cache misses in pattern order; instrumentation amortization only, not standalone policy cost"},
        {"stale_goal_eligibility","published XY from natural-only field; failed scalar projection and mixed supplier fields excluded"},
        {"decision_outcome_version",1},{"decision_outcome_order","outcome may precede decision; join by ordinal; legacy/shadow and direct diagnostic calls have no actual outcome"},{"blocker_census_version",1},{"blocker_example_limit_per_group",Impl::MaximumClaimantExamples},{"blocker_tracking_owner_limit",Impl::MaximumTrackedClaimants},
        {"outcome_columns",{"pattern","policy","source","access","probe","weighted_cost","probes","new_destination_reads","fallback","access_weighted_cost"}},
        {"exposure_proxy_contract",{{"version",1},{"selection","deep minhash reservoir replacements only; use final per-game minimum for unbiased eligible-event comparisons"},{"meaning","coarse hostile occupied-cell proximity, not combat risk"},
            {"distance","toroidal Chebyshev, capped at16;17 means beyond cap or no seed"},
            {"planes",{"units_omniscient","buildings_omniscient","units_fog_visible","buildings_fog_visible"}},
            {"visibility","observer team bit; fog plane already incorporates sharing; entity cloaking not modeled"},
            {"bands",{1,4,8}},{"fractions","near_steps/entered_steps and near_cost/entered_cost; zero denominators unavailable"},
            {"path_scope","actual reconstructed entered cells; failed continuation is partial; no-path null"}}},
        {"static_route_limits",{{"unique_outcomes",Experiment::StaticRoutes::OutcomeLimit},{"expansions_per_search",Experiment::StaticRoutes::ExpansionLimit},
            {"continuation_steps",Experiment::StaticRoutes::ContinuationLimit},{"selection","diagnostic_hash_without_replacement"},
            {"heuristic","minimum_rule_entry_chebyshev; zero_for_farm_goal_sets"}}},
        {"policies",{"first_available","closest_geometric"}}}.dump()+"\n");
}
Probe::~Probe()=default;
const Probe::Metrics& Probe::metrics() const {return impl->metrics;}
void Probe::publish(Uint32 tick) {
    impl->publish(tick);
    impl->publishMissing(tick);
    if(!impl->finished) {
        impl->publishBaselines(tick);impl->expireJourneys(tick);
        // Publications inside a decision can precede later moves at the same
        // tick. Only a later boundary covers those events exclusively.
        if(!impl->lastTransportEventTick || tick>*impl->lastTransportEventTick)impl->movementEndTick=tick;
        else impl->movementEndTick.reset();
    }
}
void Probe::finish()
{
    if(impl->finished)return;
    impl->publish(UINT32_MAX,true);
    impl->publishMissing(UINT32_MAX,true);
    impl->publishBaselines(UINT32_MAX,true);
    while(!impl->journeys.empty())impl->closeJourney(impl->journeys.begin(),"end_of_segment",impl->latestLiveTick);
    const auto& m=impl->metrics;
    impl->write(Json{{"kind","summary"},{"capture_only_version",impl->config.captureOnly?1:0},{"capture_only_skipped_nonretained",impl->captureOnlySkipped},{"complete",true},{"decisions",m.decisions},{"sampled",m.sampled},
        {"contested",m.contested},{"submitted",m.submitted},{"completed",m.completed},
        {"excluded_sample_limit",m.excludedSampleLimit},{"excluded_queue_limit",m.excludedQueueLimit},{"excluded_missing_field",m.excludedMissingField},
        {"material_decisions",impl->materialDecisions},{"material_sampled",impl->materialSampled},
        {"material_contested",impl->materialContested},{"sampled_infinite",impl->sampledInfinite},
        {"sampled_pooled",impl->sampledPooled},{"sampled_exhausted_pools",impl->sampledExhaustedPools},
        {"stale_scalar_ineligible",impl->staleScalarIneligible},{"stale_mixed_field_ineligible",impl->staleMixedFieldIneligible},
        {"stale_invalid_xy",impl->staleInvalidXY},{"capture_bytes",m.captureBytes},{"live",impl->liveSummary()},{"decision_outcomes",impl->outcomeSummary()},
        {"raw_resource_probes",{{"version",1},{"material_counters",impl->rawProbes},{"counter_names",{"coordinate_visits","xy_unique_candidate_checks","raw_attempted_coordinates","raw_unique_candidate_checks","raw_best_candidate_improvements","raw_final_selections"}},
            {"scope","source4 raw-enabled full-probe selections only; raw-off and movement-only zeros mean unobserved work, not zero work; at most 2 candidate phases per array coordinate; paired recoverycursor2*i+phase; A*admission budget unchanged; raw after XY per array entry, deduplicated with XY; checks precede recovery/score/eligibility rejection; improvements are provisional; final selections count only successful commits of a raw winner, including unchanged held claims"},
            {"offline_patterns","existing pattern scores/probes remain XY-only shadow estimates, not hybrid selector measurements"}}},
        {"movement_audit",{{"version",1},{"scope","all-transport-committed-grid-moves"},
            {"coverage_start",impl->config.movementStartTick?Json(*impl->config.movementStartTick):Json(nullptr)},
            {"coverage_end",impl->movementEndTick?Json(*impl->movementEndTick):Json(nullptr)},
            {"coverage_end_exclusive",true},
            {"coverage_known",impl->config.movementStartTick && impl->movementEndTick && *impl->config.movementStartTick<=*impl->movementEndTick},
            {"kind_names",{"medical","claimed_resource","unclaimed_resource","return_to_job","idle","other"}},
            {"moves",impl->movement.moves},{"weighted_cost",impl->movement.weightedCost},
            {"total_moves",impl->movement.totalMoves},{"total_weighted_cost",impl->movement.totalCost},
            {"excluded_non_neighbor_moves",impl->movement.excludedNonNeighbor},
            {"unclaimed_source_separation_available",false},
            {"unclaimed_resource_scope","natural and supplier pursuit are combined; source kind unavailable"},
            {"continuation_scope","segment totals only; prior travel and journey origins unavailable"}}}}.dump()+"\n");
    const auto file=impl->file; impl->file=nullptr;
    if(gzclose(file)!=Z_OK) throw std::runtime_error("resource claim probe close failed; dataset is incomplete");
    impl->metrics.outputBytes=std::filesystem::file_size(impl->path);
    impl->deepManifest(true);
    impl->missingManifest(true);
    impl->finished=true;
}

const Probe::LiveMetrics& Probe::liveMetrics() const {return impl->live;}
const Probe::MovementMetrics& Probe::movementMetrics() const {return impl->movement;}
void Probe::liveEvent(const Unit& unit,LiveEvent event) const
{
    if(impl->finished || impl->config.movementOnly)return;
    const auto tick=unit.owner->game->stepCounter;impl->expireJourneys(tick);
    const auto owner=unitOwner(unit.gid,unit.scriptIdentity);
    // Exact ledger lifetime is separate from sampled journey lifetime (which may
    // continue through harvest after the reservation has been released).
    if(event==LiveEvent::Released || event==LiveEvent::Retargeted)impl->claimAges.erase(owner);
    else if(event==LiveEvent::Acquired && unit.activeMaterialClaim) {
        if(impl->claimAges.contains(owner) || impl->claimAges.size()<Impl::MaximumTrackedClaimants)
            impl->claimAges.insert_or_assign(owner,Impl::ClaimAge{*unit.activeMaterialClaim,unit.materialClaimTask,tick,true});
        else ++impl->claimAgeTrackingExcluded;
    }
    ++impl->live.events.at(unsigned(event));
    auto at=impl->journeys.find(owner);
    if(event==LiveEvent::Acquired) {
        if(at!=impl->journeys.end())impl->closeJourney(at,"retargeted",tick);
        ++impl->live.journeyEligible;
        const auto phase=std::min<unsigned>(tick/4096,15);++impl->journeyEligiblePhase[phase];
        if(!unit.activeMaterialClaim)return;
        const auto priority=mix(0x4A4F55524E455931ull ^ mix(owner) ^ mix(unit.materialClaimTask) ^ mix(unit.owner->game->gameHeader.getRandomSeed()));
        if(priority%impl->config.sampleEvery)return;
        ++impl->live.journeySampled;++impl->journeySampledPhase[phase];
        if(impl->live.journeyAdmitted>=std::min<unsigned>(Impl::MaximumJourneyAdmissions,impl->config.maxSamples)){++impl->live.excludedLimit;++impl->journeyExcludedPhase[phase];return;}
        if(impl->journeys.size()+impl->closedJourneys.size()>=Impl::MaximumJourneys){++impl->live.excludedCapacity;++impl->journeyExcludedPhase[phase];return;}
        const auto& claim=*unit.activeMaterialClaim;Impl::Journey j;
        j.task=unit.materialClaimTask;j.acquired=tick;j.origin=unit.owner->map->coordToIndex(unit.posX,unit.posY);
        j.source=claim.source;j.access=claim.access;j.material=claim.material;
        if(unit.owner->map->materialClaimPolicy()==ActiveClaimPolicy::Experimental) {
            const auto& config=unit.owner->map->materialClaimPolicyConfig();
            if(!impl->cachedPolicy || *impl->cachedPolicy!=config) {
                const auto canonical=config.canonicalJson();impl->cachedPolicy=config;
                impl->cachedPolicyJson=Json::parse(canonical);impl->cachedPolicyDigest=Online::Sha256::hex(canonical);
            }
            j.policyDigest=impl->cachedPolicyDigest;
        }
        auto baseline=std::make_unique<Impl::Baseline>();
        using C=SimulationSnapshot::Component;
        const auto mask=SimulationSnapshot::bit(C::Catalogs)|SimulationSnapshot::bit(C::Terrain)|SimulationSnapshot::bit(C::Resources)
            |SimulationSnapshot::bit(C::Occupancy)|SimulationSnapshot::bit(C::Areas);
        const auto bytes=impl->storage.bytesCopied;
        baseline->world=SimulationSnapshot::capture(*unit.owner->game,SimulationSnapshot::captureCatalog(*unit.owner->game),mask,nullptr,&impl->storage,false);
        impl->metrics.captureBytes+=impl->storage.bytesCopied-bytes;
        baseline->profile={accessClass(unit.movementAccess()),unit.owner->me,unit.gid,OccupancyPolicy::Static};
        baseline->origin=j.origin;baseline->target=j.access;baseline->due=Uint64(tick)+1;
        auto* raw=baseline.get();j.baseline=std::move(baseline);j.events[unsigned(event)]=1;
        const auto inserted=impl->journeys.emplace(owner,std::move(j)).first;
        const ComputeExecutor::Group group{1,{Impl::runBaseline,raw},ComputeExecutor::NoLane};
        try {raw->batch=impl->executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(Uint64(tick)+1),
            {ComputeProducer::Other,tick,Uint64(tick)+1});}
        catch(...) {impl->journeys.erase(inserted);throw;}
        ++impl->live.journeyAdmitted;++impl->journeyAdmittedPhase[phase];return;
    }
    if(at==impl->journeys.end())return;
    ++at->second.events[unsigned(event)];
    if(event==LiveEvent::HarvestBegin)at->second.harvesting=true;
    else if(event==LiveEvent::Harvested)impl->closeJourney(at,"harvested",tick);
    else if(event==LiveEvent::HarvestFailed)impl->closeJourney(at,"harvest_failed",tick);
    else if(event==LiveEvent::Retargeted)impl->closeJourney(at,"retargeted",tick);
    else if(event==LiveEvent::Released && !at->second.harvesting) {
        at->second.releaseReason=impl->pendingReleaseReason;impl->closeJourney(at,"released",tick);
    }
}
void Probe::liveRelease(const Unit& unit,ReleaseReason reason) const
{
    if(impl->finished || impl->config.movementOnly)return;
    ++impl->live.releaseReasons.at(unsigned(reason));
    impl->pendingReleaseReason=reason;liveEvent(unit,LiveEvent::Released);impl->pendingReleaseReason.reset();
}
void Probe::selectionFilter(SelectionFilter filter,Uint64 count) const
{
    if(impl->finished || impl->config.movementOnly)return;
    auto& live=impl->live;
    (filter==SelectionFilter::TerrainComponent?live.terrainPrefilterRejections:
        filter==SelectionFilter::RefreshMargin?live.refreshMarginRejections:live.refreshMarginKept)+=count;
}
void Probe::rawResourceProbes(unsigned material,Uint64 coordinates,Uint64 xyChecks,Uint64 attempts,Uint64 checks,Uint64 improvements,Uint64 finalSelections) const
{
    if(!observesDecisionOutcomes())return;
    auto& counters=impl->rawProbes.at(material);
    counters[0]+=coordinates;counters[1]+=xyChecks;counters[2]+=attempts;counters[3]+=checks;counters[4]+=improvements;counters[5]+=finalSelections;
}
void Probe::routeWork(const RouteDiagnostics& value) const
{
    if(impl->finished || impl->config.movementOnly)return;
    auto& live=impl->live;
    live.routeCompleted+=value.completed;live.routeCanceled+=value.canceled;
    live.routeExpansions+=value.expansions;live.routeElapsedNs+=value.elapsedNs;
    live.routeRestored+=value.restored;live.routeUnobserved=value.unobserved;
    for(unsigned i=0;i<live.routeStatuses.size();++i)live.routeStatuses[i]+=value.statuses[i];
}
void Probe::routeEvent(Uint64 owner,Uint64 task,Uint32 tick,LiveEvent event) const
{
    if(impl->finished || impl->config.movementOnly)return;
    impl->expireJourneys(tick);++impl->live.events.at(unsigned(event));
    const auto at=impl->journeys.find(owner);
    if(at!=impl->journeys.end() && at->second.task==task)++at->second.events[unsigned(event)];
}
void Probe::moved(const Unit& unit,int oldX,int oldY) const
{
    if(impl->finished || (oldX==unit.posX && oldY==unit.posY))return;
    bool transport=!unit.isDead && unit.hasCapability(UnitRuntimeTraits::Transport);
    if(!transport && !unit.activeMaterialClaim)return;
    impl->expireJourneys(unit.owner->game->stepCounter);
    if(transport) {
        impl->lastTransportEventTick=unit.owner->game->stepCounter;
        if(impl->movementEndTick && *impl->movementEndTick<=*impl->lastTransportEventTick)impl->movementEndTick.reset();
    }
    auto& map=*unit.owner->map;
    if(transport && map.warpDistMax(oldX,oldY,unit.posX,unit.posY)!=1) {
        ++impl->movement.excludedNonNeighbor;transport=false;
        if(!unit.activeMaterialClaim)return;
    }
    // Movement cost needs only authoritative cell rules. Avoid warming growth
    // or area-effect caches while observing otherwise unclaimed movement.
    const auto view=map.cellView();
    const RouteProfile profile{accessClass(unit.movementAccess()),unit.owner->me,unit.gid,OccupancyPolicy::Static};
    const auto cost=routeEntryCost(view,map.coordToIndex(unit.posX,unit.posY),profile,unit.dx && unit.dy);
    if(transport) {
        auto kind=MovementKind::Other;
        if(unit.medical!=Unit::MED_FREE)kind=MovementKind::Medical;
        else if(unit.activity==Unit::ACT_FILLING && unit.displacement==Unit::DIS_GOING_TO_RESOURCE)
            kind=unit.activeMaterialClaim?MovementKind::ClaimedResource:MovementKind::UnclaimedResource;
        else if(unit.activity==Unit::ACT_FILLING && unit.displacement==Unit::DIS_GOING_TO_BUILDING)
            kind=MovementKind::ReturnToJob;
        else if(unit.activity==Unit::ACT_RANDOM)kind=MovementKind::Idle;
        const auto index=unsigned(kind);
        ++impl->movement.moves[index];impl->movement.weightedCost[index]+=cost;
        ++impl->movement.totalMoves;impl->movement.totalCost+=cost;
    }
    if(!unit.activeMaterialClaim || impl->config.movementOnly)return;
    ++impl->live.moves;impl->live.liveCost+=cost;
    const auto at=impl->journeys.find(unitOwner(unit.gid,unit.scriptIdentity));
    if(at==impl->journeys.end() || at->second.task!=unit.materialClaimTask){++impl->live.untrackedMoves;return;}
    ++at->second.steps;at->second.cost+=cost;
}

bool Probe::observesDecisionOutcomes() const { return !impl->finished && !impl->config.movementOnly; }
Probe::DecisionOutcome Probe::beginDecisionOutcome(const Unit& unit,bool pickup) const {
    DecisionOutcome out;out.owner=unitOwner(unit.gid,unit.scriptIdentity);out.preTask=unit.materialClaimTask;
    out.tick=unit.owner->game->stepCounter;out.material=unit.destinationPurpose;out.pickup=pickup;
    if(unit.activeMaterialClaim){out.preSource=unit.activeMaterialClaim->source;out.preAccess=unit.activeMaterialClaim->access;}
    return out;
}
void Probe::selectedDecisionOutcome(DecisionOutcome& out,const Unit& unit) const {
    out.selectedTask=unit.materialClaimTask;
    if(unit.activeMaterialClaim){out.selectedSource=unit.activeMaterialClaim->source;out.selectedAccess=unit.activeMaterialClaim->access;}
}
void Probe::finishDecisionOutcome(const DecisionOutcome& out,const Unit& unit) const {
    if(!observesDecisionOutcomes())return;
    auto& counts=impl->outcomes.at(out.material)[out.pickup?1:0];++counts[Impl::Eligible];
    const bool held=out.preAccess!=UINT32_MAX,hasSelection=out.selectedAccess!=UINT32_MAX;
    const char* change=!hasSelection?"no_claim":!held?"acquired":out.selectedTask==out.preTask?"held_same":
        out.selectedAccess==out.preAccess?"retarget_donor_same_access":"retarget_access";
    if(out.attempted)++counts[Impl::Attempted];else {
        ++counts[Impl::NoAttempt];
        if(std::string_view(out.noAttempt)=="cooldown")++counts[Impl::Cooldown];
        if(std::string_view(out.noAttempt)=="missing_field")++counts[Impl::MissingField];
    }
    if(out.attempted && !out.selected)++counts[Impl::NoCandidate];
    if(hasSelection)++counts[!held?Impl::Acquired:out.selectedTask==out.preTask?Impl::HeldSame:
        out.selectedAccess==out.preAccess?Impl::RetargetDonor:Impl::RetargetAccess];
    if(out.pickup && held && hasSelection && out.selectedTask!=out.preTask && !out.token.ordinal)++counts[Impl::UnlinkedHeldExchange];
    const std::string_view route(out.route),fallback(out.fallback);
    if(route=="ready")++counts[Impl::Ready];else if(route=="pending")++counts[Impl::Pending];
    else if(route=="failed")++counts[Impl::Failed];else if(route=="arrived")++counts[Impl::Arrived];
    if(fallback=="wait")++counts[Impl::Wait];else if(fallback=="roam")++counts[Impl::Roam];else if(fallback=="release_job")++counts[Impl::Release];
    if(std::string_view(out.fallbackReason)=="cooldown")++counts[Impl::CooldownWait];
    if(!out.token.ordinal)return;
    ++counts[Impl::Linked];++impl->outcomeRows;
    const auto tile=[](Uint32 v)->Json{return v==UINT32_MAX?Json(nullptr):Json(v);};
    impl->write(Json{{"kind","decision_outcome"},{"schema_version",1},{"ordinal",out.token.ordinal},{"owner",out.owner},
        {"tick",out.tick},{"material",out.material},{"phase",out.pickup?"pickup":"movement"},
        {"pre_task",out.preTask},{"pre_source",tile(out.preSource)},{"pre_access",tile(out.preAccess)},
        {"attempted",out.attempted},{"selection_succeeded",out.selected},{"not_attempted_reason",out.attempted?Json(nullptr):Json(out.noAttempt)},
        {"selected_task",out.selectedTask},{"selected_source",tile(out.selectedSource)},{"selected_access",tile(out.selectedAccess)},
        {"claim_change",change},{"selected_is_nearest_access",hasSelection?Json(out.selectedAccess==out.token.nearestAccess):Json(nullptr)},
        {"route_result",out.route},{"fallback",out.fallback},{"fallback_reason",out.fallbackReason},
        {"final_task",unit.materialClaimTask},{"final_has_claim",unit.activeMaterialClaim.has_value()}}.dump()+"\n");
}

void Probe::missingDecision(Game& game,const Unit& unit,const Uint16* field,Uint32 pickupAccess,Uint64 legacyOrdinal,bool naturalOnly) const
{
    if(impl->finished || !impl->config.deepMissing || impl->config.movementOnly)return;
    const auto ordinal=++impl->missingCalls;const auto owner=unitOwner(unit.gid,unit.scriptIdentity);
    const auto seed=game.gameHeader.getRandomSeed();
    if(mix(0x4d49535353414d50ull^mix(seed)^mix(owner)^mix(game.stepCounter)^mix(ordinal))%impl->config.missingSampleEvery)return;
    ++impl->missingSampled;auto& map=game.map;
    // Read-only classification: never project, request a field, reconcile claims,
    // or run the live pooled-resource picker merely to observe a missing label.
    const auto* natural=map.lookupMaterialGradientSlot(unit.owner->teamNumber,unit.destinationPurpose,unit.movementAccess(),false,nullptr);
    if((field && field!=natural) || (!field && !naturalOnly && unit.attachedBuilding && unit.attachedBuilding->fetchesFromMarkets())) {++impl->missingMixed;return;}
    const auto labels=field?map.materialGradientDestinations(field):nullptr;
    const auto origin=Uint32(map.coordToIndex(unit.posX,unit.posY));
    if(labels && labels->cells!=map.cellCount())throw std::logic_error("missing observer destination shape");
    auto reason=!field?Experiment::MissingReason::MissingScalar:!labels?Experiment::MissingReason::MissingXYPlane:Experiment::MissingReason::InvalidXY;
    if(labels && labels->x[origin]<map.getW() && labels->y[origin]<map.getH()){++impl->missingValid;return;}
    const auto stratum=Experiment::missingStratum(reason,MaterialId(unit.destinationPurpose),pickupAccess!=UINT32_MAX);
    auto& slot=impl->missingSlots[stratum];const auto rank=mix(0x4d49535352414e4bull^mix(seed)^mix(owner)^mix(game.stepCounter)^mix(ordinal)^mix(stratum));
    if(impl->config.deepMissingTopK){if(impl->missingAdmissionTick!=game.stepCounter){impl->missingAdmissionTick=game.stepCounter;impl->missingTickEligible=impl->missingTickSelected=impl->missingTickAdmitted=0;}++impl->missingTickEligible;}
    const int retainedSlot=impl->config.deepMissingTopK?impl->missingTopSlots[stratum].consider(rank,ordinal,unsigned(reason),true):(slot.consider(rank,ordinal,reason)?0:-1);
    if(retainedSlot<0)return;
    if(impl->config.deepMissingTopK){++impl->missingTickSelected;impl->missingPressure(ordinal,game.stepCounter,stratum);}
    const auto excluded=[&](bool queue){if(impl->config.deepMissingTopK)impl->missingTopSlots[stratum].excluded(queue);else slot.reservoir.excluded(queue);};
    if(impl->missingAdmissions>=Experiment::MissingAdmissionLimit){excluded(false);return;}
    if(impl->missingPending.size()>=impl->missingPendingCapacity()){excluded(true);return;}
    const Uint64 reserve=Uint64(map.cellCount())*Impl::CellInputAllowance+Uint64(Impl::MaximumTrackedClaimants)*Impl::OwnerInputAllowance+1024*1024;
    if(reserve>Experiment::MissingInputLimit-impl->missingInputReservations){if(impl->config.deepMissingTopK)++impl->missingTopSlots[stratum].excludedInputBytes;else ++slot.inputExcluded;return;}
    auto w=std::make_unique<Impl::MissingWork>();w->inputReservation=reserve;w->ordinal=ordinal;w->tick=game.stepCounter;w->deepStratum=stratum;w->deepRetainedSlot=unsigned(retainedSlot);w->destinations=labels;
    using C=SimulationSnapshot::Component;
    const auto mask=SimulationSnapshot::bit(C::Catalogs)|SimulationSnapshot::bit(C::Terrain)|SimulationSnapshot::bit(C::Resources)
        |SimulationSnapshot::bit(C::Occupancy)|SimulationSnapshot::bit(C::Areas)|SimulationSnapshot::bit(C::Visibility)|SimulationSnapshot::bit(C::Rules);
    const auto before=impl->missingStorage.bytesCopied;
    w->world=SimulationSnapshot::capture(game,SimulationSnapshot::captureCatalog(game),mask,nullptr,&impl->missingStorage,false);
    if(impl->missingStorage.bytesCopied-before>reserve)throw std::runtime_error("missing observer capture exceeds input reservation");
    w->owners=map.materialClaimLedger().entries();
    w->query={owner,origin,Uint8(unit.destinationPurpose),{accessClass(unit.movementAccess()),unit.owner->me,unit.gid,OccupancyPolicy::Static}};
    auto provenance=impl->deepContext;provenance.erase("game_id");provenance["metadata_mode"]=labels?"published-xy-invalid-origin":"absent-xy";provenance["censored"]=false;
    w->missingMetadata={{"design",Experiment::MissingDesign},{"stratum",stratum},{"priority",rank},{"ordinal",ordinal},
        {"legacy_ordinal",legacyOrdinal?Json(legacyOrdinal):Json(nullptr)},{"event_id",std::to_string(ordinal)},
        {"game_id",impl->deepContext.at("game_id")},{"provenance",provenance},{"owner",owner},{"pre_task",unit.materialClaimTask},
        {"tick",game.stepCounter},{"seed",seed},{"width",map.getW()},{"height",map.getH()},{"origin",origin},{"nearest",nullptr},
        {"pickup_access",pickupAccess==UINT32_MAX?Json(nullptr):Json(pickupAccess)},{"phase",pickupAccess==UINT32_MAX?"movement":"pickup"},
        {"material",unit.destinationPurpose},{"movement",unsigned(accessClass(unit.movementAccess()))},{"team_mask",unit.owner->me},
        {"field_present",field!=nullptr},{"label_present",bool(labels)},
        {"reason",reason==Experiment::MissingReason::InvalidXY?"invalid_xy":reason==Experiment::MissingReason::MissingScalar?"missing_scalar":"missing_xy_plane"},
        {"natural_only_hook",naturalOnly},{"supplier_capable_consumer",unit.attachedBuilding && unit.attachedBuilding->fetchesFromMarkets()},
        {"claims_enforced",map.activeClaimsEnabled()},{"ledger_timing","pre-legacy-reconcile observed state"},
        {"farm_areas_enabled",map.farmAreasEnabled()},{"walk_speed",unit.performance[WALK]},{"swim_speed",unit.performance[SWIM]},
        {"fly_speed",unit.performance[FLY]},{"hp",unit.hp},{"medical",int(unit.medical)}};
    if(impl->config.deepMissingTopK){w->missingMetadata["design"]=Experiment::MissingTopKDesign;w->missingMetadata["physical_slot"]=retainedSlot;w->missingMetadata["top_k"]=impl->config.deepMissingTopK;}
    if(map.materialClaimPolicy()==ActiveClaimPolicy::Experimental) {
        const auto canonical=map.materialClaimPolicyConfig().canonicalJson();w->missingMetadata["policy_config"]=Json::parse(canonical);w->missingMetadata["policy_digest"]=Online::Sha256::hex(canonical);
    }else {w->missingMetadata["policy_config"]=nullptr;w->missingMetadata["policy_digest"]=nullptr;}
    const auto payload=Impl::inputPayloadBytes(*w)+sizeof(Impl::MissingWork)-sizeof(Impl::Work)+w->missingMetadata.dump().size();
    if(payload>reserve)throw std::runtime_error("missing observer copied payload exceeds reservation");
    impl->missingPeakPayload=std::max(impl->missingPeakPayload,payload);
    auto* raw=w.get();impl->missingPending.push_back(std::move(w));
    const ComputeExecutor::Group group{1,{Impl::runMissing,raw},ComputeExecutor::NoLane};
    try {raw->batch=impl->executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(Uint64(game.stepCounter)+1),{ComputeProducer::Other,game.stepCounter,Uint64(game.stepCounter)+1});}
    catch(...){impl->missingPending.pop_back();throw;}
    impl->missingInputReservations+=reserve;impl->missingPeakInput=std::max(impl->missingPeakInput,impl->missingInputReservations);++impl->missingAdmissions;if(impl->config.deepMissingTopK)++impl->missingTickAdmitted;
}

DecisionToken Probe::decision(Game& game,const Unit& unit,const Uint16* field,Uint32 pickupAccess)
{
    if(impl->finished || impl->config.movementOnly)return {};
    missingDecision(game,unit,field,pickupAccess,impl->metrics.decisions+1);
    auto& metrics=impl->metrics;
    const Uint64 ordinal=++metrics.decisions;
    ++impl->materialDecisions.at(unit.destinationPurpose);
    const auto seed=game.gameHeader.getRandomSeed();
    if(mix(mix(seed)^mix(game.stepCounter)^mix(unitOwner(unit.gid,unit.scriptIdentity))^mix(ordinal))%impl->config.sampleEvery)return {};
    ++metrics.sampled;
    ++impl->materialSampled.at(unit.destinationPurpose);
    if(!field){++metrics.excludedMissingField;return {};}
    auto& map=game.map;
    map.reconcileMaterialClaims();
    Sint32 x=0,y=0;
    bool staleGoal=false;
    const auto destinations=map.materialGradientDestinations(field);
    if(destinations && destinations->cells!=map.cellCount())
        throw std::logic_error("claim probe destination publication shape mismatch");
    if(pickupAccess!=UINT32_MAX) { x=int(pickupAccess%map.getW()); y=int(pickupAccess/map.getW()); }
    else if(!map.projectMaterialDestination(field,unit.owner->teamNumber,unit.destinationPurpose,unit.movementAccess(),unit.posX,unit.posY,&x,&y,
        unit.attachedBuilding && unit.attachedBuilding->fetchesFromMarkets(),unit.attachedBuilding)) {
        if(!destinations){++impl->staleScalarIneligible;return {};}
        if(field!=map.lookupMaterialGradientSlot(unit.owner->teamNumber,unit.destinationPurpose,unit.movementAccess(),false,nullptr)) {
            ++impl->staleMixedFieldIneligible;return {};
        }
        const auto origin=map.coordToIndex(unit.posX,unit.posY);
        x=destinations->x[origin]; y=destinations->y[origin];
        if(x>=map.getW() || y>=map.getH()) {++impl->staleInvalidXY;return {};}
        staleGoal=true;
    }
    const auto access=map.coordToIndex(x,y);
    if(!staleGoal && !map.materialAmountAtSlot(access,unit.destinationPurpose))return {}; // Supplier cohort.
    auto source=access;
    const bool accessResource=map.resourceRegistry().valid(static_cast<ResourceId>(map.getResource(access).type));
    const auto accessYield=accessResource?map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(access).type))[unit.destinationPurpose]:YieldProperties{};
    const bool exclusive=accessResource && (accessYield.consumption==ResourceConsumption::All || accessYield.destroysDeposit);
    const bool pooled=!staleGoal && !exclusive && map.farmAreasEnabled() && map.isFarmArea(x,y,unit.owner->me) && map.isFarmableResourceByIndex(map.getResource(access).type);
    bool exhaustedPool=false;
    if(pooled) {
        const auto selected=map.pickClaimFarmHarvestTileSlot(unit.posX,unit.posY,unit.destinationPurpose,unit.owner->me,
            pickupAccess!=UINT32_MAX?std::nullopt:std::optional<size_t>(access));
        if(selected) source=*selected;
        else exhaustedPool=true; // The seed-only field is itself a contested failure case.
    }
    const bool hasResource=map.resourceRegistry().valid(static_cast<ResourceId>(map.getResource(source).type));
    const auto yield=hasResource?map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(source).type))[unit.destinationPurpose]:YieldProperties{};
    const Uint32 amount=map.materialAmountAtSlot(source,unit.destinationPurpose);
    const Uint32 reserve=pooled && yield.consumption!=ResourceConsumption::Infinite && !game.gameHeader.isResourceGrowthDisabled()?yield.seedReserve:0;
    const Uint32 capacity=!staleGoal && amount>reserve?amount-reserve:0;
    const Claim desired{Uint32(source),Uint32(access),Uint8(unit.destinationPurpose),map.getResource(source).type,exclusive,Uint16(reserve)};
    impl->sampledInfinite+=yield.consumption==ResourceConsumption::Infinite;
    impl->sampledPooled+=pooled; impl->sampledExhaustedPools+=exhaustedPool;
    if(!staleGoal && map.materialClaimAvailable(unitOwner(unit.gid,unit.scriptIdentity),desired,capacity,yield.consumption==ResourceConsumption::Infinite))return {};
    ++metrics.contested;
    ++impl->materialContested.at(unit.destinationPurpose);
    unsigned forbiddenNeighbors=0,forbiddenMaterialNeighbors=0,growingForbiddenMaterialNeighbors=0;
    Uint32 forbiddenMaterialStock=0,growingForbiddenMaterialStock=0;
    const auto collectNeighborhood=[&] {
    for (int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(!map.isForbidden(x+dx,y+dy,unit.owner->me))continue;
        ++forbiddenNeighbors;
        const auto neighbor=map.coordToIndex(x+dx,y+dy);
        const auto stock=map.materialAmountAtSlot(neighbor,unit.destinationPurpose);
        if(!stock)continue;
        ++forbiddenMaterialNeighbors;forbiddenMaterialStock+=stock;
        const auto resource=static_cast<ResourceId>(map.getResource(neighbor).type);
        const auto& traits=map.resourceRegistry().properties(resource);
        const auto& materialYield=map.resourceRegistry().yields(resource)[unit.destinationPurpose];
        // Configured regrowth potential, not a prediction of habitat/fertility
        // success or proof that an AI intentionally reserved this donor.
        if(!game.gameHeader.isResourceGrowthDisabled() && traits.ecology!=ResourceEcology::None &&
            traits.growthRate && materialYield.growthRate) {
            ++growingForbiddenMaterialNeighbors;growingForbiddenMaterialStock+=stock;
        }
    }
    };
    if(impl->config.deepStratified)collectNeighborhood();
    const unsigned stratum=impl->config.deepStratified?Experiment::deepStratum(game.stepCounter,
        MaterialId(unit.destinationPurpose),pickupAccess!=UINT32_MAX,pooled,growingForbiddenMaterialNeighbors>0):Experiment::DeepStratumCount;
    bool deepSelected=false;
    int retainedSlot=-1;
    Uint64 deepPriority=0;
    if(!impl->config.deepContext.empty() && destinations) {
        deepPriority=mix(0xD33FC1A17A93B561ull ^ mix(seed) ^ mix(game.stepCounter) ^ mix(unitOwner(unit.gid,unit.scriptIdentity)) ^ mix(ordinal));
        if(impl->config.deepStratified) {
            deepPriority=mix(deepPriority ^ mix(0x5354524154410000ull+stratum));
            if(impl->config.deepTopFour) {retainedSlot=impl->topSlots[stratum].consider(deepPriority,ordinal,staleGoal?1:exhaustedPool?2:0,!staleGoal);deepSelected=retainedSlot>=0;}
            else deepSelected=impl->deepSlots[stratum].consider(deepPriority,ordinal,staleGoal?1:exhaustedPool?2:0,!staleGoal);
        } else deepSelected=impl->deep.consider(deepPriority,ordinal);
    }
    if(!impl->config.deepContext.empty() && !destinations)++impl->deepIneligibleNoXY;
    // Sampling, eligibility, ordinal and bottom-k insertion above remain identical.
    // Non-retained events need neither frozen inputs nor Oracle/pattern work.
    if(impl->config.captureOnly && !deepSelected){++impl->captureOnlySkipped;return {};}
    if(metrics.submitted>=impl->config.maxSamples){++metrics.excludedSampleLimit;if(!impl->config.deepContext.empty() && destinations){if(impl->config.deepTopFour)impl->topSlots[stratum].excluded(false);else (impl->config.deepStratified?impl->deepSlots[stratum].reservoir:impl->deep).excluded(false);}return {};}
    if(impl->pending.size()>=impl->config.maxPending){++metrics.excludedQueueLimit;if(!impl->config.deepContext.empty() && destinations){if(impl->config.deepTopFour)impl->topSlots[stratum].excluded(true);else (impl->config.deepStratified?impl->deepSlots[stratum].reservoir:impl->deep).excluded(true);}return {};}
    // Conservative copied-input reservation: every cell includes all 12 material
    // stocks, planes and publication data; owner vectors have the engine unit cap.
    // Shared immutable catalogs and worker oracle scratch are outside this input
    // allowance and must be measured separately in calibration RSS.
    const Uint64 inputReservation=impl->config.deepStratified?
        Uint64(map.cellCount())*Impl::CellInputAllowance+Uint64(Impl::MaximumTrackedClaimants)*Impl::OwnerInputAllowance+1024*1024:0;
    if(inputReservation>Impl::InputPendingLimit-impl->pendingInputReservations) {
        ++impl->inputAdmissionExclusions;
        if(destinations){if(impl->config.deepTopFour)++impl->topSlots[stratum].excludedInputBytes;else ++impl->deepSlots[stratum].excludedInputBytes;}else ++impl->inputIneligibleExclusions;
        return {};
    }
    auto work=std::make_unique<Impl::Work>();
    work->inputReservation=inputReservation;
    impl->pendingInputReservations+=inputReservation;
    impl->peakInputReservations=std::max(impl->peakInputReservations,impl->pendingInputReservations);
    work->deepSelected=deepSelected;work->deepPriority=deepPriority;work->deepStratum=stratum;
    work->deepTopFour=impl->config.deepTopFour;work->deepTopK=impl->config.deepTopK;work->deepRetainedSlot=retainedSlot>=0?unsigned(retainedSlot):0;
    if(deepSelected)work->deepContext=impl->deepContext;
    using C=SimulationSnapshot::Component;
    const auto mask=SimulationSnapshot::bit(C::Catalogs)|SimulationSnapshot::bit(C::Terrain)|SimulationSnapshot::bit(C::Resources)
        |SimulationSnapshot::bit(C::Occupancy)|SimulationSnapshot::bit(C::Areas)|SimulationSnapshot::bit(C::Visibility)|SimulationSnapshot::bit(C::Rules);
    const auto bytes=impl->storage.bytesCopied;
    // Private exact-event capture: never reuse/invalidate the game's completed observation.
    work->world=SimulationSnapshot::capture(game,SimulationSnapshot::captureCatalog(game),mask,nullptr,&impl->storage,false);
    metrics.captureBytes+=impl->storage.bytesCopied-bytes;
    if(inputReservation && impl->storage.bytesCopied-bytes>inputReservation)
        throw std::runtime_error("stratified snapshot exceeds conservative input reservation; dataset incomplete");
    work->owners=map.materialClaimLedger().entries();
    work->selectedSource=Uint32(source);work->pooledGoal=pooled;work->desiredExclusive=exclusive;work->desiredInfinite=yield.consumption==ResourceConsumption::Infinite;
    if(map.activeClaimsEnabled()) {
        if(work->owners.size()>Impl::MaximumTrackedClaimants)throw std::logic_error("claim census exceeds engine unit bound");
        work->claimants.reserve(work->owners.size());
        for(const auto& [owner,claim]:work->owners) {
            Impl::Claimant row;row.owner=owner;row.claim=claim;
            const auto gid=Uint16(owner);const auto team=gid<Impl::MaximumTrackedClaimants?Unit::GIDtoTeam(gid):Team::MAX_COUNT;
            const auto* claimant=team<game.teamsCount() && game.teams[team]?game.teams[team]->myUnits[Unit::GIDtoID(gid)]:nullptr;
            if(claimant && unitOwner(claimant->gid,claimant->scriptIdentity)==owner) {
                row.identityValid=true;row.team=team;row.position=Uint32(map.coordToIndex(claimant->posX,claimant->posY));row.task=claimant->materialClaimTask;
                row.claimMatches=claimant->activeMaterialClaim && *claimant->activeMaterialClaim==claim;
                if(row.claimMatches) {
                    auto at=impl->claimAges.find(owner);
                    if(at!=impl->claimAges.end() && (at->second.task!=row.task || at->second.claim!=claim)) {impl->claimAges.erase(at);at=impl->claimAges.end();}
                    if(at==impl->claimAges.end()) {
                        if(impl->claimAges.size()<Impl::MaximumTrackedClaimants)at=impl->claimAges.emplace(owner,Impl::ClaimAge{claim,row.task,game.stepCounter,false}).first;
                        else ++impl->claimAgeTrackingExcluded;
                    }
                    if(at!=impl->claimAges.end() && game.stepCounter>=at->second.since) {
                        row.ageAvailable=true;row.ageExact=at->second.exact;row.age=game.stepCounter-at->second.since;
                    }
                }
            }
            work->claimants.push_back(row);
        }
    }
    work->claimAgeTrackingExcluded=impl->claimAgeTrackingExcluded;
    work->field.assign(field,field+map.cellCount());
    work->destinations=destinations;
    work->reason=staleGoal?"stale_goal":exhaustedPool?"farm_reserve":"fully_claimed";
    work->metadataEnabled=map.materialDestinationsEnabled();
    work->claimsEnforced=map.activeClaimsEnabled();
    work->experimental=map.materialClaimPolicy()==ActiveClaimPolicy::Experimental;
    work->goalRefresh=work->experimental && unit.activeMaterialClaim.has_value();
    if(work->experimental) {
        const auto& config=map.materialClaimPolicyConfig();
        if(!impl->cachedPolicy || *impl->cachedPolicy!=config) {
            const auto canonical=config.canonicalJson();
            impl->cachedPolicy=config;impl->cachedPolicyJson=Json::parse(canonical);
            impl->cachedPolicyDigest=Online::Sha256::hex(canonical);
        }
        work->policyConfig=impl->cachedPolicyJson;work->policyDigest=impl->cachedPolicyDigest;
    }
    if(work->destinations && work->destinations->cells!=map.cellCount())
        throw std::logic_error("claim probe destination publication shape mismatch");
    work->query={unitOwner(unit.gid,unit.scriptIdentity),Uint32(map.coordToIndex(unit.posX,unit.posY)),Uint8(unit.destinationPurpose),
        {accessClass(unit.movementAccess()),unit.owner->me,unit.gid,OccupancyPolicy::Static}};
    if(work->deepSelected) {
        work->outgoingEnemies=unit.owner->enemies;work->observerVisibility=unit.owner->me;
        for(int team=0;team<game.teamsCount();++team)if(game.teams[team] && (game.teams[team]->attackableTeams()&unit.owner->me))
            work->incomingAttackers|=game.teams[team]->me;
    }
    work->pickup=pickupAccess!=UINT32_MAX;
    work->preTask=unit.materialClaimTask;work->tick=game.stepCounter; work->seed=seed; work->ordinal=ordinal; work->nearest=Uint32(access);
    work->capacity=capacity; work->claimed=map.materialClaimLedger().materialCount(Uint32(source),unit.destinationPurpose);
    Json fieldKey=nullptr, fieldGeneration=nullptr;
    for (const auto& plane:map.publishedResourceFields()) if (*plane.slot==field) {
        fieldKey=plane.key; fieldGeneration=plane.generation; break;
    }
    const auto properties=hasResource?map.resourcePropertiesByIndex(map.getResource(source).type):ResourceProperties{};
    if(!impl->config.deepStratified)collectNeighborhood();
    Uint32 effectiveReserve=reserve;
    for(const auto claimant:map.materialClaimLedger().ownersAtSource(Uint32(source))) {
        if(claimant==unitOwner(unit.gid,unit.scriptIdentity))continue;
        const auto* existing=map.materialClaimLedger().find(claimant);
        if(existing && existing->material==unit.destinationPurpose)effectiveReserve=std::max<Uint32>(effectiveReserve,existing->reserve);
    }
    work->features={{"resource_material_mask",hasResource?Json(properties.materialMask):Json(nullptr)},{"farm_areas_enabled",map.farmAreasEnabled()},
        {"nearby_forbidden_count",forbiddenNeighbors},
        {"nearby_forbidden_material_count",forbiddenMaterialNeighbors},{"nearby_forbidden_material_stock",forbiddenMaterialStock},
        {"nearby_forbidden_growing_material_count",growingForbiddenMaterialNeighbors},{"nearby_forbidden_growing_material_stock",growingForbiddenMaterialStock},
        {"resource_persists_when_empty",hasResource?Json(properties.persistsWhenEmpty):Json(nullptr)},
        {"resource_growth_rate",hasResource?Json(properties.growthRate):Json(nullptr)},{"team_units",unit.owner->liveUnits.size()},{"team_buildings",unit.owner->liveBuildings.size()},
        {"colony_count",game.teamsCount()},{"unit_type",unit.typeNum},{"hp",unit.hp},{"max_hp",unit.performance[HP]},
        {"hunger",unit.hungry},{"food_capacity",unit.foodCapacity()},{"medical",int(unit.medical)},
        {"activity",int(unit.activity)},{"displacement",int(unit.displacement)},{"job_purpose",int(unit.jobPurpose)},
        {"cargo_packets",unit.carriedPacketCount()},{"carried_material",unit.carriedMaterial},
        {"harvest_level",unit.level[HARVEST]},{"walk_speed",unit.performance[WALK]},
        {"swim_speed",unit.performance[SWIM]},{"fly_speed",unit.performance[FLY]},
        {"pooled_farm",pooled},{"exhausted_pool",exhaustedPool},{"source",source},{"resource_type",hasResource?Json(map.getResource(source).type):Json(nullptr)},
        {"yield_consumption",hasResource?Json(int(yield.consumption)):Json(nullptr)},{"infinite",hasResource?Json(yield.consumption==ResourceConsumption::Infinite):Json(nullptr)},
        {"destroys_deposit",hasResource?Json(yield.destroysDeposit):Json(nullptr)},{"stock_amount",amount},{"stock_reserve",reserve},{"effective_seed_floor",effectiveReserve},
        {"effective_capacity",!staleGoal && amount>effectiveReserve?amount-effectiveReserve:0},
        {"origin_field_value",field[work->query.origin]},{"nearest_field_value",field[access]},
        {"field_key",fieldKey},{"field_generation",fieldGeneration},
        {"consumer_gid",unit.attachedBuilding?Json(unit.attachedBuilding->gid):Json(nullptr)},
        {"consumer_identity",unit.attachedBuilding?Json(unit.attachedBuilding->scriptIdentity):Json(nullptr)}};
    if(inputReservation) {
        const auto actual=Impl::inputPayloadBytes(*work);
        if(actual>inputReservation)throw std::runtime_error("stratified input vector payload exceeds reservation; dataset incomplete");
        impl->peakInputPayload=std::max(impl->peakInputPayload,actual);
    }
    auto* raw=work.get(); impl->pending.push_back(std::move(work));
    const ComputeExecutor::Group group{1,{Impl::run,raw},ComputeExecutor::NoLane};
    try { raw->batch=impl->executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(Uint64(game.stepCounter)+1),
        {ComputeProducer::Other,game.stepCounter,Uint64(game.stepCounter)+1}); }
    catch(...) {impl->pending.pop_back();throw;}
    ++metrics.submitted;
    return {ordinal,Uint32(access)};
}
}
