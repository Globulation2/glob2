// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComputeExecutor.h"
#include "sim/snapshot/SnapshotStore.h"
#include "Map.h"
#include "CommandLine.h"
// Opt-in loaded-game CPU benchmark. PROFILE_* overrides are local diagnostics.
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Engine.h"
#include "ComputeThreads.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "render/SoftwareTerrainCache.h"
#include "render/terrain/TerrainCompositor.h"
#include <RenderBackend.h>
#include <RendererSelection.h>
#include <SpriteRenderAtlas.h>
#include <RenderMemory.h>
#include <PresentationWork.h>
#include <GameplayRecording.h>
#include <stdexcept>
#include <cmath>
#include "FileManager.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <Environment.h>
#include <cstdlib>
#include <vector>
#ifdef WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#include <csignal>
#endif
#include <time.h>
#include "PerformanceTelemetry.h"
#include "MapDrawer.h"
#include "GameGUIToolManager.h"
GlobalContainer *globalContainer = nullptr;
static bool instrumentedTiming()
{
    // Interposition may pause the process or change instruction execution even
    // when the benchmark's own boundary stops are disabled.
    return std::getenv("PROFILE_BOUNDARY_STOP") || std::getenv("PROFILE_CALLGRIND") ||
        std::getenv("GLOB2_PROFILE_PERF_BOUNDARIES") ||
        std::getenv("GLOB2_PROFILE_CALLGRIND_BOUNDARIES") ||
        std::getenv("LD_PRELOAD") || std::getenv("LD_AUDIT") ||
        std::getenv("DYLD_INSERT_LIBRARIES");
}
// External profilers attach after warmup and disable counters before reporting.
// Stopping is opt-in and benchmark-only; no profiler library enters the game.
static void profileBoundary(const char *phase)
{
    if (!std::getenv("PROFILE_BOUNDARY_STOP"))
        return;
#ifndef WIN32
    printf("PROFILE_BOUNDARY phase=%s\n", phase);
    fflush(stdout);
    if (raise(SIGSTOP))
        throw std::runtime_error("Cannot stop at profiling boundary");
#else
    throw std::runtime_error("PROFILE_BOUNDARY_STOP requires POSIX signals");
#endif
}
// Linux memory gauges (bytes). Other platforms report only peak_rss_bytes.
struct ProcessMemory { long long rss = -1, hwm = -1, anon = -1, file = -1, drm = -1; };
static ProcessMemory processMemory()
{
    ProcessMemory result;
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string entry;
    while (std::getline(status, entry))
    {
        const auto colon = entry.find(':');
        if (colon == std::string::npos) continue;
        const auto key = entry.substr(0, colon);
        const long long value = std::atoll(entry.c_str() + colon + 1) * 1024;
        if (key == "VmRSS") result.rss = value;
        else if (key == "VmHWM") result.hwm = value;
        else if (key == "RssAnon") result.anon = value;
        else if (key == "RssFile") result.file = value;
    }
    // DRM fdinfo (amdgpu, i915/xe, msm, panfrost...) reports per-client GPU
    // memory. Proprietary NVIDIA does not; the runner samples nvidia-smi then.
    for (int fd = 0; fd < 1024; ++fd)
    {
        std::ifstream info("/proc/self/fdinfo/" + std::to_string(fd));
        if (!info) continue;
        std::string line;
        bool drm = false;
        long long total = 0;
        while (std::getline(info, line))
        {
            drm |= line.rfind("drm-client-id:", 0) == 0;
            if (line.rfind("drm-resident-", 0) == 0 || line.rfind("drm-memory-", 0) == 0)
            {
                const auto colon = line.find(':');
                long long amount = std::atoll(line.c_str() + colon + 1);
                if (line.find("KiB") != std::string::npos) amount *= 1024;
                else if (line.find("MiB") != std::string::npos) amount *= 1024 * 1024;
                total += amount;
            }
        }
        if (drm) result.drm = std::max(result.drm, 0LL) + total;
    }
#endif
    return result;
}
static void printMemory(const char *label, const ProcessMemory &m)
{
    printf("%s rss_bytes=%lld hwm_bytes=%lld rss_anon_bytes=%lld rss_file_bytes=%lld drm_fdinfo_bytes=%lld\n",
           label, m.rss, m.hwm, m.anon, m.file, m.drm);
}
// Callgrind can collect only measured frames: --toggle-collect='*profiledFrame*'.
template <class F> [[gnu::noinline]] static void profiledFrame(F &&body) { body(); }
static double cpu()
{
#ifdef WIN32
	FILETIME created, exited, kernel, user;
	if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
		throw std::runtime_error("GetProcessTimes failed");
	auto seconds = [](FILETIME t)
	{
		ULARGE_INTEGER value;
		value.LowPart = t.dwLowDateTime;
		value.HighPart = t.dwHighDateTime;
		return value.QuadPart * 1e-7;
	};
	return seconds(kernel) + seconds(user);
#else
	rusage r{};
	if (getrusage(RUSAGE_SELF, &r))
		throw std::runtime_error("getrusage failed");
	return r.ru_utime.tv_sec + r.ru_utime.tv_usec * 1e-6 + r.ru_stime.tv_sec +
		   r.ru_stime.tv_usec * 1e-6;
#endif
}
static std::uint64_t cpuClock()
{
#if defined(CLOCK_THREAD_CPUTIME_ID) && !defined(WIN32)
	timespec t{};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t))
		throw std::runtime_error("thread CPU clock failed");
	return std::uint64_t(t.tv_sec) * 1000000000ull + t.tv_nsec;
