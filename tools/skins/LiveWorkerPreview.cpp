// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveWorkerPreview.h"
#include "LiveWorkerRig.h"
#include <GraphicContext.h>
#include <SDL3/SDL.h>
#include <Toolkit.h>
#ifdef HAVE_OPENGL
#include <SDL3/SDL_opengl.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

using namespace GAGCore;
namespace {
using Json = nlohmann::json;
constexpr double Pi = std::numbers::pi;
std::string read(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read " + path);
    if (f.tellg() < 0 || f.tellg() > 64 * 1024 * 1024)
        throw std::runtime_error("oversized asset: " + path);
    f.seekg(0);
    return {std::istreambuf_iterator<char>(f), {}};
}
void report(const std::string &path, const Json &value) {
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    std::ofstream f(path);
    if (!(f << value.dump(2) << '\n'))
        throw std::runtime_error("cannot write " + path);
}
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void captureScreen(GraphicContext &gfx, const std::string &path) {
    const auto target = std::filesystem::absolute(path);
    if (!target.parent_path().empty())
        std::filesystem::create_directories(target.parent_path());
    if (std::filesystem::exists(target))
        std::filesystem::remove(target);
    gfx.printScreen(std::filesystem::relative(target, std::filesystem::current_path()).string());
    gfx.nextFrame();
    require(std::filesystem::is_regular_file(target), "screenshot was not saved: " + path);
}
std::size_t compressed(const std::string &bytes) {
    uLongf size = compressBound(bytes.size());
    std::vector<Bytef> output(size);
    require(compress2(output.data(), &size, reinterpret_cast<const Bytef *>(bytes.data()),
                      bytes.size(), 6) == Z_OK,
            "compression failed");
    return size;
}
double referencePhase(unsigned direction, double phase) { return (phase + (direction % 2)) * .5; }
struct Assets {
    LiveWorkerRig rig;
    SkinMesh baked;
    std::string rigBytes, bakedBytes;
    explicit Assets(const std::string &directory) {
        std::string error;
        rigBytes = read(directory + "/worker-walk.live.json");
        bakedBytes = read(directory + "/worker-walk.gsk");
        require(rig.loadJson(rigBytes, error), "rig: " + error);
        require(baked.load(directory + "/worker-walk.gsk", error), "reference: " + error);
        require(baked.frames == 256 && baked.logicalSize == 38, "unexpected worker reference");
    }
    Json sizes() const {
        return {{"rigBytes", rigBytes.size()},
                {"rigDeflateBytes", compressed(rigBytes)},
                {"bakedBytes", bakedBytes.size()},
                {"bakedDeflateBytes", compressed(bakedBytes)}};
    }
};
struct Paints {
    std::array<std::unique_ptr<DrawableSurface>, 3> paint;
    std::unique_ptr<DrawableSurface> material, glossy;
    explicit Paints(const std::string &directory) {
        for (int i = 0; i < 3; ++i) {
            paint[i] = std::make_unique<DrawableSurface>(
                directory + "/" +
                std::array<const char *, 3>{"neutral.webp", "numbered.webp", "paint.webp"}[i]);
            require(paint[i]->getW() == 512 && paint[i]->getH() == 512,
                    "missing 512x512 viewer paint");
        }
        material = loadSkinMaterialMap(directory + "/material.webp");
        require(material && material->getW() == 512 && material->getH() == 512,
                "missing viewer materials");
        glossy = std::make_unique<DrawableSurface>(512, 512);
    }
};
void labels(GraphicContext &gfx, const std::string &text) {
    gfx.drawString(20, 20, Toolkit::getFont("live-worker"), text);
}
void compare(GraphicContext &gfx, Assets &assets, Paints &paints, unsigned direction, double phase,
             double heading, int paint, std::optional<double> livePhase = {}) {
    auto mesh = assets.rig.evaluate(livePhase.value_or(referencePhase(direction, phase)), heading);
    unsigned frame = direction * 32 + static_cast<unsigned>(phase * 32) % 32;
    auto &texture = *paints.paint[paint];
    auto &material = paint == 2 ? *paints.material : *paints.glossy;
    gfx.beginFrame(GraphicContext::FrameMode::FullRedraw);
    gfx.drawFilledRect(0, 0, 1024, 960, Color(45, 50, 60));
    labels(gfx, livePhase
                    ? "Baked: nearest reference heading (left) / Live: continuous rotation (right)"
                    : "Baked worker (left) / Live worker (right)");
    gfx.drawString(20, 55, Toolkit::getFont("live-worker"),
                   "Space: pause  Left/Right: phase  Up/Down: speed  1-8: heading  R: rotate  P: "
                   "paint  Esc: exit");
    for (auto [size, y] : std::array<std::pair<int, int>, 2>{{{38, 180}, {304, 360}}}) {
        require(gfx.drawSkinMesh(assets.baked, frame, texture, material, SkinRegionWorker,
                                 256 - size / 2, y, size, size),
                "baked draw failed");
        require(gfx.drawSkinMesh(*mesh, 0, texture, material, SkinRegionWorker, 768 - size / 2, y,
                                 size, size),
                "live draw failed");
    }
}
Json benchmark(GraphicContext &gfx, Assets &assets, Paints &paints, bool live, unsigned distinct) {
    assets.rig.clearCache();
    std::array<std::unique_ptr<DrawableSurface>, 4> colors;
    for (int i = 0; i < 4; ++i) {
        colors[i] = std::make_unique<DrawableSurface>(512, 512);
        colors[i]->drawFilledRect(0, 0, 512, 512, Color(60 + i * 35, 160 - i * 20, 90 + i * 20));
    }
    std::vector<double> times;
    double cold = 0, prep = 0;
    unsigned long draws = 0;
    // The first frame includes all distinct pose reconstruction. Warm runs share
    // the same finite pose set; forced-cold evaluation is reported separately.
    for (unsigned iteration = 0; iteration < 45; ++iteration) {
        auto started = std::chrono::steady_clock::now();
        std::vector<std::shared_ptr<const SkinMesh>> held;
        std::vector<SkinMeshRequest> requests;
        for (unsigned i = 0; i < 512; ++i) {
            unsigned frame = (i / 4 + iteration) % distinct;
            unsigned direction = frame / 32, phase = frame % 32;
            const SkinMesh *mesh = &assets.baked;
            if (live) {
                held.push_back(assets.rig.evaluate(referencePhase(direction, phase / 32.),
                                                   -direction * Pi / 4));
                mesh = held.back().get();
            }
            requests.push_back({mesh, live ? 0u : frame, colors[i % 4].get(), paints.material.get(),
                                SkinRegionWorker});
        }
        auto prepared = std::chrono::steady_clock::now();
        gfx.beginFrame(GraphicContext::FrameMode::FullRedraw);
        gfx.drawFilledRect(0, 0, 1024, 960, Color(45, 50, 60));
        gfx.resetDrawCallCount();
        gfx.prepareSkinMeshes(requests);
        for (unsigned i = 0; i < requests.size(); ++i) {
            const auto &r = requests[i];
            require(gfx.drawSkinMesh(*r.mesh, r.frame, *r.texture, *r.material, r.region,
                                     (i % 32) * 32, (i / 32) * 48, 38, 38),
                    "crowd draw failed");
        }
        const auto count = gfx.getDrawCallCount();
        gfx.nextFrame();
        double ms =
            1000 *
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (iteration == 0)
            cold = ms;
        if (iteration >= 5) {
            times.push_back(ms);
            prep += 1000 * std::chrono::duration<double>(prepared - started).count();
            draws += count;
        }
    }
    auto stats = assets.rig.statistics();
    std::sort(times.begin(), times.end());
    Json result = {{"mode", live ? "live" : "baked"},
                   {"workers", 512},
                   {"distinctPoses", distinct},
                   {"paintVariants", 4},
                   {"coldFrameMs", cold},
                   {"meanMs", std::accumulate(times.begin(), times.end(), 0.) / times.size()},
                   {"p95Ms", times[static_cast<std::size_t>(std::ceil(times.size() * .95)) - 1]},
                   {"frameTimesMs", times},
                   {"meanPreparationMs", prep / times.size()},
                   {"drawsPerFrame", static_cast<double>(draws) / times.size()},
                   {"cacheHits", stats.hits},
                   {"cacheMisses", stats.misses},
                   {"deformationMs", stats.deformationSeconds * 1000},
                   {"geometryBytes", assets.rig.geometryBytes()}};
    if (live) {
        assets.rig.clearCache();
        auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < distinct; ++frame)
            assets.rig.evaluate(referencePhase(frame / 32, (frame % 32) / 32.),
                                -(frame / 32) * Pi / 4);
        result["forcedColdDistinctPosesMs"] =
            1000 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    return result;
}
} // namespace

