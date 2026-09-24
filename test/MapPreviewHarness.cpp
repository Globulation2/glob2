// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapThumbnail.h"
#include "MapPreviewGeometry.h"
#include "GUIMapPreview.h"
#include "CustomGameScreen.h"
#include "GlobalContainer.h"
#include "Map.h"
#include "YOGClient.h"
#include "YOGClientDownloadableMapList.h"
#include "MapDatabaseMessages.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "StringTable.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <zlib.h>
using namespace GAGCore;
GlobalContainer *globalContainer = nullptr;

namespace
{
std::string encode(const MapThumbnail &image)
{
	auto memory = new MemoryStreamBackend;
	BinaryOutputStream output(memory);
	image.encodeData(&output);
	return memory->takeContents();
}
MapThumbnail decode(const std::string &bytes)
{
	BinaryInputStream input(new MemoryStreamBackend(bytes.data(), bytes.size()));
	input.seekFromStart(0);
	MapThumbnail image;
	image.decodeData(&input, 0);
	return image;
}
std::string envelope(int w, int h, const std::vector<Uint8> &bytes)
{
	auto memory = new MemoryStreamBackend;
	BinaryOutputStream output(memory);
	output.writeSint16(w, "");
	output.writeSint16(h, "");
	output.writeUint32(bytes.size(), "");
	output.write(bytes.data(), bytes.size(), "");
	return memory->takeContents();
}
std::vector<Uint8> packed(const std::vector<Uint8> &bytes)
{
	uLongf n = compressBound(bytes.size());
	std::vector<Uint8> out(n);
	assert(compress2(out.data(), &n, bytes.data(), bytes.size(), 6) == Z_OK);
	out.resize(n);
	return out;
}
MapThumbnail terrain(int wDec, int hDec, bool noise = false)
{
	Map map;
	map.setSize(wDec, hDec, GRASS);
	std::mt19937 random(71);
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
		{
			int kind = noise ? random() % 3 : (x < map.getW() / 2 ? 0 : 1);
			map.setUMatPos(x, y, kind == 0 ? GRASS : kind == 1 ? WATER : SAND, 1);
		}
	MapThumbnail image;
	image.loadFromMap(map);
	return image;
}
} // namespace
struct MapPreviewHarness
{
	static void settle(Screen &screen, MapPreview *preview)
	{
		screen.dispatchPaint(false);
		if (preview->transitioning)
		{
			preview->transitionPending = false;
			preview->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs - 1;
			screen.dispatchPaint(false);
		}
	}
	static void codec()
	{
		auto image = terrain(9, 8);
		assert(image.pixels()->width == 512 && image.pixels()->height == 256);
		auto bytes = encode(image);
		auto restored = decode(bytes);
		assert(restored.isLoaded() && restored.getMapWidth() == 512 &&
			   restored.getMapHeight() == 256);
		assert(restored.pixels()->rgb == image.pixels()->rgb);
		// The old decoder's exact uncompress call must still return Z_OK.
		BinaryInputStream wire(new MemoryStreamBackend(bytes.data(), bytes.size()));
		wire.seekFromStart(0);
		wire.readSint16("");
		wire.readSint16("");
		unsigned size = wire.readUint32("");
		assert(size <= MapThumbnail::MaxEncodedBytes && bytes.size() + 3 < 65536);
		std::vector<Uint8> compressed(size), legacy(128 * 128 * 3);
		wire.read(compressed.data(), size, "");
		uLongf outputSize = legacy.size();
		assert(uncompress(legacy.data(), &outputSize, compressed.data(), size) == Z_OK);
		assert(outputSize == legacy.size());
		// Independent legacy column-major fixture: centered 128x64 map.
		std::vector<Uint8> old(128 * 128 * 3, 0);
		for (int x = 0; x < 128; ++x)
			for (int y = 32; y < 96; ++y)
			{
				old[(x * 128 + y) * 3] = x;
				old[(x * 128 + y) * 3 + 1] = y - 32;
			}
		auto oldImage = decode(envelope(512, 256, packed(old)));
		assert(oldImage.isLoaded() && oldImage.pixels()->width == 128 &&
			   oldImage.pixels()->height == 64);
		assert(oldImage.pixels()->rgb[(63 * 128 + 127) * 3] == 127);
		assert(oldImage.pixels()->rgb[(63 * 128 + 127) * 3 + 1] == 63);
		for (size_t n : {size_t(0), size_t(3), size_t(8), bytes.size() / 2, bytes.size() - 1})
			assert(!decode(bytes.substr(0, n)).isLoaded());
		assert(!decode(envelope(512, 256, {1, 2, 3, 4})).isLoaded());
		assert(!decode(envelope(0, 256, packed(old))).isLoaded());
		assert(!decode(envelope(-1, 256, packed(old))).isLoaded());
		assert(!decode(envelope(512, 256, std::vector<Uint8>(60001, 0))).isLoaded());
		old.resize(20);
		assert(!decode(envelope(512, 256, packed(old))).isLoaded());
		std::vector<Uint8> bomb(128 * 128 * 3 + 1, 7);
		assert(!decode(envelope(512, 256, packed(bomb))).isLoaded());
		auto corrupt = compressed;
		corrupt.back() ^= 0xff;
		assert(!decode(envelope(512, 256, corrupt)).isLoaded());
		for (auto shape : {std::pair<int, int>{9, 9}, {9, 7}, {7, 9}})
		{
			auto noisy = terrain(shape.first, shape.second, true);
			auto wire = encode(noisy);
			assert(wire.size() + 3 < 65536 && decode(wire).isLoaded());
		}
		MapThumbnail failed = image;
		failed.loadFromMap("maps/does-not-exist-preview-regression.map");
		assert(!failed.isLoaded() && failed.getMapWidth() == 0 && failed.getMapHeight() == 0);
		failed.loadFromMap("");
		assert(!failed.isLoaded());
		std::cout << "PASS sharp terrain, legacy/new codecs, frame limit, "
					 "malformed/short/oversized inputs and missing files\n";
	}
	static void geometry()
	{
		using G = MapPreviewGeometry;
		auto wide = G::fit({10, 20, 200, 200}, 512, 256);
		assert(wide.x == 10 && wide.y == 70 && wide.w == 200 && wide.h == 100);
		G view;
		assert(view.x(0, 512, wide) == 10 && view.y(0, 256, wide) == 70);
		assert(view.x(256, 512, wide) == 110 && view.y(128, 256, wide) == 120);
		// The previous marker formula placed this colony at y=20, in padding.
		assert(20 + 0 * 200 / 256 != view.y(0, 256, wide));
		view.drag(50, 25, wide);
		assert(view.x(0, 512, wide) == 60 && view.y(0, 256, wide) == 95);
		view.drag(200 * 37, 100 * -41, wide);
		assert(view.x(0, 512, wide) == 60 && view.y(0, 256, wide) == 95);
		view.drag(-50, -25, wide);
		assert(view.x(0, 512, wide) == 10 && view.y(0, 256, wide) == 70);
		auto tall = G::fit({0, 0, 200, 200}, 128, 512);
		assert(tall.x == 75 && tall.y == 0 && tall.w == 50 && tall.h == 200);
		for (auto dimensions : {std::pair{64, 512}, std::pair{512, 64}})
			for (auto slot : {G::Rect{15, 20, 38, 309}, G::Rect{15, 20, 11, 89},
							  G::Rect{15, 20, 309, 38}, G::Rect{15, 20, 89, 11}})
			{
				auto fitted = G::fit(slot, dimensions.first, dimensions.second);
				auto again = G::fit(fitted, dimensions.first, dimensions.second);
				assert(again.x == fitted.x && again.y == fitted.y && again.w == fitted.w &&
					   again.h == fitted.h);
			}
		std::cout << "PASS rectangular placement, positive/negative multi-period drags and inverse "
					 "drags\n";
	}
	static void network()
	{
		assert(SDLNet_Init() == 0);
		YOGClient client;
		YOGClientDownloadableMapList list(&client);
		MapHeader header;
		header.setMapName("Preview test");
		YOGDownloadableMapInfo info(header);
		info.setMapID(7);
		info.setDimensions(512, 256);
		info.setSize(100);
		list.receiveMessage(
			std::make_shared<NetDownloadableMapInfos>(std::vector<YOGDownloadableMapInfo>{info}));
		using S = YOGClientDownloadableMapList::ThumbnailState;
		assert(list.getThumbnailState("Preview test") == S::Empty);
		list.requestThumbnail("Preview test");
		assert(list.getThumbnailState("Preview test") == S::Loading);
		list.thumbnailCache[7].requestedAt = SDL_GetTicks() - 100;
		auto requested = list.thumbnailCache[7].requestedAt;
		list.requestThumbnail("Preview test");
		assert(list.thumbnailCache[7].requestedAt == requested);
		list.thumbnailCache[7].requestedAt = SDL_GetTicks() - 8001;
		assert(list.getThumbnailState("Preview test") == S::Failed);
		list.requestThumbnail("Preview test", true);
		assert(list.getThumbnailState("Preview test") == S::Loading);
		list.receiveMessage(std::make_shared<NetSendMapThumbnail>(7, MapThumbnail()));
		assert(list.getThumbnailState("Preview test") == S::Failed);
		list.requestThumbnail("Preview test", true);
		auto image = terrain(9, 8);
		list.receiveMessage(std::make_shared<NetSendMapThumbnail>(7, image));
		assert(list.getThumbnailState("Preview test") == S::Ready);
		list.requestMapListUpdate();
		list.receiveMessage(
			std::make_shared<NetDownloadableMapInfos>(std::vector<YOGDownloadableMapInfo>{info}));
		assert(list.getMapThumbnail("Preview test").pixels() == image.pixels());
		info.setSize(101);
		list.receiveMessage(
			std::make_shared<NetDownloadableMapInfos>(std::vector<YOGDownloadableMapInfo>{info}));
		assert(list.getThumbnailState("Preview test") == S::Empty);
		for (int id = 0; id < 50; ++id)
		{
			info.setMapID(id);
			list.thumbnailEntry(info);
		}
		assert(list.thumbnailCache.size() == 32);
		std::cout << "PASS online deduplication, timeout/retry, failure, refresh cache, revision "
					 "invalidation and eviction\n";
	}
	static void visuals(const std::string &output)
	{
		assert(!Toolkit::getStringTable()->getString("[Map preview loading]").empty());
		assert(!Toolkit::getStringTable()->getString("[Map preview drag help]").empty());
		std::filesystem::create_directories(output);
		struct SurfaceScreen : Screen
		{
			explicit SurfaceScreen(DrawableSurface *surface) { gfx = surface; }
			void onAction(Widget *, Action, int, int) override {}
			void paint() override
			{
				gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), Color(232, 237, 218));
			}
		} screen(globalContainer->gfx);
		auto preview = new MapPreview(30, 30, ALIGN_LEFT, ALIGN_TOP);
		preview->setDimensions(512, 360);
		screen.addWidget(preview);
		screen.dispatchInit();
		auto image = terrain(9, 8);
		preview->setMapThumbnail(image);
		preview->starts = {{256, 128, Color(255, 0, 0)}};
		auto capture = [&](const char *name)
		{
			settle(screen, preview);
			globalContainer->gfx->printScreen(output + "/" + name + ".bmp");
		};
		auto area = preview->mapArea();
		auto pixel = [&](int x, int y)
		{
			auto surface = globalContainer->gfx->getSDLSurface();
			Uint32 value = 0;
			std::memcpy(&value,
						static_cast<Uint8 *>(surface->pixels) + y * surface->pitch +
							x * surface->format->BytesPerPixel,
						surface->format->BytesPerPixel);
			Uint8 r, g, b;
			SDL_GetRGB(value, surface->format, &r, &g, &b);
			return std::array<int, 3>{r, g, b};
		};
		screen.dispatchPaint(false);
		assert(preview->transitioning && !preview->transitionPending);
		assert(
			(pixel(area.x + area.w / 4, area.y + area.h / 4) == std::array<int, 3>{211, 223, 197}));
		globalContainer->gfx->printScreen(output + "/fade-in-start.bmp");
		preview->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs / 2;
		screen.dispatchPaint(false);
		const auto fading = pixel(area.x + area.w / 4, area.y + area.h / 4);
		assert(fading[0] > 0 && fading[0] < 211 && fading[1] > 90 && fading[1] < 223);
		globalContainer->gfx->printScreen(output + "/fade-in-half.bmp");
		capture("wide-original");
		assert(!preview->transitioning && !preview->previousFrame);
		auto green = pixel(area.x + area.w / 4, area.y + area.h / 4);
		const std::array<int, 3> background{232, 237, 218};
		assert(pixel(34, 34) == background);
		assert(pixel(area.x + area.w / 2, area.y - 4) == background);
		assert(pixel(area.x + area.w / 2, area.y + area.h + 4) == background);
		auto blue = pixel(area.x + 3 * area.w / 4, area.y + area.h / 4);
		assert((green == std::array<int, 3>{0, 90, 0}));
		assert((blue == std::array<int, 3>{0, 40, 120}));
		SDL_Event event{};
		event.type = SDL_MOUSEBUTTONDOWN;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.x = area.x + 50;
		event.button.y = area.y + 50;
		screen.dispatchEvents(&event);
		event = {};
		event.type = SDL_MOUSEMOTION;
		event.motion.state = SDL_BUTTON_LMASK;
		event.motion.x = area.x + 50 + area.w / 2;
		event.motion.y = area.y + 50 + area.h / 2;
		screen.dispatchEvents(&event);
		capture("wide-dragged");
		assert(pixel(area.x + area.w / 4, area.y + area.h / 4) == blue);
		assert(pixel(area.x + 3 * area.w / 4, area.y + area.h / 4) == green);
		assert(preview->dragging);
		event = {};
		event.type = SDL_MOUSEBUTTONUP;
		event.button.button = SDL_BUTTON_LEFT;
		screen.dispatchEvents(&event);
		assert(!preview->dragging);
		auto pointUnderMouse = [&]
		{
			auto world = preview->worldArea();
			return std::array<double, 2>{
				MapPreviewGeometry::wrap(double(preview->mouseX - world.x) / world.w -
										 preview->view.offsetX),
				MapPreviewGeometry::wrap(double(preview->mouseY - world.y) / world.h -
										 preview->view.offsetY)};
		};
		const auto anchor = pointUnderMouse();
		event = {};
		event.type = SDL_MOUSEWHEEL;
		event.wheel.y = 2;
		screen.dispatchEvents(&event);
		auto zoomedAnchor = pointUnderMouse();
		assert(std::abs(anchor[0] - zoomedAnchor[0]) < 1e-12 &&
			   std::abs(anchor[1] - zoomedAnchor[1]) < 1e-12);
		capture("wide-zoomed");
		assert(preview->zoom > 1);
		event.wheel.y = -1;
		screen.dispatchEvents(&event);
		zoomedAnchor = pointUnderMouse();
		assert(std::abs(anchor[0] - zoomedAnchor[0]) < 1e-12 &&
			   std::abs(anchor[1] - zoomedAnchor[1]) < 1e-12);
		auto previous = preview->view.offsetX;
		preview->setMapThumbnail(image);
		assert(preview->view.offsetX == previous && preview->zoom > 1);
		event = {};
		event.type = SDL_MOUSEBUTTONDOWN;
		event.button.button = SDL_BUTTON_RIGHT;
		event.button.x = area.x + 50;
		event.button.y = area.y + 50;
		screen.dispatchEvents(&event);
		assert(preview->view.offsetX == 0 && preview->view.offsetY == 0 && preview->zoom == 1);
		preview->resetView();
		screen.dispatchPaint(false);
		Map flat;
		flat.setSize(9, 8, GRASS);
		MapThumbnail grass;
		grass.loadFromMap(flat);
		preview->setMapThumbnail(grass);
		preview->starts = {{256, 128, Color(0, 255, 0)}};
		screen.dispatchPaint(false);
		const int sampleX = area.x + 3 * area.w / 4, sampleY = area.y + area.h / 4;
		assert(pixel(sampleX, sampleY) == blue);
		globalContainer->gfx->printScreen(output + "/cross-fade-start.bmp");
		preview->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs / 2;
		screen.dispatchPaint(false);
		const auto blended = pixel(sampleX, sampleY);
		assert(blended[1] > 40 && blended[1] < 90 && blended[2] > 0 && blended[2] < 120);
		const auto marker = pixel(area.x + area.w / 2 - 7, area.y + area.h / 2 - 7);
		assert(marker[0] > 0 && marker[0] < 255 && marker[1] > 0 && marker[1] < 255);
		globalContainer->gfx->printScreen(output + "/cross-fade-half.bmp");
		// A second result arriving during a fade starts at the displayed blend.
		flat.setSize(9, 8, WATER);
		MapThumbnail water;
		water.loadFromMap(flat);
		// Snapshot encoding and fixture construction must not advance this test's clock.
		preview->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs / 2;
		preview->setMapThumbnail(water);
		screen.dispatchPaint(false);
		const auto restarted = pixel(sampleX, sampleY);
		for (int c = 0; c < 3; ++c)
			assert(std::abs(restarted[c] - blended[c]) < 12);
		capture("cross-fade-complete");
		assert(pixel(sampleX, sampleY) == blue && !preview->previousFrame);
		std::cout << "PASS first-image fade, terrain/marker cross-fade, rapid replacement and "
					 "frame release\n";
		preview->setMapThumbnail(terrain(8, 9));
		capture("tall-original");
		area = preview->mapArea();
		assert(pixel(area.x - 4, area.y + area.h / 2) == background);
		assert(pixel(area.x + area.w + 4, area.y + area.h / 2) == background);
		preview->setState(MapPreview::State::Loading);
		capture("online-loading");
		preview->retry = [] {};
		preview->setState(MapPreview::State::Failed);
		capture("online-failed");
		GAGGUI::ScreenStack screens(*globalContainer->gfx);
		CustomGameScreen custom(screens);
		custom.dispatchInit();
		custom.updateLayout();
		MapPreview *lobby = nullptr;
		for (auto widget : custom.widgets)
			if (auto candidate = dynamic_cast<MapPreview *>(widget))
				lobby = candidate;
		assert(lobby);
		const auto fixture = output + "/selection-fixture.map";
		std::filesystem::copy_file("maps/FourSquares1.map", fixture,
								   std::filesystem::copy_options::overwrite_existing);
		auto start = std::chrono::steady_clock::now();
		assert(custom.loadMap(fixture));
		auto cold = std::chrono::steady_clock::now();
		for (int i = 0; i < 20; ++i)
			assert(custom.loadMap(fixture));
		auto warm = std::chrono::steady_clock::now();
		std::ofstream timing(output + "/timings.txt");
		timing << "FourSquares1.map byte-identical copy, uncached selection ms: "
			   << std::chrono::duration<double, std::milli>(cold - start).count()
			   << "\n20 cached selections ms: "
			   << std::chrono::duration<double, std::milli>(warm - cold).count() << "\n";
		custom.activateGroup(custom.groups[0]);
		settle(custom, lobby);
		globalContainer->gfx->printScreen(output + "/custom-colonies.bmp");
		area = lobby->mapArea();
		event = {};
		event.type = SDL_MOUSEBUTTONDOWN;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.x = area.x + area.w / 2;
		event.button.y = area.y + area.h / 2;
		custom.dispatchEvents(&event);
		event = {};
		event.type = SDL_MOUSEMOTION;
		event.motion.state = SDL_BUTTON_LMASK;
		event.motion.x = area.x + area.w;
		event.motion.y = area.y + area.h;
		custom.dispatchEvents(&event);
		custom.dispatchPaint();
		globalContainer->gfx->printScreen(output + "/custom-colonies-wrapped.bmp");
		event = {};
		event.type = SDL_MOUSEBUTTONUP;
		event.button.button = SDL_BUTTON_LEFT;
		custom.dispatchEvents(&event);
		custom.setup.random = true;
		custom.setup.generator.setMethodDefaults(GenerationRequest::eSWAMP);
		custom.setup.generator.wDec = 9;
		custom.setup.generator.hDec = 7;
		custom.setup.setCapacity(4);
		assert(custom.generateMap());
		MapThumbnail snapshot;
		snapshot.loadFromMap(custom.snapshot);
		assert(snapshot.isLoaded() && snapshot.pixels()->rgb == lobby->thumbnail.pixels()->rgb);
		std::filesystem::copy_file(custom.snapshot, output + "/generated-wide.map",
								   std::filesystem::copy_options::overwrite_existing);
		settle(custom, lobby);
		globalContainer->gfx->printScreen(output + "/custom-wide-colonies.bmp");
		area = lobby->mapArea();
		event = {};
		event.type = SDL_MOUSEBUTTONDOWN;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.x = area.x + area.w / 2;
		event.button.y = area.y + area.h / 2;
		custom.dispatchEvents(&event);
		event = {};
		event.type = SDL_MOUSEMOTION;
		event.motion.state = SDL_BUTTON_LMASK;
		event.motion.x = area.x + area.w;
		event.motion.y = area.y + area.h;
		custom.dispatchEvents(&event);
		custom.dispatchPaint();
		globalContainer->gfx->printScreen(output + "/custom-wide-wrapped.bmp");
		event = {};
		event.type = SDL_WINDOWEVENT;
		event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
		custom.dispatchEvents(&event);
		assert(!lobby->dragging);
		const auto retained = lobby->thumbnail.pixels();
		const auto retainedArea = lobby->mapArea();
		const auto quality = custom.quality;
		assert(quality.measured && custom.message.empty());
		custom.invalidate();
		custom.dispatchPaint(false);
		const auto loadingArea = lobby->mapArea();
		assert(!custom.validMap && custom.previewBusy() && custom.message.empty());
		assert(lobby->thumbnail.pixels() == retained && custom.quality.measured &&
			   custom.quality.fairness == quality.fairness &&
			   custom.quality.score == quality.score);
		assert(loadingArea.x == retainedArea.x && loadingArea.y == retainedArea.y &&
			   loadingArea.w == retainedArea.w && loadingArea.h == retainedArea.h);
		globalContainer->gfx->printScreen(output + "/custom-reroll-retained.bmp");
		std::cout
			<< "PASS reroll preserves image, rectangle and scores without status-text flashes\n";
		const auto invalidated = output + "/invalidated.map";
		std::filesystem::copy_file(fixture, invalidated,
								   std::filesystem::copy_options::overwrite_existing);
		MapThumbnail first, second;
		first.loadFromMap(invalidated);
		second.loadFromMap(invalidated);
		assert(first.isLoaded() && first.pixels() == second.pixels());
		{
			std::ofstream broken(invalidated);
			broken << "invalid map";
		}
		second.loadFromMap(invalidated);
		assert(!second.isLoaded());
		std::filesystem::remove(invalidated);
		std::cout << "PASS native widget events and custom lobby captures: " << output << "\n";
	}
};
int main(int argc, char **argv)
{
	const bool visual = argc > 2 && std::string(argv[2]) == "--visual";
	GlobalContainer globals(argc > 1 ? argv[1] : "glob2-map-preview-tests");
	globalContainer = &globals;
	globals.runNoX = !visual;
	globals.settings.rememberUnit = false;
	globals.settings.screenWidth = argc > 4 ? std::max(640, std::atoi(argv[4])) : 800;
	globals.settings.screenHeight = argc > 5 ? std::max(480, std::atoi(argv[5])) : 600;
	globals.settings.screenFlags = 0;
	globals.settings.mute = true;
	globals.load();
	MapPreviewHarness::geometry();
	MapPreviewHarness::codec();
	MapPreviewHarness::network();
	if (visual)
		MapPreviewHarness::visuals(argc > 3 ? argv[3] : "artifacts/map-preview");
	std::cout << "ALL PRE-GAME MAP PREVIEW TESTS PASSED\n";
	return 0; // On Windows SDL renames this entry point to SDL_main.
}
