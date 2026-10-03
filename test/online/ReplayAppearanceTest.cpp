// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ReplayAppearance.h"
#include "InstanceConfig.h"
#include "OnlineStorage.h"
#include "Sha256.h"
#include <FileManager.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
using namespace Online;
TEST_SUITE("ReplayAppearance") {
    TEST_CASE("only matching recordings and trusted origins restore appearance context") {
        MemoryStorage storage;
        InstanceConfig config(storage);
        const ReplayAppearance context{"https://skins.example","44444444-4444-4444-8444-444444444444"};
        const auto digest=Sha256::hex("recording bytes");
        const auto text=replayAppearanceJson(context,digest);
        REQUIRE_FALSE(text.empty());
        CHECK_FALSE(parseReplayAppearance(text,digest,config));
        config.trust(context.origin,false);
        const auto restored=parseReplayAppearance(text,digest,config);
        REQUIRE(restored);
        CHECK(restored->origin==context.origin);
        CHECK(restored->matchId==context.matchId);
        CHECK_FALSE(parseReplayAppearance(text,Sha256::hex("different replay"),config));
        auto malformed=nlohmann::json::parse(text);
        for(const auto &match: std::vector<std::string>{"../texture","44444444-4444-4444-8444-44444444444/",std::string(36,'-')}) {
            malformed["matchId"]=match;
            CHECK_FALSE(parseReplayAppearance(malformed.dump(),digest,config));
        }
        malformed=nlohmann::json::parse(text);
        malformed["format"]=2;CHECK_FALSE(parseReplayAppearance(malformed.dump(),digest,config));
        malformed["format"]=1.0;CHECK_FALSE(parseReplayAppearance(malformed.dump(),digest,config));
        CHECK_FALSE(parseReplayAppearance(std::string(1025,' '),digest,config));
        CHECK_FALSE(parseReplayAppearance("{",digest,config));
        CHECK(replayAppearanceJson({"http://remote.example",context.matchId},digest).empty());
    }
    TEST_CASE("companions survive copies but stale and oversized files are ignored") {
        glob2test::TempDir dir("skin-replays");
        GAGCore::FileManager files("glob2");
        MemoryStorage storage;InstanceConfig config(storage);
        ReplayAppearance context{OFFICIAL_INSTANCE_ORIGIN,"44444444-4444-4444-8444-444444444444"};
        const auto path=(dir.path/"recording.replay").string();
        const std::string bytes="immutable simulation recording";
        {std::ofstream out(path,std::ios::binary);out<<bytes;}
        CHECK_FALSE(readReplayAppearance(files,path,config));
        REQUIRE(writeReplayAppearance(files,path,context));
        CHECK(glob2test::readFile(path)==bytes);
        REQUIRE(readReplayAppearance(files,path,config));
        const auto copy=(dir.path/"renamed.replay").string();
        std::filesystem::copy_file(path,copy);
        std::filesystem::copy_file(path+".appearance.json",copy+".appearance.json");
        REQUIRE(readReplayAppearance(files,copy,config));
        {std::ofstream out(copy,std::ios::app);out<<"changed";}
        CHECK_FALSE(readReplayAppearance(files,copy,config));
        {std::ofstream out(path+".appearance.json");out<<std::string(1025,' ');}
        CHECK_FALSE(readReplayAppearance(files,path,config));
        CHECK_FALSE(writeReplayAppearance(files,(dir.path/"missing.replay").string(),context));
        const auto large=(dir.path/"large.replay").string();
        {std::ofstream out(large,std::ios::binary);out.seekp(64*1024*1024);out.put('x');}
        CHECK_FALSE(writeReplayAppearance(files,large,context));
    }
}
