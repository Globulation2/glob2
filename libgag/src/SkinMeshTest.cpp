// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SkinMesh.h>
#include "src/online/SwarmMeshCatalog.h"
#include "src/online/SkinSpriteManifest.h"
#include <SDL3_image/SDL_image.h>
#include <cstdlib>
#include "src/online/SkinViewTransforms.h"
#include <cmath>
#include <SkinAtlasCache.h>
#include <StreamBackend.h>
#include <SDLGraphicContext.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <bit>
#include <cstdint>
#include <limits>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

namespace
{
void word(std::string &bytes, std::uint32_t value)
{
    for (int i=0; i<4; ++i) bytes.push_back(static_cast<char>(value >> (i*8)));
}
void replaceWord(std::string &bytes, std::size_t offset, std::uint32_t value)
{
    std::string encoded; word(encoded,value); bytes.replace(offset,4,encoded);
}
std::string fixture(unsigned frames = 256)
{
    std::string bytes="GSK1";
    for (auto value : {3u,3u,frames,38u}) word(bytes,value);
    for (int i=0; i<6; ++i) word(bytes,std::bit_cast<std::uint32_t>(0.5f));
    for (auto index : {0u,1u,2u}) word(bytes,index);
    for (unsigned f=0; f<frames; ++f)
        for (int v=0; v<3; ++v)
            for (int c=0; c<6; ++c)
                word(bytes,std::bit_cast<std::uint32_t>(c==5 ? 1.f : 0.f));
    return bytes;
}
}
TEST_SUITE("SkinMesh")
{
    TEST_CASE("virtual asset streams use the same bounded decoder")
    {
        auto bytes=fixture();
        GAGCore::MemoryStreamBackend input(bytes.data(),bytes.size());
        GAGCore::SkinMesh mesh;
        std::string error;
        REQUIRE(mesh.load(input,error));
        CHECK(mesh.frames==256);
        const auto identity=mesh.identity;
        bytes.pop_back();
        GAGCore::MemoryStreamBackend truncated(bytes.data(),bytes.size());
        CHECK_FALSE(mesh.load(truncated,error));
        CHECK(mesh.identity==identity);
        GAGCore::FileStreamBackend missing(nullptr);
        CHECK_FALSE(mesh.load(missing,error));
    }
    TEST_CASE("static swarm pose")
    {
        glob2test::TempDir directory("skin-static");
        const auto path = directory.path / "swarm.gsk";
        glob2test::writeFile(path, fixture(1));
        GAGCore::SkinMesh mesh;
        std::string error;
        REQUIRE(mesh.load(path.string(), error));
        CHECK(mesh.frames == 1);
        CHECK(mesh.poses.size() == 18);
    }
    TEST_CASE("static camera ring preserves UVs, scale and independent cache identities")
    {
        const std::array<const char *,7> shapes{"swarm", "swarm-crown", "swarm-clutch", "swarm-toadstool", "swarm-coral", "swarm-skep", "swarm-bloom"};
        for (unsigned shape=0; shape<shapes.size(); ++shape)
        {
            GAGCore::SkinMesh mesh; std::string error;
            REQUIRE(mesh.load((glob2test::sourceRoot()/"data/skins/colony-v1"/(std::string(shapes[shape])+".gsk")).string(), error));
            const auto &t=Online::SkinViews[shape];
            auto zero=mesh.rotatedView(0,t.inverse,t.projection,t.normals);
            REQUIRE(zero.poses.size()==mesh.poses.size());
            for (unsigned i=0;i<mesh.poses.size();++i) CHECK(std::abs(zero.poses[i]-mesh.poses[i])<0.00001f);
            auto previous=mesh.identity;
            for (unsigned angle=0;angle<360;angle+=5)
            {
                auto rotated=mesh.rotatedView(angle,t.inverse,t.projection,t.normals);
                CHECK(rotated.identity!=previous);
                CHECK(rotated.identity!=mesh.identity);
                CHECK(rotated.uv==mesh.uv);
                CHECK(rotated.indices==mesh.indices);
                for (unsigned v=0;v<mesh.vertices;++v)
                {
                    const auto i=v*6;
                    CHECK(std::abs(rotated.poses[i])<1.25f);
                    CHECK(std::abs(rotated.poses[i+1])<1.25f);
                    // Recovered world height is invariant around the ring.
                    const auto height=[&](const auto &p){ return t.inverse[8]*p[i]+t.inverse[9]*p[i+1]+t.inverse[10]*p[i+2]+t.inverse[11]; };
                    CHECK(std::abs(height(rotated.poses)-height(mesh.poses))<0.00001f);
                    const auto length=[&](const auto &p){return p[i+3]*p[i+3]+p[i+4]*p[i+4]+p[i+5]*p[i+5];};
                    CHECK(std::abs(length(rotated.poses)-length(mesh.poses))<0.00001f);
                }
                previous=rotated.identity;
            }
            CHECK(mesh.rotatedView(360,t.inverse,t.projection,t.normals).identity==0);
        }
    }
#ifdef HAVE_OPENGL
    TEST_CASE("rotated swarms render independently with every material [display][artifacts]")
    {
        glob2test::ToolkitScope toolkit;
        auto *gfx=GAGCore::Toolkit::initGraphic(640,480,GAGCore::GraphicContext::USEGPU,"Swarm camera and material contract");
        GAGCore::SkinMesh mesh; std::string error;
        REQUIRE(mesh.load((glob2test::sourceRoot()/"data/skins/colony-v1/swarm-crown.gsk").string(),error));
        const auto &t=Online::SkinViews[1];
        std::array<GAGCore::SkinMesh,4> variants;
        for (unsigned i=0;i<4;++i) variants[i]=mesh.rotatedView(i*90,t.inverse,t.projection,t.normals);
        GAGCore::DrawableSurface paint(512,512), material(512,512);
        paint.drawFilledRect(0,0,512,512,GAGCore::Color(237,146,82));
        auto pixels=[&]() {
            glFinish(); std::vector<unsigned char> result(640*480*4);
            glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
            CHECK(glGetError()==GL_NO_ERROR); return result;
        };
        auto draw=[&](unsigned id,bool reverse) {
            material.drawFilledRect(0,0,512,512,GAGCore::Color(id,id,id));
            std::vector<GAGCore::SkinMeshRequest> requests;
            for (auto &variant:variants) requests.push_back({&variant,0,&paint,&material,GAGCore::SkinRegionSwarm});
            gfx->prepareSkinMeshes(requests);
            gfx->drawFilledRect(0,0,640,480,GAGCore::Color(38,33,45));
            for (unsigned k=0;k<4;++k) {
                unsigned i=reverse?3-k:k;
                REQUIRE(gfx->drawSkinMesh(variants[i],0,paint,material,GAGCore::SkinRegionSwarm,12+i*156,148,144,144));
            }
            return pixels();
        };
        for (unsigned id=0;id<4;++id) {
            const auto forward=draw(id,false);
            const auto reverse=draw(id,true);
            unsigned differences=0, maximum=0;
            for (unsigned i=0;i<forward.size();++i) if (forward[i]!=reverse[i]) { ++differences; maximum=std::max(maximum,unsigned(std::abs(int(forward[i])-int(reverse[i])))); }
            INFO("material " << id << " differences " << differences << " maximum " << maximum);
            // Reallocation in different atlas slots can shift sparse bilinear
            // rounding by one channel level; angle/cache mixups are far larger.
            CHECK(maximum<=1);
            CHECK(differences<=100);
            gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/swarm-material-"+std::to_string(id)+".bmp");
            // Substantial non-background coverage proves this is not an empty fallback.
            unsigned changed=0;
            for (unsigned p=0;p<forward.size();p+=4) changed+=forward[p]!=38 || forward[p+1]!=33 || forward[p+2]!=45;
            CHECK(changed>1000);
        }
    }
#endif
    TEST_CASE("bounded transactional decoding")
    {
        glob2test::TempDir directory("skin-mesh");
        const auto path = directory.path / "mesh.gsk";
        const auto valid = fixture();
        glob2test::writeFile(path,valid);
        GAGCore::SkinMesh mesh;
        std::string error;
        REQUIRE(mesh.load(path.string(),error));
        CHECK(mesh.vertices==3);
        CHECK(mesh.frames==256);
        CHECK(mesh.poses.size()==256*3*6);
        const auto identity=mesh.identity;
        for (int corruption=0; corruption<8; ++corruption)
        {
            auto bad=valid;
            switch(corruption)
            {
                case 0: bad[3]='2'; break;
                case 1: replaceWord(bad,4,0xffffffffu); break;
                case 2: replaceWord(bad,8,4); break;
                case 3: replaceWord(bad,12,255); break;
                case 4: replaceWord(bad,20,std::bit_cast<std::uint32_t>(-1.f)); break;
                case 5: replaceWord(bad,44,3); break;
                case 6: replaceWord(bad,56,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN())); break;
                case 7: bad.pop_back(); break;
            }
            glob2test::writeFile(path,bad);
            CHECK_FALSE(mesh.load(path.string(),error));
            CHECK_FALSE(error.empty());
            CHECK(mesh.identity==identity);
            CHECK(mesh.vertices==3);
        }
        glob2test::writeFile(path,valid);
        REQUIRE(mesh.load(path.string(),error));
        CHECK(mesh.identity!=identity);
        CHECK(error.empty());
    }
}

