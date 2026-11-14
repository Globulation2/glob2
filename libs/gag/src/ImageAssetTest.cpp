// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "LossyAlphaFixture.h"
#include "MipReduction.h"
#include <FileManager.h>
#include <GraphicContext.h>
#include <AssetLoader.h>
#include <RenderMemory.h>
#include <stdexcept>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <fstream>
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
    const bool previous = GAGCore::AssetImage::compressHighResolution.exchange(true);
    struct Restore { bool value; ~Restore() { GAGCore::AssetImage::compressHighResolution = value; } } restore{previous};
    GAGCore::AssetImage image(surface);
    image.prepareUpload(true);
    REQUIRE(image.mips.size() > 3);
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
    // exercising both production decoder paths against the same reference.
    for (const auto decoder : {IMG_Load_IO, SDL_LoadPNG_IO}) {
        auto surface = decoder(SDL_IOFromConstMem(rgba16Fixture::png, sizeof(rgba16Fixture::png)), true);

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
