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

#include "ClaimMixedPatterns.h"
TEST_CASE("mixed policy binds frame asset and retains one cursor per typed entry" * doctest::test_suite("ClaimPolicyConfig"))
{
    ClaimPolicyConfig config;config.sourceVersion=5;config.pattern=std::string(ResourceClaims::Mixed::IllustrativeName);
    for(auto frame:{ClaimPolicyConfig::PatternFrame::MapAxes,ClaimPolicyConfig::PatternFrame::NearestD4}) {
        config.patternFrame=frame;const auto text=config.canonicalJson();
        CHECK(ClaimPolicyConfig::parse(text)==config);
        CHECK(config.maximumRecoveryCandidateCount()==13);
        CHECK(config.recoveryCandidateCount(13)==13);
        auto wire=nlohmann::json::parse(text);wire["direct_resource_probes"]=false;
        CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        wire=nlohmann::json::parse(text);wire.erase("pattern_frame");CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        wire=nlohmann::json::parse(text);wire["pattern_format"]="mixed-v2";CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        wire=nlohmann::json::parse(text);wire["selection"]="closest_geometric";CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
    }
    auto d4=nlohmann::json::parse(config.canonicalJson());config.patternFrame=ClaimPolicyConfig::PatternFrame::MapAxes;
    const auto mapaxes=nlohmann::json::parse(config.canonicalJson());CHECK(d4["pattern_digest"]!=mapaxes["pattern_digest"]);
    d4["pattern_frame"]="mapaxes";CHECK_THROWS(ClaimPolicyConfig::parse(d4.dump()));
    for(unsigned version:{1u,2u,3u,4u}) {
        ClaimPolicyConfig legacy;legacy.sourceVersion=version;
        auto wire=nlohmann::json::parse(legacy.canonicalJson());wire["pattern_format"]="mixed-v1";
        CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
    }
}
TEST_CASE("mixed D4 frame matches wrapped signed displacement and precomputed views" * doctest::test_suite("ClaimPolicyConfig"))
{
    using namespace ResourceClaims::Mixed;
    for(unsigned origin:{0u,7u,31u,511u,1023u})for(unsigned nearest=0;nearest<1024;++nearest) {
        int dx=int(nearest%32)-int(origin%32),dy=int(nearest/32)-int(origin/32);
        if(dx>=16)dx-=32;
        if(dx< -16)dx+=32;
        if(dy>=16)dy-=32;
        if(dy< -16)dy+=32;
        const auto expected=unsigned(dx<0)|(unsigned(dy<0)<<1)|(unsigned(std::abs(dy)>std::abs(dx))<<2);
        CHECK(frame(32,32,origin,nearest)==expected);
    }
    CHECK(frame(32,32,17,UINT32_MAX)==0);CHECK(frame(32,32,17,17)==0);
    CHECK(frame(32,64,0,16)==1); // Negative half-width tie.
    CHECK(frame(32,64,0,32*32)==6); // Negative half-height, swapped major axis.
    CHECK(frame(32,64,31+63*32,0)==0); // Wrapped positive diagonal, no swap.
    CHECK(frame(16,64,0,15+2*16)==5); // dx=-1, dy=2.
    for(unsigned f=0;f<8;++f) {
        const auto v=view(IllustrativeName,f);REQUIRE(v.x.size()==13);
        for(unsigned i=0;i<13;++i) {
            int x=(f&4)?Illustrative[i].y:Illustrative[i].x,y=(f&4)?Illustrative[i].x:Illustrative[i].y;
            CHECK(v.x[i]==((f&1)?-x:x));CHECK(v.y[i]==((f&2)?-y:y));CHECK(v.tag[i]==Illustrative[i].tag);
        }
    }
}

TEST_CASE("learned research assets bind exact trained sequence and typed cursor count" * doctest::test_suite("ClaimPolicyConfig"))
{
    struct Model {const char* name; const char* digest; unsigned entries;};
    const std::array<Model,3> models{{
        {"discovery3424-r24-support4-v1","58ff1585709e4ee43375df51db5189e8f61898fec229ca22f34b7e5b68056ad6",21},
        {"discovery3424-r32-support4-v1","a40c126ef9f29de1a200923825325988772f245b0d018308046e82c1822fe785",22},
        {"final-r32-exact24-assembled-v1","44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c",24}
    }};
    for(const auto& model:models) {
        ClaimPolicyConfig config; config.sourceVersion=5; config.pattern=model.name;
        config.patternFrame=ClaimPolicyConfig::PatternFrame::NearestD4;
        const auto canonical=nlohmann::json::parse(config.canonicalJson());
        CHECK(canonical.at("pattern_digest")==model.digest);
        CHECK(config.maximumRecoveryCandidateCount()==model.entries);
        CHECK(ClaimPolicyConfig::parse(canonical.dump())==config);
        for(unsigned frame=0;frame<8;++frame) {
            const auto view=ResourceClaims::Mixed::view(model.name,frame);
            REQUIRE(view.x.size()==model.entries);
            for(auto tag:view.tag)CHECK(tag<=3);
        }
        CHECK(ResourceClaims::Mixed::view(model.name,8).x.empty());
    }
}

