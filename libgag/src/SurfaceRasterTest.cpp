// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SurfaceRaster.h>
#include <array>
#include <cstring>
#include <memory>
#include <limits>

namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface surface(int width, int height, SDL_PixelFormat format)
{
    Surface result(SDL_CreateSurface(width,height,format),SDL_DestroySurface);
    REQUIRE(result);
    return result;
}
Uint32 pixel(SDL_Surface *value,int x,int y)
{
    Uint32 result;
    std::memcpy(&result,static_cast<Uint8 *>(value->pixels)+y*value->pitch+x*4,4);
    return result;
}
void put(SDL_Surface *value,int x,int y,Uint32 packed)
{
    std::memcpy(static_cast<Uint8 *>(value->pixels)+y*value->pitch+x*4,&packed,4);
}
}
TEST_SUITE("SurfaceRaster")
{
    TEST_CASE("native nearest offsets skip far source origins without intermediate overflow")
    {
        auto source=surface(4,4,SDL_PIXELFORMAT_RGBA32),target=surface(8,8,SDL_PIXELFORMAT_RGBA32);
        const auto background=SDL_MapSurfaceRGBA(target.get(),20,30,40,255);
        REQUIRE(SDL_FillSurfaceRect(target.get(),nullptr,background));
        const SDL_Rect clip{2,2,1,1};REQUIRE(SDL_SetSurfaceClipRect(target.get(),&clip));
        for(const SDL_Rect src:std::array<SDL_Rect,2>{{{std::numeric_limits<int>::max(),0,4,4},
                {0,std::numeric_limits<int>::max(),4,4}}}) {
            GAGCore::SurfaceRaster::blit(target.get(),source.get(),src,{2,2,4,4},173,false,
                GAGCore::SurfaceRaster::BlitBlend::Triangle);
            CHECK(pixel(target.get(),2,2)==background);
        }
    }
    TEST_CASE("native nearest copies retain alpha keys modulation and clipped source coordinates")
    {
        // Build the reference from independent one-pixel copies. Their source
        // rectangles are in bounds, so they do not share the large copy's
        // clipped/out-of-bounds coordinate calculation.
        for (const auto format : {SDL_PIXELFORMAT_RGBA32,SDL_PIXELFORMAT_ARGB8888})
        for (const Uint8 opacity : {Uint8(173),Uint8(255)})
        for (bool keyed : {false,true})
        for (const SDL_Rect destination : std::array<SDL_Rect,4>{{{1,1,8,7},{-2,-1,8,7},{5,4,8,7},{1,1,8,14}}})
        {
            if (opacity==255 && destination.h!=7) continue; // SDL RLE rounds only unscaled copies.
            CAPTURE(format);CAPTURE(opacity);CAPTURE(keyed);
            CAPTURE(destination.x);CAPTURE(destination.y);CAPTURE(destination.h);
            auto source=surface(5,4,format);
            auto actual=surface(10,9,format),reference=surface(10,9,format);
            for(int y=0;y<source->h;++y)for(int x=0;x<source->w;++x)
                put(source.get(),x,y,SDL_MapSurfaceRGBA(source.get(),Uint8(20+x*37),Uint8(10+y*53),90,
                    Uint8((x+y*3)*29)));
            const auto key=pixel(source.get(),2,1);
            REQUIRE(SDL_SetSurfaceColorKey(source.get(),keyed,key));
            REQUIRE(SDL_SetSurfaceColorMod(source.get(),31,97,181));
            REQUIRE(SDL_SetSurfaceAlphaMod(source.get(),79));
            const SDL_Rect clip{2,1,6,6},src{-1,-2,8,7};
            REQUIRE(SDL_SetSurfaceClipRect(actual.get(),&clip));
            REQUIRE(SDL_SetSurfaceClipRect(reference.get(),&clip));
            for(int y=0;y<9;++y)for(int x=0;x<10;++x) {
                const auto background=SDL_MapSurfaceRGBA(actual.get(),Uint8(x*17),Uint8(y*23),121,Uint8(80+x*11));
                put(actual.get(),x,y,background);put(reference.get(),x,y,background);
            }
            GAGCore::SurfaceRaster::blit(actual.get(),source.get(),src,destination,opacity,false,
                GAGCore::SurfaceRaster::BlitBlend::Triangle);
            for(int y=0;y<9;++y)for(int x=0;x<10;++x) {
                if(x<destination.x || x>=destination.x+destination.w || y<destination.y || y>=destination.y+destination.h) continue;
                const int sx=src.x+int(((std::int64_t(x)-destination.x)*2+1)*src.w/(std::int64_t(destination.w)*2));
                const int sy=src.y+int(((std::int64_t(y)-destination.y)*2+1)*src.h/(std::int64_t(destination.h)*2));
                if(sx<0 || sx>=source->w || sy<0 || sy>=source->h) continue;
                GAGCore::SurfaceRaster::blit(reference.get(),source.get(),{sx,sy,1,1},{x,y,1,1},opacity,false,
                    GAGCore::SurfaceRaster::BlitBlend::Triangle);
            }
            for(int y=0;y<9;++y)for(int x=0;x<10;++x)
                CHECK(pixel(actual.get(),x,y)==pixel(reference.get(),x,y));
            Uint8 r,g,b,alpha;
            REQUIRE(SDL_GetSurfaceColorMod(source.get(),&r,&g,&b));
            REQUIRE(SDL_GetSurfaceAlphaMod(source.get(),&alpha));
            CHECK(r==31);CHECK(g==97);CHECK(b==181);CHECK(alpha==79);
            Uint32 retainedKey;
            CHECK(SDL_GetSurfaceColorKey(source.get(),&retainedKey)==keyed);
            if(keyed) CHECK(retainedKey==key);
        }
    }
}

