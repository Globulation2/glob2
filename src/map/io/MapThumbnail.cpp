// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapThumbnail.h"
#include "BinaryStream.h"
#include "FileManager.h"
#include "GUIBase.h"
#include "Map.h"
#include "MapHeader.h"
#include "Toolkit.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <list>
#include <iostream>
#include <stdexcept>
#include <zlib.h>
using namespace GAGCore;
namespace
{
constexpr int LegacyBytes = 128 * 128 * 3;
const int colors[7][3] = {{0,90,0}, {0,40,120}, {170,170,0},
    {0,60,0}, {211,207,167}, {104,112,124}, {41,157,165}};
std::vector<Uint8> compress(const std::vector<Uint8>& input)
{
    uLongf length = compressBound(input.size());
    std::vector<Uint8> output(length);
    if (compress2(output.data(), &length, input.data(), input.size(), 6) != Z_OK)
        throw std::runtime_error("Could not compress map preview");
    output.resize(length);
    return output;
}
size_t inflateExact(const Uint8* input, size_t size, Uint8* output, size_t expected)
{
    z_stream z{};
    z.next_in = const_cast<Bytef*>(input); z.avail_in = size;
    z.next_out = output; z.avail_out = expected;
    if (inflateInit(&z) != Z_OK) throw std::runtime_error("Map preview inflater");
    int result = inflate(&z, Z_FINISH);
    const auto used = z.total_in;
    const bool good = result == Z_STREAM_END && z.total_out == expected;
    inflateEnd(&z);
    if (!good) throw std::runtime_error("Invalid map preview pixels");
    return used;
}
std::shared_ptr<MapThumbnail::Image> resample(const MapThumbnail::Image& source, int w, int h)
{
    auto out = std::make_shared<MapThumbnail::Image>();
    out->width = w; out->height = h; out->rgb.resize(w*h*3);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x)
    {
        int sums[3] = {}, count = 0;
        const int x0=x*source.width/w, y0=y*source.height/h;
        for (int sy=y0; sy<std::max(y0+1,(y+1)*source.height/h); ++sy)
            for (int sx=x0; sx<std::max(x0+1,(x+1)*source.width/w); ++sx)
            {
                for (int c=0; c<3; ++c) sums[c] += source.rgb[(sy*source.width+sx)*3+c];
                ++count;
            }
        for (int c=0; c<3; ++c) out->rgb[(y*w+x)*3+c] = sums[c]/count;
    }
    return out;
}
}

void MapThumbnail::loadFromMap(const std::string& filename)
{
    *this = MapThumbnail();
    if (filename.empty()) return;
    // Only cache real, revision-addressable files. Sixteen 512x512 RGB images
    // bound retained pixels to 12 MiB; shared copies do not duplicate them.
    struct Entry { std::string path; std::filesystem::file_time_type time; uintmax_t size; MapThumbnail thumbnail; };
    static std::list<Entry> cache;
    std::string path;
    std::error_code ec;
    auto files = Toolkit::getFileManager();
    for (unsigned i=0; i<=files->getDirCount(); ++i)
    {
        auto candidate = i == files->getDirCount() ? std::filesystem::path(filename)
            : std::filesystem::path(files->getDir(i)) / filename;
        if (std::filesystem::is_regular_file(candidate, ec))
        { path = std::filesystem::canonical(candidate, ec).string(); break; }
    }
    auto time = std::filesystem::last_write_time(path, ec);
    const bool timed = !ec;
    auto size = std::filesystem::file_size(path, ec);
    const bool cacheable = timed && !ec;
    if (cacheable)
        for (auto i=cache.begin(); i!=cache.end(); ++i)
            if (i->path == path && i->time == time && i->size == size)
            { *this=i->thumbnail; cache.splice(cache.begin(), cache, i); return; }
    try
    {
        BinaryInputStream stream(files->openInputStreamBackend(filename));
        BinaryInputStream::CheckedReads checked(&stream);
        if (!stream.isValid() || stream.isEndOfStream() || !stream.canSeek()) return;
        MapHeader header;
        if (!header.load(&stream)) return;
        stream.seekFromStart(header.getMapOffset());
        Map map;
        if (!map.load(&stream, header)) return;
        loadFromMap(map);
        if (cacheable && isLoaded())
        {
            cache.remove_if([&](const Entry& e) { return e.path == path; });
            cache.push_front({path,time,size,*this});
            while (cache.size()>16) cache.pop_back();
        }
    }
    catch (const std::exception& error) { *this=MapThumbnail(); std::cerr << "Map thumbnail load: " << error.what() << '\n'; }
}

void MapThumbnail::loadFromMap(const Map& map)
{
    *this=MapThumbnail();
    const int mw=map.getW(), mh=map.getH();
    if (mw<=0 || mh<=0 || mw>32767 || mh>32767) return;
    const int longest=std::max(mw,mh), resolution=std::min(MaxResolution,longest);
    auto result=std::make_shared<Image>();
    result->width=std::max(1,mw*resolution/longest);
    result->height=std::max(1,mh*resolution/longest);
    result->rgb.resize(result->width*result->height*3);
    for (int y=0; y<result->height; ++y) for (int x=0; x<result->width; ++x)
    {
        int sums[3]={}, count=0;
        // Half-open cells neither double-count boundaries nor sample the next
        // torus period at the right/bottom edge.
        for (int sy=y*mh/result->height; sy<(y+1)*mh/result->height; ++sy)
            for (int sx=x*mw/result->width; sx<(x+1)*mw/result->width; ++sx)
            {
                const auto terrain=map.getUMTerrain(sx,sy);
                int color=terrain==GRASS ? 0 : terrain==WATER ? 1 : 2;
                const int resources[]={WOOD,WHEAT,STONE,ALGA};
                for (int r=0; r<4; ++r) if (map.isResourceTakeable(sx,sy,resources[r]))
                { color=r+3; break; }
                color=std::clamp(color,0,6);
                for (int c=0; c<3; ++c) sums[c]+=colors[color][c];
                ++count;
            }
        for (int c=0; c<3; ++c) result->rgb[(y*result->width+x)*3+c]=sums[c]/count;
    }
    lastW=mw; lastH=mh; image=std::move(result);
}