TEST_SUITE("SkinAtlasCache")
{
    TEST_CASE("visible hits survive a full atlas eviction batch")
    {
        GAGCore::SkinAtlasCache cache;
        using Key = GAGCore::SkinAtlasCache::Key;
        for (unsigned i=0; i<cache.Capacity; ++i)
            CHECK(cache.reserve(Key{1,i,1,1,1,1,0}) == i);
        // Protect the oldest half before admitting an equally large new set.
        for (unsigned i=0; i<cache.Capacity/2; ++i) REQUIRE(cache.touch(Key{1,i,1,1,1,1,0}));
        for (unsigned i=0; i<cache.Capacity/2; ++i) cache.reserve(Key{2,i,1,1,1,1,0});
        CHECK(cache.size() == cache.Capacity);
        for (unsigned i=0; i<cache.Capacity; ++i)
            CHECK(cache.find(Key{1,i,1,1,1,1,0}).has_value() == (i<cache.Capacity/2));
        for (unsigned i=0; i<cache.Capacity/2; ++i) REQUIRE(cache.find(Key{2,i,1,1,1,1,0}));
    }
    TEST_CASE("paint and material revisions, lifetimes, region, mesh and pose are independent cache keys")
    {
        using Cache = GAGCore::SkinAtlasCache;
        Cache cache;
        const auto base = Cache::key(1,0,1,1,1,1,0);
        cache.reserve(base);
        CHECK_FALSE(cache.find(Cache::key(1,0,1,2,1,1,0)));  // paint revision
        CHECK_FALSE(cache.find(Cache::key(1,0,2,1,1,1,0)));  // paint lifetime
        CHECK_FALSE(cache.find(Cache::key(1,0,1,1,1,2,0)));  // material revision
        CHECK_FALSE(cache.find(Cache::key(1,0,1,1,2,1,0)));  // material lifetime
        for (std::uint8_t region=1; region<4; ++region)
            CHECK_FALSE(cache.find(Cache::key(1,0,1,1,1,1,region)));
        CHECK_FALSE(cache.find(Cache::key(2,0,1,1,1,1,0)));
        CHECK_FALSE(cache.find(Cache::key(1,1,1,1,1,1,0)));
        // A paint surface reused as a material (or vice versa) is a different key.
        CHECK_FALSE(cache.find(Cache::key(1,0,1,1,3,1,0)));
        cache.reserve(Cache::key(1,0,3,1,1,1,0));
        CHECK_FALSE(cache.find(Cache::key(1,0,1,1,3,1,0)));
        CHECK(cache.reserve(base) == 0);
        CHECK(cache.size() == 2);
        cache = {};
        CHECK_FALSE(cache.find(base));
        CHECK(cache.reserve(base) == 0);
    }
}

