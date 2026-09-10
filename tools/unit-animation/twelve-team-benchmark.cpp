// SPDX-License-Identifier: GPL-3.0-or-later
// Non-CI 12-team memory/perf soak test for the shader/bounded-cache redesign.
//
// This is a synthetic render-loop benchmark, not a full AI-driven match: it
// does not generate a river/crater-lakes map or run Cortex. What it does
// reproduce exactly is the mechanism the original composite cache regressed
// on -- many units, many team colors, continuously varying poses, drawn every
// tick through the real GraphicContext::drawSprite / drawUnitMotionBlur path
// -- for as many ticks as asked, with full-viewport redraw every tick
// ("full-map revealed drawing"). Positions evolve from a fixed seed via a
// tiny deterministic synthetic simulation that rendering never touches, so
// the checksum comparison between blur variants is a structural property of
// this harness (render and simulation are separate steps) rather than an
// emergent one -- it demonstrates the same decoupling the real engine relies
// on, not a substitute for it.
//
// Usage: twelve-team-benchmark <blur:on|off> [ticks=5000] [label=river]
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SDL.h>
#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "render/UnitAnimation.h"

using namespace GAGCore;

namespace
{
	size_t residentBytes()
	{
#ifdef __APPLE__
		mach_task_basic_info info{};
		mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
		if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
			return info.resident_size;
#endif
		return 0;
	}

	struct SyntheticUnit
	{
		int team;
		// A tiny deterministic walk: this is the "simulation," entirely
		// independent of how (or whether) the frame gets rendered.
		uint32_t rngState;
		int x, y, dir, delta, speed;
	};

	uint32_t xorshift(uint32_t &s)
	{
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return s;
	}

	void stepSimulation(std::vector<SyntheticUnit> &units)
	{
		for (auto &u : units)
		{
			u.delta += u.speed;
			if (u.delta >= 256)
			{
				u.delta &= 255;
				u.x = (u.x + 129) % 3968; // 124 tiles * 32px, wraps like a toroidal map
				u.y = (u.y + 97) % 3968;
				if ((xorshift(u.rngState) & 7) == 0)
					u.dir = xorshift(u.rngState) & 7;
			}
		}
	}

	uint64_t checksum(const std::vector<SyntheticUnit> &units)
	{
		uint64_t h = 1469598103934665603ull;
		for (auto &u : units)
			for (int v : {u.x, u.y, u.dir, u.delta})
			{
				h ^= static_cast<uint32_t>(v);
				h *= 1099511628211ull;
			}
		return h;
	}
}

int main(int argc, char **argv)
{
	const bool blur = argc > 1 && std::string(argv[1]) == "on";
	const int ticks = argc > 2 ? std::atoi(argv[2]) : 5000;
	const std::string label = argc > 3 ? argv[3] : "river";
	const bool hd = argc > 4 && std::string(argv[4]) == "hd";
	const int numTeams = 12, unitsPerTeam = 13; // 156, matching the ~157 baseline

	Toolkit::init("codex-glob2-12team-benchmark");
	auto *gfx = Toolkit::initGraphic(1280, 800, GraphicContext::USEGPU, "12-team unit benchmark");
	Sprite::setHighResolution(hd);
	std::cout << "map=" << label << " blur=" << (blur ? "on" : "off") << " ticks=" << ticks << " hd=" << hd
	          << " shader=" << (gfx->hasUnitShader() ? "yes" : "no") << std::endl;

	auto *sprite = Toolkit::getSprite("data/gfx/unit");
	std::vector<Color> teamColors(numTeams);
	for (int i = 0; i < numTeams; ++i)
		teamColors[i].setHSV(i * 30.0f, 0.8f, 0.9f);

	std::vector<SyntheticUnit> units;
	uint32_t seed = label == "river" ? 0x52697645u /* "RivE" */ : 0x43726174u /* "Crat" */;
	for (int t = 0; t < numTeams; ++t)
		for (int i = 0; i < unitsPerTeam; ++i)
		{
			uint32_t s = seed + t * 1000003u + i * 7919u;
			units.push_back({t, s, static_cast<int>(xorshift(s) % 3968), static_cast<int>(xorshift(s) % 3968),
			                  static_cast<int>(xorshift(s) & 7), static_cast<int>(xorshift(s) & 255),
			                  8 + static_cast<int>(xorshift(s) % 23)});
		}

	std::vector<double> windowFrameMs;
	windowFrameMs.reserve(250);
	const auto benchStart = std::chrono::steady_clock::now();
	for (int tick = 0; tick < ticks; ++tick)
	{
		const auto simStart = std::chrono::steady_clock::now();
		stepSimulation(units);
		const double simMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - simStart).count();

		const auto frameStart = std::chrono::steady_clock::now();
		gfx->drawFilledRect(0, 0, 1280, 800, 24, 31, 40); // full-viewport redraw every tick
		for (auto &u : units)
		{
			sprite->setBaseColor(teamColors[u.team]);
			const int px = (u.x >> 5) % 40 * 32, py = (u.y >> 5) % 25 * 32;
			if (blur)
			{
				const int span = std::max(1, u.speed);
				drawUnitMotionBlur(64, u.dir, u.delta, span, [&](int f, int a)
				{ gfx->drawSprite(px, py, sprite, f, static_cast<Uint8>(a)); });
			}
			else
				gfx->drawSprite(px, py, sprite, unitAnimationFrame(64, u.dir, u.delta));
		}
		glFinish();
		const double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
		windowFrameMs.push_back(frameMs);
		SDL_PumpEvents();

		if ((tick + 1) % 250 == 0 || tick + 1 == ticks)
		{
			auto sorted = windowFrameMs;
			std::sort(sorted.begin(), sorted.end());
			const auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * sorted.size()))]; };
			std::cout << "tick=" << (tick + 1) << " units=" << units.size()
			          << " team_color_cache_entries=" << sprite->getTeamColorCacheEntries()
			          << " team_color_cache_bytes=" << sprite->getTeamColorCacheBytes()
			          << " gpu_allocated_bytes=" << DrawableSurface::allocatedTextureBytes()
			          << " rss_bytes=" << residentBytes()
			          << " frame_p50_ms=" << pct(0.50) << " frame_p95_ms=" << pct(0.95) << " frame_max_ms=" << sorted.back()
			          << " sim_ms=" << simMs
			          << " checksum=" << std::hex << checksum(units) << std::dec
			          << std::endl;
			windowFrameMs.clear();
		}
	}
	const double totalS = std::chrono::duration<double>(std::chrono::steady_clock::now() - benchStart).count();
	std::cout << "done: " << ticks << " ticks in " << totalS << " s, final team_color_cache_bytes="
	          << sprite->getTeamColorCacheBytes() << ", final rss_bytes=" << residentBytes() << std::endl;
	Toolkit::close();
}