void MapThumbnail::encodeData(OutputStream* stream) const
{
    // Old clients inflate just the first zlib stream and ignore trailing bytes
    // inside compressedLength. The optional MPV2 member fits the same frame.
    std::vector<Uint8> legacy(LegacyBytes,0);
    if (image)
    {
        const int longest=std::max(lastW,lastH);
        auto small=resample(*image,std::max(1,lastW*128/longest),std::max(1,lastH*128/longest));
        int ox=(128-small->width)/2, oy=(128-small->height)/2;
        for (int y=0; y<small->height; ++y) for (int x=0; x<small->width; ++x)
            for (int c=0; c<3; ++c)
                legacy[((x+ox)*128+y+oy)*3+c]=small->rgb[(y*small->width+x)*3+c];
    }
    auto encoded=compress(legacy);
    if (image)
    {
        auto detail=image;
        while (std::max(detail->width,detail->height)>128)
        {
            auto member=compress(detail->rgb);
            if (encoded.size()+8+member.size()<=MaxEncodedBytes)
            {
                encoded.insert(encoded.end(),{'M','P','V','2',Uint8(detail->width>>8),Uint8(detail->width),
                    Uint8(detail->height>>8),Uint8(detail->height)});
                encoded.insert(encoded.end(),member.begin(),member.end());
                break;
            }
            detail=resample(*detail,std::max(1,detail->width/2),std::max(1,detail->height/2));
        }
    }
    stream->writeEnterSection("MapThumbnail");
    stream->writeSint16(lastW,"lastW"); stream->writeSint16(lastH,"lastH");
    stream->writeUint32(encoded.size(),"compressedLength");
    stream->write(encoded.data(),encoded.size(),"compressed");
    stream->writeLeaveSection();
}

void MapThumbnail::decodeData(InputStream* stream, Uint32)
{
    *this=MapThumbnail();
    try
    {
        BinaryInputStream::CheckedReads checked(stream);
        stream->readEnterSection("MapThumbnail");
        const int mw=stream->readSint16("lastW"), mh=stream->readSint16("lastH");
        const Uint32 length=stream->readUint32("compressedLength");
        if (length==0 || length>MaxEncodedBytes) throw std::runtime_error("Map preview length");
        std::vector<Uint8> encoded(length), legacy(LegacyBytes);
        stream->read(encoded.data(),length,"compressed");
        stream->readLeaveSection();
        if (mw<=0 || mh<=0) return;
        const size_t used=inflateExact(encoded.data(),length,legacy.data(),legacy.size());
        std::shared_ptr<Image> result=std::make_shared<Image>();
        if (used<length)
        {
            if (length-used<8 || std::memcmp(encoded.data()+used,"MPV2",4)!=0)
                throw std::runtime_error("Unknown map preview extension");
            auto p=encoded.data()+used+4;
            result->width=(p[0]<<8)|p[1]; result->height=(p[2]<<8)|p[3];
            if (result->width<1 || result->height<1 || result->width>MaxResolution || result->height>MaxResolution ||
                std::abs(result->width*mh-result->height*mw)>std::max(mw,mh))
                throw std::runtime_error("Map preview dimensions");
            result->rgb.resize(result->width*result->height*3);
            if (inflateExact(encoded.data()+used+8,length-used-8,result->rgb.data(),result->rgb.size())!=length-used-8)
                throw std::runtime_error("Trailing map preview data");
        }
        else
        {
            result->width=std::max(1,mw*128/std::max(mw,mh));
            result->height=std::max(1,mh*128/std::max(mw,mh));
            result->rgb.resize(result->width*result->height*3);
            int ox=(128-result->width)/2, oy=(128-result->height)/2;
            for (int y=0; y<result->height; ++y) for (int x=0; x<result->width; ++x)
                for (int c=0; c<3; ++c) result->rgb[(y*result->width+x)*3+c]=legacy[((x+ox)*128+y+oy)*3+c];
        }
        lastW=mw; lastH=mh; image=std::move(result);
    }
    catch (const std::exception& error) { *this=MapThumbnail(); std::cerr << "Map thumbnail decode: " << error.what() << '\n'; }
}

void MapThumbnail::loadIntoSurface(DrawableSurface* surface) const
{
    if (!image || !surface || surface->getW()<=0 || surface->getH()<=0) return;
    auto scaled=resample(*image,surface->getW(),surface->getH());
    for (int y=0; y<scaled->height; ++y) for (int x=0; x<scaled->width; ++x)
    {
        auto p=&scaled->rgb[(y*scaled->width+x)*3];
        surface->drawPixel(x,y,Color(p[0],p[1],p[2]));
    }
}
