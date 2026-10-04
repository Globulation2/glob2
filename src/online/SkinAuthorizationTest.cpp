// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "SkinAuthorization.h"
#include "SwarmMeshCatalog.h"
#include <nlohmann/json.hpp>
#include <limits>
using namespace Online;
namespace {
nlohmann::json fixture()
{
    return nlohmann::json::parse(glob2test::readFile(glob2test::sourceRoot()/"test/fixtures/skins/authorization.json"));
}
}
TEST_SUITE("SkinAuthorization")
{
    TEST_CASE("server-format signatures authorize only their bound appearance")
    {
        const auto f=fixture();
        SkinAuthorization auth(f["valid"],f["jwks"].dump(),"https://example.test",f["claims"]["matchId"],2,1700000000);
        REQUIRE(auth.state()==SkinAuthorization::State::Verified);
        REQUIRE(auth.skin());
        CHECK(auth.skin()->buildingColor==0x112233);
        CHECK(auth.skin()->team==2);
        CHECK(auth.skin()->textureHash==f["claims"]["version"]["textureSha256"].get<std::string>());
        for(const auto &entry:f["invalid"].items())
        {
            INFO(entry.key());
            SkinAuthorization invalid(entry.value(),f["jwks"].dump(),"https://example.test",f["claims"]["matchId"],2,1700000000);
            CHECK(invalid.state()==SkinAuthorization::State::Rejected);
            CHECK(invalid.skin()==nullptr);
        }
    }
    TEST_CASE("the chosen swarm mesh is part of the signed manifest")
    {
        const auto f=fixture();
        const auto mesh=[&](const std::string &token){
            SkinAuthorization auth(token,f["jwks"].dump(),"https://example.test",f["claims"]["matchId"],2,1700000000);
            REQUIRE(auth.state()==SkinAuthorization::State::Verified);
            return auth.skin()->swarmMesh;
        };
        // Skins from before mesh choice carry no swarmMesh and keep the classic swarm.
        CHECK(mesh(f["valid"])==0);
        CHECK(mesh(f["swarm"]["classic"])==0);
        CHECK(SWARM_MESHES[mesh(f["swarm"]["crown"])].id=="crown");
    }
    TEST_CASE("untrusted key sets and bounded malformed inputs fail closed")
    {
        auto f=fixture();
        const auto check=[&](const std::string &token,const std::string &keys,std::int64_t now=1700000000){
            SkinAuthorization auth(token,keys,"https://example.test",f["claims"]["matchId"],2,now);
            CHECK(auth.state()==SkinAuthorization::State::Rejected);
            CHECK(auth.skin()==nullptr);
        };
        check(f["valid"],"{}");check(f["valid"],"not json");
        check("..",f["jwks"].dump());check(std::string(8193,'a'),f["jwks"].dump());
        check(f["valid"],std::string(65537,' '));
        check(f["valid"],f["jwks"].dump(),std::numeric_limits<std::int64_t>::max());
        check(f["valid"],f["jwks"].dump(),-1);
        f["jwks"]["keys"].push_back(f["jwks"]["keys"][0]);
        check(f["valid"],f["jwks"].dump());
    }
}
