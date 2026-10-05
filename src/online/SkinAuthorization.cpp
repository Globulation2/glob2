// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinAuthorization.h"
#include "Sha256.h"
#include "SwarmMeshCatalog.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <stdexcept>
#include <chrono>
#ifdef __EMSCRIPTEN__
// Implemented by the browser host; shared validation contains no browser APIs.
extern "C" {
int startSkinSignature(const char *key, const char *message, const char *signature);
int pollSkinSignature(int id);
void deleteSkinSignature(int id);
}
#else
#include <openssl/evp.h>
#endif

namespace Online
{
namespace
{
using Json=nlohmann::json;
std::vector<unsigned char> decode(const std::string &text)
{
    std::vector<unsigned char> result;
    std::uint32_t value=0; int bits=0;
    for(char c:text)
    {
        const int digit=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='-'?62:c=='_'?63:-1;
        if(digit<0)throw std::runtime_error("invalid base64url");
        value=(value<<6)|digit;bits+=6;
        if(bits>=8){bits-=8;result.push_back(static_cast<unsigned char>(value>>bits));}
    }
    if(bits>=6||(bits&&(value&((1u<<bits)-1))))throw std::runtime_error("noncanonical base64url");
    return result;
}
bool uuid(const std::string &s)
{
    if(s.size()!=36)return false;
    for(std::size_t i=0;i<s.size();++i)
        if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
        else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return true;
}
std::int64_t number(const Json &v,std::int64_t maximum)
{
    if(!v.is_number_integer()||v<0||v>maximum)throw std::runtime_error("invalid integer");
    return v.get<std::int64_t>();
}
}
struct SkinAuthorization::Impl
{
    State status=State::Rejected;
    AuthorizedSkin appearance;
#ifdef __EMSCRIPTEN__
    int signature=0;
    std::chrono::steady_clock::time_point deadline;
#endif
};
SkinAuthorization::SkinAuthorization(const std::string &token,const std::string &jwks,const std::string &issuer,const std::string &matchId,int team,std::int64_t now)
    :impl(std::make_unique<Impl>())
{
    try
    {
        if(token.size()>8192||jwks.size()>65536||team<0||team>=32||now<0||now>INT64_MAX-30||!uuid(matchId))return;
        const auto a=token.find('.'),b=a==std::string::npos?a:token.find('.',a+1);
        if(a==std::string::npos||b==std::string::npos||token.find('.',b+1)!=std::string::npos)return;
        const auto headerBytes=decode(token.substr(0,a)),claimBytes=decode(token.substr(a+1,b-a-1));
        const auto header=Json::parse(headerBytes),claims=Json::parse(claimBytes),keys=Json::parse(jwks);
        if(header.at("alg")!="EdDSA"||header.at("typ")!="glob2-colony-skin+jwt"||claims.at("aud")!="glob2-colony-renderer"||claims.at("iss")!=issuer||claims.at("matchId")!=matchId)return;
        if(header.contains("crit")||header.contains("b64"))return;
        const auto issued=number(claims.at("iat"),INT64_MAX),expires=number(claims.at("exp"),INT64_MAX);
        if(issued>now+30||expires<now-30||expires<issued||expires-issued>86400)return;
        if(claims.contains("nbf")&&number(claims.at("nbf"),INT64_MAX)>now+30)return;
        if(number(claims.at("team"),31)!=team)return;
        auto &skin=impl->appearance;
        skin.expiresAt=expires;
        skin.accountId=claims.at("accountId").get<std::string>();
        if(!uuid(skin.accountId)||claims.at("sub")!=skin.accountId)return;
        const auto &version=claims.at("version");
        skin.versionId=version.at("id").get<std::string>();skin.skinId=version.at("skinId").get<std::string>();
        skin.textureHash=version.at("textureSha256").get<std::string>();skin.materialHash=version.at("materialSha256").get<std::string>();
        skin.manifestHash=version.at("manifestSha256").get<std::string>();
        if(!uuid(skin.versionId)||!uuid(skin.skinId)||!Sha256::isHexDigest(skin.textureHash)||!Sha256::isHexDigest(skin.materialHash)||
           !Sha256::isHexDigest(skin.manifestHash)||version.at("layout")!="colony-v2")return;
        const auto defaultColor=number(version.at("buildingColor"),0xffffff);
        skin.buildingColor=number(claims.at("buildingColor"),0xffffff);skin.team=team;
        skin.swarmMesh=swarmMeshIndex(version.value("swarmMesh",std::string(SWARM_MESHES[0].id)));
        if(skin.swarmMesh<0)return;
        if(version.contains("swarmViewAngle"))skin.swarmViewAngle=number(version.at("swarmViewAngle"),359);
        // Same key order as the API's JSON.stringify of the manifest. Classic
        // skins leave swarmMesh out; clients that do not know other meshes
        // reject them outright instead of drawing their paint on the classic swarm.
        nlohmann::ordered_json manifest={{"skinId",skin.skinId},{"textureSha256",skin.textureHash},{"materialSha256",skin.materialHash},
                                         {"layout","colony-v2"},{"buildingColor",defaultColor}};
        if(skin.swarmMesh)manifest["swarmMesh"]=std::string(SWARM_MESHES[skin.swarmMesh].id);
        if(skin.swarmViewAngle)manifest["swarmViewAngle"]=skin.swarmViewAngle;
        if(Sha256::hex(manifest.dump())!=skin.manifestHash)return;
        const auto kid=header.at("kid").get<std::string>();
        if(kid.empty()||kid.size()>128||!keys.at("keys").is_array()||keys.at("keys").size()>32)return;
        std::string encodedKey;
        bool foundKey=false;
        for(const auto &key:keys.at("keys"))if(key.value("kid",std::string())==kid)
        {
            if(foundKey||key.at("kty")!="OKP"||key.at("crv")!="Ed25519"||key.value("alg",std::string("EdDSA"))!="EdDSA"||key.value("use",std::string("sig"))!="sig")return;
            foundKey=true;
            if(key.contains("key_ops") && (key["key_ops"]!=Json::array({"verify"})))return;
            encodedKey=key.at("x").get<std::string>();
        }
        const auto key=decode(encodedKey),signature=decode(token.substr(b+1));
        if(key.size()!=32||signature.size()!=64)return;
        const auto message=token.substr(0,b);
#ifdef __EMSCRIPTEN__
        impl->signature=startSkinSignature(encodedKey.c_str(),message.c_str(),token.substr(b+1).c_str());
        impl->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        impl->status=State::Pending;
#else
        auto *publicKey=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,nullptr,key.data(),key.size());
        auto *context=EVP_MD_CTX_new();
        if(publicKey&&context&&EVP_DigestVerifyInit(context,nullptr,nullptr,nullptr,publicKey)==1&&EVP_DigestVerify(context,signature.data(),signature.size(),reinterpret_cast<const unsigned char*>(message.data()),message.size())==1)impl->status=State::Verified;
        EVP_MD_CTX_free(context);EVP_PKEY_free(publicKey);
#endif
    }
    catch(const std::exception &){impl->status=State::Rejected;}
}
SkinAuthorization::~SkinAuthorization()
{
#ifdef __EMSCRIPTEN__
    if(impl->signature)deleteSkinSignature(impl->signature);
#endif
}
SkinAuthorization::State SkinAuthorization::state()
{
#ifdef __EMSCRIPTEN__
    if(impl->status==State::Pending){const int status=pollSkinSignature(impl->signature);if(status)impl->status=status>0?State::Verified:State::Rejected;else if(std::chrono::steady_clock::now()>=impl->deadline)impl->status=State::Rejected;}
#endif
    return impl->status;
}
const AuthorizedSkin *SkinAuthorization::skin(){return state()==State::Verified?&impl->appearance:nullptr;}
}
