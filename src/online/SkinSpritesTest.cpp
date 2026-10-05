// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "SkinSprites.h"
#include "OnlineStorage.h"
#include "OnlineFakes.h"
#include "Sha256.h"
#include <GraphicContext.h>
#include <Toolkit.h>
#include <webp/encode.h>
namespace {
std::string image(unsigned size,bool lossless=false,unsigned red=210,bool full=false) {
    std::vector<uint8_t> pixels(size*size*4,0);
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) if(full || x%128<64) {
        auto *p=pixels.data()+(y*size+x)*4;p[0]=red;p[1]=40;p[2]=20;p[3]=255;
    }
    uint8_t *encoded=nullptr;const auto count=lossless?WebPEncodeLosslessRGBA(pixels.data(),size,size,size*4,&encoded):WebPEncodeRGBA(pixels.data(),size,size,size*4,90,&encoded);
    std::string result(reinterpret_cast<char *>(encoded),count);WebPFree(encoded);return result;
}
struct Fixture {
    Online::AuthorizedSkin skin;
    nlohmann::json doc;
    std::string unit=image(1024),swarm=image(128);
    Fixture() {
        skin.team=2;skin.versionId="00000000-0000-0000-0000-000000000001";
        skin.manifestHash=std::string(64,'a');skin.textureHash=std::string(64,'b');skin.materialHash=std::string(64,'c');skin.spriteRenderRevision=std::string(64,'d');
        doc={{"format","colony-sprites-v1"},{"renderRevision",skin.spriteRenderRevision},{"sourceManifestSha256",skin.manifestHash},
             {"textureSha256",skin.textureHash},{"materialSha256",skin.materialHash},{"swarmMesh","classic"},{"swarmViewAngle",0},
             {"frameMapping",{{"directions",8},{"phases",32},{"phaseShift",3},{"direction8Shift",5}}},{"tileSize",128},{"padding",1.25},{"logicalSizes",Online::SkinSpriteLogicalSizes},{"encoding","bundled-images-v3-webp-only"},{"pages",nlohmann::json::array()}};
        for(unsigned i=0;i<29;++i) {
            const auto &bytes=i<28?unit:swarm;
            doc["pages"].push_back({{"clip",Online::SkinSpriteClips[i<28?i/4:7]},{"first",i<28?i%4*64:0},
                {"frames",i<28?64:1},{"width",i<28?1024:128},{"height",i<28?1024:128},{"bytes",bytes.size()},{"sha256",Online::Sha256::hex(bytes)}});
        }
        skin.spriteManifestHash=Online::Sha256::hex(doc.dump());
    }
};
}
TEST_SUITE("SkinSprites") {
TEST_CASE("bundle binds complete frame layout and source identity before decoding") {
    Fixture f;Online::SkinSpriteManifest manifest;
    REQUIRE(manifest.parse(f.doc.dump(),f.skin));CHECK(manifest.pages.size()==29);
    CHECK(Online::validSkinSpritePage(f.unit,manifest.pages[0]));
    CHECK_FALSE(Online::validSkinSpritePage(f.swarm,manifest.pages[0]));
    auto lossless=image(1024,true);auto info=manifest.pages[0];info.hash=Online::Sha256::hex(lossless);info.bytes=lossless.size();
    CHECK(lossless.size()<f.unit.size());CHECK(Online::validSkinSpritePage(lossless,info));
    CHECK_FALSE(manifest.parse(std::string(65537,' '),f.skin));REQUIRE(manifest.parse(f.doc.dump(),f.skin));
    auto corrupted=f.unit;corrupted.back()^=1;CHECK_FALSE(Online::validSkinSpritePage(corrupted,manifest.pages[0]));
    for(const char *key:{"sourceManifestSha256","renderRevision","encoding","tileSize","padding","logicalSizes"}) {
        auto doc=f.doc;doc[key]="bad";auto skin=f.skin;skin.spriteManifestHash=Online::Sha256::hex(doc.dump());
        CHECK_FALSE(manifest.parse(doc.dump(),skin));CHECK(manifest.pages.empty());
    }
    auto doc=f.doc;doc["pages"][0]["first"]=1;f.skin.spriteManifestHash=Online::Sha256::hex(doc.dump());CHECK_FALSE(manifest.parse(doc.dump(),f.skin));
}
TEST_CASE("software requests demand pages and removes appearance without an OpenGL context") {
    glob2test::ToolkitScope toolkit;
    auto *gfx=GAGCore::Toolkit::initGraphic(128,128,0,"skin sprite test");
    Fixture f;Online::MemoryStorage storage;OnlineFakes::Http http;
    Online::SkinSprites sprites(storage,"https://example.test",[&](auto request){return http.start(std::move(request));});
    sprites.install(f.skin);sprites.poll();
    auto manifest=http.pending("/manifest");REQUIRE(manifest);manifest->replyRaw(200,f.doc.dump());sprites.poll();
    CHECK_FALSE(sprites.draw(*gfx,2,0,0,10,10,38,38));sprites.poll();
    auto page=http.pending("/pages/"+Online::Sha256::hex(f.unit));REQUIRE(page);CHECK(page->request.responseLimit==f.unit.size());page->replyRaw(200,f.unit);CHECK_FALSE(sprites.draw(*gfx,2,0,0,10,10,38,38));sprites.poll();
    REQUIRE(sprites.draw(*gfx,2,0,0,10,10,38,38));CHECK(sprites.decodedBytes()==65*8*1024*4);
    // Compaction preserves placement, clipping, filtering and faded pixels at
    // normal and enlarged scales, including transparent RGB from lossy WebP.
    glob2test::TempDir files("skin-packed-page");files.write("page.webp",f.unit);
    GAGCore::DrawableSurface original(files.path("page.webp"));
    for(float size:{38.f,76.f,190.f})for(unsigned char alpha:{uint8_t(255),uint8_t(127)}) {
        gfx->drawFilledRect(0,0,128,128,GAGCore::Color(15,25,35));
        REQUIRE(sprites.draw(*gfx,2,0,0,10,10,size,size,nullptr,alpha));
        auto *surface=gfx->getSDLSurface();
        std::string actual(static_cast<const char*>(surface->pixels),surface->pitch*surface->h);
        gfx->drawFilledRect(0,0,128,128,GAGCore::Color(15,25,35));
        gfx->drawSkinSprite(10-size*.125f,10-size*.125f,size*1.25f,size*1.25f,&original,0,0,128,128,alpha);
        unsigned maximum=0;
        const auto *expected=static_cast<const unsigned char*>(surface->pixels);
        for(std::size_t i=0;i<actual.size();++i)maximum=std::max(maximum,unsigned(std::abs(int(static_cast<unsigned char>(actual[i]))-int(expected[i]))));
        CHECK(maximum<=1);
    }
    CHECK(http.count("/pages/"+Online::Sha256::hex(f.unit))==1);
    // Other clips share the same content-addressed page.
    CHECK(sprites.draw(*gfx,2,3,0,10,10,40,40));
    sprites.remove(2);CHECK_FALSE(sprites.draw(*gfx,2,0,0,10,10,38,38));CHECK(sprites.decodedBytes()==0);
}
TEST_CASE("decoded pages evict at 64 MiB and reload verified disk content") {
    glob2test::ToolkitScope toolkit;
    auto *gfx=GAGCore::Toolkit::initGraphic(128,128,0,"skin cache test");
    Fixture f;Online::MemoryStorage storage;OnlineFakes::Http http;
    std::vector<std::string> content;
    for(unsigned i=0;i<17;++i) {
        content.push_back(image(1024,true,100+i,true));
        auto &page=f.doc["pages"][i];page["sha256"]=Online::Sha256::hex(content.back());page["bytes"]=content.back().size();
    }
    f.skin.spriteManifestHash=Online::Sha256::hex(f.doc.dump());
    Online::SkinSprites sprites(storage,"https://example.test",[&](auto request){return http.start(std::move(request));});
    sprites.install(f.skin);sprites.poll();http.pending("/manifest")->replyRaw(200,f.doc.dump());sprites.poll();
    for(unsigned i=0;i<17;++i) {
        CHECK_FALSE(sprites.draw(*gfx,2,i/4,i%4*64,0,0,38,38));sprites.poll();
        auto request=http.pending("/pages/"+Online::Sha256::hex(content[i]));REQUIRE(request);request->replyRaw(200,content[i]);
        sprites.draw(*gfx,2,i/4,i%4*64,0,0,38,38);sprites.poll();
        REQUIRE(sprites.draw(*gfx,2,i/4,i%4*64,0,0,38,38));
        CHECK(sprites.decodedBytes()<=64*1024*1024);
    }
    CHECK(sprites.decodedBytes()==64*1024*1024);
    CHECK_FALSE(sprites.draw(*gfx,2,0,0,0,0,38,38));sprites.poll();
    REQUIRE(sprites.draw(*gfx,2,0,0,0,0,38,38));
    CHECK(http.count("/pages/"+Online::Sha256::hex(content[0]))==1);
    sprites.remove(2);CHECK(sprites.decodedBytes()==0);
}
TEST_CASE("corrupt disk pages are rejected and replacement downloads are bounded") {
    glob2test::ToolkitScope toolkit;
    auto *gfx=GAGCore::Toolkit::initGraphic(128,128,0,"skin corruption test");
    Fixture f;Online::MemoryStorage storage;OnlineFakes::Http http;
    auto bad=f.unit;bad.back()^=1;storage.write("online/skin-sprites/"+Online::Sha256::hex(f.unit)+".webp",bad);
    Online::SkinSprites sprites(storage,"https://example.test",[&](auto request){return http.start(std::move(request));});
    sprites.install(f.skin);sprites.poll();http.pending("/manifest")->replyRaw(200,f.doc.dump());sprites.poll();
    for(unsigned i=0;i<3;++i) {
        CHECK_FALSE(sprites.draw(*gfx,2,0,0,0,0,38,38));sprites.poll();
        auto request=http.pending("/pages/"+Online::Sha256::hex(f.unit));REQUIRE(request);request->replyRaw(200,bad);
        sprites.draw(*gfx,2,0,0,0,0,38,38);sprites.poll();
    }
    CHECK_FALSE(sprites.draw(*gfx,2,0,0,0,0,38,38));sprites.poll();
    CHECK(http.count("/pages/"+Online::Sha256::hex(f.unit))==3);CHECK(sprites.decodedBytes()==0);
}

}
