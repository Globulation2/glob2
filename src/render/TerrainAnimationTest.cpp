// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoothness guards for animated terrain (docs/assets/terrain-rendering.md,
// "Animation clock and smoothness"). Water, surf and lava must move by exactly
// the time elapsed on every presented frame, at every frame rate, on every
// renderer, and in step across the view. These cases fail if any link of the
// chain (AnimationClock, the presentation time a view hands its terrain, the
// direct pass's uniform and shader, the portable renderer's keyframe
// crossfade) rounds time back to whole animation steps.
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "DrawFixtures.h"
#include "Map.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "TerrainBuiltins.h"
#include "AnimationClock.h"
#include "MapRenderState.h"
#include "ViewState.h"
#include "SoftwareTerrainCache.h"
#include "terrain/DirectTerrain.h"
#include "terrain/DirectTerrainData.h"
#include "terrain/TerrainArtBorder.h"
#include "terrain/TerrainCompositor.h"
#include "terrain/TerrainCatalogIO.h"
#include "terrain/TerrainMaterials.h"
#include <DirectTerrainShader.h>
#include <GraphicContext.h>
#include <RenderBackend.h>
#include <RendererSelection.h>
#include <Toolkit.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using GAGCore::RendererBackend;

// Display intervals the guards play: 30 Hz, 60 Hz with ±2 ms of jitter,
// 120 Hz and 144 Hz.
struct Rate
{
	const char *name;
	double milliseconds, jitter;
};
constexpr Rate Rates[] = {{"30 Hz", 1000.0 / 30, 0}, {"60 Hz +-2 ms", 1000.0 / 60, 2},
						  {"120 Hz", 1000.0 / 120, 0}, {"144 Hz", 1000.0 / 144, 0}};
std::uint64_t intervalNs(const Rate &rate, int frame)
{
	static constexpr double pattern[] = {1, -1, 0.5, -0.25, 1, -0.75, -1, 0.25};
	return std::uint64_t(std::llround((rate.milliseconds + rate.jitter * pattern[frame % 8]) * 1e6));
}
double stepsOf(std::uint64_t ns)
{
	return double(ns) * AnimationClock::REFERENCE_RATE / double(AnimationClock::NS_PER_SECOND);
}

struct ClockOverride
{
	int fixed = AnimationClock::fixedSteps;
	std::uint64_t simulated = AnimationClock::simulatedFrameNs;
	ClockOverride() { AnimationClock::fixedSteps = 0; }
	~ClockOverride()
	{
		AnimationClock::fixedSteps = fixed;
		AnimationClock::simulatedFrameNs = simulated;
	}
};

// The body of the GLSL function whose definition starts with `signature`.
std::string shaderFunction(std::string_view signature)
{
	const std::string source = GAGCore::DirectTerrainFragment;
	const auto start = source.find(signature);
	REQUIRE(start != std::string::npos);
	auto open = source.find('{', start);
	int depth = 0;
	for (auto i = open; i < source.size(); ++i)
	{
		depth += source[i] == '{';
		depth -= source[i] == '}';
		if (!depth)
			return source.substr(start, i + 1 - start);
	}
	FAIL("unterminated shader function");
	return {};
}
std::string statement(const std::string &body, std::string_view begins)
{
	const auto start = body.find(begins);
	REQUIRE(start != std::string::npos);
	return body.substr(start, body.find(';', start) - start);
}

// ---------------------------------------------------------------------------
// Display guards: the whole draw path, frame after frame.

constexpr int Width = 640, Height = 480;
// Vertices of the sand island in a 64x64 sea; cells are 32 pixels at zoom 1.
constexpr int IslandLeft = 5, IslandTop = 4, IslandRight = 8, IslandBottom = 8;
// Whether the cell whose top-left corner is vertex (cx, cy) has an island
// vertex within `margin` cells of its corners.
bool nearIsland(int cx, int cy, int margin)
{
	return cx + 1 + margin >= IslandLeft && cx - margin <= IslandRight && cy + 1 + margin >= IslandTop &&
		   cy - margin <= IslandBottom;
}

bool wanted(RendererBackend backend)
{
	// GLOB2_ANIMATION_TEST_RENDERERS limits the renderers (comma separated
	// names), for machines where some must not run.
	const char *list = std::getenv("GLOB2_ANIMATION_TEST_RENDERERS");
	if (!list || !*list)
		return true;
	const std::string names = std::string(",") + list + ",";
	return names.find("," + std::string(GAGCore::rendererBackendName(backend)) + ",") != std::string::npos;
}

// Whether a renderer interpolates within animation steps. The software
// renderer composes whole steps on the CPU: it stays correct and in phase.
bool continuous(RendererBackend backend) { return backend != RendererBackend::Software; }

