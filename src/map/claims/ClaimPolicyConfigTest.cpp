// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ClaimPolicyConfig.h"
#include <nlohmann/json.hpp>
using ResourceClaims::ClaimPolicyConfig;
TEST_SUITE("ClaimPolicyConfig")
{
TEST_CASE("explicit policy JSON roundtrips canonical defaults and immutable asset identity")
{
    const auto defaults=ClaimPolicyConfig::parse(R"({"source_version":1})");
    ClaimPolicyConfig legacy;legacy.sourceVersion=1;CHECK(defaults==legacy);
    CHECK_FALSE(defaults.usesRecovery());
    CHECK(defaults.destinationRefreshJ==0);CHECK(defaults.routeRefreshJ==0);
    CHECK_FALSE(defaults.usePrimaryGradient);
    const auto canonical=defaults.canonicalJson();
    CHECK(ClaimPolicyConfig::parse(canonical)==defaults);
    CHECK(defaults.digest().size()==64);
    auto changed=nlohmann::json::parse(canonical);
    changed["fallback"]="roam";
    CHECK(ClaimPolicyConfig::parse(changed.dump()).digest()!=defaults.digest());
    changed["pattern_digest"]=std::string(64,'0');
    CHECK_THROWS(ClaimPolicyConfig::parse(changed.dump()));
}
TEST_CASE("live refresh grid and bounds reject silent parameter coercion")
{
    for(unsigned j:{0,1,2,3,4,6,8,12,16,24,32,48,64,96,128}) {
        const auto value=ClaimPolicyConfig::parse(nlohmann::json{{"source_version",1},{"destination_refresh_j",j},{"route_refresh_j",j}}.dump());
        CHECK(value.destinationRefreshJ==j);CHECK(value.routeRefreshJ==j);
    }
    for(const auto& invalid:{R"({})",R"({"source_version":2})",R"({"source_version":true})",
        R"({"source_version":1,"route_refresh_j":5})",R"({"source_version":1,"route_refresh_j":192})",
        R"({"source_version":1,"destination_refresh_j":-1})",R"({"source_version":1,"maximum_expansions":4097})",
        R"({"source_version":1,"maximum_expansions":0})",R"({"source_version":1,"route_refresh_j":1.0})",
        R"({"source_version":1,"fallback_after_decisions":0})",R"({"source_version":1,"use_primary_gradient":1})",
        R"({"source_version":1,"selection":"oracle_best"})",R"({"source_version":1,"pattern":"unknown"})",
        R"({"source_version":1,"typo":1})"}) CHECK_THROWS(ClaimPolicyConfig::parse(invalid));
}
TEST_CASE("all explicit fallback and routing choices remain represented in policy digest")
{
    const auto policy=ClaimPolicyConfig::parse(R"({"source_version":1,"selection":"closest_geometric","fallback":"bounded_wait_release","fallback_after_decisions":24,"route_target":"probe_then_gradient","pending_step":"direct_heading","destination_refresh_j":6,"route_refresh_j":12,"maximum_expansions":512,"use_primary_gradient":true})");
    CHECK(policy.selection==ClaimPolicyConfig::Selection::ClosestGeometric);
    CHECK(policy.fallback==ClaimPolicyConfig::Fallback::BoundedWaitRelease);
    CHECK(policy.routeTarget==ClaimPolicyConfig::RouteTarget::ProbeThenGradient);
    CHECK(policy.pendingStep==ClaimPolicyConfig::PendingStep::DirectHeading);
    CHECK(ClaimPolicyConfig::parse(policy.canonicalJson())==policy);
}
}

