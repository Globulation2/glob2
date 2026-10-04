// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SkinMesh.h>
#include <SkinAtlasCache.h>
#include <StreamBackend.h>
#include <SDLGraphicContext.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <bit>
#include <cstdint>
#include <limits>

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
