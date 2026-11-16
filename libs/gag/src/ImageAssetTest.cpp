// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <array>
#include "LossyAlphaFixture.h"
#include "MipReduction.h"
#include <FileManager.h>
#include <GraphicContext.h>
#include <AssetLoader.h>
#include "EtcpakSse41.h"
#if !defined(GLOB2_WEBGL2)
#include "AstcEncoder.h"
#endif
#include <etcpak/ProcessDxtc.hpp>
#include <etcpak/ProcessRGB.hpp>
#include <RenderMemory.h>
#include <stdexcept>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
namespace {
const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
const unsigned char pixels[] = {17,39,71,255,121,77,55,0,25,80,100,128,0,255,30,255};
std::string read(SDL_IOStream *stream) {
    REQUIRE(stream != nullptr);
    char data[16] = {}; const auto size = SDL_ReadIO(stream, data, sizeof(data));
    SDL_CloseIO(stream); return std::string(data, size);
}
}
TEST_SUITE("ImageAssets") {
#if !defined(GLOB2_WEBGL2) && SDL_BYTEORDER == SDL_LIL_ENDIAN
TEST_CASE("worker-prepared BC3 levels decode close to their pixels") {
    // A smooth gradient, hard edges and graded alpha: the cases HD art has.
    auto *surface = SDL_CreateSurface(64, 32, SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(surface != nullptr);
    for (int y = 0; y < surface->h; ++y) for (int x = 0; x < surface->w; ++x) {
        auto *p = static_cast<unsigned char*>(surface->pixels) + y * surface->pitch + x * 4;
        // Left half: a smooth two-axis gradient, as in painted terrain. Right
        // half: 8-pixel cells of two colours meeting at hard edges, with
        // graded alpha in the last quarter.
        const bool cell = ((x / 8) + (y / 8)) % 2;
        p[0] = x < 32 ? 40 + x * 4 : (cell ? 30 : 210);
        p[1] = x < 32 ? 30 + y * 5 : (cell ? 60 : 190);
        p[2] = x < 32 ? 90 + (x + y) * 2 : (cell ? 40 : 170);
        p[3] = x < 48 ? 255 : (y * 8) % 256;
    }
    using Compression = GAGCore::AssetImage::BlockCompression;
    const auto previous = GAGCore::AssetImage::highResolutionCompression.exchange(Compression::BC3);
    struct Restore { Compression value; ~Restore() { GAGCore::AssetImage::highResolutionCompression = value; } } restore{previous};
    GAGCore::AssetImage image(surface);
    image.prepareUpload(true);
    REQUIRE(image.mips.size() > 3);
    CHECK(image.compression == Compression::BC3);
    const auto expand = [](unsigned c, int bits) { return int((c << (8 - bits)) | (c >> (2 * bits - 8))); };
    for (const auto &level : image.mips) {
        CAPTURE(level.width);
        if (level.width < 4 || level.height < 4) { CHECK(level.compressed.empty()); continue; }
        REQUIRE(level.compressed.size() == size_t(level.width / 4) * (level.height / 4) * 16);
        double squared = 0; int worstAlpha = 0;
        const auto *block = level.compressed.data();
        for (int by = 0; by < level.height; by += 4) for (int bx = 0; bx < level.width; bx += 4, block += 16) {
            const int a0 = block[0], a1 = block[1];
            int alphas[8] = {a0, a1};
            for (int i = 1; i < 7; ++i) alphas[i + 1] = a0 > a1 ? ((7 - i) * a0 + i * a1 + 3) / 7 : (i < 5 ? ((5 - i) * a0 + i * a1 + 2) / 5 : (i == 5 ? 0 : 255));
            std::uint64_t alphaBits = 0;
            for (int i = 0; i < 6; ++i) alphaBits |= std::uint64_t(block[2 + i]) << (8 * i);
            const unsigned c0 = block[8] | block[9] << 8, c1 = block[10] | block[11] << 8;
            int palette[4][3];
            for (int k = 0; k < 2; ++k) {
                const unsigned c = k ? c1 : c0;
                palette[k][0] = expand(c >> 11, 5); palette[k][1] = expand((c >> 5) & 63, 6); palette[k][2] = expand(c & 31, 5);
            }
            for (int c = 0; c < 3; ++c) {
                palette[2][c] = (2 * palette[0][c] + palette[1][c] + 1) / 3;
                palette[3][c] = (palette[0][c] + 2 * palette[1][c] + 1) / 3;
            }
            const std::uint32_t colorBits = block[12] | block[13] << 8 | block[14] << 16 | std::uint32_t(block[15]) << 24;
            for (int i = 0; i < 16; ++i) {
                const auto *p = level.pixels.data() + (size_t(by + i / 4) * level.width + bx + i % 4) * 4;
                const auto *q = palette[(colorBits >> (2 * i)) & 3];
                for (int c = 0; c < 3; ++c) { const double d = q[c] - p[2 - c]; squared += d * d; }
                worstAlpha = std::max(worstAlpha, std::abs(alphas[(alphaBits >> (3 * i)) & 7] - p[3]));
            }
        }
        const double rmse = std::sqrt(squared / (3.0 * level.width * level.height));
        // Below 16 pixels, 4x4 blocks mix the gradient, both cell colours and
        // graded alpha: more colours than a block's four-entry palette holds.
        CHECK(rmse < (level.width >= 16 ? 12 : 24));
        CHECK(worstAlpha <= 18);
    }
}
#endif
TEST_CASE("block compression follows the requested format and skips what it cannot encode") {
    using Compression = GAGCore::AssetImage::BlockCompression;
    std::vector<unsigned char> pixels(8 * 4 * 4, 200);
    CHECK(GAGCore::AssetImage::compressBlocks(pixels.data(), 8, 4, Compression::Uncompressed, true).empty());
    CHECK(GAGCore::AssetImage::compressBlocks(pixels.data(), 2, 2, Compression::BC3, true).empty());
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
    const auto bc3 = GAGCore::AssetImage::compressBlocks(pixels.data(), 8, 4, Compression::BC3, true);
    const auto etc2 = GAGCore::AssetImage::compressBlocks(pixels.data(), 8, 4, Compression::ETC2, true);
    // Sixteen bytes for each 4x4 pixels in both formats.
    CHECK(bc3.size() == 32);
    CHECK(etc2.size() == 32);
    CHECK(bc3 != etc2);
    // A flat opaque block: BC3 stores its alpha endpoints first.
    CHECK(bc3[0] == 200);
    // Premultiplied blocks scale colour by alpha before encoding, as SDL GPU
    // premultiplies RGBA uploads; alpha is unchanged.
    std::vector<unsigned char> translucent(8 * 4 * 4);
    for (size_t i = 0; i < translucent.size(); i += 4) { translucent[i] = 40; translucent[i + 1] = 120; translucent[i + 2] = 240; translucent[i + 3] = 128; }
    const auto straight = GAGCore::AssetImage::compressBlocks(translucent.data(), 8, 4, Compression::BC3, true);
    const auto premultiplied = GAGCore::AssetImage::compressBlocks(translucent.data(), 8, 4, Compression::PremultipliedBC3, true);
    REQUIRE(premultiplied.size() == 32);
    CHECK(premultiplied[0] == 128);
    // The first texel's decoded colour (BC1 palette after the alpha half).
    const auto texel = [](const std::vector<unsigned char> &block) {
        const unsigned c0 = block[8] | block[9] << 8, c1 = block[10] | block[11] << 8;
        int palette[4][3];
        for (int k = 0; k < 2; ++k) {
            const unsigned c = k ? c1 : c0;
            palette[k][0] = int(c >> 11) * 255 / 31; palette[k][1] = int((c >> 5) & 63) * 255 / 63; palette[k][2] = int(c & 31) * 255 / 31;
        }
        for (int c = 0; c < 3; ++c) {
            palette[2][c] = c0 > c1 ? (2 * palette[0][c] + palette[1][c]) / 3 : (palette[0][c] + palette[1][c]) / 2;
            palette[3][c] = c0 > c1 ? (palette[0][c] + 2 * palette[1][c]) / 3 : 0;
        }
        const auto *q = palette[block[12] & 3];
        return std::array<int, 3>{q[0], q[1], q[2]};
    };
    // BGRA bytes: red is 240 straight and (240 * 128 + 127) / 255 = 120 premultiplied.
    CHECK(std::abs(texel(straight)[0] - 240) <= 8);
    CHECK(std::abs(texel(premultiplied)[0] - 120) <= 8);
    CHECK(std::abs(texel(premultiplied)[1] - 60) <= 8);
    CHECK(std::abs(texel(premultiplied)[2] - 20) <= 8);
    // The prepared image records the format its levels hold.
    const auto previous = GAGCore::AssetImage::highResolutionCompression.exchange(Compression::ETC2);
    struct Restore { Compression value; ~Restore() { GAGCore::AssetImage::highResolutionCompression = value; } } restore{previous};
    auto *surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(surface != nullptr);
    SDL_FillSurfaceRect(surface, nullptr, 0xff336699u);
    GAGCore::AssetImage image(surface);
    image.prepareUpload(true);
#if !defined(GLOB2_WEBGL2)
    CHECK(image.compression == Compression::ETC2);
    REQUIRE_FALSE(image.mips.empty());
    CHECK(image.mips.front().compressed.size() == 4 * 16);
#endif
#endif
}
TEST_CASE("the SSE4.1 encoders match the portable ones where the CPU runs them") {
    const auto simd = GAGCore::EtcpakSse41::available();
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
    if (__builtin_cpu_supports("sse4.1")) CHECK(simd.bc3 != nullptr);
#endif
    if (!simd.bc3) { CHECK(simd.etc2Rgba == nullptr); return; }
    REQUIRE(simd.etc2Rgba != nullptr);
    // Flat 4x4 blocks of distinct colours and alphas: one exact encoding each.
    constexpr int width = 16, height = 8;
    std::vector<std::uint32_t> pixels(width * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const unsigned block = (y / 4) * (width / 4) + x / 4;
            pixels[y * width + x] = (255u - block * 20) << 24 | (block * 31 % 256) << 16 | (block * 57 % 256) << 8 | (block * 89 % 256);
        }
    const std::uint32_t count = (width / 4) * (height / 4);
    std::vector<std::uint64_t> portable(count * 2), sse(count * 2);
    CompressBc3(pixels.data(), portable.data(), count, width);
    simd.bc3(pixels.data(), sse.data(), count, width);
    CHECK(portable == sse);
    CompressEtc2Rgba(pixels.data(), portable.data(), count, width, true);
    simd.etc2Rgba(pixels.data(), sse.data(), count, width);
    CHECK(portable == sse);
}
#if !defined(GLOB2_WEBGL2) && SDL_BYTEORDER == SDL_LIL_ENDIAN
TEST_CASE("worker-prepared premultiplied ASTC 4x4 levels decode close to their premultiplied pixels") {
    // The gradient, hard edges and graded alpha of the BC3 case above.
    auto *surface = SDL_CreateSurface(64, 32, SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(surface != nullptr);
    for (int y = 0; y < surface->h; ++y) for (int x = 0; x < surface->w; ++x) {
        auto *p = static_cast<unsigned char*>(surface->pixels) + y * surface->pitch + x * 4;
        const bool cell = ((x / 8) + (y / 8)) % 2;
        p[0] = x < 32 ? 40 + x * 4 : (cell ? 30 : 210);
        p[1] = x < 32 ? 30 + y * 5 : (cell ? 60 : 190);
        p[2] = x < 32 ? 90 + (x + y) * 2 : (cell ? 40 : 170);
        p[3] = x < 48 ? 255 : (y * 8) % 256;
    }
    using Compression = GAGCore::AssetImage::BlockCompression;
    // The RGBA chain the blocks replace, prepared without encoding.
    GAGCore::AssetImage reference(SDL_DuplicateSurface(surface));
    REQUIRE(reference.surface != nullptr);
    reference.prepareUpload(true, false);
    CHECK(reference.compression == Compression::Uncompressed);
    const auto previous = GAGCore::AssetImage::highResolutionCompression.exchange(Compression::PremultipliedASTC4x4);
    struct Restore { Compression value; ~Restore() { GAGCore::AssetImage::highResolutionCompression = value; } } restore{previous};
    GAGCore::AssetImage image(surface);
    image.prepareUpload(true);
    CHECK(image.compression == Compression::PremultipliedASTC4x4);
    REQUIRE(image.mips.size() == reference.mips.size());
    REQUIRE(image.mips.size() > 3);
    for (size_t index = 0; index < image.mips.size(); ++index) {
        const auto &level = image.mips[index];
        const auto &pixels = reference.mips[index].pixels;
        CAPTURE(level.width);
        if (level.width < 4 || level.height < 4) { CHECK(level.compressed.empty()); CHECK_FALSE(level.pixels.empty()); continue; }
        // The device samples and restores from the blocks alone.
        CHECK(level.pixels.empty());
        REQUIRE(level.compressed.size() == size_t(level.width / 4) * (level.height / 4) * 16);
        const auto decoded = GAGCore::AstcEncoder::decode(level.compressed.data(), level.width, level.height);
        REQUIRE(decoded.size() == pixels.size());
        double squared = 0; int worstAlpha = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const unsigned alpha = pixels[i + 3];
            for (int c = 0; c < 3; ++c) {
                // Reference bytes are BGRA; decoded ones RGBA, premultiplied as SDL GPU rounds.
                const int expected = int((unsigned(pixels[i + 2 - c]) * alpha + 127) / 255);
                const double d = int(decoded[i + c]) - expected; squared += d * d;
            }
            worstAlpha = std::max(worstAlpha, std::abs(int(decoded[i + 3]) - int(alpha)));
        }
        const double rmse = std::sqrt(squared / (3.0 * level.width * level.height));
        // The BC3 bounds, except alpha below 16 pixels: ASTC codes alpha with
        // colour, and those blocks mix the gradient, both cells and graded alpha.
        CHECK(rmse < (level.width >= 16 ? 12 : 24));
        CHECK(worstAlpha <= (level.width >= 16 ? 18 : 32));
    }
}
TEST_CASE("premultiplied ASTC 4x4 blocks keep flat translucent colour and alpha") {
    using Compression = GAGCore::AssetImage::BlockCompression;
    std::vector<unsigned char> translucent(8 * 4 * 4);
    for (size_t i = 0; i < translucent.size(); i += 4) { translucent[i] = 40; translucent[i + 1] = 120; translucent[i + 2] = 240; translucent[i + 3] = 128; }
    CHECK(GAGCore::AssetImage::compressBlocks(translucent.data(), 2, 2, Compression::PremultipliedASTC4x4, true).empty());
    CHECK(GAGCore::AssetImage::compressBlocks(translucent.data(), 6, 4, Compression::PremultipliedASTC4x4, true).empty());
    for (const bool bgra : {true, false}) {
        CAPTURE(bgra);
        const auto blocks = GAGCore::AssetImage::compressBlocks(translucent.data(), 8, 4, Compression::PremultipliedASTC4x4, bgra);
        REQUIRE(blocks.size() == 32);
        const auto decoded = GAGCore::AstcEncoder::decode(blocks.data(), 8, 4);
        REQUIRE(decoded.size() == translucent.size());
        // (240 * 128 + 127) / 255 = 120, (120 * 128 + 127) / 255 = 60, (40 * 128 + 127) / 255 = 20.
        const std::array<int, 4> expected = bgra ? std::array<int, 4>{120, 60, 20, 128} : std::array<int, 4>{20, 60, 120, 128};
        for (size_t i = 0; i < decoded.size(); i += 4)
            for (int c = 0; c < 4; ++c) CHECK(std::abs(int(decoded[i + c]) - expected[c]) <= 1);
    }
}
TEST_CASE("HD frame block encoding timing [artifacts][benchmark]") {
    // Every stride-th shipped HD frame (GLOB2_BLOCK_BENCH_STRIDE, default 32),
    // prepared as asset workers do; times each format's encode of the chain.
    using Compression = GAGCore::AssetImage::BlockCompression;
    using Clock = std::chrono::steady_clock;
    const auto root = glob2test::sourceRoot() / "content/data/highres/v1";
    std::ifstream frames(root / "frames.txt");
    if (!frames) { MESSAGE("No shipped HD frames"); return; }
    const char *strideText = std::getenv("GLOB2_BLOCK_BENCH_STRIDE");
    const unsigned stride = strideText ? unsigned(std::max(1, std::atoi(strideText))) : 32u;
    struct Totals { double seconds = 0, worst = 0; };
    std::array<Totals, 2> totals{}; // BC3, ASTC
    std::uint64_t sampled = 0, all = 0, sampledPixels = 0;
    std::string line;
    for (unsigned index = 0; std::getline(frames, line);) {
        std::istringstream fields(line);
        std::string name, file; unsigned width = 0, height = 0, scale = 0;
        if (!(fields >> name >> width >> height >> scale >> file) || !width || !height) continue;
        ++all;
        if (index++ % stride) continue;
        SDL_Surface *loaded = IMG_Load((root / file).string().c_str());
        if (!loaded) continue;
        GAGCore::AssetImage image(SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888));
        SDL_DestroySurface(loaded);
        REQUIRE(image.surface != nullptr);
        image.prepareUpload(true, false);
        ++sampled;
        for (const auto &level : image.mips) sampledPixels += std::uint64_t(level.width) * level.height;
        for (size_t format = 0; format < totals.size(); ++format) {
            const auto start = Clock::now();
            for (const auto &level : image.mips)
                GAGCore::AssetImage::compressBlocks(level.pixels.data(), level.width, level.height,
                    format == 0 ? Compression::PremultipliedBC3 : Compression::PremultipliedASTC4x4, true);
            const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
            totals[format].seconds += seconds; totals[format].worst = std::max(totals[format].worst, seconds);
        }
    }
    REQUIRE(sampled > 0);
    const char *names[] = {"premultiplied BC3 (etcpak)", "premultiplied ASTC 4x4 (astcenc)"};
    MESSAGE("HD frames: " << sampled << " of " << all << " sampled, " << sampledPixels / 1e6
            << " Mpx of chains");
    for (size_t format = 0; format < totals.size(); ++format) {
        const double perFrame = totals[format].seconds / sampled;
        MESSAGE(std::string(names[format]) << ": " << perFrame * 1e3 << " ms per frame (worst " << totals[format].worst * 1e3
                << " ms), " << sampledPixels / totals[format].seconds / 1e6 << " Mpx/s; every shipped frame "
                << perFrame * all << " s on one thread");
    }
}
#endif
TEST_CASE("prepared image adoption separates shared pixels and transfers exclusive pixels") {
    auto *pixels = SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(pixels != nullptr);
    auto *row = static_cast<Uint32*>(pixels->pixels);
    row[0] = 0x00112233u; row[1] = 0x80445566u;
    GAGCore::AssetImage image(pixels);
    auto shared = GAGCore::DrawableSurface::fromAssetImage(image, false, false);
    CHECK(image.surface == pixels);
    CHECK(shared->getSDLSurface() != pixels);
    CHECK(static_cast<Uint32*>(shared->getSDLSurface()->pixels)[0] == row[0]);
    static_cast<Uint32*>(shared->getSDLSurface()->pixels)[0] = 0;
    CHECK(row[0] == 0x00112233u);
    auto exclusive = GAGCore::DrawableSurface::fromAssetImage(image, true, false);
    CHECK(image.surface == nullptr);
    CHECK(exclusive->getSDLSurface() == pixels);
    CHECK(static_cast<Uint32*>(exclusive->getSDLSurface()->pixels)[1] == 0x80445566u);
    GAGCore::AssetImage missing(nullptr);
    CHECK_THROWS_AS(GAGCore::DrawableSurface::fromAssetImage(missing, true, false), std::runtime_error);
}
TEST_CASE("the renderer memory report counts the decoded pixels surfaces hold") {
    const auto before = GAGCore::DrawableSurface::residentPixels();
    auto *pixels = SDL_CreateSurface(8, 4, SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(pixels != nullptr);
    const auto bytes = size_t(pixels->pitch) * pixels->h;
    GAGCore::AssetImage image(pixels);
    {
        auto surface = GAGCore::DrawableSurface::fromAssetImage(image, true, false);
        const auto held = GAGCore::DrawableSurface::residentPixels();
        CHECK(held.surfaces == before.surfaces + 1);
        CHECK(held.surfaceBytes == before.surfaceBytes + bytes);
        const auto report = GAGCore::RenderMemory::report();
        CHECK(report.cpuSurfaceBytes == held.surfaceBytes);
        CHECK(GAGCore::RenderMemory::fields(report).find(
            "cpu_surface_bytes=" + std::to_string(held.surfaceBytes) + " ") != std::string::npos);
    }
    const auto released = GAGCore::DrawableSurface::residentPixels();
    CHECK(released.surfaces == before.surfaces);
    CHECK(released.surfaceBytes == before.surfaceBytes);
}
TEST_CASE("mip preparation preserves padded alpha weighted integer filtering") {
    for (const auto size : {std::pair<int, int>{7, 5}, {1, 9}, {9, 1}, {8, 8}}) for (int alphaMode : {0, 1, 2}) {
        auto *surface = SDL_CreateSurface(size.first, size.second, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(surface != nullptr);
        for (int y = 0; y < surface->h; ++y) for (int x = 0; x < surface->w; ++x) {
            auto *p = static_cast<unsigned char*>(surface->pixels) + y * surface->pitch + x * 4;
            p[0] = (x * 37 + y * 11) % 256; p[1] = (x * 19 + y * 43) % 256;
            p[2] = (x * 53 + y * 17) % 256;
            p[3] = alphaMode == 0 ? 0 : alphaMode == 1 ? 255 :
                (x < surface->w / 2 ? 255 : (x * 71 + y * 23) % 256);
        }
        GAGCore::AssetImage image(surface);
        image.prepareUpload(true);
        REQUIRE(!image.mips.empty());
        const auto &base = image.mips.front();
        for (int y = 0; y < base.height; ++y) for (int x = 0; x < base.width; ++x) {
            const auto *expected = image.uploadPixels.empty()
                ? static_cast<const unsigned char*>(surface->pixels) + std::min(y, surface->h - 1) * surface->pitch + std::min(x, surface->w - 1) * 4
                : image.uploadPixels.data() + (size_t(std::min(y, surface->h - 1)) * surface->w + std::min(x, surface->w - 1)) * 4;
            CHECK(std::memcmp(base.pixels.data() + (size_t(y) * base.width + x) * 4, expected, 4) == 0);
        }
        for (size_t i = 1; i < image.mips.size(); ++i) {
            const auto &input = image.mips[i - 1], &output = image.mips[i];
            auto inPlace = input.pixels;
            GAGCore::downsampleMip(inPlace.data(), input.width, input.height, inPlace.data());
            CHECK(std::equal(output.pixels.begin(), output.pixels.end(), inPlace.begin()));
            for (int y = 0; y < output.height; ++y) for (int x = 0; x < output.width; ++x) {
                unsigned sum[4] = {};
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                    const auto *p = input.pixels.data() + (size_t(std::min(input.height - 1, y * 2 + dy)) * input.width + std::min(input.width - 1, x * 2 + dx)) * 4;
                    sum[3] += p[3]; for (int c = 0; c < 3; ++c) sum[c] += p[c] * p[3];
                }
                const auto *p = output.pixels.data() + (size_t(y) * output.width + x) * 4;
                CHECK(p[3] == (sum[3] + 2) / 4);
                for (int c = 0; c < 3; ++c)
                    CHECK(p[c] == (sum[3] ? (sum[c] + sum[3] / 2) / sum[3] : 0));
            }
        }
    }
}
TEST_CASE("WebP decoder preserves exact RGBA including transparent RGB") {
    auto stream=SDL_IOFromConstMem(webp,sizeof(webp)); REQUIRE(stream!=nullptr);
    auto surface=IMG_Load_IO(stream,1); REQUIRE(surface!=nullptr);
    auto rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32); REQUIRE(rgba!=nullptr);
    CHECK(rgba->w==2); CHECK(rgba->h==2);
    for(int y=0;y<2;++y) CHECK(std::memcmp(static_cast<char*>(rgba->pixels)+y*rgba->pitch,pixels+y*8,8)==0);
    SDL_DestroySurface(rgba); SDL_DestroySurface(surface);
}
TEST_CASE("Q90 lossy WebP preserves dimensions and exact alpha") {
    using namespace lossyAlphaFixture;
    auto surface = IMG_Load_IO(SDL_IOFromConstMem(lossyAlphaFixture::webp, sizeof(lossyAlphaFixture::webp)), true);
    REQUIRE(surface != nullptr);
    CHECK(surface->w == width); CHECK(surface->h == height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            Uint8 r, g, b, a;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
            CHECK(a == alpha[y * width + x]);
        }
    SDL_DestroySurface(surface);
}
TEST_CASE("16-bit RGBA rounds normalized channels to the exporter reference") {
    // Keep one JUnit result for the strict native selection inventory while
    // exercising both production decoder paths against the same reference:
    // IMG_Load_IO (skins, terrain packs) and IMG_LoadPNG_IO (map asset bundles
    // and sprites). SDL core's own SDL_LoadPNG_IO is not one of them; SDL 3.4.18
    // truncates 16-bit channels there instead of rounding.
    using Decoder = SDL_Surface *(*)(SDL_IOStream *);
    const Decoder decoders[] = {
        [](SDL_IOStream *io) { return IMG_Load_IO(io, true); },
        [](SDL_IOStream *io) {
            SDL_Surface *surface = IMG_LoadPNG_IO(io);
            SDL_CloseIO(io);
            return surface;
        },
    };
    for (const auto decoder : decoders) {
        auto surface = decoder(SDL_IOFromConstMem(rgba16Fixture::png, sizeof(rgba16Fixture::png)));

        REQUIRE(surface != nullptr);
        auto rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(rgba != nullptr);
        CHECK(rgba->w == 2); CHECK(rgba->h == 2);
        for (int y = 0; y < 2; ++y)
            CHECK(std::memcmp(static_cast<char*>(rgba->pixels) + y * rgba->pitch,
                              rgba16Fixture::rgba + y * 8, 8) == 0);
        SDL_DestroySurface(rgba); SDL_DestroySurface(surface);
    }
}
#ifndef __EMSCRIPTEN__
TEST_CASE("Native PNG and JPEG loading and saving remain available") {
    glob2test::TempDir scratch("image-save-codecs");
    auto surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    for (int y = 0; y < 2; ++y)
        std::memcpy(static_cast<char*>(surface->pixels) + y * surface->pitch, pixels + y * 8, 8);
    const auto png = (scratch.path / "image.png").string();
    const auto jpeg = (scratch.path / "image.jpg").string();
    REQUIRE(IMG_SavePNG(surface, png.c_str()));
    REQUIRE(IMG_SaveJPG(surface, jpeg.c_str(), 100));
    SDL_DestroySurface(surface);
    // Match FileManager's stream-based image loading. macOS's filename-only
    // ImageIO path premultiplies RGB, losing invisible colors and rounding
    // partial alpha; the configured PNG stream decoder remains lossless.
    auto stream = SDL_IOFromFile(png.c_str(), "rb");
    REQUIRE(stream != nullptr);
    auto loaded = IMG_Load_IO(stream, true);
    REQUIRE(loaded != nullptr);
    auto rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(rgba != nullptr);
    for (int y = 0; y < 2; ++y)
        CHECK(std::memcmp(static_cast<char*>(rgba->pixels) + y * rgba->pitch, pixels + y * 8, 8) == 0);
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(loaded);
    loaded = IMG_Load(jpeg.c_str());
    REQUIRE(loaded != nullptr);
    CHECK(loaded->w == 2);
    CHECK(loaded->h == 2);
    SDL_DestroySurface(loaded);
}
#endif
TEST_CASE("WebP artwork lookup preserves directories without PNG alternatives") {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir scratch("image-artwork");
    auto root=scratch.path;
    std::filesystem::create_directories(root/"first");std::filesystem::create_directories(root/"second");
    std::ofstream(root/"first"/"logical.webp")<<"first-webp";
    std::ofstream(root/"first"/"logical.png")<<"first-png";
    std::ofstream(root/"second"/"logical.webp")<<"second-webp";
    std::ofstream(root/"first"/"png-only.png")<<"png-only";
    GAGCore::FileManager files("asset-tests");files.addDir((root/"first").string());files.addDir((root/"second").string());
    CHECK(read(files.openImage("logical.webp"))=="first-webp");
    CHECK(read(files.openImage((root/"first"/"logical.webp").string()))=="first-webp");
    CHECK(files.openImage("png-only.webp")==nullptr);
    // External previews and imports still open their exact PNG names.
    CHECK(read(files.openImage("png-only.png"))=="png-only");
    CHECK(files.openImage((root/"second"/"logical.png").string())==nullptr);
}
}