TEST_SUITE("SkinMaterialMap")
{
    TEST_CASE("greyscale material maps decode to exact ids")
    {
        glob2test::ToolkitScope toolkit; GAGCore::Toolkit::initGraphic(64,64,0,"skin material");
        // The authorization fixture's 8-bit greyscale map cycles ids 0..3 in
        // 32-row bands; SDL_image may decode it with an inexact grey palette.
        const auto fixture=nlohmann::json::parse(glob2test::readFile(glob2test::sourceRoot()/"test/fixtures/skins/authorization.json"));
        const auto hex=fixture["materialHex"].get<std::string>();
        std::string bytes;
        for (std::size_t i=0; i<hex.size(); i+=2) bytes.push_back(static_cast<char>(std::stoul(hex.substr(i,2),nullptr,16)));
        glob2test::TempDir directory("skin-material");
        glob2test::writeFile(directory.path/"material.png",bytes);
        auto material=GAGCore::loadSkinMaterialMap((directory.path/"material.png").string());
        REQUIRE(material);
        CHECK(material->getW()==512);
        CHECK(material->getH()==512);
        auto *raw=material->getSDLSurface();
        for (int band=0; band<16; ++band)
        {
            Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(raw,7,band*32+5,&r,&g,&b,&a));
            CHECK(r==band%4); CHECK(g==band%4); CHECK(b==band%4); CHECK(a==255);
        }
        CHECK_FALSE(GAGCore::loadSkinMaterialMap((directory.path/"missing.png").string()));
    }
}

