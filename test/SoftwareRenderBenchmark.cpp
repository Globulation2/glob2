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
class SoftwareRenderBenchmark
{
  public:
	static int run(int argc, char **argv)
	{
		SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
		// Keep -s dimensions as measured pixels across display densities. Opt in
		// to native Retina/HiDPI presentation explicitly for lifecycle profiling.
		if (!getenv("PROFILE_NATIVE_DISPLAY"))
			SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
		globalContainer = new GlobalContainer;
		globalContainer->parseArgs(argc, argv);
		globalContainer->settings.mute = 1;
		globalContainer->settings.autosaveGames = false;
		globalContainer->load();
		auto *gfx = globalContainer->gfx;
		SDL_version version;
		SDL_GetVersion(&version);
		printf("SDL version=%u.%u.%u revision=%s\n", version.major, version.minor, version.patch,
			   SDL_GetRevision());
		if (!getenv("PROFILE_VISIBLE"))
			SDL_HideWindow(SDL_GetWindowFromID(gfx->windowID()));
		if (gfx->getOptionFlags() & (GraphicContext::USEGPU | GraphicContext::PORTABLEGPU))
			throw std::runtime_error("Benchmark requires the software backend (-G)");
		{
			GameGUI gui;
			const char *path = getenv("PROFILE_SAVE");
			if (!path || !*path)
				throw std::runtime_error("PROFILE_SAVE must name a saved game");
			BinaryInputStream stream(
				glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), path));
			if (!gui.load(&stream, true))
				throw std::runtime_error("Cannot load benchmark save");
			gui.game.softwareTerrainCache = std::make_unique<SoftwareTerrainCache>();
			if (const char *c = getenv("PROFILE_TERRAIN_CACHE"))
				gui.game.softwareTerrainCache->enabled = atoi(c) != 0;
			gui.localPlayer = gui.localTeamNo = 0;
			gui.adjustLocalTeam();
			gui.adjustInitialViewport();
			gui.gamePaused = false;
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
			std::vector<double> draw, present;
			RenderOperations initialOps{};
			std::uint64_t initialHits = 0, initialRebuilds = 0;
			double c0 = 0;
			double freq = SDL_GetPerformanceFrequency();
			const auto checksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
			for (int i = -warmup; i < frames; i++)
			{
				SDL_PumpEvents();
				if (i == 0)
				{
					c0 = cpu();
					initialOps = gfx->backendOperations();
					initialHits = gui.game.softwareTerrainCache->cacheHits();
					initialRebuilds = gui.game.softwareTerrainCache->cacheRebuilds();
					PerformanceTelemetry::collector().reset();
				}
				Uint64 a = SDL_GetPerformanceCounter();
				// Isolate the retention-copy cost without a production graphics setting.
				if (getenv("PROFILE_PRESERVE_FRAME"))
					gfx->beginFrame(GraphicContext::FrameMode::PreserveContent);
				if (std::string(mode) == "gui")
					gui.drawAll(0);
				else
				{
					gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
					gfx->setClipRect();
					gui.game.drawMap(0, 0, gfx->getW() - 160, gfx->getH(), 0, 0, gui.viewportX,
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
			if (gui.game.checkSum(nullptr, nullptr, nullptr, true) != checksum)
				throw std::runtime_error("rendering changed simulation checksum");
			printf("simulation_checksum=%u\n", checksum);
			auto output = [&](const char *label, std::vector<double> v)
			{
				double sum = 0;
				for (double x : v)
					sum += x;
				std::sort(v.begin(), v.end());
				printf("%s mean_ms=%.4f median_ms=%.4f p95_ms=%.4f\n", label, sum / v.size(),
					   v[v.size() / 2], v[(v.size() - 1) * 95 / 100]);
			};
			output("draw", draw);
			output("present", present);
			printf("process_cpu_ms_per_frame=%.4f\n", totalCpu * 1000 / frames);
			fflush(stdout);
			auto ops = gfx->backendOperations();
			printf("backend_ops blits=%llu fills=%llu triangles=%llu\n",
				   (unsigned long long)(ops.blits - initialOps.blits),
				   (unsigned long long)(ops.fills - initialOps.fills),
				   (unsigned long long)(ops.triangles - initialOps.triangles));
			printf("terrain_cache bytes=%zu hits=%llu rebuilds=%llu\n",
				   gui.game.softwareTerrainCache->bytes(),
				   (unsigned long long)(gui.game.softwareTerrainCache->cacheHits() - initialHits),
				   (unsigned long long)(gui.game.softwareTerrainCache->cacheRebuilds() -
										initialRebuilds));
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