struct Scene
{
	RendererBackend backend;
	std::optional<glob2test::HeadlessGlobals> globals;
	std::optional<glob2test::HeadlessGame> fixture;
	ViewState view;
	GAGCore::GraphicContext *gfx = nullptr;
	std::vector<std::size_t> open, shore; // RGBA offsets of the measured pixels
	struct Pair { int ax, ay, bx, by; };

	explicit Scene(RendererBackend renderer) : backend(renderer)
	{
		globals.emplace(glob2test::GlobalsOptions{.display = true, .width = Width, .height = Height,
			.screenFlags = renderer == RendererBackend::LegacyGL ? Uint32(GAGCore::GraphicContext::USEGPU) : 0u,
			.beforeLoad = [renderer](GlobalContainer &g) {
				g.settings.rendererBackend = renderer;
				g.settings.clouds = false;
				g.settings.cloudShadows = false;
			}});
		gfx = (*globals)->gfx;
		fixture.emplace(glob2test::GameOptions{.wDec = 6, .hDec = 6, .terrain = WATER, .discovered = true});
		for (int y = IslandTop; y <= IslandBottom; ++y)
			for (int x = IslandLeft; x <= IslandRight; ++x)
				fixture->game.map.setVertexTerrain(x, y, SAND);
		// Shore cells have both sand and water corners; open water lies two
		// cells clear of the island.
		for (int cy = 0; cy < Height / 32; ++cy)
			for (int cx = 0; cx < Width / 32; ++cx)
			{
				const bool allSand = cx >= IslandLeft && cx + 1 <= IslandRight && cy >= IslandTop && cy + 1 <= IslandBottom;
				auto *into = nearIsland(cx, cy, 0) && !allSand ? &shore : !nearIsland(cx, cy, 2) ? &open : nullptr;
				if (!into)
					continue;
				for (int y = cy * 32 + 2; y < cy * 32 + 30; ++y)
					for (int x = cx * 32 + 2; x < cx * 32 + 30; ++x)
						into->push_back((std::size_t(y) * Width + x) * 4);
			}
	}
	bool available() const { return gfx->backendSwitchStatus().active == backend; }

	// One frame of the full map draw, captured as Width x Height RGBA.
	float zoom = 1.f;
	std::vector<Uint8> frame(bool paused)
	{
		gfx->setClipRect();
		gfx->drawFilledRect(0, 0, Width, Height, 11, 23, 37);
		gfx->beginMapTransform(zoom, 0, 0, 0, 0, Width, Height);
		glob2test::drawMap(fixture->game, 0, 0, int(Width / zoom), int(Height / zoom), 0, 0, 0, 0, 0, view,
						   Uint32(Game::DRAW_NO_CLOUD_LAYER | Game::DRAW_WHOLE_MAP), nullptr, nullptr, paused);
		gfx->endMapTransform();
		return capture();
	}
	std::vector<Uint8> capture()
	{
		std::vector<Uint8> rows(std::size_t(Width) * Height * 4);
		const auto sample = [&](const Uint8 *pixels, int width, int height, std::size_t pitch, bool bottomUp)
		{
			REQUIRE(width * Height == height * Width);
			const int scale = width / Width;
			for (int y = 0; y < Height; ++y)
			{
				const int source = bottomUp ? height - 1 - y * scale : y * scale;
				for (int x = 0; x < Width; ++x)
					std::memcpy(rows.data() + (std::size_t(y) * Width + x) * 4,
								pixels + source * pitch + std::size_t(x) * scale * 4, 4);
			}
		};
		GAGCore::Sprite::flushBatches(gfx);
#ifdef HAVE_OPENGL
		if (gfx->isLegacyGLScreen())
		{
			glFinish();
			GLint viewport[4];
			glGetIntegerv(GL_VIEWPORT, viewport);
			std::vector<Uint8> pixels(std::size_t(viewport[2]) * viewport[3] * 4);
			glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			sample(pixels.data(), viewport[2], viewport[3], std::size_t(viewport[2]) * 4, true);
			gfx->nextFrame();
			return rows;
		}
#endif
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured(nullptr, SDL_DestroySurface);
		if (!gfx->rendererCapabilities().accelerated && gfx->getSDLSurface())
			captured.reset(SDL_ConvertSurface(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32));
		else
		{
			gfx->renderer->flush();
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> raw(gfx->renderer->capture(), SDL_DestroySurface);
			REQUIRE(raw);
			captured.reset(SDL_ConvertSurface(raw.get(), SDL_PIXELFORMAT_RGBA32));
		}
		REQUIRE(captured);
		sample(static_cast<const Uint8 *>(captured->pixels), captured->w, captured->h, std::size_t(captured->pitch), false);
		gfx->nextFrame();
		return rows;
	}
	// Terrain has nothing left to compose or upload for this view.
	bool terrainSettled() const
	{
		bool settled = true;
		if (const auto *pages = view.render.existingTerrainCache())
			settled = settled && pages->complete() && !pages->pendingPages() && !pages->missingKeyframeCells();
		if (const auto *direct = view.render.existingDirectTerrain())
			settled = settled && !direct->preparing() && !direct->pendingHdMasks() && !direct->pendingBaseMasks();
		return settled;
	}
	// The image at the current presentation time once every page, mask and
	// keyframe it needs has arrived: background work only decides when
	// terrain is ready, never what a time looks like.
	std::vector<Uint8> settled()
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::minutes(2);
		auto last = frame(true);
		for (int stable = 0;;)
		{
			const bool ready = terrainSettled();
			auto next = frame(true);
			stable = ready && next == last ? stable + 1 : 0;
			auto previousFrame = std::move(last);
			last = std::move(next);
			if (stable >= 2)
				return last;
			if (std::chrono::steady_clock::now() >= end)
			{
				const auto *pages = view.render.existingTerrainCache();
				const auto *direct = view.render.existingDirectTerrain();
				std::size_t differing = 0;
				for (std::size_t i = 0; i < last.size(); ++i) differing += last[i] != previousFrame[i];
				FAIL("terrain never settled: pages " << (pages ? 1 : 0) << " complete " << (pages && pages->complete())
					 << " pending " << (pages ? pages->pendingPages() : 0) << " missing keyframes "
					 << (pages ? pages->missingKeyframeCells() : 0) << " direct " << (direct ? 1 : 0)
					 << " preparing " << (direct && direct->preparing()) << " masks "
					 << (direct ? direct->pendingHdMasks() : 0) << "/" << (direct ? direct->pendingBaseMasks() : 0)
					 << " differing bytes " << differing);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
		}
	}
	// Sets the presentation time to `steps` plus `fraction` and settles.
	std::vector<Uint8> at(int steps, float fraction)
	{
		ClockOverride clock;
		AnimationClock::simulatedFrameNs = 0;
		view.render.pinAnimation(steps);
		view.render.animationClock.presetNext(0, fraction);
		frame(false);
		CHECK(view.render.animationNow().value() == doctest::Approx(steps + double(fraction)).epsilon(1e-6));
		return settled();
	}
	// Advances one frame of `ns` and settles.
	std::vector<Uint8> advance(std::uint64_t ns)
	{
		ClockOverride clock;
		AnimationClock::simulatedFrameNs = ns;
		frame(false);
		return settled();
	}
	// Draws one frame `ns` after the last, then waits that long, as a display
	// would: background work lands when it lands.
	std::vector<Uint8> live(std::uint64_t ns)
	{
		ClockOverride clock;
		AnimationClock::simulatedFrameNs = ns;
		auto image = frame(false);
		std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
		return image;
	}
};

