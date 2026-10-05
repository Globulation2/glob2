// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <AssetLoader.h>
#include <FileManager.h>
#include <ThreadSupport.h>
#include <atomic>
#include <algorithm>
#include <future>
#include <cstring>
#include <fstream>

namespace {
const unsigned char webp[] = {82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
const unsigned char pixels[] = {17,39,71,255,121,77,55,0,25,80,100,128,0,255,30,255};
void write(const std::filesystem::path& path, const void *bytes, size_t size) {
    std::ofstream out(path, std::ios::binary); out.write(static_cast<const char*>(bytes), size);
}
}
TEST_SUITE("AssetLoader") {
TEST_CASE("WebP requests deduplicate and preserve alpha and invisible RGB in both execution modes") {
    glob2test::TempDir temp("parallel-assets");
    write(temp.path / "frame.webp", webp, sizeof(webp));
    GAGCore::FileManager files("asset-loader-test"); files.addDir(temp.path.string());
    for (unsigned workers : {0u, 2u}) {
        GAGCore::AssetLoader loader(files, {workers, workers, 16});
        auto first = loader.requestImage("frame.webp");
        auto second = loader.requestImage("frame.webp");
        auto image = loader.wait(first); REQUIRE(image);
        REQUIRE(second.get() == image);
        REQUIRE(image->surface->format == SDL_PIXELFORMAT_ARGB8888);
        auto *rgba = SDL_ConvertSurface(image->surface, SDL_PIXELFORMAT_RGBA32); REQUIRE(rgba);
        for (int y = 0; y < 2; ++y)
            CHECK(std::memcmp(static_cast<char*>(rgba->pixels) + y * rgba->pitch, pixels + y * 8, 8) == 0);
        SDL_DestroySurface(rgba);
        CHECK(loader.metrics().submitted == 2);
        CHECK(loader.metrics().oversizedJobs == 2); // encoded bytes and output/scratch
        first.cancel(); CHECK(first.state() == GAGCore::AssetLoader::State::Cancelled);
        CHECK(second.get() == image);
    }
}
TEST_CASE("read backpressure yields to decoders and retained metadata cannot stall images") {
    glob2test::TempDir temp("asset-backpressure");
    write(temp.path / "metadata", webp, sizeof(webp));
    for (unsigned i = 0; i < 8; ++i) write(temp.path / (std::to_string(i) + ".webp"), webp, sizeof(webp));
    GAGCore::FileManager files("asset-loader-test"); files.addDir(temp.path.string());
    for (unsigned workers : {0u, 2u}) {
        GAGCore::AssetLoader loader(files, {workers, workers, 128});
        auto retained = loader.requestBytes("metadata"); REQUIRE(loader.wait(retained));
        std::vector<GAGCore::AssetLoader::Handle<GAGCore::AssetImage>> images;
        for (unsigned i = 0; i < 8; ++i) images.push_back(loader.requestImage(std::to_string(i) + ".webp"));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::any_of(images.begin(), images.end(), [](const auto& image) { return image.pending(); }) &&
               std::chrono::steady_clock::now() < deadline) { loader.poll(); std::this_thread::yield(); }
        for (const auto& image : images) CHECK(image.get() != nullptr);
        CHECK(retained.get()->size() == sizeof(webp));
        // A ready image can precede destruction of its completed reader job,
        // which still owns the encoded bytes. Verify cleanup within the same deadline.
        while (loader.metrics().bufferedEncodedBytes && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        CHECK(loader.metrics().bufferedEncodedBytes == 0);
    }
}
TEST_CASE("dependency continuations do not block workers and preparation overlaps") {
    if (!GAGCore::ThreadSupport::available) return;
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {2, 1, 1024});
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<unsigned> entered{0};
    auto work = [&] { ++entered; gate.wait(); return std::make_shared<const unsigned>(7); };
    auto first = loader.request<unsigned>("first", {}, work, 64);
    auto second = loader.request<unsigned>("second", {}, work, 64);
    auto continuation = loader.request<unsigned>("sum", {first.dependency(), second.dependency()},
        [first, second] { return std::make_shared<const unsigned>(*first.get() + *second.get()); }, 64);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (entered < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    const bool overlapping = entered == 2;
    release.set_value();
    REQUIRE(overlapping);
    auto value = loader.wait(continuation); REQUIRE(value); CHECK(*value == 14);
    CHECK(loader.metrics().peakCpu == 2);
    CHECK(loader.metrics().peakWorkingBytes <= 1024);
}
TEST_CASE("exclusive transfer refuses copied subscriptions and borrowed payloads") {
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {0, 0, 1024});
    auto handle = loader.request<unsigned>("exclusive", {}, [] { return std::make_shared<const unsigned>(9); });
    REQUIRE(loader.wait(handle));
    auto copy = handle; CHECK(!handle.take()); copy = {};
    auto borrowed = handle.get(); CHECK(!handle.take()); borrowed.reset();
    auto owned = handle.take(); REQUIRE(owned); CHECK(*owned == 9);
    CHECK(handle.state() == GAGCore::AssetLoader::State::Cancelled);
    CHECK(loader.metrics().completed == 1); CHECK(loader.metrics().failed == 0);
}
TEST_CASE("retained continuation inputs survive cancellation of the caller subscription") {
    if (!GAGCore::ThreadSupport::available) return;
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {1, 1, 1024});
    auto input = loader.request<unsigned>("retained-input", {}, [] { return std::make_shared<const unsigned>(9); });
    REQUIRE(loader.wait(input));
    auto retained = input.retain();
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto gate = release.get_future().share();
    auto continuation = loader.request<unsigned>("retained-continuation", {retained.dependency()},
        [retained, &entered, gate] { entered.set_value(); gate.wait(); return retained.get(); });
    const bool running = started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    input.cancel();
    release.set_value();
    REQUIRE(running);
    CHECK(input.state() == GAGCore::AssetLoader::State::Cancelled);
    CHECK(!input.retain().get());
    auto value = loader.wait(continuation);
    REQUIRE(value); CHECK(*value == 9);
}
TEST_CASE("unique completed requests do not accumulate expired cache metadata") {
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {0, 0, 1024});
    auto live = loader.request<unsigned>("live", {}, [] { return std::make_shared<const unsigned>(7); });
    REQUIRE(loader.wait(live));
    for (unsigned i = 0; i < 512; ++i) {
        auto transient = loader.request<unsigned>("unique:" + std::to_string(i), {}, [] { return std::make_shared<const unsigned>(1); });
        REQUIRE(loader.wait(transient));
    }
    CHECK(loader.metrics().cachedKeys <= 129);
    auto duplicate = loader.request<unsigned>("live", {}, [] { FAIL("live cache entry was pruned"); return std::make_shared<const unsigned>(0); });
    CHECK(duplicate.get() == live.get());
}
TEST_CASE("reader workers overlap independently of CPU preparation") {
    if (!GAGCore::ThreadSupport::available) return;
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {1, 2, 1024});
    std::promise<void> release; auto gate = release.get_future().share();
    std::atomic<unsigned> entered{0};
    auto read = [&] { ++entered; gate.wait(); return std::make_shared<const unsigned>(1); };
    auto first = loader.requestRead<unsigned>("read-a", read);
    auto second = loader.requestRead<unsigned>("read-b", read);
    auto cpu = loader.request<unsigned>("prepare", {}, [] { return std::make_shared<const unsigned>(2); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((entered < 2 || cpu.pending()) && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    const bool overlapping = entered == 2 && cpu.get() != nullptr;
    release.set_value(); REQUIRE(overlapping);
    REQUIRE(loader.wait(first)); REQUIRE(loader.wait(second));
    CHECK(loader.metrics().peakReads == 2);
}
TEST_CASE("finalizers run only on the owner thread and expired owners cancel publication") {
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {2, 1, 1024});
    auto lifetime = std::make_shared<int>(0);
    auto expired = std::make_shared<int>(0);
    const auto owner = std::this_thread::get_id();
    bool published = false;
    auto handle = loader.request<unsigned>("publish", {}, [] { return std::make_shared<const unsigned>(9); });
    loader.onReady<unsigned>(handle, lifetime, [&](std::shared_ptr<const unsigned> value) {
        CHECK(std::this_thread::get_id() == owner); REQUIRE(value); CHECK(*value == 9); published = true;
    });
    loader.onReady<unsigned>(handle, expired, [](std::shared_ptr<const unsigned>) { FAIL("expired owner received a result"); });
    expired.reset();
    REQUIRE(loader.wait(handle));
    loader.poll(); CHECK(published);
}
TEST_CASE("nonpositive polling budgets do not start cooperative work or finalizers") {
    GAGCore::FileManager files("asset-loader-test");
    GAGCore::AssetLoader loader(files, {0, 0, 1024});
    unsigned prepared = 0, published = 0;
    auto lifetime = std::make_shared<unsigned>(0);
    auto handle = loader.request<unsigned>("zero-budget", {}, [&] { ++prepared; return std::make_shared<const unsigned>(9); });
    loader.onReady<unsigned>(handle, lifetime, [&](std::shared_ptr<const unsigned>) { ++published; });
    loader.poll(std::chrono::milliseconds::zero());
    loader.poll(std::chrono::milliseconds(-1));
    CHECK(handle.pending()); CHECK(prepared == 0); CHECK(published == 0);
    REQUIRE(loader.wait(handle));
    loader.poll();
    CHECK(prepared == 1); CHECK(published == 1);
}
TEST_CASE("failures cancellation invalidation and shutdown leave usable handles") {
    glob2test::TempDir temp("asset-errors");
    write(temp.path / "bad.webp", "bad", 3);
    GAGCore::FileManager files("asset-loader-test"); files.addDir(temp.path.string());
    GAGCore::AssetLoader loader(files, {0, 0, 128});
    auto missing = loader.requestImage("absent.webp");
    CHECK(!loader.wait(missing)); CHECK(missing.state() == GAGCore::AssetLoader::State::Failed);
    auto png = loader.requestImage("bad.png"); CHECK(!loader.wait(png));
    auto corrupt = loader.requestImage("bad.webp");
    CHECK(!loader.wait(corrupt)); CHECK(!corrupt.error().empty());
    auto cancelled = loader.request<unsigned>("cancel", {}, [] { FAIL("cancelled work executed"); return std::make_shared<const unsigned>(0); });
    cancelled.cancel(); loader.poll();
    CHECK(cancelled.state() == GAGCore::AssetLoader::State::Cancelled);
    auto old = loader.requestBytes("bad.webp"); REQUIRE(loader.wait(old));
    write(temp.path / "bad.webp", webp, sizeof(webp));
    loader.invalidate();
    auto fresh = loader.requestImage("bad.webp"); REQUIRE(loader.wait(fresh));
    CHECK(old.get()->size() == 3);
    auto pending = loader.requestBytes("not-started");
    loader.shutdown(); CHECK(pending.state() == GAGCore::AssetLoader::State::Cancelled);
    CHECK(fresh.get() != nullptr);
}
}
