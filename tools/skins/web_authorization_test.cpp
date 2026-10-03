// SPDX-License-Identifier: GPL-3.0-or-later
// Browser-only integration test of the production WebCrypto authorization path.
#include "SkinAuthorization.h"
#include <nlohmann/json.hpp>
#include "../../browser/SkinSignature.h"
#include <fstream>
#include <iostream>

int main()
{
    std::ifstream input("/fixture.json");
    const auto fixture=nlohmann::json::parse(input);
    unsigned checked=0,failed=0;
    auto verify=[&](const std::string &name,const std::string &token,bool accepted){
        Online::SkinAuthorization auth(token,fixture["jwks"].dump(),"https://example.test",fixture["claims"]["matchId"],2,1700000000);
        while(auth.state()==Online::SkinAuthorization::State::Pending)emscripten_sleep(1);
        ++checked;
        if((auth.skin()!=nullptr)!=accepted){std::cerr<<"Failed "<<name<<'\n';++failed;}
    };
    verify("valid signature",fixture["valid"],true);
    for(const auto &entry:fixture["invalid"].items())verify(entry.key(),entry.value(),false);
    // Destroying a pending verifier must not resurrect or leak its JS entry.
    { Online::SkinAuthorization cancelled(fixture["valid"],fixture["jwks"].dump(),"https://example.test",fixture["claims"]["matchId"],2,1700000000); }
    emscripten_sleep(50);
    const auto outstanding=EM_ASM_INT({return Module.glob2SkinSignatures.entries.size;});
    if(outstanding)++failed;
    EM_ASM({window.authorizationResult=({checked:$0,failed:$1,outstanding:$2});},checked,failed,outstanding);
    return failed?1:0;
}
