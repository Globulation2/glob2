#include "../../libgag/src/MacScrollMonitor.mm"
#include <cassert>
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
int main()
{
 @autoreleasepool {
  assert(SDL_Init(SDL_INIT_VIDEO));
  SDL_Window *window = SDL_CreateWindow("Mac scroll adapter check", 320, 240, SDL_WINDOW_HIDDEN);
  assert(window);
  NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  assert(native);
  GAGCore::installMacScrollMonitor(window);
  TestScroll *source = [TestScroll new];
  source.window = native;
  source.hasPreciseScrollingDeltas = YES;
  source.isDirectionInvertedFromDevice = YES;
  source.phase = NSEventPhaseBegan;
  source.scrollingDeltaX = -1.25;
  source.scrollingDeltaY = 2.5;
  source.locationInWindow = NSMakePoint(12, 24);
  assert(GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(window), native, (NSEvent*)source) == nil);
  SDL_Event queued{};
  assert(SDL_PeepEvents(&queued, 1, SDL_GETEVENT, GAGCore::gestureScrollEventType(), GAGCore::gestureScrollEventType()) == 1);
  auto sample = GAGCore::scrollGesture(queued);
  assert(sample && sample->dx == -1.25 && sample->dy == 2.5);
  assert(sample->phase == GAGCore::ScrollGesturePhase::Began && sample->sequence != 0);
  assert(sample->wheelX == .125f && sample->wheelY == .25f);
  assert(sample->direction == SDL_MOUSEWHEEL_FLIPPED);
  const auto sequence = sample->sequence;
  source.phase = NSEventPhaseEnded; source.scrollingDeltaX = source.scrollingDeltaY = 0;
  assert(GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(window), native, (NSEvent*)source) == nil);
  assert(SDL_PeepEvents(&queued, 1, SDL_GETEVENT, GAGCore::gestureScrollEventType(), GAGCore::gestureScrollEventType()) == 1);
  assert(GAGCore::scrollGesture(queued)->phase == GAGCore::ScrollGesturePhase::Ended);
  source.phase = NSEventPhaseNone; source.momentumPhase = NSEventPhaseBegan;
  assert(GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(window), native, (NSEvent*)source) == nil);
  assert(SDL_PeepEvents(&queued, 1, SDL_GETEVENT, GAGCore::gestureScrollEventType(), GAGCore::gestureScrollEventType()) == 1);
  assert(GAGCore::scrollGesture(queued)->sequence == sequence);
  source.momentumPhase = NSEventPhaseNone;
  assert(GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(window), native, (NSEvent*)source) == (NSEvent*)source);
  source.hasPreciseScrollingDeltas = NO; source.phase = NSEventPhaseBegan;
  assert(GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(window), native, (NSEvent*)source) == (NSEvent*)source);
  source.hasPreciseScrollingDeltas = YES;
  GAGCore::MacScrollDetail::routeScroll(SDL_GetWindowID(window), native, (NSEvent*)source);
  GAGCore::removeMacScrollMonitor();
  assert(!SDL_HasEvent(GAGCore::gestureScrollEventType()));
  [source release];
  SDL_DestroyWindow(window); SDL_Quit();
  puts("Native phase normalization, zero-delta ends, momentum sequence, wheel pass-through and teardown passed.");
 }
}
