// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReplayAppearance.h"
#include "InstanceConfig.h"
#include "Sha256.h"
#include <FileManager.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <array>
#include <memory>

namespace Online {
namespace {
constexpr std::size_t ReplayLimit = 64 * 1024 * 1024;
constexpr std::size_t MetadataLimit = 1024;
bool matchIdValid(const std::string &id)
{
    if(id.size()!=36)return false;
    for(std::size_t i=0;i<id.size();++i)
        if(i==8||i==13||i==18||i==23) { if(id[i]!='-')return false; }
        else if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;
    return true;
}
std::string hashReplay(GAGCore::FileManager &files,const std::string &filename)
{
    std::unique_ptr<GAGCore::StreamBackend> input(files.openInputStreamBackend(filename));
    if(!input || !input->isValid())return {};
    input->seekFromEnd(0);
    auto remaining=input->getPosition();
    if(!remaining || remaining>ReplayLimit)return {};
    input->seekFromStart(0);
    Sha256 hash;
    std::array<char,16384> bytes;
    while(remaining) {
        const auto count=std::min(remaining,bytes.size());
        if(!input->readExact(bytes.data(),count))return {};
        hash.update(bytes.data(),count);
        remaining-=count;
    }
    return Sha256::toHex(hash.finish());
}
}
std::string replayAppearanceJson(const ReplayAppearance &context,const std::string &hash)
{
    const auto origin=normalizeOrigin(context.origin);
    if(!origin || !matchIdValid(context.matchId) || !Sha256::isHexDigest(hash))return {};
    const auto text=nlohmann::json{{"format",1},{"origin",*origin},{"matchId",context.matchId},{"replaySha256",hash}}.dump();
    return text.size()<=MetadataLimit ? text : std::string();
}
std::optional<ReplayAppearance> parseReplayAppearance(const std::string &text,
    const std::string &hash,const InstanceConfig &config)
{
    if(text.size()>MetadataLimit || !Sha256::isHexDigest(hash))return {};
    try {
        const auto json=nlohmann::json::parse(text);
        if(!json.at("format").is_number_integer() || json.at("format")!=1 || json.at("replaySha256")!=hash)return {};
        ReplayAppearance result{json.at("origin").get<std::string>(),json.at("matchId").get<std::string>()};
        const auto origin=normalizeOrigin(result.origin);
        if(!origin || !config.isTrusted(*origin) || !matchIdValid(result.matchId))return {};
        result.origin=*origin;
        return result;
    } catch(const std::exception &) { return {}; }
}
bool writeReplayAppearance(GAGCore::FileManager &files,const std::string &filename,
    const ReplayAppearance &context)
{
    const auto text=replayAppearanceJson(context,hashReplay(files,filename));
    return !text.empty() && files.writeFileAtomic(filename+".appearance.json",text);
}
std::optional<ReplayAppearance> readReplayAppearance(GAGCore::FileManager &files,
    const std::string &filename,const InstanceConfig &config)
{
    std::unique_ptr<GAGCore::StreamBackend> input(files.openInputStreamBackend(filename+".appearance.json"));
    if(!input || !input->isValid())return {};
    input->seekFromEnd(0);
    const auto size=input->getPosition();
    if(!size || size>MetadataLimit)return {};
    input->seekFromStart(0);
    std::string text(size,'\0');
    if(!input->readExact(text.data(),size))return {};
    return parseReplayAppearance(text,hashReplay(files,filename),config);
}
}
