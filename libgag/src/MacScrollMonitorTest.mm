// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GestureScroll.h>
#include "MacScrollMonitorPrivate.h"
#import <AppKit/AppKit.h>
// Synthetic native samples exercise the callback's phase/delta normalization.
@interface TestScroll : NSObject
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
@implementation TestScroll
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
		TestScroll *source = [[[TestScroll alloc] init] autorelease];
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
}
