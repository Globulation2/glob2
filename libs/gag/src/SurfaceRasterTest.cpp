// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SurfaceRaster.h>
#include <array>
#include <cstring>
#include <memory>
#include <limits>
#include <algorithm>
#include <cmath>
#include <vector>

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

namespace
{
Uint32 portableReference(Uint32 sample, Uint32 original,
                         const SDL_PixelFormatDetails &source, const SDL_PixelFormatDetails &target,
                         Uint8 opacity, Uint8 red, Uint8 green, Uint8 blue, bool forceAlpha)
{
    const unsigned alpha = (source.Amask ? (sample & source.Amask) >> source.Ashift : 255) * opacity / 255;
    if (!alpha) return original;
    const auto channel = [&](Uint32 smask, unsigned sshift, Uint32 dmask, unsigned dshift, unsigned tint)
    {
        const unsigned value = ((sample & smask) >> sshift) * tint / 255;
        return ((value * alpha + ((original & dmask) >> dshift) * (255-alpha)) / 255) << dshift;
    };
    const unsigned outAlpha = forceAlpha ? 255 : alpha + ((original & target.Amask) >> target.Ashift)*(255-alpha)/255;
    return channel(source.Rmask,source.Rshift,target.Rmask,target.Rshift,red) |
        channel(source.Gmask,source.Gshift,target.Gmask,target.Gshift,green) |
        channel(source.Bmask,source.Bshift,target.Bmask,target.Bshift,blue) |
        (target.Amask ? outAlpha << target.Ashift : 0);
}
}
TEST_CASE("portable nearest scalar and SIMD match independent arithmetic across tiles formats and signed pitches" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    constexpr int width=23,height=11,pitch=29;
    const auto sourceFormat=*SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    std::array<Uint32,pitch*height> source;
    for (std::size_t i=0;i<source.size();++i)
        source[i]=Uint32(i*2654435761u);
    source[0]=0; source[1]=0xff123456; source[2]=0x01123456;
    for (const auto format : {SDL_PIXELFORMAT_ARGB8888,SDL_PIXELFORMAT_RGBA32,SDL_PIXELFORMAT_XRGB8888})
    for (const Uint8 opacity : {Uint8(0),Uint8(1),Uint8(37),Uint8(128),Uint8(254),Uint8(255)})
    for (bool forceAlpha : {false,true})
    for (bool tint : {false,true})
    for (bool opaque : {false,true})
    for (bool signedPitch : {false,true})
    for (const SDL_Rect destination : std::array<SDL_Rect,3>{{{0,0,width,height},{-3,-2,37,19},{4,1,13,7}}})
    {
        const auto targetFormat=*SDL_GetPixelFormatDetails(format);
        std::array<Uint32,pitch*height> scalar,automatic,expected;
        for (std::size_t i=0;i<scalar.size();++i) scalar[i]=Uint32(i*977u+0x137ab892);
        automatic=expected=scalar;
        const int base=signedPitch ? pitch*(height-1) : 0;
        const int rowStep=signedPitch ? -pitch : pitch;
        RasterCommand command;
        command.kind=RasterCommand::Kind::Blit;
        command.blend=RasterCommand::Blending::Portable;
        command.source={reinterpret_cast<const Uint8 *>(source.data()+base),width,height,rowStep*4,sourceFormat};
        command.sourceRect={0,0,width,height};
        command.destination=destination;
        command.clip={1,0,width-2,height};
        command.opacity=opacity;
        command.forceOpaqueAlpha=forceAlpha;command.opaque=opaque;
        if(tint) {command.red=31;command.green=97;command.blue=181;}
        for(int y=0;y<height;++y)for(int x=1;x<width-1;++x)
        {
            if(x<destination.x || x>=destination.x+destination.w || y<destination.y || y>=destination.y+destination.h)continue;
            const int sx=int((std::int64_t(x-destination.x)*2+1)*width/(2*destination.w));
            const int sy=int((std::int64_t(y-destination.y)*2+1)*height/(2*destination.h));
            const int index=base+y*rowStep+x;
            const auto sample=source[base+sy*rowStep+sx];
            if(opaque && opacity==255 && !tint && sourceFormat.format==targetFormat.format)
                expected[index]=sample;
            else
                expected[index]=portableReference(opaque ? sample|sourceFormat.Amask : sample,
                    expected[index],sourceFormat,targetFormat,opacity,command.red,command.green,command.blue,forceAlpha);
        }
        WritablePixelView a{reinterpret_cast<Uint8 *>(automatic.data()+base),width,height,rowStep*4,targetFormat};
        WritablePixelView b{reinterpret_cast<Uint8 *>(scalar.data()+base),width,height,rowStep*4,targetFormat};
        executeCommand(command,b,{0,0,width,height},KernelMode::Scalar);
        for(int y=0;y<height;y+=5)for(int x=0;x<width;x+=7)
            executeCommand(command,a,{x,y,7,5});
        CHECK(scalar==expected);
        CHECK(automatic==expected);
    }
}
TEST_CASE("portable fill scalar and SIMD match independent arithmetic with stored alpha clipping and signed pitches" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    for (const auto packedFormat : {SDL_PIXELFORMAT_ARGB8888,SDL_PIXELFORMAT_RGBA32,SDL_PIXELFORMAT_XRGB8888})
    for (const Uint8 storedAlpha : {Uint8(0),Uint8(127),Uint8(255)})
    for (bool negativePitch : {false,true})
    for (unsigned alpha=0;alpha<=255;++alpha)
    {
        const auto format=*SDL_GetPixelFormatDetails(packedFormat);
        std::array<Uint32,37*3> actual,scalar,expected;
        for(unsigned i=0;i<actual.size();++i)actual[i]=i*2654435761u;
        scalar=expected=actual;
        const int base=negativePitch ? 74 : 0,step=negativePitch ? -37 : 37;
        RasterCommand command;
        command.kind=RasterCommand::Kind::Fill;
        command.fillBlend=FillBlend::PortableSourceOver;
        command.destination={0,0,31,3};command.clip={1,0,29,3};
        command.color=(0x31u<<format.Rshift)|(0x7bu<<format.Gshift)|(0xc9u<<format.Bshift)|
            (format.Amask ? Uint32(storedAlpha)<<format.Ashift : Uint32(storedAlpha)<<24);
        command.opacity=Uint8(alpha);
        // Packed color alpha does not participate: explicit fill opacity owns coverage.
        for(int y=0;y<3;++y)for(int x=1;x<30;++x)
        {
            const int index=base+y*step+x;
            expected[index]=portableReference(command.color|format.Amask,expected[index],format,format,
                Uint8(alpha),255,255,255,false);
        }
        const WritablePixelView output{reinterpret_cast<Uint8 *>(actual.data()+base),31,3,step*4,format};
        for(int y=0;y<3;y+=2)for(int x=0;x<31;x+=7)
            executeCommand(command,output,{x,y,7,2});
        executeCommand(command,{reinterpret_cast<Uint8 *>(scalar.data()+base),31,3,step*4,format},
            {0,0,31,3},KernelMode::Scalar);
        CHECK(actual==expected);CHECK(scalar==expected);
    }
}
TEST_CASE("portable linear sampling uses whole fractional geometry and premultiplied frame edges" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    const auto format=*SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    const std::array<Uint32,8> input{0xffff0000,0x0000ff00,0xff0000ff,0xffffffff,
                                  0x00ffffff,0x80123456,0x01334455,0xff876543};
    for (const auto targetFormat : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
    for(const SDL_FRect dst : std::array<SDL_FRect,3>{{{-.25f,.75f,17.5f,9.25f},{2.125f,-.375f,4.5f,3.75f},{1.5f,2.5f,8.f,4.f}}})
    for(Uint8 opacity : {Uint8(0),Uint8(1),Uint8(127),Uint8(255)})
    {
        const auto tf=*SDL_GetPixelFormatDetails(targetFormat);
        std::array<Uint32,24*15> actual,expected,whole;
        for(std::size_t i=0;i<actual.size();++i)actual[i]=Uint32(i*977+0x475bc991);
        whole=expected=actual;
        RasterCommand command;
        command.kind=RasterCommand::Kind::Blit;
        command.blend=RasterCommand::Blending::Portable;
        command.sampling=RasterCommand::Sampling::LinearPremultiplied;
        command.source={reinterpret_cast<const Uint8 *>(input.data()),4,2,16,format};
        command.sourceRect={1,0,2,2}; // Adjacent opaque frame pixels cannot bleed in.
        command.destinationFloat=dst;
        const int left=int(std::ceil(dst.x-.5f)),top=int(std::ceil(dst.y-.5f));
        const int right=int(std::ceil(dst.x+dst.w-.5f)),bottom=int(std::ceil(dst.y+dst.h-.5f));
        command.destination={left,top,right-left,bottom-top};
        command.clip={1,1,19,12};command.opacity=opacity;
        if(opacity)
        for(int y=std::max(1,top);y<std::min(13,bottom);++y)
        for(int x=std::max(1,left);x<std::min(20,right);++x)
        {
            const float u=std::clamp((x-dst.x+.5f)*2/dst.w-.5f,0.f,1.f);
            const float v=std::clamp((y-dst.y+.5f)*2/dst.h-.5f,0.f,1.f);
            const int ix=int(u),iy=int(v);
            const float fx=u-ix,fy=v-iy;
            const float weights[]{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
            const Uint32 samples[]{input[iy*4+1+ix],input[iy*4+1+std::min(ix+1,1)],
                input[std::min(iy+1,1)*4+1+ix],input[std::min(iy+1,1)*4+1+std::min(ix+1,1)]};
            float coverage=0,red=0,green=0,blue=0;
            for(int i=0;i<4;++i)
            {
                const float a=float(samples[i]>>24)*weights[i]/255.f;
                coverage+=a;red+=((samples[i]>>16)&255)*a;green+=((samples[i]>>8)&255)*a;blue+=(samples[i]&255)*a;
            }
            const float fade=opacity/255.f;
            coverage*=fade;red*=fade;green*=fade;blue*=fade;
            const Uint32 original=expected[y*24+x];
            const auto c=[&](float value,unsigned before){return Uint32(std::clamp(std::lround(value+before*(1-coverage)),0l,255l));};
            expected[y*24+x]=(c(coverage*255,(original & tf.Amask) >> tf.Ashift) << tf.Ashift) |
                (c(red,(original & tf.Rmask) >> tf.Rshift) << tf.Rshift) |
                (c(green,(original & tf.Gmask) >> tf.Gshift) << tf.Gshift) |
                (c(blue,(original & tf.Bmask) >> tf.Bshift) << tf.Bshift);
        }
        for(int y=0;y<15;y+=3)for(int x=0;x<21;x+=5)
            executeCommand(command,{reinterpret_cast<Uint8 *>(actual.data()),21,15,24*4,tf},{x,y,5,3});
        executeCommand(command,{reinterpret_cast<Uint8 *>(whole.data()),21,15,24*4,tf},{0,0,21,15},KernelMode::Scalar);
        CHECK(actual==expected);CHECK(whole==expected);
    }
}
TEST_CASE("SDL nearest fixed-point centers preserve samples across clipped tile boundaries" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    const auto format=*SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    auto input=surface(6,3,SDL_PIXELFORMAT_ARGB8888);
    for(int y=0;y<3;++y)for(int x=0;x<6;++x)put(input.get(),x,y,0xff000000u|Uint32((y*6+x)*977));
    for(const int width : {9,257,513})
    {
        auto reference=surface(width,7,SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(SDL_SetSurfaceBlendMode(input.get(),SDL_BLENDMODE_NONE));
        const SDL_Rect src{0,0,6,3}; SDL_Rect dst{0,0,width,7};
        REQUIRE(SDL_BlitSurfaceScaled(input.get(),&src,reference.get(),&dst,SDL_SCALEMODE_NEAREST));
        std::vector<Uint32> expected(std::size_t(width)*7,0x72456381),tiled=expected,whole=expected;
        const int stepX=(6*65536)/width,stepY=(3*65536)/7;
        for(int y=1;y<6;++y)for(int x=1;x<width-1;++x)
        {
            const int sx=(stepX/2+x*stepX)/65536,sy=(stepY/2+y*stepY)/65536;
            expected[std::size_t(y)*width+x]=pixel(input.get(),sx,sy);
            CHECK(expected[std::size_t(y)*width+x]==pixel(reference.get(),x,y));
        }
        RasterCommand command;
        command.kind=RasterCommand::Kind::Blit;
        command.blend=RasterCommand::Blending::Portable;
        command.sampling=RasterCommand::Sampling::SDLNearest;
        command.source={static_cast<const Uint8 *>(input->pixels),6,3,input->pitch,format};
        command.sourceRect=src;command.destination=dst;command.clip={1,1,width-2,5};
        for(int x=0;x<width;x+=7)
            executeCommand(command,{reinterpret_cast<Uint8 *>(tiled.data()),width,7,width*4,format},{x,0,7,7});
        executeCommand(command,{reinterpret_cast<Uint8 *>(whole.data()),width,7,width*4,format},dst,KernelMode::Scalar);
        CHECK(tiled==expected);CHECK(whole==expected);
    }
}
TEST_CASE("wide portable aliased views retain forward row traversal without sampling allocations" *
          doctest::test_suite("SurfaceRaster"))
{
    using namespace GAGCore::SurfaceRaster;
    const auto format=*SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    for (const auto sampling : {RasterCommand::Sampling::Nearest,RasterCommand::Sampling::SDLNearest})
    {
        std::array<Uint32,350*2> actual,expected;
        for(std::size_t i=0;i<actual.size();++i)actual[i]=0xff000000u|Uint32(i*977);
        expected=actual;
        RasterCommand command;
        command.kind=RasterCommand::Kind::Blit;
        command.blend=RasterCommand::Blending::Portable;command.sampling=sampling;
        command.source={reinterpret_cast<const Uint8 *>(actual.data()),200,2,350*4,format};
        command.sourceRect={0,0,200,2};command.destination={10,0,300,2};command.clip={0,0,320,2};
        command.opaque=true;
        for(int y=0;y<2;++y)for(int x=10;x<310;++x)
        {
            const int delta=x-10;
            const int step=(200*65536)/300;
            const int sx=sampling==RasterCommand::Sampling::Nearest ? (2*delta+1)*200/600 :
                (step/2+delta*step)/65536;
            expected[y*350+x]=expected[y*350+sx];
        }
        executeCommand(command,{reinterpret_cast<Uint8 *>(actual.data()),320,2,350*4,format},{0,0,320,2});
        CHECK(actual==expected);
    }
}