TEST_CASE("optional refresh margin and terrain prefilter keep every existing canonical identity" * doctest::test_suite("ClaimPolicyConfig"))
{
    std::vector<ClaimPolicyConfig> existing;
    for(unsigned version:{1u,2u,3u,4u}) {ClaimPolicyConfig c;c.sourceVersion=version;existing.push_back(c);}
    {ClaimPolicyConfig c;c.sourceVersion=5;c.pattern="final-r32-exact24-assembled-v1";
        c.patternFrame=ClaimPolicyConfig::PatternFrame::NearestD4;c.destinationRefreshJ=12;existing.push_back(c);}
    for(const auto& config:existing) {
        CAPTURE(config.sourceVersion);
        const auto canonical=config.canonicalJson();
        CHECK(canonical.find("refresh_switch_margin_permille")==std::string::npos);
        CHECK(canonical.find("terrain_component_prefilter")==std::string::npos);
        // Explicit defaults are accepted and canonicalize to the same identity.
        auto wire=nlohmann::json::parse(canonical);
        wire["refresh_switch_margin_permille"]=0;wire["terrain_component_prefilter"]=false;
        const auto explicitDefaults=ClaimPolicyConfig::parse(wire.dump());
        CHECK(explicitDefaults==config);CHECK(explicitDefaults.digest()==config.digest());
        CHECK(explicitDefaults.canonicalJson()==canonical);
        for(unsigned margin:{1u,250u,900u}) {
            wire=nlohmann::json::parse(canonical);wire["refresh_switch_margin_permille"]=margin;
            const auto parsed=ClaimPolicyConfig::parse(wire.dump());
            CHECK(parsed.refreshSwitchMarginPermille==margin);CHECK(parsed.digest()!=config.digest());
            CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
            CHECK(nlohmann::json::parse(parsed.canonicalJson()).at("refresh_switch_margin_permille")==margin);
        }
        wire=nlohmann::json::parse(canonical);wire["terrain_component_prefilter"]=true;
        const auto prefilter=ClaimPolicyConfig::parse(wire.dump());
        CHECK(prefilter.terrainComponentPrefilter);CHECK(prefilter.digest()!=config.digest());
        CHECK(ClaimPolicyConfig::parse(prefilter.canonicalJson())==prefilter);
        for(const auto& invalid:{nlohmann::json(901),nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json(true),nlohmann::json("10"),nlohmann::json(nullptr)}) {
            wire=nlohmann::json::parse(canonical);wire["refresh_switch_margin_permille"]=invalid;
            CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
        for(const auto& invalid:{nlohmann::json(1),nlohmann::json(0),nlohmann::json("true"),nlohmann::json(nullptr)}) {
            wire=nlohmann::json::parse(canonical);wire["terrain_component_prefilter"]=invalid;
            CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
        auto outOfRange=config;outOfRange.refreshSwitchMarginPermille=901;CHECK_THROWS(outOfRange.validate());
    }
    // Digests recorded from the build before these options existed.
    CHECK(existing[1].digest()=="ecd0f62776a44cc849227ded464b60e3032d4de9549e92164f176406fe251c4b");
}

TEST_CASE("claim oversubscription keeps every existing canonical identity and has a strict domain" * doctest::test_suite("ClaimPolicyConfig"))
{
    std::vector<ClaimPolicyConfig> existing;
    for(unsigned version:{1u,2u,3u,4u}) {ClaimPolicyConfig c;c.sourceVersion=version;existing.push_back(c);}
    {ClaimPolicyConfig c;c.sourceVersion=5;c.pattern="final-r32-exact24-assembled-v1";
        c.patternFrame=ClaimPolicyConfig::PatternFrame::NearestD4;c.destinationRefreshJ=12;c.opportunisticAdjacentPickup=true;existing.push_back(c);}
    for(const auto& config:existing) {
        CAPTURE(config.sourceVersion);
        CHECK(config.claimOversubscriptionPermille==1000);
        const auto canonical=config.canonicalJson();
        CHECK(canonical.find("claim_oversubscription_permille")==std::string::npos);
        // Explicit 1000 is the default and canonicalizes to the same identity.
        auto wire=nlohmann::json::parse(canonical);wire["claim_oversubscription_permille"]=1000;
        const auto exact=ClaimPolicyConfig::parse(wire.dump());
        CHECK(exact==config);CHECK(exact.digest()==config.digest());CHECK(exact.canonicalJson()==canonical);
        for(unsigned permille:{1001u,2000u,3000u,100000u}) {
            wire=nlohmann::json::parse(canonical);wire["claim_oversubscription_permille"]=permille;
            const auto parsed=ClaimPolicyConfig::parse(wire.dump());
            CHECK(parsed.claimOversubscriptionPermille==permille);CHECK(parsed.digest()!=config.digest());
            CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
            CHECK(nlohmann::json::parse(parsed.canonicalJson()).at("claim_oversubscription_permille")==permille);
        }
        wire=nlohmann::json::parse(canonical);wire["claim_oversubscription_permille"]="unlimited";
        const auto unlimited=ClaimPolicyConfig::parse(wire.dump());
        CHECK(unlimited.claimOversubscriptionPermille==ClaimPolicyConfig::UnlimitedClaimOversubscription);
        CHECK(unlimited.digest()!=config.digest());CHECK(ClaimPolicyConfig::parse(unlimited.canonicalJson())==unlimited);
        CHECK(nlohmann::json::parse(unlimited.canonicalJson()).at("claim_oversubscription_permille")=="unlimited");
        for(const auto& invalid:{nlohmann::json(999),nlohmann::json(0),nlohmann::json(100001),nlohmann::json(-1),
            nlohmann::json(4294967295u),nlohmann::json(1500.0),nlohmann::json(true),nlohmann::json("2000"),
            nlohmann::json("Unlimited"),nlohmann::json(nullptr)}) {
            CAPTURE(invalid.dump());
            wire=nlohmann::json::parse(canonical);wire["claim_oversubscription_permille"]=invalid;
            CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
        for(unsigned bad:{0u,999u,100001u,4294967294u}) {auto c=config;c.claimOversubscriptionPermille=bad;CHECK_THROWS(c.validate());}
    }
    // Digests recorded before this option existed (source 2 default; source 5
    // production policy as reported by game run).
    CHECK(existing[1].digest()=="ecd0f62776a44cc849227ded464b60e3032d4de9549e92164f176406fe251c4b");
    CHECK(existing[4].digest()=="362f54bc04bd81fb1120c727cbb6413348f0dbd1fa63278d61e1728503c3e9f0");
}
namespace
{
ClaimPolicyConfig productionPolicy()
{
    ClaimPolicyConfig c;c.sourceVersion=5;c.pattern="final-r32-exact24-assembled-v1";
    c.patternFrame=ClaimPolicyConfig::PatternFrame::NearestD4;c.destinationRefreshJ=12;c.opportunisticAdjacentPickup=true;
    return c;
}
void checkExistingIdentities()
{
    // Digests recorded before the overhead experiments existed.
    CHECK(ClaimPolicyConfig{}.digest()=="ecd0f62776a44cc849227ded464b60e3032d4de9549e92164f176406fe251c4b");
    CHECK(productionPolicy().digest()=="362f54bc04bd81fb1120c727cbb6413348f0dbd1fa63278d61e1728503c3e9f0");
}
}
TEST_CASE("pending gradient step is a new canonical value that leaves existing identities unchanged" * doctest::test_suite("ClaimPolicyConfig"))
{
    checkExistingIdentities();
    for(const auto& base:{ClaimPolicyConfig{},productionPolicy()}) {
        CHECK(base.pendingStep==ClaimPolicyConfig::PendingStep::Wait);
        auto wire=nlohmann::json::parse(base.canonicalJson());wire["pending_step"]="gradient";
        const auto parsed=ClaimPolicyConfig::parse(wire.dump());
        CHECK(parsed.pendingStep==ClaimPolicyConfig::PendingStep::Gradient);
        CHECK(parsed.digest()!=base.digest());CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
        CHECK(nlohmann::json::parse(parsed.canonicalJson()).at("pending_step")=="gradient");
        for(const char* bad:{"Gradient","redecide","gradient_step",""}) {
            CAPTURE(bad);wire["pending_step"]=bad;CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
    }
}
TEST_CASE("claim distance factor is omitted at its default and has a strict domain" * doctest::test_suite("ClaimPolicyConfig"))
{
    checkExistingIdentities();
    for(const auto& base:{ClaimPolicyConfig{},productionPolicy()}) {
        CHECK(base.maxClaimDistanceFactorPermille==0);
        const auto canonical=base.canonicalJson();
        CHECK(canonical.find("max_claim_distance_factor_permille")==std::string::npos);
        auto wire=nlohmann::json::parse(canonical);wire["max_claim_distance_factor_permille"]=0;
        CHECK(ClaimPolicyConfig::parse(wire.dump()).digest()==base.digest());
        for(unsigned permille:{1000u,2500u,100000u}) {
            wire["max_claim_distance_factor_permille"]=permille;
            const auto parsed=ClaimPolicyConfig::parse(wire.dump());
            CHECK(parsed.maxClaimDistanceFactorPermille==permille);CHECK(parsed.digest()!=base.digest());
            CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
            CHECK(nlohmann::json::parse(parsed.canonicalJson()).at("max_claim_distance_factor_permille")==permille);
        }
        for(const auto& invalid:{nlohmann::json(1),nlohmann::json(999),nlohmann::json(100001),nlohmann::json(-1),
            nlohmann::json(1500.0),nlohmann::json(true),nlohmann::json("2000"),nlohmann::json(nullptr)}) {
            CAPTURE(invalid.dump());wire["max_claim_distance_factor_permille"]=invalid;
            CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
        for(unsigned bad:{1u,999u,100001u}) {auto c=base;c.maxClaimDistanceFactorPermille=bad;CHECK_THROWS(c.validate());}
    }
}
TEST_CASE("traffic sidestep and wait limit are omitted at defaults and have strict domains" * doctest::test_suite("ClaimPolicyConfig"))
{
    checkExistingIdentities();
    for(const auto& base:{ClaimPolicyConfig{},productionPolicy()}) {
        CHECK_FALSE(base.trafficSidestep);CHECK(base.trafficWaitLimit==0);
        const auto canonical=base.canonicalJson();
        CHECK(canonical.find("traffic_")==std::string::npos);
        auto wire=nlohmann::json::parse(canonical);wire["traffic_sidestep"]=false;wire["traffic_wait_limit"]=0;
        CHECK(ClaimPolicyConfig::parse(wire.dump()).digest()==base.digest());
        wire["traffic_sidestep"]=true;
        const auto sidestep=ClaimPolicyConfig::parse(wire.dump());
        CHECK(sidestep.trafficSidestep);CHECK(sidestep.digest()!=base.digest());CHECK(ClaimPolicyConfig::parse(sidestep.canonicalJson())==sidestep);
        wire["traffic_sidestep"]=false;
        for(unsigned limit:{1u,8u,64u}) {
            wire["traffic_wait_limit"]=limit;const auto parsed=ClaimPolicyConfig::parse(wire.dump());
            CHECK(parsed.trafficWaitLimit==limit);CHECK(parsed.digest()!=base.digest());
            CHECK(nlohmann::json::parse(parsed.canonicalJson()).at("traffic_wait_limit")==limit);
            CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
        }
        for(const auto& invalid:{nlohmann::json(65),nlohmann::json(-1),nlohmann::json(2.0),nlohmann::json(true),nlohmann::json("4")}) {
            CAPTURE(invalid.dump());wire["traffic_wait_limit"]=invalid;CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
        wire["traffic_wait_limit"]=0;
        for(const auto& invalid:{nlohmann::json(1),nlohmann::json("true"),nlohmann::json(nullptr)}) {
            CAPTURE(invalid.dump());wire["traffic_sidestep"]=invalid;CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
        }
        auto c=base;c.trafficWaitLimit=65;CHECK_THROWS(c.validate());
    }
}
TEST_CASE("gradient fallback is a new canonical value that leaves existing identities unchanged" * doctest::test_suite("ClaimPolicyConfig"))
{
    checkExistingIdentities();
    for(const auto& base:{ClaimPolicyConfig{},productionPolicy()}) {
        auto wire=nlohmann::json::parse(base.canonicalJson());wire["fallback"]="gradient";
        const auto parsed=ClaimPolicyConfig::parse(wire.dump());
        CHECK(parsed.fallback==ClaimPolicyConfig::Fallback::Gradient);CHECK(parsed.digest()!=base.digest());
        CHECK(nlohmann::json::parse(parsed.canonicalJson()).at("fallback")=="gradient");
        CHECK(ClaimPolicyConfig::parse(parsed.canonicalJson())==parsed);
        wire["fallback"]="Gradient";CHECK_THROWS(ClaimPolicyConfig::parse(wire.dump()));
    }
}