#else
	return std::uint64_t(cpu() * 1e9);
#endif
}
static void distribution(const char *label, std::vector<double> values)
{
    if (values.empty())
        throw std::runtime_error("Benchmark produced no displayed frames");
    double total = 0;
    for (const auto value : values)
        total += value;
    std::sort(values.begin(), values.end());
    printf("%s mean_ms=%.4f median_ms=%.4f p95_ms=%.4f p99_ms=%.4f\n", label, total / values.size(),
           values[values.size() / 2], values[(values.size() - 1) * 95 / 100],
           values[(values.size() - 1) * 99 / 100]);
}
class SoftwareRenderBenchmark
{
static void interactiveBenchmark()
{
    const auto *path = std::getenv("PROFILE_SAVE");
    if (!path || !*path)
        throw std::runtime_error("PROFILE_SAVE must name a saved game");
    Engine engine;
    if (engine.initCustom(path) != Engine::EE_NO_ERROR)
        throw std::runtime_error("Cannot load interactive fixture");
    const int fps = std::getenv("PROFILE_FPS") ? std::stoi(std::getenv("PROFILE_FPS")) : 60;
    const double seconds =
        std::getenv("PROFILE_SECONDS") ? std::stod(std::getenv("PROFILE_SECONDS")) : 10;
    if (fps < 1 || fps > 1000 || seconds <= 0 || seconds > 3600)
        throw std::runtime_error("Invalid interactive benchmark duration or FPS");
    const Uint64 period = 1000000000ULL / fps;
    // The schedule below is the sole pacing authority for this probe.
    globalContainer->gfx->setTargetRenderFps(0);
    engine.beginSession(SDL_GetTicks());
    const auto initialTick = engine.gui.game.stepCounter;
    if (!engine.startSimulationThread(SDL_GetTicks()))
        throw std::runtime_error("Interactive benchmark requires a simulation thread");
    std::vector<double> draw, age, lateness;
    unsigned missed = 0, opportunities = 0;
    const auto began = SDL_GetTicksNS();
    const double cpuBegan = cpu();
    auto due = began;
    while (SDL_GetTicksNS() - began < seconds * 1e9)
    {
        const auto now = SDL_GetTicksNS();
        if (now < due)
        {
            SDL_Delay(Uint32(std::min(Uint64(5), (due - now) / 1000000)));
            continue;
        }
        if (!engine.threadedClientFrame(SDL_GetTicks(), {}))
            break;
        ++opportunities;
        if (std::getenv("PROFILE_PAN"))
        {
            engine.gui.camera.originX += .25;
            engine.gui.viewportX = engine.gui.camera.tileX();
        }
        const auto renderedBefore = PerformanceTelemetry::collector().calls[
            static_cast<unsigned>(PerformanceTelemetry::Id::Render)];
        const auto start = SDL_GetTicksNS();
        if (globalContainer->gfx->beginRenderFrame())
            engine.drawSession();
        const auto end = SDL_GetTicksNS();
        // AcquireScene can initially have no published frame. Exclude that
        // startup opportunity from displayed-frame distributions.
        const auto &scene = engine.gui.drawnScene();
        if (PerformanceTelemetry::collector().calls[
                static_cast<unsigned>(PerformanceTelemetry::Id::Render)] != renderedBefore)
        {
            draw.push_back((end - start) / 1e6);
            const auto displayed = SDL_GetTicks();
            age.push_back(displayed >= scene.tickTime ? double(displayed - scene.tickTime) : 0);
            lateness.push_back(engine.simulationDeadlineLatenessNs() / 1e6);
            missed += end > due + period;
        }
        due += period;
        if (end > due + period)
            due = end; // No catch-up drawing after a missed frame.
    }
    const double totalCpu = cpu() - cpuBegan;
    engine.stopSimulationThread();
    printf("timing_valid=%s\n", instrumentedTiming() ? "false" : "true");
    printf("interactive_progress initial_tick=%u final_tick=%u\n",
           initialTick, engine.gui.game.stepCounter);
    printf("interactive frames=%zu opportunities=%u undrawn_opportunities=%zu "
           "missed_render_deadlines=%u period_ns=%llu\n", draw.size(), opportunities,
           opportunities - draw.size(), missed, (unsigned long long)period);
    printf("process_cpu_ms_per_frame=%.4f\n", totalCpu * 1000 / draw.size());
    distribution("draw_and_present", draw);
    distribution("displayed_scene_age", age);
    distribution("last_simulation_deadline_lateness", lateness);
    PerformanceTelemetry::collector().write(std::cout, "interactive", engine.gui.game.stepCounter,
                                            true);
    engine.gui.isRunning = false;
    engine.finishSession();
}
  public:
	static int run(int argc, char **argv)
	{
		SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#ifdef __APPLE__
		// Create the window hidden and never activate it; PROFILE_VISIBLE shows it.
		if (!std::getenv("PROFILE_VISIBLE"))
			GAGCore::setProcessEnvironment("GLOB2_TEST_HIDDEN_WINDOWS", "1", 0);
#endif
		// Keep -s dimensions as measured pixels across display densities. Opt in
		// to native Retina/HiDPI presentation explicitly for lifecycle profiling.
		const bool nativeDisplay = getenv("PROFILE_NATIVE_DISPLAY") != nullptr;
		globalContainer = new GlobalContainer;
		std::vector<std::string> launchArgs{"play"};
        launchArgs.insert(launchArgs.end(),argv+1,argv+argc);
        globalContainer->applyCommand(Cli::parse(launchArgs));
		globalContainer->settings.mute = getenv("PROFILE_AUDIO") ? 0 : 1;
		globalContainer->settings.autosaveGames = false;
		globalContainer->load();
		auto *gfx = globalContainer->gfx;
        if (!nativeDisplay && !gfx->setRes(gfx->getRequestedW(), gfx->getRequestedH(),
                gfx->getOptionFlags() | GraphicContext::LOWPIXELDENSITY))
            throw std::runtime_error(std::string("Cannot create fixed-pixel benchmark window: ") + SDL_GetError());
        const int version = SDL_GetVersion();
        printf("SDL version=%d.%d.%d revision=%s\n", SDL_VERSIONNUM_MAJOR(version),
            SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), SDL_GetRevision());
		if (!getenv("PROFILE_VISIBLE"))
			SDL_HideWindow(SDL_GetWindowFromID(gfx->windowID()));
        const bool portable = gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL;
		if (!getenv("PROFILE_RECORD") && gfx->rendererCapabilities().accelerated && !portable)
			throw std::runtime_error("Benchmark requires the software (--renderer software) or portable SDL (--renderer sdl) backend");
        {
            SDL_Renderer *sdlRenderer = SDL_GetRenderer(SDL_GetWindowFromID(gfx->windowID()));
            int vsync = 0;
            if (sdlRenderer) SDL_GetRenderVSync(sdlRenderer, &vsync);
            printf("RENDERER backend=%s sdl_render_driver=%s vsync=%d max_texture=%d\n",
                   std::string(GAGCore::rendererBackendName(gfx->backendSwitchStatus().active)).c_str(),
                   sdlRenderer ? SDL_GetRendererName(sdlRenderer) : "none", vsync, gfx->maximumTextureSize());
        }
        // SDL_Renderer queues commands until presentation; skipping it would
        // grow the queue and leave GPU work unmeasured.
        if (portable && getenv("PROFILE_NO_PRESENT"))
            throw std::runtime_error("Portable SDL measurements must present (unset PROFILE_NO_PRESENT)");
        if (const char *mode = std::getenv("PROFILE_MODE");
            mode && std::string(mode) == "interactive")
        {
            interactiveBenchmark();
            delete globalContainer;
            globalContainer = nullptr;
            return 0;
        }
		{
			GameGUI gui;
            GAGCore::PresentationWorkScope cpuWork(gui.game.map.computeExecutor());
			const char *path = getenv("PROFILE_SAVE");
			if (!path || !*path)
				throw std::runtime_error("PROFILE_SAVE must name a saved game");
			BinaryInputStream stream(
				glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), path));
			if (!gui.load(&stream, true))
				throw std::runtime_error("Cannot load benchmark save");
            // Standalone GameGUI loading does not start an Engine session.
            // Apply the same compute budget before measuring rendering batches.
            gui.game.map.configureCompute(resolveComputeThreadCount(globalContainer->computeThreads));
            printf("compute_threads=%zu\n", gui.game.map.computeExecutor().threadCount());
			auto &terrainCache = gui.view.render.terrainCache(gui.game.map.identity());
			if (const char *c = getenv("PROFILE_TERRAIN_CACHE"))
				terrainCache.enabled = atoi(c) != 0;
			gui.localPlayer = gui.localTeamNo = 0;
			gui.adjustLocalTeam();
            // Camera bounds come from the extracted scene. Setting zoom before
            // that scene exists silently clamps half-scale requests to 1.0.
            gui.prepareLocalPresentation();
			gui.adjustInitialViewport();
            gui.updateCamera();
            const char *mode = getenv("PROFILE_MODE");
            if (!mode) mode = "gui";
            if (std::string(mode) != "gui" && std::string(mode) != "map")
                throw std::runtime_error("PROFILE_MODE must be gui, map or interactive");
            if (std::string(mode) == "map")
            {
                // Preserve the original map-only benchmark's origin.
                gui.camera.originX = gui.camera.originY = 0;
                gui.viewportX = gui.viewportY = 0;
            }
			gui.gamePaused = getenv("PROFILE_PAUSED") != nullptr;
			// PROFILE_SELECT=building|flag|unit selects the local team's first such entity,
			// so captures include the selection panel and the map-view selection markers.
			if (const char *select = getenv("PROFILE_SELECT"))
			{
				const std::string kind = select;
				Team *team = gui.game.teams[0];
				bool selected = false;
				for (Building *b : team->liveBuildings.entries())
					if (!selected && kind != "unit")
						if (kind == "flag" ? b->type->isVirtual : (!b->type->isVirtual && b->type->maxUnitWorking))
						{
							gui.setSelection(GameGUI::BUILDING_SELECTION, b);
							selected = true;
						}
				for (Unit *u : team->liveUnits.entries())
					if (!selected && kind == "unit")
					{
						gui.setSelection(GameGUI::UNIT_SELECTION, u);
						selected = true;
					}
				if (!selected)
					throw std::runtime_error("PROFILE_SELECT found nothing to select");
			}
			// PROFILE_TOOL=<building type> activates the building tool with the cursor
			// over the middle of the map view, so captures include the placement preview.
			if (const char *tool = getenv("PROFILE_TOOL"))
			{
				gui.toolManager.activateBuildingTool(tool);
				gui.selectionMode = GameGUI::TOOL_SELECTION;
				gui.mouseX = (gfx->getW() - 160) / 2;
				gui.mouseY = gfx->getH() / 2;
			}
			if (const char *z = getenv("PROFILE_ZOOM"))
			{
				gui.updateCamera();
				const float zoom = std::stof(z);
				if (!std::isfinite(zoom) || zoom <= 0)
					throw std::runtime_error("Zoom must be finite and positive");
				gui.camera.setZoom(zoom, 300, 300);
                if (std::abs(gui.camera.zoom - zoom) > 1e-6)
                    throw std::runtime_error("Requested PROFILE_ZOOM is outside camera bounds");
				gui.viewportX = gui.camera.tileX();
				gui.viewportY = gui.camera.tileY();
			}
			// Games poll asset work every frame, so optional HD layers finish
			// streaming. PROFILE_HD_COMPLETE=1 measures that steady state instead
			// of the partially prefetched layers this frame loop never publishes:
			// layers requested by any warmup frame finish before measurement.
			const bool completeHighResolution = getenv("PROFILE_HD_COMPLETE") && atoi(getenv("PROFILE_HD_COMPLETE"));
			if (completeHighResolution)
				GAGCore::Sprite::finishHighResolution();
			int units = 0, buildings = 0;
			for (int t = 0; t < gui.game.mapHeader.getNumberOfTeams(); t++)
			{
				units += gui.game.teams[t]->liveUnits.size();
				buildings += gui.game.teams[t]->liveBuildings.size();
			}
			if (getenv("PROFILE_FRACTION"))
			{
				gui.updateCamera();
				gui.camera.originX += .5;
				gui.viewportX = gui.camera.tileX();
			}
			if (const char *x = getenv("PROFILE_OFFSET_X"))
				gui.camera.originX += std::stof(x);
			if (const char *y = getenv("PROFILE_OFFSET_Y"))
				gui.camera.originY += std::stof(y);
			if (!std::isfinite(gui.camera.originX) || !std::isfinite(gui.camera.originY))
				throw std::runtime_error("Camera offsets must be finite");
			gui.viewportX = gui.camera.tileX();
			gui.viewportY = gui.camera.tileY();
			gui.view.render.minimumZoom = gui.camera.minimumZoom();
			printf("CAMERA zoom=%.4f fractional=%.4f,%.4f offset=%.4f,%.4f\n", gui.camera.zoom,
				   gui.camera.fractionX(), gui.camera.fractionY(), gui.camera.offsetX,
				   gui.camera.offsetY);
			const int frames = getenv("PROFILE_FRAMES") ? atoi(getenv("PROFILE_FRAMES")) : 240;
			const int warmup = getenv("PROFILE_WARMUP") ? atoi(getenv("PROFILE_WARMUP")) : 30;
			if (frames < 1 || warmup < 0)
				throw std::runtime_error("frames must be positive and warmup nonnegative");
			const bool clouds = !(getenv("PROFILE_CLOUDS") && atoi(getenv("PROFILE_CLOUDS")) == 0);
			globalContainer->settings.clouds = clouds;
			globalContainer->settings.cloudShadows = clouds;
			// PROFILE_ADAPTIVE_ZOOM=0 draws overlays scaled with the map, as before
			// adaptive zoom detail, for before/after captures from one build.
			if (const char *adaptive = getenv("PROFILE_ADAPTIVE_ZOOM"))
				globalContainer->settings.adaptiveZoomDetail = atoi(adaptive) != 0;
			printf("READY save=%s tick=%u map=%dx%d teams=%d units=%d buildings=%d surface=%dx%d "
				   "camera=%d,%d mode=%s clouds=%d frames=%d driver=%s\n",
				   path, gui.game.stepCounter, gui.game.map.getW(), gui.game.map.getH(),
				   gui.game.mapHeader.getNumberOfTeams(), units, buildings, gfx->getW(),
				   gfx->getH(), gui.viewportX, gui.viewportY, mode, clouds, frames,
				   SDL_GetCurrentVideoDriver());
			fflush(stdout);
			if (getenv("PROFILE_CPU_SCOPES"))
			{
				PerformanceTelemetry::collector().clock = cpuClock;
				PerformanceTelemetry::collector().reset();
			}
			auto &recorder = GAGCore::Recording::recorder();
			if (const char *record = getenv("PROFILE_RECORD"); record && std::string(record) != "off")
			{
				if (getenv("PROFILE_RECORD_SOFTWARE")) recorder.options.encoder = GAGCore::Recording::EncoderPreference::Software;
				if (!recorder.start(record)) throw std::runtime_error(recorder.status().error);
				recorder.beginMatch("benchmark",path,0,gui.game.stepCounter);
				recorder.matchFrame(gui.game.stepCounter,false,1);
			}
			const double seconds = getenv("PROFILE_SECONDS") ? std::stod(getenv("PROFILE_SECONDS")) : 0;
			Uint64 measuredStart = 0;
			std::vector<double> draw, present, frame;
            draw.reserve(frames); present.reserve(frames); frame.reserve(frames);
			RenderOperations initialOps{};
            decltype(terrainCache.workMetrics) initialWork{};
            if (const char* mode = getenv("PROFILE_COMPUTE_TELEMETRY"))
                gui.game.map.computeExecutor().configureTelemetry(ComputeTelemetry::parseMode(mode));
            auto initialGraphics = gui.game.map.computeExecutor().graphicsMetrics();
			if (std::string(mode) == "gui")
			{
				// This probe holds simulation state fixed. Seed its stalled rate so
				// differing run durations cannot change the HUD from "-" to "0.0".
				const Uint32 now = Uint32(SDL_GetTicks());
				gui.tickRate = {};
				gui.tickRate.sample(now - TickRateMeter::UPDATE_MS, 0, 0);
				gui.tickRate.sample(now, 0, 0);
				printf("fixed_hud_tick_rate=0.0\n");
			}
			std::uint64_t initialHits = 0, initialRebuilds = 0;
			auto &compositor = globalContainer->terrainCompositor(gui.game.map.frozenAssetBundle());
			TerrainVisual::Compositor::HighResolutionCoverage initialCoverage;
			double c0 = 0;
			double freq = SDL_GetPerformanceFrequency();
			const auto checksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
			// SDL_Renderer output never reaches the CPU surface; read the last
			// measured frame back before presentation instead.
			std::shared_ptr<SDL_Surface> portableCapture;
			std::string portableCaptureError;
			for (int i = -warmup; i < frames || (seconds > 0 && (i < 0 || (SDL_GetPerformanceCounter()-measuredStart)/freq < seconds)); i++)
			{
				SDL_PumpEvents();
				if (completeHighResolution && i <= 0)
					GAGCore::Sprite::finishHighResolution();
				if (i == 0)
				{
                    printMemory("memory_begin", processMemory());
					profileBoundary("begin");
                    c0 = cpu(); measuredStart = SDL_GetPerformanceCounter();
					initialOps = gfx->backendOperations();
                    initialWork = terrainCache.workMetrics;
                    initialGraphics = gui.game.map.computeExecutor().graphicsMetrics();
					initialHits = terrainCache.cacheHits();
					initialRebuilds = terrainCache.cacheRebuilds();
					initialCoverage = compositor.highResolutionCoverage();
					PerformanceTelemetry::collector().reset();
				}
				Uint64 a = 0, b = 0, c = 0;
				auto body = [&] {
				if (getenv("PROFILE_PAN"))
				{ gui.camera.originX += .25; gui.viewportX = gui.camera.tileX(); }
				a = SDL_GetPerformanceCounter();
				// Isolate the retention-copy cost without a production graphics setting.
				if (getenv("PROFILE_PRESERVE_FRAME"))
					gfx->beginFrame(GraphicContext::FrameMode::PreserveContent);
				// Standalone tools observe through the production snapshot boundary.
				gui.game.snapshots().invalidateBoundary();
				gui.prepareLocalPresentation();
				if (std::string(mode) == "gui")
					gui.drawAll(0);
				else
				{
					gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
					gfx->setClipRect();
					MapDrawer::drawMap(0, 0, gfx->getW() - 160, gfx->getH(), 0, 0, gui.viewportX,
									 gui.viewportY, 0, gui.view, Game::DRAW_AREA |
                                         (getenv("PROFILE_WHOLE_MAP") ? Game::DRAW_WHOLE_MAP : 0),
                                         nullptr, nullptr, gui.gamePaused);
				}
				b = SDL_GetPerformanceCounter();
				if (portable && i == frames - 1 && getenv("PROFILE_CAPTURE") && !portableCapture)
					gfx->requestMemoryCapture([&](auto image, auto, auto why) {
						portableCapture = std::move(image); portableCaptureError = why; });
				if (!getenv("PROFILE_NO_PRESENT"))
					gfx->nextFrame();
				c = SDL_GetPerformanceCounter();
				};
				if (i >= 0) profiledFrame(body); else body();
				if (i >= 0)
				{
					draw.push_back(1000. * (b - a) / freq);
					present.push_back(1000. * (c - b) / freq);
                    frame.push_back(1000. * (c - a) / freq);
				}
			}
            // Finish pending owner segments before disabling external counters.
            gfx->getSDLSurface();
            const double elapsed = (SDL_GetPerformanceCounter()-measuredStart)/freq;
			double totalCpu = cpu() - c0;
            const auto steadyMemory = processMemory();
            profileBoundary("end");
            printMemory("memory_end", steadyMemory);
            printf("timing_valid=%s\n", instrumentedTiming() ? "false" : "true");
			printf("render_fps=%.4f elapsed_seconds=%.4f frames=%zu\n",draw.size()/elapsed,elapsed,draw.size());
			recorder.stop(); recorder.shutdown();
			if (const char *record = getenv("PROFILE_RECORD"); record && std::string(record) != "off")
			{
				const auto status = recorder.status();
				printf("recording encoder=%s dimensions=%dx%d dropped_frames=%llu dropped_audio_samples=%llu\n",status.encoder.c_str(),status.width,status.height,(unsigned long long)status.droppedFrames,(unsigned long long)status.droppedAudioSamples);
				if (status.state != GAGCore::Recording::State::Complete) throw std::runtime_error(status.error);
			}