TEST_CASE(
    "regional kernels match the original rasterizer across clipped tiles and alpha arithmetic" *
    doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    for (auto format : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
        for (Uint8 alpha : {Uint8(0), Uint8(1), Uint8(79), Uint8(254), Uint8(255)})
            for (SDL_Rect dst :
                 std::array<SDL_Rect, 3>{{{-4, -3, 37, 31}, {1, 2, 13, 11}, {4, 3, 21, 17}}})
            {
                auto input = surface(13, 11, format), original = surface(31, 29, format),
                     actual = surface(31, 29, format);
                for (int y = 0; y < 11; ++y)
                    for (int x = 0; x < 13; ++x)
                        put(input.get(), x, y,
                            SDL_MapSurfaceRGBA(input.get(), x * 17, y * 21, 39,
                                               Uint8((x + y * 3) * 19)));
                const SDL_Rect clip{2, 1, 27, 26}, src{-1, -2, 13, 11};
                REQUIRE(SDL_SetSurfaceClipRect(original.get(), &clip));
                for (int y = 0; y < 29; ++y)
                    for (int x = 0; x < 31; ++x)
                    {
                        const auto value = SDL_MapSurfaceRGBA(original.get(), x * 7, y * 9, 123,
                                                              Uint8(90 + x * 3));
                        put(original.get(), x, y, value);
                        put(actual.get(), x, y, value);
                    }
                blit(original.get(), input.get(), src, dst, alpha, false, BlitBlend::Triangle);
                RasterCommand command;
                command.kind = RasterCommand::Kind::Blit;
                command.source = {static_cast<const Uint8 *>(input->pixels), input->w, input->h,
                                  input->pitch, *SDL_GetPixelFormatDetails(format)};
                command.sourceRect = src;
                command.destination = dst;
                command.clip = clip;
                command.opacity = alpha;
                WritablePixelView output{static_cast<Uint8 *>(actual->pixels), actual->w, actual->h,
                                         actual->pitch, *SDL_GetPixelFormatDetails(format)};
                for (int y = 0; y < 29; y += 7)
                    for (int x = 0; x < 31; x += 7)
                        executeRegion(std::span(&command, 1), output, {x, y, 7, 7});
                for (int y = 0; y < 29; ++y)
                    for (int x = 0; x < 31; ++x)
                        CHECK(pixel(actual.get(), x, y) == pixel(original.get(), x, y));
                for (auto blend : {FillBlend::Native, FillBlend::SourceOver})
                {
                    const auto color = SDL_MapSurfaceRGBA(actual.get(), 49, 149, 209, 255);
                    fill(original.get(), dst, color, alpha, blend);
                    command.kind = RasterCommand::Kind::Fill;
                    command.color = color;
                    command.fillBlend = blend;
                    for (int y = 0; y < 29; y += 7)
                        for (int x = 0; x < 31; x += 7)
                            executeRegion(std::span(&command, 1), output, {x, y, 7, 7});
                    for (int y = 0; y < 29; ++y)
                        for (int x = 0; x < 31; ++x)
                            CHECK(pixel(actual.get(), x, y) == pixel(original.get(), x, y));
                }
            }
}

TEST_CASE("opaque regional rows preserve padded pitches source clipping and scaled height" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    const auto format = *SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    std::array<Uint32, 20 * 7> source;
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = 0x73100000u + Uint32(i * 977);
    for (const int height : {11, 17})
    {
        std::array<Uint32, 24 * 12> actual, expected;
        actual.fill(0x12345678); expected = actual;
        RasterCommand command;
        command.kind = RasterCommand::Kind::Blit;
        command.source = {reinterpret_cast<const Uint8 *>(source.data()), 13, 7, 20 * 4, format};
        command.sourceRect = {-3, -2, 20, 11};
        command.destination = {1, 1, 20, height};
        command.clip = {3, 2, 12, 8};
        command.opaque = true;
        // Independent scalar sampling also checks untouched row padding.
        for (int y = 2; y < 10; ++y)
            for (int x = 3; x < 15; ++x)
            {
                const int sx = -3 + x - 1;
                const int sy = -2 + ((y - 1) * 2 + 1) * 11 / (height * 2);
                if (sx >= 0 && sx < 13 && sy >= 0 && sy < 7)
                    expected[y * 24 + x] = source[sy * 20 + sx];
            }
        const WritablePixelView output{
            reinterpret_cast<Uint8 *>(actual.data()), 19, 12, 24 * 4, format};
        for (int y = 0; y < 12; y += 5)
            for (int x = 0; x < 19; x += 5)
                executeCommand(command, output, {x, y, 5, 5});
        CHECK(actual == expected);
    }
}

TEST_CASE("opaque regional alias views retain forward pixel traversal" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    const auto format = *SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    for (const int sourceOffset : {0, 1})
    {
        std::array<Uint32, 12 * 2> actual, expected;
        for (std::size_t i = 0; i < actual.size(); ++i)
            actual[i] = 0xff000000u + Uint32(i * 977);
        expected = actual;
        const int destinationX = 1 - sourceOffset;
        RasterCommand command;
        command.kind = RasterCommand::Kind::Blit;
        command.source = {reinterpret_cast<const Uint8 *>(actual.data() + sourceOffset),
                          9, 2, 12 * 4, format};
        command.sourceRect = {0, 0, 8, 2};
        command.destination = {destinationX, 0, 8, 2};
        command.clip = {0, 0, 10, 2};
        command.opaque = true;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 8; ++x)
                expected[y * 12 + destinationX + x] = expected[y * 12 + sourceOffset + x];
        executeCommand(command,
                       {reinterpret_cast<Uint8 *>(actual.data()), 10, 2, 12 * 4, format},
                       {0, 0, 10, 2});
        CHECK(actual == expected);
    }
}