int verifyLiveWorker(const std::string &directory, const std::string &output) {
    try {
        Assets assets(directory);
        double position = 0, normal = 0;
        unsigned cases = 0;
        for (unsigned direction = 0; direction < 8; ++direction)
            for (unsigned phase = 0; phase < 32; ++phase) {
                auto live = assets.rig.evaluate(referencePhase(direction, phase / 32.),
                                                -direction * Pi / 4);
                require(live->indices == assets.baked.indices && live->uv == assets.baked.uv,
                        "topology or UV mismatch");
                require(live->vertices == assets.baked.vertices, "vertex count mismatch");
                for (unsigned v = 0; v < live->vertices; ++v) {
                    auto offset = (direction * 32 + phase) * live->vertices * 6 + v * 6;
                    double n = 0;
                    for (int c = 0; c < 3; ++c) {
                        double p = live->poses[v * 6 + c] - assets.baked.poses[offset + c];
                        if (c < 2)
                            position = std::max(position, std::abs(p) * 38 / 2);
                        double d = live->poses[v * 6 + 3 + c] - assets.baked.poses[offset + 3 + c];
                        n += d * d;
                    }
                    normal = std::max(normal, std::sqrt(n));
                }
                ++cases;
            }
        require(position < .05 && normal < .001, "reference pose tolerances exceeded");
        assets.rig.clearCache();
        auto first = assets.rig.evaluate(0, 0);
        require(first == assets.rig.evaluate(1, 2 * Pi) &&
                    first == assets.rig.evaluate(-1, -2 * Pi),
                "periodic cache key mismatch");
        auto fractional = assets.rig.evaluate(.123, Pi / 7);
        require(fractional == assets.rig.evaluate(.123, Pi / 7), "cache hit loses identity");
        for (unsigned i = 0; i < 200; ++i) {
            auto pose = assets.rig.evaluate(i / 200., Pi / 7);
            for (float value : pose->poses)
                require(std::isfinite(value), "nonfinite interpolated pose");
        }
        require(assets.rig.geometryBytes() <=
                    128 *
                        ((assets.baked.vertices * 8 + assets.baked.indices.size()) * sizeof(float)),
                "geometry cache exceeds bound");
        require(first->identity != assets.rig.evaluate(0, 0)->identity,
                "evicted pose reused an identity");
        bool rejected = false;
        try {
            assets.rig.evaluate(std::numeric_limits<double>::infinity(), 0);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "nonfinite request accepted");
        Json valid = Json::parse(assets.rigBytes);
        unsigned malformed = 0;
        auto reject = [&](Json value) {
            LiveWorkerRig candidate;
            std::string error;
            require(!candidate.loadJson(value.dump(), error) && !error.empty(),
                    "malformed rig accepted");
            ++malformed;
        };
        auto bad = valid;
        bad["version"] = 2;
        reject(bad);
        bad = valid;
        bad["samples"].erase(bad["samples"].begin());
        reject(bad);
        bad = valid;
        bad["samples"][0][0][7] = 0;
        reject(bad);
        bad = valid;
        bad["samples"][0][0][3] = 10;
        reject(bad);
        bad = valid;
        bad["indices"][0] = 99999;
        reject(bad);
        bad = valid;
        bad["descriptors"][0] = Json::array({"average", Json::array({0, 1, 2})});
        reject(bad);
        bad = valid;
        bad["radii"][0] = -1;
        reject(bad);
        bad = valid;
        bad["uv"][0][0] = 2;
        reject(bad);
        bad = valid;
        bad["definition"]["paths"][0][0] = 0;
        reject(bad);
        bad = valid;
        bad["threshold"] = 1000;
        reject(bad);
        std::string error;
        require(!assets.rig.loadJson("{", error), "invalid JSON accepted");
        require(assets.rig.evaluate(0, 0) != nullptr, "failed reload discarded valid state");
        LiveWorkerRig tooLarge;
        require(!tooLarge.loadJson(std::string(1024 * 1024 + 1, ' '), error),
                "oversized rig accepted");
        // Reference topology is closed and connected, hence socket vertices are
        // shared rather than two coincident rings that can separate under motion.
        std::map<std::pair<unsigned, unsigned>, unsigned> edges;
        for (unsigned i = 0; i < assets.baked.indices.size(); i += 3)
            for (int k = 0; k < 3; ++k) {
                unsigned a = assets.baked.indices[i + k], b = assets.baked.indices[i + (k + 1) % 3];
                if (a > b)
                    std::swap(a, b);
                ++edges[{a, b}];
            }
        for (auto [edge, count] : edges)
            require(count == 2, "open or non-manifold socket");
        Json result = assets.sizes();
        result.update({{"referencePoses", cases},
                       {"maxPositionPixels", position},
                       {"maxNormalVectorError", normal},
                       {"malformedCases", malformed + 2},
                       {"cacheAndInterpolationPassed", true},
                       {"socketTopologyPassed", true},
                       {"geometryBytes", assets.rig.geometryBytes()}});
        report(output + "-verification.json", result);
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Live worker verification: " << e.what() << '\n';
        return 1;
    }
}

