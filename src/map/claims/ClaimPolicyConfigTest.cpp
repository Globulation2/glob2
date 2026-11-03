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

TEST_CASE("version two recovery identity is explicit strict and preserves legacy canonical bytes")
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