// Mean absolute channel difference over `pixels`, in 8-bit levels.
double change(const std::vector<Uint8> &a, const std::vector<Uint8> &b, const std::vector<std::size_t> &pixels)
{
	double total = 0;
	for (auto p : pixels)
		for (int k = 0; k < 3; ++k)
			total += std::abs(int(a[p + k]) - int(b[p + k]));
	return pixels.empty() ? 0 : total / (3.0 * double(pixels.size()));
}
// Share of `pixels` with a channel jumping more than `levels`.
double flashes(const std::vector<Uint8> &a, const std::vector<Uint8> &b, const std::vector<std::size_t> &pixels, int levels)
{
	std::size_t count = 0;
	for (auto p : pixels)
	{
		int largest = 0;
		for (int k = 0; k < 3; ++k)
			largest = std::max(largest, std::abs(int(a[p + k]) - int(b[p + k])));
		count += largest > levels;
	}
	return pixels.empty() ? 0 : double(count) / double(pixels.size());
}
// Largest channel difference between the 32-pixel cells (ax, ay) and (bx, by).
int cellDifference(const std::vector<Uint8> &image, int ax, int ay, int bx, int by)
{
	int largest = 0;
	for (int y = 2; y < 30; ++y)
		for (int x = 2; x < 30; ++x)
		{
			const auto a = (std::size_t(ay * 32 + y) * Width + ax * 32 + x) * 4;
			const auto b = (std::size_t(by * 32 + y) * Width + bx * 32 + x) * 4;
			for (int k = 0; k < 3; ++k)
				largest = std::max(largest, std::abs(int(image[a + k]) - int(image[b + k])));
		}
	return largest;
}
double median(std::vector<double> values)
{
	std::sort(values.begin(), values.end());
	return values.empty() ? 0 : values[values.size() / 2];
}