TEST_CASE("version two recovery identity is explicit strict and preserves legacy canonical bytes" * doctest::test_suite("ClaimPolicyConfig"))
{
    const ClaimPolicyConfig config;
    CHECK(config.usesRecovery());
    CHECK(ClaimPolicyConfig::parse(config.canonicalJson())==config);
    auto json=nlohmann::json::parse(config.canonicalJson());
    for(const char* key:{"recovery_source_version","recovery_cache_entries","recovery_ttl_ticks","recovery_attempt_window_ticks","recovery_max_attempts","recovery_backoff_ticks","recovery_selection"}) {
        auto missing=json;missing.erase(key);CHECK_THROWS(ClaimPolicyConfig::parse(missing.dump()));
        auto wrong=json;wrong[key]=true;CHECK_THROWS(ClaimPolicyConfig::parse(wrong.dump()));
    }
    const auto legacy=ClaimPolicyConfig::parse(R"({"source_version":1})");
    CHECK(legacy.canonicalJson().find("recovery_")==std::string::npos);
    CHECK(legacy.digest()!=config.digest());
}

TEST_CASE("adjacent pickup exchange is an explicit version three policy" * doctest::test_suite("ClaimPolicyConfig"))
{
    const ClaimPolicyConfig baseline;
    CHECK(baseline.sourceVersion==2);
    CHECK(baseline.digest()=="ecd0f62776a44cc849227ded464b60e3032d4de9549e92164f176406fe251c4b");
    auto json=nlohmann::json::parse(baseline.canonicalJson());
    CHECK_FALSE(json.contains("opportunistic_adjacent_pickup"));
    json["source_version"]=3;
    CHECK_THROWS(ClaimPolicyConfig::parse(json.dump()));
    for(bool enabled:{false,true}) {
        json["opportunistic_adjacent_pickup"]=enabled;
        const auto config=ClaimPolicyConfig::parse(json.dump());
        CHECK(config.usesRecovery());CHECK(config.opportunisticAdjacentPickup==enabled);
        CHECK(ClaimPolicyConfig::parse(config.canonicalJson())==config);
        CHECK(config.digest()!=baseline.digest());
    }
    for(const auto& invalid:{nlohmann::json(1),nlohmann::json("true"),nlohmann::json(nullptr)}) {
        json["opportunistic_adjacent_pickup"]=invalid;CHECK_THROWS(ClaimPolicyConfig::parse(json.dump()));
    }
    for(unsigned version:{1u,2u,4u}) {
        json["source_version"]=version;json["opportunistic_adjacent_pickup"]=false;
        CHECK_THROWS(ClaimPolicyConfig::parse(json.dump()));
    }
}

TEST_CASE("direct resource probes require explicit source four and preserve prior identities" * doctest::test_suite("ClaimPolicyConfig"))
{
    ClaimPolicyConfig config;CHECK(config.sourceVersion==2);CHECK_FALSE(config.directResourceProbes);
    auto wire=nlohmann::json::parse(config.canonicalJson());
    wire["source_version"]=4;wire["opportunistic_adjacent_pickup"]=true;
    CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
    for(bool enabled:{false,true}) {
        wire["direct_resource_probes"]=enabled;
        const auto parsed=ClaimPolicyConfig::parse(wire.dump());CHECK(parsed.directResourceProbes==enabled);
        CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
    }
    for(auto invalid:{nlohmann::json(1),nlohmann::json("true"),nlohmann::json(nullptr)}) {
        wire["direct_resource_probes"]=invalid;CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
    }
    wire["direct_resource_probes"]=false;
    for(unsigned version:{1u,2u,3u}) {wire["source_version"]=version;CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));}
    wire["source_version"]=4;wire.erase("opportunistic_adjacent_pickup");CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
}

TEST_CASE("paired raw candidate cursor count is gated and overflow checked" * doctest::test_suite("ClaimPolicyConfig"))
{
    ClaimPolicyConfig config;
    CHECK(config.recoveryCandidateCount(17)==17);
    config.sourceVersion=3;CHECK(config.recoveryCandidateCount(17)==17);
    config.sourceVersion=4;CHECK(config.recoveryCandidateCount(17)==17);
    config.directResourceProbes=true;CHECK(config.recoveryCandidateCount(17)==34);
    CHECK_THROWS(config.recoveryCandidateCount(std::size_t(UINT32_MAX)/2+1));
    CHECK(config.maximumRecoveryCandidateCount()>0);
}
