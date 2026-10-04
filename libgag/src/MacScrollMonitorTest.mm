// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <ui/Host.h>
#include <ui/Controls.h>
#include <ui/Containers.h>
#include <ui/Canvas.h>
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <filesystem>
#include <memory>
#include <GestureScroll.h>
#include "MacScrollMonitorPrivate.h"
#import <AppKit/AppKit.h>
// Synthetic native samples exercise the callback's phase/delta normalization.
@interface Glob2NativeScrollSample : NSObject
@property(assign) NSWindow *window;
@property NSEventPhase phase;
@property NSEventPhase momentumPhase;
@property BOOL hasPreciseScrollingDeltas;
@property BOOL isDirectionInvertedFromDevice;
@property CGFloat scrollingDeltaX;
@property CGFloat scrollingDeltaY;
@property NSPoint locationInWindow;
@property NSTimeInterval timestamp;
@end
@implementation Glob2NativeScrollSample
@end

namespace
{
struct NativeWindow
{
	SDL_Window *window = nullptr;
	NativeWindow()
	{
		REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
		window = SDL_CreateWindow("Mac gesture contract", 320, 240, SDL_WINDOW_HIDDEN);
		REQUIRE(window);
		GAGCore::installMacScrollMonitor(window);
	}
	~NativeWindow()
	{
		GAGCore::removeMacScrollMonitor();
		SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
};
}
TEST_SUITE("MacScrollMonitor")
{
GLOB2_TEST_CASE("native phases and delta normalization; wheel pass-through; teardown", "[display]")
{
	@autoreleasepool
	{
		NativeWindow scope;
		NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(scope.window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
		REQUIRE(native);
		Glob2NativeScrollSample *source = [[[Glob2NativeScrollSample alloc] init] autorelease];
		source.window = native;
		source.hasPreciseScrollingDeltas = YES;
		source.isDirectionInvertedFromDevice = YES;
		source.phase = NSEventPhaseBegan;
		source.scrollingDeltaX = -1.25;
		source.scrollingDeltaY = 2.5;
		source.locationInWindow = NSMakePoint(12, 24);
		auto route = [&] { return GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(scope.window), native, (NSEvent *)source); };
		auto take = [&]
		{
			SDL_Event queued{};
			REQUIRE(SDL_PeepEvents(&queued, 1, SDL_GETEVENT, GAGCore::gestureScrollEventType(), GAGCore::gestureScrollEventType()) == 1);
			return *GAGCore::scrollGesture(queued);
		};
		CHECK(route() == nil); // The monitor suppresses the duplicate native wheel.
		auto sample = take();
		CHECK(sample.dx == -1.25); CHECK(sample.dy == 2.5);
		CHECK(sample.x == 12); CHECK(sample.y == 216);
		CHECK(sample.phase == GAGCore::ScrollGesturePhase::Began);
		CHECK(sample.sequence != 0);
		CHECK(sample.wheelX == .125f); CHECK(sample.wheelY == .25f);
		CHECK(sample.direction == SDL_MOUSEWHEEL_FLIPPED);
		const auto sequence = sample.sequence;
		source.phase = NSEventPhaseEnded;
		source.scrollingDeltaX = source.scrollingDeltaY = 0;
		CHECK(route() == nil);
		CHECK(take().phase == GAGCore::ScrollGesturePhase::Ended);
		source.phase = NSEventPhaseNone; source.momentumPhase = NSEventPhaseBegan;
		CHECK(route() == nil);
		CHECK(take().sequence == sequence);
		source.momentumPhase = NSEventPhaseNone;
		CHECK(route() == (NSEvent *)source); // Precise but phase-less wheel.
		source.hasPreciseScrollingDeltas = NO; source.phase = NSEventPhaseBegan;
		CHECK(route() == (NSEvent *)source); // Conventional wheel.
		source.hasPreciseScrollingDeltas = YES; source.phase = NSEventPhaseMayBegin;
		CHECK(route() == (NSEvent *)source); // Preflight does not capture content.
		source.phase = NSEventPhaseBegan;
		CHECK(route() == nil);
		GAGCore::removeMacScrollMonitor();
		CHECK_FALSE(SDL_HasEvent(GAGCore::gestureScrollEventType()));
	}
}
GLOB2_TEST_CASE("native widget glide and edge spring visual evidence", "[display][artifacts]")
{
	glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 640, .height = 480,
		.screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
	glob2test::HeadlessGlobals globals(options);
	auto *gfx = globalContainer->gfx;
	using namespace GAGGUI::ui;
	Theme theme;
	auto presentation = Presentation::forSurface(640, 480);
	ToolkitTextMeasurer measure(theme, false, 1);
	std::string stage = "Two-finger scroll";
	Host host(theme, [&](const Presentation &)
	{
		std::vector<Element> items;
		for (int i = 0; i < 30; ++i)
			items.push_back(height(40, button("item/" + std::to_string(i), "List item " + std::to_string(i + 1), [] {})));
		return padding(Insets::all(32), column({label(stage), height(340, scroll("list", column(items))), label("Mac gesture scrolling")}, {16}));
	});
	host.setMeasurer(&measure); host.setPresentation(presentation); host.layoutIfNeeded();
	const auto output = glob2test::artifactDir() / "mac-gesture-frames";
	std::filesystem::create_directories(output);
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	REQUIRE(pixels);
	Uint32 time = 1000;
	int frame = 0;
	GAGCore::GestureScrollEvent gesture;
	gesture.sequence = 501; gesture.logical = true; gesture.x = 100; gesture.y = 120;
	auto send = [&](GAGCore::ScrollGesturePhase phase, double dy, GAGCore::ScrollGesturePhase momentum = GAGCore::ScrollGesturePhase::None)
	{
		gesture.timestamp = SDL_MS_TO_NS(time); gesture.phase = phase;
		gesture.momentum = momentum; gesture.dy = dy;
		REQUIRE(host.event(GAGCore::gestureScrollEvent(gesture)));
	};
	auto paint = [&]
	{
		time += 16; host.update(time);
		gfx->drawToSurface(pixels.get(), 1, [&]
		{
			SurfaceCanvas canvas(*gfx, theme, presentation);
			canvas.fillRect({0, 0, 640, 480}, theme.palette.paper);
			host.paint(canvas, time);
		});
		char name[32]; std::snprintf(name, sizeof(name), "frame-%04d.png", frame++);
		REQUIRE(IMG_SavePNG(pixels.get(), (output / name).string().c_str()));
	};
	for (int i = 0; i < 15; ++i) paint();
	send(GAGCore::ScrollGesturePhase::Began, 0);
	for (int i = 0; i < 20; ++i) { send(GAGCore::ScrollGesturePhase::Changed, -6.125); paint(); }
	send(GAGCore::ScrollGesturePhase::Ended, 0);
	CHECK(host.find("list")->scrollOffset() == 123);
	stage = "Glide after release"; host.invalidate();
	for (int i = 0; i < 40; ++i)
	{
		send(GAGCore::ScrollGesturePhase::None, -8 * std::pow(.92, i), i == 0 ? GAGCore::ScrollGesturePhase::Began : GAGCore::ScrollGesturePhase::Changed);
		paint();
	}
	send(GAGCore::ScrollGesturePhase::None, 0, GAGCore::ScrollGesturePhase::Ended);
	CHECK(host.find("list")->scrollOffset() > 123);
	const int coastEnd = host.find("list")->scrollOffset();
	for (int i = 0; i < 15; ++i) paint();
	CHECK(host.find("list")->scrollOffset() == coastEnd);
	stage = "Gentle edge bounce"; host.invalidate(); host.find("list")->scrollTo(0, host);
	gesture.sequence++;
	send(GAGCore::ScrollGesturePhase::Began, 0);
	for (int i = 0; i < 12; ++i) { send(GAGCore::ScrollGesturePhase::Changed, 10); paint(); }
	CHECK(host.find("list")->overscroll() < 0);
	send(GAGCore::ScrollGesturePhase::Ended, 0);
	for (int i = 0; i < 90; ++i) paint();
	CHECK(host.find("list")->overscroll() == 0);
	CHECK(host.find("list")->scrollOffset() == 0);
	CHECK_FALSE(host.animating());
}

}