// Open-water cells two or more cells clear of the island, in different 16-cell
// pages (the portable renderer's pages, and other tiles of the direct pass's
// view), that show the same pixels at the first time: they must show the same
// pixels at every later time.
std::vector<Scene::Pair> lockstepPairs(const std::vector<Uint8> &image)
{
	std::vector<Scene::Pair> pairs;
	const auto openCell = [](int x, int y) { return !nearIsland(x, y, 2); };
	for (int by = 0; by < Height / 32 && pairs.size() < 6; ++by)
		for (int bx = 16; bx < Width / 32 && pairs.size() < 6; ++bx)
			for (int ay = 0; ay < Height / 32; ++ay)
				for (int ax = 0; ax < 12; ++ax)
					if (openCell(ax, ay) && openCell(bx, by) && cellDifference(image, ax, ay, bx, by) <= 2 &&
						std::none_of(pairs.begin(), pairs.end(), [&](const auto &p) { return p.bx == bx && p.by == by; }))
						pairs.push_back({ax, ay, bx, by});
	return pairs;
}

// The crossfade weight an image shows between the flipbook endpoints A and B,
// over `pixels`: the least-squares w of I = A + w (B - A). Thousands of pixels
// average out 8-bit rounding, which dominates the change of one frame at high
// frame rates, so this reads the phase each frame actually shows.
double shownWeight(const std::vector<Uint8> &image, const std::vector<Uint8> &a, const std::vector<Uint8> &b,
				   const std::vector<std::size_t> &pixels)
{
	double along = 0, length = 0;
	for (auto p : pixels)
		for (int k = 0; k < 3; ++k)
		{
			const double d = double(b[p + k]) - double(a[p + k]);
			// Channels that change by a few levels show more of the 8-bit
			// rounding (which renderers do differently) than of the weight.
			if (std::abs(d) < 12)
				continue;
			along += (double(image[p + k]) - double(a[p + k])) * d;
			length += d * d;
		}
	return length > 0 ? along / length : 0;
}

// How the frames of a sequence are drawn: each settled at its time (the
// rendering of a time), or back to back at the display's pace while terrain
// work runs in the background, as in play.
enum class Pace { Settled, Live };

void checkSmoothWater(RendererBackend backend, Pace pace)
{
	const auto name = std::string(GAGCore::rendererBackendName(backend));
	if (!wanted(backend))
	{
		MESSAGE(name << " excluded by GLOB2_ANIMATION_TEST_RENDERERS");
		return;
	}
	Scene scene(backend);
	if (!scene.available())
	{
		MESSAGE(name << " unavailable here; omitted");
		return;
	}
	REQUIRE(scene.open.size() > 10000);
	REQUIRE(scene.shore.size() > 5000);
	const auto &catalog = globalContainer->terrainCompositor(scene.fixture->game.map.frozenAssetBundle()).catalog();
	const int ticks = catalog.materials[catalog.find("water")].animationTicks;
	REQUIRE(ticks > 1);
	const bool smooth = continuous(backend);
	constexpr int Frames = 24;
	constexpr int Start = 81590; // just before the shipped catalogue's wrap period
	for (const auto &rate : Rates)
	{
		CAPTURE(name);
		CAPTURE(rate.name);
		// The flipbook's endpoints over the sequence: each whole frame of it.
		double end = Start + 0.3;
		for (int i = 0; i < Frames; ++i)
			end += stepsOf(intervalNs(rate, i));
		std::map<std::int64_t, std::vector<Uint8>> keys;
		for (auto key = std::int64_t(Start / ticks); key <= std::int64_t(end / ticks) + 1; ++key)
			keys[key] = scene.at(int(key * ticks), 0.f);
		auto previous = scene.at(Start, 0.3f);
		const auto pairs = lockstepPairs(previous);
		CAPTURE(pairs.size());
		REQUIRE(pairs.size() >= 2);
		std::vector<double> shoreRates;
		double before = scene.view.render.animationNow().value();
		for (int i = 0; i < Frames; ++i)
		{
			CAPTURE(i);
			const auto ns = intervalNs(rate, i);
			auto image = pace == Pace::Settled ? scene.advance(ns) : scene.live(ns);
			const double now = scene.view.render.animationNow().value();
			// The view's presentation time moved by exactly the frame.
			CHECK(now - before == doctest::Approx(stepsOf(ns)).epsilon(1e-4));
			const bool stepped = int(std::floor(now)) != int(std::floor(before));
			before = now;
			// The water shows this frame's time: the crossfade weight between
			// the flipbook frames around it is the share of the frame elapsed.
			const double shown = smooth ? now : std::floor(now);
			const auto key = std::int64_t(std::floor(shown / ticks));
			const double expected = (shown - double(key * ticks)) / ticks;
			const double weight = shownWeight(image, keys.at(key), keys.at(key + 1), scene.open);
			const double open = change(previous, image, scene.open);
			const double shore = change(previous, image, scene.shore);
			CAPTURE(now);
			CAPTURE(expected);
			CAPTURE(weight);
			CAPTURE(open);
			CAPTURE(shore);
			CHECK(std::abs(weight - expected) < 0.03);
			if (smooth || stepped)
			{
				// Every frame moves the water: no frozen frame.
				CHECK(open > 0);
				CHECK(shore > 0);
				shoreRates.push_back(shore / (smooth ? stepsOf(ns) : 1.0));
			}
			else
				CHECK(open == 0); // whole steps: nothing moves between them
			// No shoreline flash: shore pixels do not jump.
			CHECK(flashes(previous, image, scene.shore, 48) < 0.002);
			// Lockstep: cells far apart, in other pages, show one phase.
			for (const auto &p : pairs)
			{
				CAPTURE(p.ax); CAPTURE(p.ay); CAPTURE(p.bx); CAPTURE(p.by);
				CHECK(cellDifference(image, p.ax, p.ay, p.bx, p.by) <= 3);
			}
			previous = std::move(image);
		}
		// The wash moves at a steady pace: no frame of the shore moves several
		// times as far as its time, or a small part of it.
		const double typical = median(shoreRates);
		CAPTURE(typical);
		for (std::size_t i = 0; i < shoreRates.size(); ++i)
		{
			CAPTURE(i);
			CHECK(shoreRates[i] >= 0.25 * typical);
			CHECK(shoreRates[i] <= 4 * typical);
		}
	}
}
} // namespace

