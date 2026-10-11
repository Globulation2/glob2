// SPDX-License-Identifier: GPL-3.0-or-later
// Loads each bundled sprite family to final renderer readiness. Run identical
// assets with GLOB2_ASSET_THREADS / IO_THREADS to compare pipeline configurations.
#include <AssetLoader.h>
#include <FileManager.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

class GlobalContainer;
GlobalContainer *globalContainer = nullptr;

static double processCpuMs()
{
#ifdef WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("Cannot read process CPU time");
    auto ticks = [](FILETIME time) {
        ULARGE_INTEGER value;
        value.LowPart = time.dwLowDateTime; value.HighPart = time.dwHighDateTime;
        return value.QuadPart;
    };
    return (ticks(kernel) + ticks(user)) * 1e-4;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage)) throw std::runtime_error("Cannot read process CPU time");
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
        (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
#endif
}

int main(int argc, char **argv)
{
    using namespace GAGCore;
    try {
        Toolkit::init("glob2-asset-benchmark");
        if (argc > 1) Toolkit::getFileManager()->addDir(argv[1]);
        const bool gpu = std::getenv("GLOB2_ASSET_BENCHMARK_GPU");
        Toolkit::initGraphic(640, 480, gpu ? GraphicContext::USEGPU : 0, "Asset loading benchmark");
        Sprite::setHighResolution(std::getenv("GLOB2_ASSET_BENCHMARK_HD"));
        const auto start = std::chrono::steady_clock::now();
        const double cpuStart = processCpuMs();
        auto directory = Toolkit::assets().requestDirectory("data/gfx");
        auto names = Toolkit::assets().wait(directory);
        if (!names) throw std::runtime_error(directory.error());
        std::set<std::string> families;
        for (const auto &name : names->names) {
            if (name.ends_with(".sheet")) families.insert(name.substr(0, name.size() - 6));
            else if (name.ends_with("0.webp")) {
                auto prefix = name.substr(0, name.size() - 6);
                if (!prefix.empty() && !std::isdigit(static_cast<unsigned char>(prefix.back()))) families.insert(prefix);
            }
        }
        for (const auto &name : families) Toolkit::requestSprite("data/gfx/" + name, name == "ressource");
        const auto deadline = start + std::chrono::minutes(3);
        while (!Toolkit::pollAssets(4)) {
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Asset benchmark timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#ifdef HAVE_OPENGL
        if (gpu) {
            glFinish();
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Asset upload produced an OpenGL error");
        }
#endif
        const double readyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const double readyCpuMs = processCpuMs() - cpuStart;
        auto fingerprintLayers = [&] {
            std::uint64_t fingerprint = 14695981039346656037ull;
            auto hash = [&](const unsigned char *data, size_t size) {
                for (size_t i = 0; i < size; ++i) { fingerprint ^= data[i]; fingerprint *= 1099511628211ull; }
            };
            size_t frames = 0;
            for (const auto &name : families) {
                auto *sprite = Toolkit::findSprite("data/gfx/" + name);
                if (!sprite) throw std::runtime_error("Missing sprite: " + name);
                for (int i = 0; i < sprite->getFrameCount(); ++i) {
                    ++frames;
                    for (auto *image : {sprite->nativeFrame(i), sprite->baseFrame(i)}) {
                        if (!image) continue;
                        const auto *surface = image->getSDLSurface();
                        for (int y = 0; y < surface->h; ++y)
                            hash(static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch, size_t(surface->w) * 4);
                    }
                }
            }
            return std::pair{fingerprint, frames};
        };
        const auto [fingerprint, frames] = fingerprintLayers();
        const auto metrics = Toolkit::assets().metrics();
        std::cout << "ASSET_BENCHMARK phase=startup ready_ms=" << readyMs << " cpu_ms=" << readyCpuMs << " families=" << families.size() << " frames=" << frames
            << " cpu_threads=" << metrics.cpuThreads << " io_threads=" << metrics.ioThreads
            << " peak_cpu=" << metrics.peakCpu << " peak_reads=" << metrics.peakReads
            << " peak_scratch=" << metrics.peakWorkingBytes << " jobs=" << metrics.completed
            << " failed=" << metrics.failed << " oversized=" << metrics.oversizedJobs
            << " hd_cpu_bytes=" << Sprite::highResolutionStats().cpuBytes
            << " texture_bytes=" << DrawableSurface::allocatedTextureBytes()
            << " fingerprint=" << std::hex << fingerprint << std::dec << std::endl;
        if (std::getenv("GLOB2_ASSET_BENCHMARK_RELOAD")) {
            Sprite::setHighResolution(false);
            const auto reloadStart = std::chrono::steady_clock::now();
            const double reloadCpuStart = processCpuMs();
            Sprite::requestHighResolution(true);
            const auto reloadDeadline = reloadStart + std::chrono::minutes(3);
            while (!Toolkit::pollAssets(2)) {
                if (std::chrono::steady_clock::now() >= reloadDeadline) throw std::runtime_error("HD reload timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
#ifdef HAVE_OPENGL
            if (gpu) {
                glFinish();
                if (glGetError() != GL_NO_ERROR) throw std::runtime_error("HD reload produced an OpenGL error");
            }
#endif
            const double reloadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - reloadStart).count();
            const double reloadCpuMs = processCpuMs() - reloadCpuStart;
            const auto [reloadFingerprint, reloadFrames] = fingerprintLayers();
            if (std::getenv("GLOB2_ASSET_BENCHMARK_HD") && reloadFingerprint != fingerprint)
                throw std::runtime_error("HD reload changed sprite pixels");
            std::cout << "ASSET_BENCHMARK phase=reload ready_ms=" << reloadMs << " cpu_ms=" << reloadCpuMs
                << " frames=" << reloadFrames << " hd_cpu_bytes=" << Sprite::highResolutionStats().cpuBytes
                << " texture_bytes=" << DrawableSurface::allocatedTextureBytes()
                << " fingerprint=" << std::hex << reloadFingerprint << std::dec << std::endl;
        }
        Toolkit::close();
        return families.empty() ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; Toolkit::close(); return 1;
    }
}