#ifndef WIN32
			rusage memory{}; getrusage(RUSAGE_SELF,&memory);
			printf("peak_rss_bytes=%llu\n",(unsigned long long)memory.ru_maxrss *
#ifdef __APPLE__
				1ull
#else
				1024ull
#endif
			);
#endif
			if (gui.game.checkSum(nullptr, nullptr, nullptr, true) != checksum)
				throw std::runtime_error("rendering changed simulation checksum");
			printf("simulation_checksum=%u\n", checksum);
            distribution("draw", draw);
            distribution("present", present);
            distribution("frame", frame);
			printf("process_cpu_ms_per_frame=%.4f\n", totalCpu * 1000 / draw.size());
			fflush(stdout);
			auto ops = gfx->backendOperations();
			printf("backend_ops blits=%llu fills=%llu triangles=%llu\n",
				   (unsigned long long)(ops.blits - initialOps.blits),
				   (unsigned long long)(ops.fills - initialOps.fills),
				   (unsigned long long)(ops.triangles - initialOps.triangles));
            if (portable)
            {
                // Per-frame submission costs: `triangles` counts logical submissions and
                // `geometryBatches` the SDL_RenderGeometry calls they were merged into.
                const double n = double(draw.size());
                const auto per = [&](std::uint64_t now, std::uint64_t then) { return (now - then) / n; };
                printf("portable_frame geometry_calls=%.1f vertices=%.1f texture_switches=%.1f "
                       "texture_uploads=%.2f upload_bytes=%.0f gpu_prepared_uploads=%.2f "
                       "texture_creates=%.2f texture_destroys=%.2f cpu_premultiplied_bytes=%.0f geometry_batches=%.1f\n",
                       per(ops.triangles, initialOps.triangles), per(ops.vertices, initialOps.vertices),
                       per(ops.textureSwitches, initialOps.textureSwitches),
                       per(ops.textureUploads, initialOps.textureUploads),
                       per(ops.textureUploadBytes, initialOps.textureUploadBytes),
                       per(ops.gpuPreparedUploads, initialOps.gpuPreparedUploads),
                       per(ops.textureCreates, initialOps.textureCreates),
                       per(ops.textureDestroys, initialOps.textureDestroys),
                       per(ops.cpuPremultipliedBytes, initialOps.cpuPremultipliedBytes),
                       per(ops.geometryBatches, initialOps.geometryBatches));
                printf("portable_warmup texture_uploads=%llu upload_bytes=%llu texture_creates=%llu\n",
                       (unsigned long long)initialOps.textureUploads,
                       (unsigned long long)initialOps.textureUploadBytes,
                       (unsigned long long)initialOps.textureCreates);
                printf("portable_memory textures=%llu texture_bytes=%llu peak_texture_bytes=%llu "
                       "gpu_prepared_textures=%llu source_surface_bytes=%llu shared_source_textures=%llu "
                       "staging_texture_bytes=%llu batch_capacity_bytes=%llu frame_surface_bytes=%llu\n",
                       (unsigned long long)ops.textureCount, (unsigned long long)ops.textureBytes,
                       (unsigned long long)ops.peakTextureBytes, (unsigned long long)ops.gpuPreparedTextures,
                       (unsigned long long)ops.sourceSurfaceBytes, (unsigned long long)ops.sharedSourceTextures,
                       (unsigned long long)ops.stagingTextureBytes,
                       (unsigned long long)ops.batchCapacityBytes,
                       (unsigned long long)(gfx->getSDLSurface() ? std::uint64_t(gfx->getSDLSurface()->h) * gfx->getSDLSurface()->pitch : 0));
            }
			printf("terrain_cache bytes=%zu mask_bytes=%zu hits=%llu rebuilds=%llu\n",
				   terrainCache.bytes(), terrainCache.maskBytes(),
				   (unsigned long long)(terrainCache.cacheHits() - initialHits),
				   (unsigned long long)(terrainCache.cacheRebuilds() -
										initialRebuilds));
            // Artwork held outside the backend's texture cache (CPU bytes).
            printf("artwork_memory terrain_compositor_bytes=%zu sprite_atlas_bytes=%zu drawable_texture_bytes=%zu\n",
                   compositor.sourceBytes(), GAGCore::spriteAtlasMemoryBytes(),
                   GAGCore::DrawableSurface::allocatedTextureBytes());
            // HD sprite layers resident at the end (decoded pixels and textures),
            // and the deferred frames kept decoded after leaving their windows.
            {
                std::size_t cpu = 0, gpu = 0, holders = 0;
                for (const auto &usage : GAGCore::RenderMemory::sprites())
                {
                    cpu += usage.highResolutionCpuBytes;
                    gpu += usage.highResolutionGpuBytes;
                    holders += usage.highResolutionCpuBytes + usage.highResolutionGpuBytes > 0;
                    if (getenv("PROFILE_SPRITE_HD") && usage.highResolutionCpuBytes + usage.highResolutionGpuBytes)
                        printf("sprite_hd_usage name=%s cpu_bytes=%zu gpu_bytes=%zu\n", usage.name.c_str(),
                               usage.highResolutionCpuBytes, usage.highResolutionGpuBytes);
                }
                printf("sprite_hd cpu_bytes=%zu gpu_bytes=%zu sprites=%zu retired_bytes=%zu\n", cpu, gpu, holders,
                       GAGCore::Sprite::retiredHighResolutionBytes());
            }
            // Streaming fallback: a prepared view drew native art for a material
            // whose HD frames had already appeared. Warmup is reported separately.
            const auto coverage = compositor.highResolutionCoverage();
            printf("terrain_hd prepares=%llu fallback_prepares=%llu warmup_prepares=%llu warmup_fallback_prepares=%llu\n",
                   (unsigned long long)(coverage.prepares - initialCoverage.prepares),
                   (unsigned long long)(coverage.fallbacks - initialCoverage.fallbacks),
                   (unsigned long long)initialCoverage.prepares,
                   (unsigned long long)initialCoverage.fallbacks);
            const auto work = terrainCache.workMetrics;
            const auto graphics = gui.game.map.computeExecutor().graphicsMetrics();
            printf("terrain_cpu batches=%llu jobs=%llu cells=%llu serial_pages=%llu "
                   "batch_ns=%llu peak_reserved_bytes=%zu\n",
                   (unsigned long long)(work.batches - initialWork.batches), (unsigned long long)(work.jobs - initialWork.jobs),
                   (unsigned long long)(work.cells - initialWork.cells), (unsigned long long)(work.serialPages - initialWork.serialPages),
                   (unsigned long long)(work.batchNs - initialWork.batchNs), work.peakReservedBytes);
            printf("raster_cpu batches=%llu tiles=%llu references=%llu jobs=%llu "
                   "serial_batches=%llu snapshot_bytes=%llu source_reuses=%llu source_retained_bytes=%llu peak_retained_bytes=%llu "
                   "fallback_operations=%llu\n",
                   (unsigned long long)(ops.rasterBatches - initialOps.rasterBatches), (unsigned long long)(ops.rasterTiles - initialOps.rasterTiles),
                   (unsigned long long)(ops.rasterReferences - initialOps.rasterReferences),
                   (unsigned long long)(ops.rasterJobs - initialOps.rasterJobs),
                   (unsigned long long)(ops.rasterSerialBatches - initialOps.rasterSerialBatches),
                   (unsigned long long)(ops.rasterSnapshotBytes - initialOps.rasterSnapshotBytes),
                   (unsigned long long)(ops.rasterSourceReuses - initialOps.rasterSourceReuses),
                   (unsigned long long)ops.rasterSourceRetainedBytes,
                   (unsigned long long)ops.rasterPeakRetainedBytes,
                   (unsigned long long)(ops.rasterFallbacks - initialOps.rasterFallbacks));
            const auto computeMode = gui.game.map.computeExecutor().telemetryMode();
            const bool graphicsAvailable = computeMode == ComputeTelemetryMode::Trace;
            const auto field = [](std::uint64_t value, bool available) { return available ? std::to_string(value) : std::string("na"); };
            printf("graphics_executor mode=%s worker_available=%s batches=%s parallel_batches=%s jobs=%s "
                   "summed_queue_ns=%s summed_active_ns=%s owner_wait_ns=%s\n",
                   ComputeTelemetry::ModeNames[unsigned(computeMode)].data(), graphicsAvailable ? "true" : "false",
                   field(graphics.batches-initialGraphics.batches,graphicsAvailable).c_str(),
                   field(graphics.parallelBatches-initialGraphics.parallelBatches,graphicsAvailable).c_str(),
                   field(graphics.jobs-initialGraphics.jobs,graphicsAvailable).c_str(),
                   field(graphics.queueNs-initialGraphics.queueNs,graphicsAvailable).c_str(),
                   field(graphics.activeNs-initialGraphics.activeNs,graphicsAvailable).c_str(),
                   field(graphics.waitNs-initialGraphics.waitNs,computeMode!=ComputeTelemetryMode::Off).c_str());
            if (const char *path = std::getenv("PROFILE_COMPUTE_TRACE")) {
                gfx->getSDLSurface(); // Fence the final segment before observing drained work.
                std::ofstream output(path);
                if (!output) throw std::runtime_error("Cannot open compute trace output");
                ComputeTelemetry::writeJson(output, gui.game.map.computeExecutor().telemetrySnapshot(true));
                if (!output) throw std::runtime_error("Cannot write compute trace output");
            }
			PerformanceTelemetry::collector().write(std::cout, "profile", gui.game.stepCounter,
													false);
			if (const char *p = getenv("PROFILE_CAPTURE"))
			{
				if (portable && !portableCapture)
					throw std::runtime_error("Portable capture failed: " + portableCaptureError);
				if (!SDL_SaveBMP(portable ? portableCapture.get() : gfx->getSDLSurface(), p))
					throw std::runtime_error(SDL_GetError());
			}
		}
		delete globalContainer;
		globalContainer = nullptr;
		return 0;
	}
};
int main(int argc, char **argv)
{
	try
	{
		return SoftwareRenderBenchmark::run(argc, argv);
	}
	catch (const std::exception &e)
	{
		fprintf(stderr, "%s\n", e.what());
		delete globalContainer;
		globalContainer = nullptr;
		return 1;
	}
}
