// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackedSpriteFrame.h"
#include "AssetVideoDecoder.h"
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

namespace GAGCore {
namespace {
bool safeFile(const std::string& file, const char *suffix) {
    return file.ends_with(suffix) && file.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == std::string::npos &&
        file.find("..") == std::string::npos;
}
bool readNumber(std::istringstream& input, unsigned& value) {
    std::string field;
    if (!std::getline(input, field, ',')) return false;
    const auto end = field.data() + field.size();
    const auto result = std::from_chars(field.data(), end, value);
    return result.ec == std::errc() && result.ptr == end && value <= 2048;
}
}

bool PackedSpriteFrame::parse(const std::string& token, PackedSpriteFrame& out) {
    if (token.empty() || token[0] != '@' || token.size() > 512 || token.back() == ',') return false;
    std::istringstream input(token.substr(1));
    PackedSpriteFrame item;
    if (!std::getline(input, item.file, ',')) return false;
    if (safeFile(item.file, ".h264")) {
        if (!std::getline(input, item.alpha, ',') ||
            (item.alpha != "-" && !safeFile(item.alpha, ".z")) ||
            !readNumber(input, item.width) || !readNumber(input, item.height) ||
            !readNumber(input, item.count) || !readNumber(input, item.frame) ||
            !AssetVideoDecoder::validDimensions(item.width, item.height, item.count) ||
            item.frame >= item.count)
            return false;
        if (input.peek() == std::char_traits<char>::eof()) {
            item.cropWidth = item.width;
            item.cropHeight = item.height;
            out = std::move(item);
            return true;
        }
    } else if (!safeFile(item.file, ".webp")) return false;
    if (!readNumber(input, item.x) || !readNumber(input, item.y) ||
        !readNumber(input, item.cropWidth) || !readNumber(input, item.cropHeight) ||
        !item.cropWidth || !item.cropHeight || item.x + item.cropWidth > 2048 ||
        item.y + item.cropHeight > 2048 || input.peek() != std::char_traits<char>::eof())
        return false;
    if (item.count && (item.x + item.cropWidth > item.width || item.y + item.cropHeight > item.height))
        return false;
    out = std::move(item);
    return true;
}

AssetLoader::Handle<AssetImage> PackedSpriteFrame::request(AssetLoader& loader,
        const std::string& path, bool mipmaps, AssetLoader::Priority priority) const {
    const auto key = "packed-sprite:" + path + (mipmaps ? ":mips" : ":base");
    if (auto ready = loader.findReady(key, typeid(AssetImage), priority))
        return AssetLoader::Handle<AssetImage>(std::move(ready));
    const auto directory = path.substr(0, path.find_last_of('/') + 1);
    AssetLoader::Handle<AssetImage> page;
    AssetLoader::Handle<AssetVideoFrames> video;
    AssetLoader::Handle<AssetLoader::Bytes> opacity;
    std::vector<AssetLoader::Dependency> dependencies;
    if (count) {
        video = loader.requestVideoFrames(directory + file, width, height, count, priority);
        dependencies.push_back(video.dependency());
        if (alpha != "-") {
            const auto rawSize = size_t(width) * height;
            auto bytes = loader.requestBytesImpl(directory + alpha, priority, true, rawSize + 65536);
            opacity = loader.request<AssetLoader::Bytes>("packed-alpha:" + directory + alpha + ':' + std::to_string(rawSize),
                {bytes.dependency()}, [bytes, rawSize] {
                    auto source = bytes.get();
                    auto result = std::make_shared<AssetLoader::Bytes>(rawSize);
                    z_stream stream{};
                    stream.next_in = const_cast<unsigned char*>(source->data());
                    stream.avail_in = static_cast<uInt>(source->size());
                    stream.next_out = result->data();
                    stream.avail_out = static_cast<uInt>(rawSize);
                    if (inflateInit(&stream) != Z_OK)
                        throw std::runtime_error("Cannot initialize packed sprite alpha decoder");
                    const int status = inflate(&stream, Z_FINISH);
                    inflateEnd(&stream);
                    if (status != Z_STREAM_END || stream.total_out != rawSize || stream.avail_in)
                        throw std::runtime_error("Invalid packed sprite alpha");
                    return result;
                }, rawSize, priority);
            dependencies.push_back(opacity.dependency());
        }
    } else {
        page = loader.requestImage(directory + file, priority);
        dependencies.push_back(page.dependency());
    }
    return loader.request<AssetImage>(key, std::move(dependencies), [item = *this, page, video, opacity, mipmaps] {
        auto source = item.count ? video.get()->frame(item.frame) : page.get();
        if (!source || !source->surface ||
            item.x + item.cropWidth > unsigned(source->surface->w) ||
            item.y + item.cropHeight > unsigned(source->surface->h))
            throw std::runtime_error("Packed sprite crop is outside its frame");
        SDL_Rect crop{int(item.x), int(item.y), int(item.cropWidth), int(item.cropHeight)};
        auto result = std::make_shared<AssetImage>(SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_ARGB8888));
        if (!result->surface) throw std::runtime_error(SDL_GetError());
        auto *surface = source->surface;
        const auto *format = SDL_GetPixelFormatDetails(surface->format);
        if (!format || !SDL_ConvertPixels(crop.w, crop.h, surface->format,
                static_cast<const uint8_t*>(surface->pixels) + crop.y * surface->pitch + crop.x * format->bytes_per_pixel,
                surface->pitch, result->surface->format, result->surface->pixels, result->surface->pitch))
            throw std::runtime_error(SDL_GetError());
        if (const auto alpha = opacity.get()) {
            for (unsigned row = 0; row < item.cropHeight; ++row) {
                auto *pixels = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(result->surface->pixels) +
                    row * result->surface->pitch);
                for (unsigned col = 0; col < item.cropWidth; ++col)
                    pixels[col] = (pixels[col] & 0x00ffffffu) |
                        (uint32_t((*alpha)[(item.y + row) * item.width + item.x + col]) << 24);
            }
        }
        result->prepareUpload(mipmaps);
        return result;
    }, size_t(cropWidth) * cropHeight * 16, priority);
}
}