TEST_SUITE("TerrainAnimation")
{
	// -----------------------------------------------------------------------
	// Headless guards: the functions each renderer calls.

	TEST_CASE("the shipped catalogue's animations all repeat within the shaders' wrap period")
	{
		glob2test::HeadlessGlobals globals;
		const auto catalog = TerrainVisual::loadCatalog();
		const int period = TerrainVisual::animationPeriod(catalog);
		// 16 frames of 6 ticks (water), 4 of 16 (lava), 300 and 340-tick washes.
		CHECK(period == 81600);
		bool animated = false;
		for (const auto &material : catalog.materials)
		{
			CAPTURE(material.key);
			if (material.animationFrames > 1)
			{
				animated = true;
				CHECK(period % (material.animationFrames * material.animationTicks) == 0);
			}
			if (material.surf.period > 0)
				CHECK(period % material.surf.period == 0);
		}
		CHECK(animated);
	}

    TEST_CASE("water atlas gutters take their texels from the adjoining tile at the same phase")
    {
        glob2test::HeadlessGlobals globals;
        auto &compositor = globals->terrainCompositor();
        const auto &catalog = compositor.catalog();
        const auto water = catalog.find("water");
        const auto &material = catalog.materials[water];
        compositor.finishMaterials(std::span(&water, 1));
        const int grid = material.variantGrid;
        REQUIRE(grid > 1);

        // The top-left tile tests wraps in both directions, including the
        // diagonal. An interior tile also tests edges within the layout.
        for (const unsigned variant : {0u, unsigned(grid + 1)})
            for (const unsigned phase : {0u, unsigned(material.animationFrames - 1)})
                for (const int density : {16, 32, 64})
                {
                    const auto border = DirectTerrainData::artBorder(compositor, {water, variant}, phase, true, density);
                    REQUIRE(border.size() == std::size_t(4 * (density + 1)));
                    const auto check = [&](int x, int y, const auto &pixel)
                    {
                        const auto wrap = [grid, density](int value)
                        {
                            const int extent = grid * density;
                            return (value + extent) % extent;
                        };
                        const int wx = wrap(int(variant % grid) * density + x);
                        const int wy = wrap(int(variant / grid) * density + y);
                        const unsigned expectedVariant = (wy / density) * grid + wx / density;
                        const auto *surface = compositor.phaseSurface(water, expectedVariant, phase, true);
                        REQUIRE(surface);
                        const auto *source = static_cast<const Uint32 *>(surface->pixels);
                        const int sourceX = (wx % density) * 32 / density;
                        const int sourceY = (wy % density) * 32 / density;
                        const int box = std::max(1, 32 / density);
                        for (int k = 0; k < 3; ++k)
                        {
                            int sum = 0;
                            for (int row = 0; row < box; ++row)
                                for (int col = 0; col < box; ++col)
                                    sum += (source[(sourceY + row) * 32 + sourceX + col] >> (16 - k * 8)) & 255;
                            CHECK(int(pixel[k]) == (sum + box * box / 2) / (box * box));
                        }
                        CHECK(pixel[3] == 255);
                    };
                    std::size_t i = 0;
                    TerrainVisual::visitArtBorder(density, [&](int x, int y, int, int, int, int)
                    {
                        check(x, y, border[i++]);
                    });
                }
    }

	TEST_CASE("the direct pass's crossfade and surf follow continuous time across the wrap at every rate")
	{
		// The shader's crossfade weight and wash phase, as DirectTerrainShader.h
		// computes them from the uniform DirectTerrain hands it.
		const auto material = shaderFunction("vec4 material(");
		CHECK(statement(material, "float blend =") == "float blend = clamp(stepTime / data.z, 0.0, 1.0)");
		CHECK(material.find("floor(stepTime") == std::string::npos);
		const auto surf = shaderFunction("vec3 surf(");
		const auto phase = statement(surf, "float phase =");
		CHECK(phase == "float phase = mod(animationTime, shape.x) * 1024.0 / shape.x");
		CHECK(surf.find("mod(animationTime, shallow.w)") == std::string::npos);
		const auto glslMod = [](float x, float y) { return x - y * std::floor(x / y); };
		glob2test::HeadlessGlobals globals;
		const int period = TerrainVisual::animationPeriod(TerrainVisual::loadCatalog());
		REQUIRE(period > 0);
		ClockOverride override;
		for (const auto &rate : Rates)
			for (int start : {0, period - 30, 50 * period - 30})
			{
				CAPTURE(rate.name);
				CAPTURE(start);
				AnimationClock clock;
				int steps = start;
				AnimationTime previous{steps, 0};
				for (int frame = 0; frame < 240; ++frame)
				{
					CAPTURE(frame);
					AnimationClock::simulatedFrameNs = intervalNs(rate, frame);
					steps += clock.advanceAt(false, 1);
					const AnimationTime now{steps, clock.fraction()};
					const double elapsed = now.value() - previous.value();
					CHECK(elapsed == doctest::Approx(stepsOf(AnimationClock::simulatedFrameNs)).epsilon(1e-4));
					// The uniform DirectTerrain hands the shaders.
					const float a = previous.wrapped(period), b = now.wrapped(period);
					// What float resolves at the period's far end: two rounding steps.
					const double resolution = 2 * std::ldexp(1.0, std::ilogb(float(period)) - 23);
					// Water: 6 ticks a flipbook frame, 16 frames; lava: 16 and 4.
					for (const float ticks : {6.f, 16.f})
					{
						const float frames = ticks == 6.f ? 16.f : 4.f;
						const auto weight = [&](float t) {
							const float stepTime = glslMod(t, ticks);
							return glslMod(std::floor(t / ticks), frames) + std::clamp(stepTime / ticks, 0.f, 1.f);
						};
						double moved = weight(b) - weight(a);
						if (moved < 0) moved += frames; // the flipbook's own cycle
						CHECK(std::abs(moved - elapsed / ticks) <= resolution / ticks);
						CHECK(moved > 0);
					}
					// Surf: 300- and 340-tick washes, in 1024ths of their period.
					for (const float wavePeriod : {300.f, 340.f})
					{
						const auto wash = [&](float t) { return glslMod(t, wavePeriod) * 1024.f / wavePeriod; };
						double washed = wash(b) - wash(a);
						if (washed < 0) washed += 1024;
						CHECK(std::abs(washed - elapsed * 1024 / wavePeriod) <= resolution * 1024 / wavePeriod);
						CHECK(washed > 0);
					}
					previous = now;
				}
			}
	}

	TEST_CASE("the portable renderer's keyframe crossfade moves by the elapsed time every frame")
	{
		ClockOverride override;
		for (const auto &rate : Rates)
			for (int interval : {6, 16})
			{
				CAPTURE(rate.name);
				CAPTURE(interval);
				AnimationClock clock;
				int steps = 1000;
				AnimationTime previous{steps, 0};
				for (int frame = 0; frame < 240; ++frame)
				{
					CAPTURE(frame);
					AnimationClock::simulatedFrameNs = intervalNs(rate, frame);
					steps += clock.advanceAt(false, 1);
					const AnimationTime now{steps, clock.fraction()};
					// What SoftwareTerrainCache::updateKeyframes draws: keyframe
					// `index` beneath `index + 1` at keyframeAlpha.
					const auto a = SoftwareTerrainCache::keyframeBlend(previous, interval);
					const auto b = SoftwareTerrainCache::keyframeBlend(now, interval);
					const auto alpha = [](const SoftwareTerrainCache::KeyframeBlend &blend) {
						return double(blend.key) * 255 + SoftwareTerrainCache::keyframeAlpha(255, blend.weight);
					};
					const double expected = (now.value() - previous.value()) / interval * 255;
					const double drawn = alpha(b) - alpha(a);
					CHECK(drawn >= expected - 1.01);
					CHECK(drawn <= expected + 1.01);
					CHECK(drawn > 0);
					previous = now;
				}
			}
	}

	// -----------------------------------------------------------------------
	// Display guards: the whole draw path, frame by frame, on each renderer.

	TEST_CASE("portable SDL water animates smoothly and in step every frame at every rate [display]")
	{
		checkSmoothWater(RendererBackend::PortableSDL, Pace::Settled);
	}
	TEST_CASE("SDL GPU water animates smoothly and in step every frame at every rate [display]")
	{
		checkSmoothWater(RendererBackend::SDLGPU, Pace::Settled);
	}
	TEST_CASE("Programmable GL water animates smoothly and in step every frame at every rate [display]")
	{
		checkSmoothWater(RendererBackend::ProgrammableGL, Pace::Settled);
	}
	TEST_CASE("Legacy GL water animates smoothly and in step every frame at every rate [display]")
	{
		checkSmoothWater(RendererBackend::LegacyGL, Pace::Settled);
	}
	TEST_CASE("software water advances in whole steps and in step at every rate [display]")
	{
		checkSmoothWater(RendererBackend::Software, Pace::Settled);
	}
	// The same, drawn back to back at the display's pace without waiting for
	// background terrain work: the time each frame shows must not depend on
	// when workers finish (flipbook endpoints, keyframes).
	TEST_CASE("portable SDL water keeps its phase while terrain work runs in the background [display]")
	{
		checkSmoothWater(RendererBackend::PortableSDL, Pace::Live);
	}
	TEST_CASE("SDL GPU water keeps its phase while terrain work runs in the background [display]")
	{
		checkSmoothWater(RendererBackend::SDLGPU, Pace::Live);
	}
	TEST_CASE("Programmable GL water keeps its phase while terrain work runs in the background [display]")
	{
		checkSmoothWater(RendererBackend::ProgrammableGL, Pace::Live);
	}
	TEST_CASE("Legacy GL water keeps its phase while terrain work runs in the background [display]")
	{
		checkSmoothWater(RendererBackend::LegacyGL, Pace::Live);
	}

	TEST_CASE("renderers show water at the same fractional time [display]")
	{
		struct Shot { RendererBackend backend; std::vector<Uint8> whole, half; };
		std::vector<Shot> shots;
		std::vector<std::size_t> water;
		for (auto backend : {RendererBackend::SDLGPU, RendererBackend::ProgrammableGL, RendererBackend::LegacyGL,
							 RendererBackend::PortableSDL, RendererBackend::Software})
		{
			if (!wanted(backend))
				continue;
			Scene scene(backend);
			if (!scene.available())
			{
				MESSAGE(GAGCore::rendererBackendName(backend) << " unavailable here; omitted");
				continue;
			}
			if (water.empty())
			{
				water = scene.open;
				water.insert(water.end(), scene.shore.begin(), scene.shore.end());
			}
			Shot shot{backend, scene.at(81591, 0.f), scene.at(81591, 0.5f)};
			shots.push_back(std::move(shot));
		}
		if (shots.size() < 2)
		{
			MESSAGE("fewer than two renderers available; comparison omitted");
			return;
		}
		// The first renderer is the reference; a direct-pass one where available.
		const auto &reference = shots.front();
		CAPTURE(GAGCore::rendererBackendName(reference.backend));
		// Half a step changes the image: the reference interpolates too.
		if (continuous(reference.backend))
			CHECK(change(reference.whole, reference.half, water) > 0.05);
		for (std::size_t i = 1; i < shots.size(); ++i)
		{
			const auto &shot = shots[i];
			CAPTURE(GAGCore::rendererBackendName(shot.backend));
			const double whole = change(shot.whole, reference.whole, water);
			const double half = change(shot.half, reference.half, water);
			CAPTURE(whole);
			CAPTURE(half);
			CHECK(whole < 3.0);
			CHECK(half < 3.0);
			if (continuous(shot.backend) && continuous(reference.backend))
			{
				// At the same fractional time, nearer the reference's image at
				// that time than at the whole step before it.
				const double behind = change(shot.half, reference.whole, water);
				CAPTURE(behind);
				CHECK(half < behind);
			}
		}
	}

	TEST_CASE("portable SDL calms open water from shared keyframes under still overlays [display]")
	{
		if (!wanted(RendererBackend::PortableSDL))
			return;
		std::optional<Scene> portable(std::in_place, RendererBackend::PortableSDL);
		if (!portable->available())
		{
			MESSAGE("portable SDL unavailable here; omitted");
			return;
		}
		// The calmed band: water cells near the island that are not shore cells,
		// which the shore-calm reach quiets.
		std::vector<std::size_t> calmed;
		std::size_t island = 0; // cells on the island or within a cell of it
		for (int cy = 0; cy < Height / 32; ++cy)
			for (int cx = 0; cx < Width / 32; ++cx)
			{
				island += nearIsland(cx, cy, 1);
				if (!nearIsland(cx, cy, 2) || nearIsland(cx, cy, 0))
					continue;
				for (int y = cy * 32 + 2; y < cy * 32 + 30; ++y)
					for (int x = cx * 32 + 2; x < cx * 32 + 30; ++x)
						calmed.push_back((std::size_t(y) * Width + x) * 4);
			}
		const auto whole = portable->at(81591, 0.f), half = portable->at(81591, 0.5f);
		const auto *pages = portable->view.render.existingTerrainCache();
		REQUIRE(pages);
		const auto cells = pages->keyframeCells();
		CAPTURE(cells.shared);
		CAPTURE(cells.calmOverlay);
		CAPTURE(cells.own);
		// Calmed open water no longer composes keyframes of its own: only the
		// island's cells and those just around it, where land and surf mix, do.
		CHECK(cells.calmOverlay > 0);
		CHECK(cells.shared > 0);
		CHECK(cells.own > 0);
		CHECK(cells.own <= island);
		// It moves within a step, as the shared keyframes crossfade under it:
		// a little, as the calm band keeps an eighth of the waves at the shore.
		CHECK(change(whole, half, calmed) > 0.02);
		portable.reset(); // One display scene at a time.
		// And it shows what the direct pass shows there, at whole and half steps.
		for (auto backend : {RendererBackend::SDLGPU, RendererBackend::ProgrammableGL})
		{
			if (!wanted(backend))
				continue;
			Scene direct(backend);
			if (!direct.available())
				continue;
			CAPTURE(GAGCore::rendererBackendName(backend));
			const auto referenceWhole = direct.at(81591, 0.f), referenceHalf = direct.at(81591, 0.5f);
			const double wholeChange = change(whole, referenceWhole, calmed), halfChange = change(half, referenceHalf, calmed);
			CAPTURE(wholeChange);
			CAPTURE(halfChange);
			CHECK(wholeChange < 3.0);
			CHECK(halfChange < 3.0);
			CHECK(halfChange < change(half, referenceWhole, calmed));
			break;
		}
	}

	// A zoom changes the density the view's pages compose at, and keyframes
	// are composed per density: until the new density's arrive, the water
	// draws the previous one's rather than its page's still pixels. The clock
	// stands still, so the previous density always holds the keyframes shown
	// and the check is independent of how fast the workers compose.
	TEST_CASE("portable SDL water draws the previous density's keyframes while a zoom changes density [display]")
	{
		if (!wanted(RendererBackend::PortableSDL))
			return;
		Scene scene(RendererBackend::PortableSDL);
		if (!scene.available())
		{
			MESSAGE("portable SDL unavailable here; omitted");
			return;
		}
		// Settled zoomed out, every cell the zoom in shows has keyframes at
		// that density alone.
		scene.zoom = 0.4f;
		scene.at(81591, 0.5f);
		const auto *pages = scene.view.render.existingTerrainCache();
		REQUIRE(pages);
		std::set<std::pair<int, int>> densities;
		std::size_t still = 0, animated = 0, borrowed = 0;
		// In from 0.4 to 1.6.
		constexpr int Frames = 120;
		const double start = scene.view.render.animationNow().value();
		for (int f = 1; f <= Frames; ++f)
		{
			scene.zoom = 0.4f + 1.2f * float(f) / Frames;
			scene.live(1); // A nanosecond a frame: the clock stays within the key.
			densities.insert({pages->samplingResolution(), pages->samplingReduction()});
			const auto cells = pages->keyframeCells();
			still += cells.still;
			animated += cells.shared + cells.calmOverlay + cells.own;
			borrowed += pages->missingKeyframeCells() - cells.still;
		}
		CAPTURE(densities.size());
		CAPTURE(animated);
		CAPTURE(borrowed);
		CHECK(scene.view.render.animationNow().value() == doctest::Approx(start).epsilon(1e-6));
		CHECK(densities.size() >= 3);
		CHECK(borrowed > 0); // Cells drew another density's keyframes.
		CHECK(still == 0);
	}

	// With the machine's cores busy, as with other programs running, the
	// workers fall behind the keyframes the view wants: the water then holds
	// or slows as one, never tearing into cells at different phases.
	TEST_CASE("portable SDL water keeps one phase across the view while the machine is loaded [display]")
	{
		if (!wanted(RendererBackend::PortableSDL))
			return;
		Scene scene(RendererBackend::PortableSDL);
		if (!scene.available())
		{
			MESSAGE("portable SDL unavailable here; omitted");
			return;
		}
		const auto start = scene.at(81591, 0.f);
		const auto *pages = scene.view.render.existingTerrainCache();
		REQUIRE(pages);
		std::atomic<bool> busy{true};
		std::vector<std::thread> load;
		for (unsigned i = 0; i < std::max(2u, std::thread::hardware_concurrency() * 2); ++i)
			load.emplace_back([&busy]
			{
				volatile std::uint64_t spin = 0;
				while (busy.load(std::memory_order_relaxed))
					spin = spin + 1;
			});
		std::size_t torn = 0, still = 0, tornFrames = 0;
		std::vector<Uint8> last;
		for (int f = 0; f < 240; ++f)
		{
			last = scene.live(16'666'667);
			const auto cells = pages->keyframeCells();
			torn += cells.torn;
			still += cells.still;
			tornFrames += cells.torn + cells.still > 0;
		}
		busy = false;
		for (auto &thread : load)
			thread.join();
		CAPTURE(torn);
		CAPTURE(still);
		CAPTURE(tornFrames);
		CHECK(change(start, last, scene.open) > 0); // The water moved.
		CHECK(torn == 0);
		CHECK(still == 0);
	}
}
