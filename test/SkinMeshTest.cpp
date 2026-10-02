// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SkinMesh.h>
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