int previewLiveWorker(GraphicContext &gfx, const std::string &directory, const std::string &output,
                      const std::string &mode) {
    try {
        Assets assets(directory);
        Paints paints(directory);
        Toolkit::loadFont("data/fonts/sans.ttf", 16, "live-worker");
        if (mode == "--live-benchmark") {
            Json runs = Json::array();
            for (unsigned distinct : {32u, 128u})
                for (int pair = 0; pair < 5; ++pair)
                    for (int order = 0; order < 2; ++order) {
                        bool live = (order + pair) % 2 == 1;
                        auto result = benchmark(gfx, assets, paints, live, distinct);
                        result["pair"] = pair;
                        runs.push_back(result);
                        std::cout << result.dump() << std::endl;
                    }
            Json result = assets.sizes();
            result["runs"] = runs;
            Json summary = Json::array();
            for (unsigned distinct : {32u, 128u}) {
                std::array<double, 2> p95{};
                for (unsigned mode = 0; mode < 2; ++mode) {
                    std::vector<double> samples;
                    for (const auto &run : runs)
                        if (run["distinctPoses"] == distinct &&
                            run["mode"] == (mode ? "live" : "baked"))
                            for (const auto &ms : run["frameTimesMs"])
                                samples.push_back(ms.get<double>());
                    std::sort(samples.begin(), samples.end());
                    p95[mode] =
                        samples[static_cast<std::size_t>(std::ceil(samples.size() * .95)) - 1];
                }
                summary.push_back({{"distinctPoses", distinct},
                                   {"bakedP95Ms", p95[0]},
                                   {"liveP95Ms", p95[1]},
                                   {"ratio", p95[1] / p95[0]},
                                   {"within25Percent", p95[1] <= p95[0] * 1.25}});
            }
            result["pooledFrameSummary"] = summary;
            result["videoDriver"] = SDL_GetCurrentVideoDriver();
#ifdef HAVE_OPENGL
            result["glVendor"] = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
            result["glRenderer"] = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
            result["glVersion"] = reinterpret_cast<const char *>(glGetString(GL_VERSION));
#endif
            result["resolution"] = {1024, 960};
            report(output + "-benchmark.json", result);
            return 0;
        }
        if (mode == "--live-capture") {
            for (int paint = 0; paint < 3; ++paint)
                for (unsigned direction = 0; direction < 8; ++direction)
                    for (unsigned page = 0; page < 8; ++page) {
                        gfx.beginFrame(GraphicContext::FrameMode::FullRedraw);
                        gfx.drawFilledRect(0, 0, 1024, 960, Color(45, 50, 60));
                        labels(gfx,
                               "Baked (upper rows) / Live (lower rows), four successive phases");
                        auto &texture = *paints.paint[paint];
                        auto &material = paint == 2 ? *paints.material : *paints.glossy;
                        for (unsigned i = 0; i < 4; ++i) {
                            unsigned phase = page * 4 + i;
                            auto pose = assets.rig.evaluate(referencePhase(direction, phase / 32.),
                                                            -direction * Pi / 4);
                            require(gfx.drawSkinMesh(assets.baked, direction * 32 + phase, texture,
                                                     material, SkinRegionWorker, i * 256 + 14, 160,
                                                     228, 228),
                                    "capture reference failed");
                            require(gfx.drawSkinMesh(*pose, 0, texture, material, SkinRegionWorker,
                                                     i * 256 + 14, 520, 228, 228),
                                    "capture live failed");
                        }
                        captureScreen(gfx, output + "-paint" + std::to_string(paint) + "-heading" +
                                               std::to_string(direction) + "-page" +
                                               std::to_string(page) + ".bmp");
                    }
            for (double phase : {0., .015625, .984375, 1.}) {
                compare(gfx, assets, paints, 0, phase, 0, 1);
                captureScreen(gfx, output + "-intermediate-" + std::to_string(phase) + ".bmp");
            }
            return 0;
        }
        bool running = true, paused = false, rotating = false;
        double phase = 0, heading = 0, speed = .5;
        unsigned direction = 0, rotationFamily = 0;
        int paint = 0;
        auto previous = std::chrono::steady_clock::now();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    auto key = event.key.key;
                    if (key == SDLK_ESCAPE)
                        running = false;
                    else if (key == SDLK_SPACE)
                        paused = !paused;
                    else if (key == SDLK_LEFT) {
                        phase = std::fmod(phase + 1 - 1. / 32, 1);
                        paused = true;
                    } else if (key == SDLK_RIGHT) {
                        phase = std::fmod(phase + 1. / 32, 1);
                        paused = true;
                    } else if (key == SDLK_UP)
                        speed = std::min(4., speed * 2);
                    else if (key == SDLK_DOWN)
                        speed = std::max(.0625, speed * .5);
                    else if (key == SDLK_R) {
                        rotating = !rotating;
                        rotationFamily = direction % 2;
                    } else if (key == SDLK_P)
                        paint = (paint + 1) % 3;
                    else if (key >= SDLK_1 && key <= SDLK_8) {
                        direction = key - SDLK_1;
                        heading = -direction * Pi / 4;
                        rotating = false;
                    }
                }
            }
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - previous).count();
            previous = now;
            if (!paused)
                phase = std::fmod(phase + dt * speed, 1);
            if (rotating) {
                heading -= dt * .4;
                direction = static_cast<unsigned>(std::llround(-heading / (Pi / 4))) % 8;
            }
            compare(gfx, assets, paints, direction, phase, heading, paint,
                    rotating ? std::optional<double>(referencePhase(rotationFamily, phase))
                             : std::nullopt);
            gfx.nextFrame();
            SDL_Delay(1);
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Live worker viewer: " << e.what() << '\n';
        return 1;
    }
}
