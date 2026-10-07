// SPDX-License-Identifier: GPL-3.0-or-later
// Paired renderer diagnostic. No simulation runs; use game_preview for Scene costs.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SkinMesh.h>
#include <SkinModel.h>
#include <PerformanceTelemetry.h>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace GAGCore;
using Json = nlohmann::json;
static double milliseconds()
{
	return std::chrono::duration<double, std::milli>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
static Json distribution(const std::vector<double> &values)
{
	auto sorted = values;
	std::sort(sorted.begin(), sorted.end());
	double sum = 0;
	for (auto value : values)
		sum += value;
	auto percentile = [&](double p)
	{ return sorted[std::min(sorted.size() - 1, std::size_t(p * (sorted.size() - 1)))]; };
	return {{"mean", sum / values.size()},
			{"p50", percentile(.50)},
			{"p95", percentile(.95)},
			{"p99", percentile(.99)},
			{"max", sorted.back()}};
}
int main(int argc, char **argv)
{
#ifndef HAVE_OPENGL
	std::cerr << "OpenGL build required\n";
	return 2;
#else
	if (argc != 9)
	{
		std::cerr << "skin-rig-benchmark ASSETS OUTPUT.json baked|rig warm|miss UNITS FRAMES "
					 "WARMUP PHASES\n";
		return 2;
	}
	try
	{
		const double processStart = milliseconds();
		const std::string assets = argv[1], output = argv[2], mode = argv[3], cache = argv[4];
		const unsigned units = std::stoul(argv[5]), frames = std::stoul(argv[6]),
					   warmup = std::stoul(argv[7]), phases = std::stoul(argv[8]);
		if ((mode != "baked" && mode != "rig") || (cache != "warm" && cache != "miss") ||
			units < 1 || units > 4096 || frames < 20 || frames > 10000 || warmup < 1 ||
			warmup > 1000 || phases < 1 || phases > 256)
			throw std::runtime_error("Invalid benchmark parameters");
		Toolkit::init("glob2-skin-rig-benchmark");
		struct Close
		{
			~Close() { Toolkit::close(); }
		} close;
		auto *gfx =
			Toolkit::initGraphic(1280, 960, GraphicContext::USEGPU | GraphicContext::NOAUDIO,
								 "Rig performance measurement");
		gfx->setTargetRenderFps(0);
		if (!SDL_GL_SetSwapInterval(0))
			throw std::runtime_error(SDL_GetError());
		int swapInterval = -99;
		if (!SDL_GL_GetSwapInterval(&swapInterval) || swapInterval != 0)
			throw std::runtime_error("Cannot disable swap interval");
		Json result = {{"format", "glob2-rig-benchmark-v1"},
					   {"mode", mode},
					   {"cache", cache},
					   {"units", units},
					   {"phasesPerPaint", phases},
					   {"paints", 4},
					   {"frames", frames},
					   {"warmup", warmup},
					   {"width", gfx->getW()},
					   {"height", gfx->getH()},
					   {"swapInterval", swapInterval},
					   {"videoDriver", SDL_GetCurrentVideoDriver()},
					   {"glVendor", reinterpret_cast<const char *>(glGetString(GL_VENDOR))},
					   {"glRenderer", reinterpret_cast<const char *>(glGetString(GL_RENDERER))},
					   {"glVersion", reinterpret_cast<const char *>(glGetString(GL_VERSION))}};
		// Settle desktop mapping outside all timed samples.
		const double settleStart = milliseconds();
		for (unsigned i = 0; i < 25; ++i)
		{
			SDL_PumpEvents();
			gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
			gfx->drawFilledRect(0, 0, 1280, 960, Color(45, 50, 60));
			gfx->nextFrame();
			SDL_Delay(20);
		}
		const double settleMs = milliseconds() - settleStart;
		result["windowSettleMs"] = settleMs;
		SkinMesh mesh;
		std::string error;
		const double loadStart = milliseconds();
		if (!mesh.load(assets + "/worker-walk." + (mode == "rig" ? "gsr" : "gsk"), error))
			throw std::runtime_error(error);
		result["meshLoadMs"] = milliseconds() - loadStart;
		if (bool(mesh.model) != (mode == "rig"))
			throw std::runtime_error("Unexpected geometry backend");
		result["meshBytes"] = mesh.poses.size() * sizeof(float) + mesh.uv.size() * sizeof(float) +
							  mesh.indices.size() * sizeof(std::uint32_t);
		if (mesh.model)
		{
			const auto &m = *mesh.model;
			std::size_t size = m.rest().size() * sizeof(float) + m.uv().size() * sizeof(float) +
							   m.indices().size() * sizeof(std::uint32_t) +
							   m.influences().size() * sizeof(SkinInfluence) +
							   m.bones().size() * sizeof(SkinBone);
			for (const auto &clip : m.clips())
				size += sizeof(SkinClip) + clip.tracks.size() * sizeof(SkinTransform);
			result["meshBytes"] = result["meshBytes"].get<std::size_t>() + size;
		}
		std::array<std::unique_ptr<DrawableSurface>, 4> paints;
		for (unsigned i = 0; i < paints.size(); ++i)
		{
			paints[i] = std::make_unique<DrawableSurface>(assets + "/paint.webp");
			paints[i]->drawFilledRect(0, 0, 128, 256,
									  Color(50 + i * 50, 180 - i * 30, 70 + i * 35));
		}
		auto material = loadSkinMaterialMap(assets + "/material.webp");
		if (!material || material->getW() != 512 || material->getH() != 512)
			throw std::runtime_error("Missing material fixture");
		for (const auto &paint : paints)
			if (paint->getW() != 512 || paint->getH() != 512)
				throw std::runtime_error("Missing paint fixture");
		// First draw includes lazy shader creation, atlas allocation and first geometry upload.
		glFinish();
		const double coldStart = milliseconds();
		gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
		gfx->drawFilledRect(0, 0, 1280, 960, Color(45, 50, 60));
		if (!gfx->drawSkinMesh(mesh, 0, *paints[0], *material, SkinRegionWorker, 0, 0, 38, 38))
			throw std::runtime_error("Cold draw failed");
		gfx->nextFrame();
		glFinish();
		result["coldFirstDrawMs"] = milliseconds() - coldStart;
		result["startupExcludingSettleMs"] = milliseconds() - processStart - settleMs;
		const bool gpuRig = bool(gfx->skinResources.rigProgram);
		result["gpuRig"] = gpuRig;
		const char *deformation = std::getenv("GLOB2_SKIN_DEFORMATION");
		if (mode == "rig" && !(deformation && std::string(deformation) == "cpu") && !gpuRig)
			throw std::runtime_error("Rig shader unavailable; refusing mislabeled GPU measurement");
		// Fully populate the 4 x 256 pose/paint working set before warm measurements.
		std::vector<SkinMeshRequest> all;
		for (unsigned sample = 0; sample < 256; ++sample)
			for (const auto &paint : paints)
				all.push_back({&mesh, sample, paint.get(), material.get(), SkinRegionWorker});
		glFinish();
		const double fillStart = milliseconds();
		gfx->prepareSkinMeshes(all);
		glFinish();
		result["fillAtlasMs"] = milliseconds() - fillStart;
		const bool timers = SDL_GL_ExtensionSupported("GL_ARB_timer_query");
		result["gpuTimers"] = timers;
		std::vector<GLuint> queries(timers ? frames : 0);
		if (timers)
			glGenQueries(queries.size(), queries.data());
		std::vector<double> wall, gpu, prepare, composite, present;
		std::vector<unsigned long> rasterDraws;
		Json scopeSamples = Json::array();
		std::vector<SkinMeshRequest> requests(units);
		const unsigned columns = units > 1024 ? 64 : 32;
		const float cellWidth = 1280.f / columns;
		const float cellHeight = 960.f / ((units + columns - 1) / columns);
		for (unsigned frame = 0; frame < warmup + frames; ++frame)
		{
			SDL_PumpEvents();
			for (unsigned i = 0; i < units; ++i)
				requests[i] = {&mesh, ((i / 4) % phases + frame) % 256, paints[i % 4].get(),
							   material.get(), SkinRegionWorker};
			// Invalidate only pose-atlas lookup entries. Retain rest buffers, palettes,
			// textures and framebuffer allocations; no repaint/upload is introduced.
			if (cache == "miss")
				gfx->skinResources.slots = {};
			auto &profile = PerformanceTelemetry::collector();
			profile.reset();
			gfx->resetDrawCallCount();
			const bool measured = frame >= warmup;
			const double start = milliseconds();
			if (timers && measured)
				glBeginQuery(GL_TIME_ELAPSED, queries[frame - warmup]);
			gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
			gfx->drawFilledRect(0, 0, 1280, 960, Color(45, 50, 60));
			gfx->resetDrawCallCount();
			const double p0 = milliseconds();
			gfx->prepareSkinMeshes(requests);
			const double p1 = milliseconds();
			const auto rasters = gfx->getDrawCallCount();
			const unsigned expected = cache == "warm" ? 0 : std::min(units, phases * 4);
			if (rasters != expected)
				throw std::runtime_error("Unexpected atlas miss count: " + std::to_string(rasters));
			for (unsigned i = 0; i < units; ++i)
			{
				const auto &r = requests[i];
				if (!gfx->drawSkinMesh(mesh, r.frame, *r.texture, *r.material, r.region,
									   (i % columns) * cellWidth, (i / columns) * cellHeight,
									   std::min(38.f, cellWidth), std::min(38.f, cellHeight)))
					throw std::runtime_error("Composite failed");
			}
			Sprite::flushBatches(gfx);
			const double p2 = milliseconds();
			if (timers && measured)
				glEndQuery(GL_TIME_ELAPSED);
			gfx->nextFrame();
			const double end = milliseconds();
			if (measured)
			{
				wall.push_back(end - start);
				prepare.push_back(p1 - p0);
				composite.push_back(p2 - p1);
				present.push_back(end - p2);
				rasterDraws.push_back(rasters);
				Json scopes = Json::object();
				for (auto [id, name] :
					 {std::pair{PerformanceTelemetry::Id::SkinGeometry, "geometry"},
					  {PerformanceTelemetry::Id::SkinRaster, "raster"}})
				{
					auto metric = profile.total[static_cast<unsigned>(id)];
					metric.merge(profile.window[static_cast<unsigned>(id)]);
					scopes[name] = {{"calls", metric.calls}, {"ms", metric.time.total / 1e6}};
				}
				scopeSamples.push_back(scopes);
			}
		}
		glFinish();
		if (timers)
		{
			for (auto query : queries)
			{
				GLuint64 ns = 0;
				glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
				gpu.push_back(ns / 1e6);
			}
			glDeleteQueries(queries.size(), queries.data());
		}
		result["wallMs"] = distribution(wall);
		result["prepareMs"] = distribution(prepare);
		result["compositeMs"] = distribution(composite);
		result["presentMs"] = distribution(present);
		if (timers)
			result["gpuMs"] = distribution(gpu);
		result["samples"] = {{"wallMs", wall},        {"gpuMs", gpu},
							 {"prepareMs", prepare},  {"compositeMs", composite},
							 {"presentMs", present},  {"rasterDraws", rasterDraws},
							 {"scopes", scopeSamples}};
		result["atlasPages"] = gfx->skinResources.colors.size();
		result["restModels"] = gfx->skinResources.rigs.size();
		result["cpuPoseFloats"] = gfx->skinResources.cpuPose.size();
		if (glGetError() != GL_NO_ERROR)
			throw std::runtime_error("GL error during benchmark");
		std::ofstream file(output);
		file << result.dump(2) << '\n';
		if (!file)
			throw std::runtime_error("Cannot write results");
		std::cout << mode << ' ' << cache << ' ' << units << " wall=" << result["wallMs"].dump()
				  << " gpu=" << result.value("gpuMs", Json()).dump() << '\n';
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
#endif
}
