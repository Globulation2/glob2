// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "SkinDownloads.h"
#include "OnlineFakes.h"
#include "OnlineStorage.h"
#include "Sha256.h"
using namespace Online;
namespace {
struct DownloadFixture {
    nlohmann::json f=nlohmann::json::parse(glob2test::readFile(glob2test::sourceRoot()/"test/fixtures/skins/authorization.json"));
    MemoryStorage storage;
    OnlineFakes::Http http;
    std::string texture;
    DownloadFixture(){const auto hex=f["textureHex"].get<std::string>();for(std::size_t i=0;i<hex.size();i+=2)texture.push_back(static_cast<char>(std::stoul(hex.substr(i,2),nullptr,16)));}
    std::unique_ptr<SkinDownloads> loader(std::string token={}){
        return std::make_unique<SkinDownloads>(storage,"https://example.test",f["claims"]["matchId"],std::vector<SkinDownloads::Ticket>{{2,token.empty()?f["valid"].get<std::string>():token}},[this](auto r){return http.start(std::move(r));});
    }
    void keys(){auto request=http.pending("jwks.json");REQUIRE(request);CHECK(request->request.responseLimit==65536);request->reply(200,f["jwks"]);}
    std::string path(){return "online/skins/"+f["claims"]["version"]["textureSha256"].get<std::string>()+".png";}
};
}
TEST_SUITE("SkinDownloads") {
    TEST_CASE("verified texture downloads are cached and reused after fresh authorization"){
        DownloadFixture f;auto loader=f.loader();
        loader->poll(1700000000);CHECK(loader->takeReady().empty());CHECK_FALSE(loader->done());
        f.keys();loader->poll(1700000000);
        auto request=f.http.pending("/texture");REQUIRE(request);CHECK(request->request.responseLimit==256*1024);
        request->replyRaw(200,f.texture);loader->poll(1700000000);
        auto ready=loader->takeReady();REQUIRE(ready.size()==1);CHECK(ready[0].path==f.path());CHECK(ready[0].skin.buildingColor==0x112233);CHECK(loader->done());
        CHECK(f.storage.files.at(f.path())==f.texture);
        auto again=f.loader();f.keys();again->poll(1700000000);CHECK(again->takeReady().size()==1);CHECK(f.http.count("/texture")==1);
    }
    TEST_CASE("corrupt cache and network bytes cannot reach the renderer"){
        DownloadFixture f;f.storage.files[f.path()]="corrupt";
        auto loader=f.loader();f.keys();loader->poll(1700000000);
        CHECK(f.storage.files.count(f.path())==0);
        auto request=f.http.pending("/texture");REQUIRE(request);
        auto bytes=f.texture;bytes.back()^=1;request->replyRaw(200,bytes);loader->poll(1700000000);
        CHECK(loader->done());CHECK(loader->takeReady().empty());CHECK(f.storage.files.count(f.path())==0);
    }
    TEST_CASE("invalid authorization does not download a texture"){
        DownloadFixture f;auto loader=f.loader(f.f["invalid"]["signature"]);f.keys();loader->poll(1700000000);
        CHECK(loader->done());CHECK(loader->takeReady().empty());CHECK(f.http.count("/texture")==0);
    }
    TEST_CASE("failed key fetch falls back without waiting for a texture"){
        DownloadFixture f;auto loader=f.loader();f.http.pending("jwks.json")->replyRaw(503,"unavailable");loader->poll(1700000000);
        CHECK(loader->done());CHECK(loader->takeReady().empty());CHECK(f.http.count("/texture")==0);
    }
    TEST_CASE("download concurrency is bounded and ambiguous teams fail closed"){
        DownloadFixture f;
        std::vector<SkinDownloads::Ticket> tickets;
        for(int team=0;team<8;++team)tickets.push_back({team,f.f["teams"][team]});
        SkinDownloads loader(f.storage,"https://example.test",f.f["claims"]["matchId"],tickets,[&](auto r){return f.http.start(std::move(r));});
        f.keys();loader.poll(1700000000);
        CHECK(f.http.count("/texture")==4);
        loader.poll(1700000000);CHECK(f.http.count("/texture")==4);
        for(int i=0;i<4;++i)f.http.pending("/texture")->replyRaw(200,f.texture);
        loader.poll(1700000000);
        CHECK(loader.done());CHECK(loader.takeReady().size()==8);
        CHECK(f.http.count("/texture")==4); // remaining teams reuse the verified cache
        tickets.push_back(tickets.front());
        SkinDownloads duplicate(f.storage,"https://example.test",f.f["claims"]["matchId"],tickets,[&](auto r){return f.http.start(std::move(r));});
        CHECK(duplicate.done());CHECK(duplicate.takeReady().empty());
    }

    TEST_CASE("cache eviction bounds retained assets while preserving current appearances"){
        DownloadFixture f;
        for(int i=0;i<80;++i)f.storage.files["online/skins/"+Sha256::hex(std::to_string(i))+".png"]="old";
        auto loader=f.loader();f.keys();loader->poll(1700000000);
        f.http.pending("/texture")->replyRaw(200,f.texture);loader->poll(1700000000);
        CHECK(loader->takeReady().size()==1);CHECK(f.storage.files.size()==64);
        CHECK(f.storage.files.at(f.path())==f.texture);
    }

    TEST_CASE("moderation removes cached appearances and restoration verifies them again"){
        DownloadFixture f;f.storage.files[f.path()]=f.texture;
        auto loader=f.loader();f.keys();loader->poll(1700000000);
        REQUIRE(loader->takeReady().size()==1);
        loader->poll(1700000059);CHECK(f.http.count("/skins")==0);
        loader->poll(1700000060);
        auto snapshot=f.http.pending("/skins");REQUIRE(snapshot);
        CHECK(snapshot->request.responseLimit==512*1024);
        snapshot->reply(200,{{"colonySkins",nlohmann::json::array()}});loader->poll(1700000060);
        CHECK(loader->takeRemoved()==std::vector<int>{2});
        loader->poll(1700000061); // complete the empty batch
        loader->poll(1700000120);
        snapshot=f.http.pending("/skins");REQUIRE(snapshot);
        snapshot->reply(200,{{"colonySkins",nlohmann::json::array({{{"team",2},{"assertion",f.f["valid"]}}})}});
        loader->poll(1700000120);f.keys();loader->poll(1700000121);
        REQUIRE(loader->takeReady().size()==1);
        CHECK(f.http.count("/texture")==0);
        CHECK(loader->takeRemoved().empty());
    }
    TEST_CASE("failed or malformed refresh keeps valid paint but expiry removes it"){
        DownloadFixture f;f.storage.files[f.path()]=f.texture;
        auto loader=f.loader();f.keys();loader->poll(1700000000);
        REQUIRE(loader->takeReady().size()==1);
        loader->poll(1700000060);f.http.pending("/skins")->replyRaw(503,"offline");loader->poll(1700000060);
        CHECK(loader->takeRemoved().empty());
        loader->poll(1700000120);
        f.http.pending("/skins")->reply(200,{{"colonySkins",nlohmann::json::array({{{"team",33},{"assertion","bad"}}})}});loader->poll(1700000120);
        CHECK(loader->takeRemoved().empty());
        const auto expiry=f.f["claims"]["exp"].get<std::int64_t>();
        loader->poll(expiry);CHECK(loader->takeRemoved()==std::vector<int>{2});
        loader->poll(expiry+1);CHECK(loader->takeRemoved().empty());
    }
    TEST_CASE("texture completing after authorization expiry is never installed"){
        DownloadFixture f;auto loader=f.loader();f.keys();loader->poll(1700000000);
        auto texture=f.http.pending("/texture");REQUIRE(texture);texture->replyRaw(200,f.texture);
        loader->poll(f.f["claims"]["exp"].get<std::int64_t>());
        CHECK(loader->takeReady().empty());CHECK(loader->done());
    }

    TEST_CASE("initial downloads finish before a newer moderation snapshot starts"){
        DownloadFixture f;auto loader=f.loader();f.keys();loader->poll(1700000000);
        loader->poll(1700000060);CHECK(f.http.count("/skins")==0);
        f.http.pending("/texture")->replyRaw(200,f.texture);loader->poll(1700000061);
        REQUIRE(loader->takeReady().size()==1);
        loader->poll(1700000062);auto snapshot=f.http.pending("/skins");REQUIRE(snapshot);
        snapshot->reply(200,{{"colonySkins",nlohmann::json::array()}});loader->poll(1700000062);
        CHECK(loader->takeRemoved()==std::vector<int>{2});
        CHECK(loader->takeReady().empty());
    }
    TEST_CASE("initial key outages recover through the match refresh"){
        DownloadFixture f;f.storage.files[f.path()]=f.texture;
        auto loader=f.loader();f.http.pending("jwks.json")->replyRaw(503,"offline");loader->poll(1700000000);
        CHECK(loader->done());loader->poll(1700000060);
        auto snapshot=f.http.pending("/skins");REQUIRE(snapshot);
        snapshot->reply(200,{{"colonySkins",nlohmann::json::array({{{"team",2},{"assertion",f.f["valid"]}}})}});
        loader->poll(1700000060);f.keys();loader->poll(1700000061);
        CHECK(loader->takeReady().size()==1);
    }

    TEST_CASE("refreshed additions still require signatures and reject duplicate teams atomically"){
        DownloadFixture f;f.storage.files[f.path()]=f.texture;
        auto loader=f.loader();f.keys();loader->poll(1700000000);REQUIRE(loader->takeReady().size()==1);
        loader->poll(1700000060);
        f.http.pending("/skins")->reply(200,{{"colonySkins",nlohmann::json::array({
            {{"team",0},{"assertion",f.f["teams"][0]}},
            {{"team",0},{"assertion",f.f["teams"][0]}}
        })}});
        loader->poll(1700000060);CHECK(loader->takeRemoved().empty());CHECK(loader->takeReady().empty());
        loader->poll(1700000120);
        f.http.pending("/skins")->reply(200,{{"colonySkins",nlohmann::json::array({
            {{"team",2},{"assertion",f.f["invalid"]["signature"]}}
        })}});
        loader->poll(1700000120);f.keys();loader->poll(1700000121);
        CHECK(loader->takeReady().empty());CHECK(loader->takeRemoved().empty());CHECK(f.http.count("/texture")==0);
    }

    TEST_CASE("replay context fetches a fresh snapshot immediately without old tickets"){
        DownloadFixture f;
        SkinDownloads loader(f.storage,"https://example.test",f.f["claims"]["matchId"],{},[&](auto r){return f.http.start(std::move(r));});
        loader.poll(1700000000);
        auto snapshot=f.http.pending("/skins");REQUIRE(snapshot);
        snapshot->reply(200,{{"colonySkins",nlohmann::json::array({{{"team",2},{"assertion",f.f["valid"]}}})}});
        loader.poll(1700000000);f.keys();loader.poll(1700000000);
        f.http.pending("/texture")->replyRaw(200,f.texture);loader.poll(1700000000);
        REQUIRE(loader.takeReady().size()==1);
    }

}
