#include "CommandLine.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in loaded-game CPU benchmark. PROFILE_* overrides are local diagnostics.
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Engine.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "render/SoftwareTerrainCache.h"
#include <RenderBackend.h>
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
#include <cstdlib>
#include <vector>
#ifdef WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#include <time.h>
#include "PerformanceTelemetry.h"
GlobalContainer *globalContainer = nullptr;
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
    globalContainer->gfx->setTargetRenderFps(fps);
    engine.beginSession(SDL_GetTicks());
    if (!engine.startSimulationThread(SDL_GetTicks()))
        throw std::runtime_error("Interactive benchmark requires a simulation thread");
    std::vector<double> draw, age, lateness;
    unsigned missed = 0;
    const auto began = SDL_GetTicksNS();
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
        if (std::getenv("PROFILE_PAN"))
        {
            engine.gui.camera.originX += .25;
            engine.gui.viewportX = engine.gui.camera.tileX();
        }
        const auto start = SDL_GetTicksNS();
        engine.drawSession();
        const auto end = SDL_GetTicksNS();
        // AcquireScene can initially have no published frame. Exclude that
        // startup opportunity from displayed-frame distributions.
        const auto &scene = engine.gui.drawnScene();
        if (scene.tickTime)
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
    engine.stopSimulationThread();
    printf("interactive frames=%zu missed_render_deadlines=%u period_ns=%llu\n", draw.size(),
           missed, (unsigned long long)period);
    distribution("draw_and_present", draw);
    distribution("displayed_scene_age", age);
    distribution("last_simulation_deadline_lateness", lateness);
    PerformanceTelemetry::collector().write(std::cout, "interactive", engine.gui.game.stepCounter,
                                            true);
    engine.finishSession();
}
class SoftwareRenderBenchmark
{
  public:
	static int run(int argc, char **argv)
	{
		SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
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
		if (!getenv("PROFILE_RECORD") && gfx->rendererCapabilities().accelerated)
			throw std::runtime_error("Benchmark requires the software backend (-G)");
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
			auto &terrainCache = gui.view.render.terrainCache(gui.game.map.identity());
			if (const char *c = getenv("PROFILE_TERRAIN_CACHE"))
				terrainCache.enabled = atoi(c) != 0;
			gui.localPlayer = gui.localTeamNo = 0;
			gui.adjustLocalTeam();
			gui.adjustInitialViewport();
			gui.gamePaused = false;
			// PROFILE_SELECT=building|flag|unit selects the local team's first such entity,
			// so captures include the selection panel and the map-view selection markers.
			if (const char *select = getenv("PROFILE_SELECT"))
			{
				const std::string kind = select;
				Team *team = gui.game.teams[0];
				bool selected = false;
				for (int i = 0; i < Building::MAX_COUNT && !selected && kind != "unit"; i++)
					if (Building *b = team->myBuildings[i])
						if (kind == "flag" ? b->type->isVirtual : (!b->type->isVirtual && b->type->maxUnitWorking))
						{
							gui.setSelection(GameGUI::BUILDING_SELECTION, b);
							selected = true;
						}
				for (int i = 0; i < Unit::MAX_COUNT && !selected && kind == "unit"; i++)
					if (Unit *u = team->myUnits[i])
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
				gui.viewportX = gui.camera.tileX();
				gui.viewportY = gui.camera.tileY();
			}
			int units = 0, buildings = 0;
			for (int t = 0; t < gui.game.mapHeader.getNumberOfTeams(); t++)
			{
				for (int i = 0; i < Unit::MAX_COUNT; i++)
					units += gui.game.teams[t]->myUnits[i] != nullptr;
				for (int i = 0; i < Building::MAX_COUNT; i++)
					buildings += gui.game.teams[t]->myBuildings[i] != nullptr;
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
			const char *mode = getenv("PROFILE_MODE");
			if (!mode)
				mode = "gui";
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
			std::vector<double> draw, present;
			RenderOperations initialOps{};
			std::uint64_t initialHits = 0, initialRebuilds = 0;
			double c0 = 0;
			double freq = SDL_GetPerformanceFrequency();
			const auto checksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
			for (int i = -warmup; i < frames || (seconds > 0 && (i < 0 || (SDL_GetPerformanceCounter()-measuredStart)/freq < seconds)); i++)
			{
				SDL_PumpEvents();
				if (i == 0)
				{
					c0 = cpu(); measuredStart = SDL_GetPerformanceCounter();
					initialOps = gfx->backendOperations();
					initialHits = terrainCache.cacheHits();
					initialRebuilds = terrainCache.cacheRebuilds();
					PerformanceTelemetry::collector().reset();
				}
				if (getenv("PROFILE_PAN"))
				{ gui.camera.originX += .25; gui.viewportX = gui.camera.tileX(); }
				Uint64 a = SDL_GetPerformanceCounter();
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
					Game::drawMap(0, 0, gfx->getW() - 160, gfx->getH(), 0, 0, gui.viewportX,
									 gui.viewportY, 0, gui.view, Game::DRAW_AREA);
				}
				Uint64 b = SDL_GetPerformanceCounter();
				if (!getenv("PROFILE_NO_PRESENT"))
					gfx->nextFrame();
				Uint64 c = SDL_GetPerformanceCounter();
				if (i >= 0)
				{
					draw.push_back(1000. * (b - a) / freq);
					present.push_back(1000. * (c - b) / freq);
				}
			}
			double totalCpu = cpu() - c0;
			const double elapsed = (SDL_GetPerformanceCounter()-measuredStart)/freq;
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
			printf("process_cpu_ms_per_frame=%.4f\n", totalCpu * 1000 / draw.size());
			fflush(stdout);
			auto ops = gfx->backendOperations();
			printf("backend_ops blits=%llu fills=%llu triangles=%llu\n",
				   (unsigned long long)(ops.blits - initialOps.blits),
				   (unsigned long long)(ops.fills - initialOps.fills),
				   (unsigned long long)(ops.triangles - initialOps.triangles));
			printf("terrain_cache bytes=%zu mask_bytes=%zu hits=%llu rebuilds=%llu\n",
				   terrainCache.bytes(), terrainCache.maskBytes(),
				   (unsigned long long)(terrainCache.cacheHits() - initialHits),
				   (unsigned long long)(terrainCache.cacheRebuilds() -
										initialRebuilds));
            const auto work = terrainCache.workMetrics;
            const auto graphics = gui.game.map.computeExecutor().graphicsMetrics();
            printf("terrain_cpu batches=%llu jobs=%llu cells=%llu serial_pages=%llu "
                   "batch_ns=%llu peak_reserved_bytes=%zu\n",
                   (unsigned long long)work.batches, (unsigned long long)work.jobs,
                   (unsigned long long)work.cells, (unsigned long long)work.serialPages,
                   (unsigned long long)work.batchNs, work.peakReservedBytes);
            printf("raster_cpu batches=%llu tiles=%llu references=%llu "
                   "serial_batches=%llu snapshot_bytes=%llu peak_retained_bytes=%llu "
                   "fallback_operations=%llu\n",
                   (unsigned long long)ops.rasterBatches, (unsigned long long)ops.rasterTiles,
                   (unsigned long long)ops.rasterReferences,
                   (unsigned long long)ops.rasterSerialBatches,
                   (unsigned long long)ops.rasterSnapshotBytes,
                   (unsigned long long)ops.rasterPeakRetainedBytes,
                   (unsigned long long)ops.rasterFallbacks);
            printf("graphics_executor batches=%llu parallel_batches=%llu jobs=%llu "
                   "summed_queue_ns=%llu summed_active_ns=%llu owner_wait_ns=%llu\n",
                   (unsigned long long)graphics.batches,
                   (unsigned long long)graphics.parallelBatches, (unsigned long long)graphics.jobs,
                   (unsigned long long)graphics.queueNs, (unsigned long long)graphics.activeNs,
                   (unsigned long long)graphics.waitNs);
			PerformanceTelemetry::collector().write(std::cout, "profile", gui.game.stepCounter,
													false);
			if (const char *p = getenv("PROFILE_CAPTURE"))
				SDL_SaveBMP(gfx->getSDLSurface(), p);
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