#ifdef HAVE_OPENGL
TEST_SUITE("SkinReadback") {
TEST_CASE("export readback matches live compositing for every pose and rotated swarm [display]") {
    glob2test::ToolkitScope toolkit;
    auto *gfx=GAGCore::Toolkit::initGraphic(640,480,GAGCore::GraphicContext::USEGPU|GAGCore::GraphicContext::NOAUDIO,"skin readback");
    const auto source=nlohmann::json::parse(glob2test::readFile(glob2test::sourceRoot()/"test/fixtures/skins/authorization.json"));
    glob2test::TempDir files("skin-readback");
    for(const auto &name:{"texture","material"}) {
        const auto hex=source[std::string(name)+"Hex"].get<std::string>();std::string bytes;
        for(std::size_t i=0;i<hex.size();i+=2)bytes.push_back(char(std::stoul(hex.substr(i,2),nullptr,16)));
        glob2test::writeFile(files.path/(std::string(name)+".png"),bytes);
    }
    GAGCore::DrawableSurface paint((files.path/"texture.png").string());
    auto material=GAGCore::loadSkinMaterialMap((files.path/"material.png").string());REQUIRE(material);
    nlohmann::json bundle;const char *exportPath=std::getenv("GLOB2_SKIN_EXPORT_DIR");
    if(exportPath)bundle=nlohmann::json::parse(glob2test::readFile(std::filesystem::path(exportPath)/"manifest.json"));
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> exported(nullptr,SDL_DestroySurface);
    unsigned loadedPage=~0u;std::uint64_t poses=0;
    const auto compare=[&](const GAGCore::SkinMesh &mesh,unsigned frame,unsigned region,unsigned clip) {
        std::vector<uint8_t> rgba;REQUIRE(gfx->readSkinMesh({&mesh,frame,&paint,material.get(),uint8_t(region)},rgba));
        REQUIRE(rgba.size()==128*128*4);
        gfx->drawFilledRect(0,0,640,480,GAGCore::Color(20,40,60));
        REQUIRE(gfx->drawSkinMesh(mesh,frame,paint,*material,region,12.8f,12.8f,102.4f,102.4f));
        std::vector<uint8_t> live(128*128*4);glReadPixels(0,480-128,128,128,GL_RGBA,GL_UNSIGNED_BYTE,live.data());
        unsigned error=0,alphaErrors=0;
        for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)for(unsigned c=0;c<3;++c) {
            const unsigned at=(y*128+x)*4,gl=((127-y)*128+x)*4;
            const auto expected=(unsigned(rgba[at+c])*rgba[at+3]+unsigned(c==0?20:c==1?40:60)*(255-rgba[at+3])+127)/255;
            error=std::max(error,unsigned(std::abs(int(expected)-int(live[gl+c]))));
        }
        INFO("clip "<<clip<<" frame "<<frame);CHECK(error<=1);
        if(exportPath && clip<8) {
            const unsigned page=clip<7?clip*4+frame/64:28;
            if(page!=loadedPage) {
                const auto hash=bundle["pages"][page]["sha256"].get<std::string>();
                std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(IMG_Load((std::filesystem::path(exportPath)/(hash+".webp")).string().c_str()),SDL_DestroySurface);
                REQUIRE(image);exported.reset(SDL_ConvertSurface(image.get(),SDL_PIXELFORMAT_RGBA32));REQUIRE(exported);loadedPage=page;
            }
            const unsigned cell=clip<7?frame%64:0;
            for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x) {
                const auto *pixel=static_cast<const uint8_t *>(exported->pixels)+((cell/8)*128+y)*exported->pitch+((cell%8)*128+x)*4;
                alphaErrors+=pixel[3]!=rgba[(y*128+x)*4+3];
            }
            CHECK(alphaErrors==0);
        }
        ++poses;
    };
    for(unsigned clip=0;clip<7;++clip) {
        GAGCore::SkinMesh mesh;std::string error;
        REQUIRE(mesh.load((glob2test::sourceRoot()/"data/skins/colony-v1"/(std::string(Online::SkinSpriteClips[clip])+".gsk")).string(),error));
        for(unsigned frame=0;frame<256;++frame)compare(mesh,frame,clip<3?0:clip<6?1:2,clip);
    }
    for(unsigned choice=0;choice<Online::SWARM_MESHES.size();++choice) {
        GAGCore::SkinMesh mesh;std::string error;
        REQUIRE(mesh.load((glob2test::sourceRoot()/"data/skins/colony-v1"/Online::SWARM_MESHES[choice].file).string(),error));
        for(unsigned angle:{0u,127u,359u}) {
            const auto &view=Online::SkinViews[choice];
            auto oriented=angle?mesh.rotatedView(angle,view.inverse,view.projection,view.normals):mesh;
            compare(oriented,0,3,choice==0 && angle==0?7:8);
        }
    }
    CHECK(poses==1813);
}
}
#endif