namespace
{
void checkSignedPitchAlias(bool negativeSource)
{
    using namespace GAGCore::SurfaceRaster;
    const auto format = *SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    std::array<Uint32, 16> actual, expected;
    for (std::size_t i = 0; i < actual.size(); ++i)
        actual[i] = 0xff000000u + Uint32(i * 977);
    expected = actual;
    const int sourceOffset = negativeSource ? 7 : 3;
    const int sourceHeight = negativeSource ? 4 : 1;
    const int sourcePitch = negativeSource ? -8 : 8;
    const int targetOffset = negativeSource ? 4 : 8;
    const int targetHeight = negativeSource ? 1 : 4;
    const int targetPitch = negativeSource ? 8 : -8;
    RasterCommand command;
    command.kind = RasterCommand::Kind::Blit;
    command.source = {reinterpret_cast<const Uint8 *>(actual.data() + sourceOffset),
                      2, sourceHeight, sourcePitch, format};
    command.sourceRect = {0, 0, 2, sourceHeight};
    command.destination = command.clip = {0, 0, 2, targetHeight};
    command.opaque = true;
    // Preserve forward sampling when vertical scaling reaches an overlapping
    // row. An overlapping memcpy would instead retain the second source word.
    for (int y = 0; y < targetHeight; ++y)
    {
        const int sy = (2 * y + 1) * sourceHeight / (2 * targetHeight);
        for (int x = 0; x < 2; ++x)
            expected[targetOffset + y * (targetPitch / 4) + x] =
                expected[sourceOffset + sy * (sourcePitch / 4) + x];
    }
    executeCommand(command,
                   {reinterpret_cast<Uint8 *>(actual.data() + targetOffset),
                    2, targetHeight, targetPitch, format},
                   command.destination);
    CHECK(actual == expected);
}
}
TEST_CASE("negative source pitch preserves aliased regional forward sampling" *
          doctest::test_suite("SurfaceRaster"))
{
    checkSignedPitchAlias(true);
}
TEST_CASE("negative target pitch preserves aliased regional forward sampling" *
          doctest::test_suite("SurfaceRaster"))
{
    checkSignedPitchAlias(false);
}
