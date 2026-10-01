// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <algorithm>
#include <cstdlib>
#include <TouchInput.h>
#include <MapCamera.h>
#include <InterfacePresentation.h>
#include <cmath>
using namespace GAGCore;
namespace
{
void require(bool value, const char *message)
{
	GLOB2_REQUIRE(value, message);
}
void near(double a, double b)
{
	CHECK_MESSAGE(std::abs(a - b) < 0.000001, "Coordinate mismatch: " << a << " vs " << b);
}
}
TEST_SUITE("MobileInput")
{
TEST_CASE("zoom anchoring; seams; rotation; safe layouts; gestures and cancellation")
{
	{
		require(parsePresentationPreference("") == PresentationPreference::Automatic,
				"Missing preference defaults to Automatic");
		require(parsePresentationPreference("garbage") == PresentationPreference::Automatic,
				"Invalid preference defaults to Automatic");
		for (bool touch : {false, true})
		{
			InputCapabilities input{touch, true, true, true};
			const double boundary = 480 + (touch ? 288 : 160);
			for (double scale : {1., 1.5, 2.})
			{
				ViewportMetrics metrics{boundary * scale, 480 * scale, scale};
				auto resolve =
					[&](PresentationPreference preference = PresentationPreference::Automatic)
				{ return resolvePresentation(preference, metrics, input); };
				require(resolve().layout == PresentationLayout::Spacious,
						"Exact fit must be Spacious");
				require(resolve(PresentationPreference::Compact).layout ==
							PresentationLayout::Compact,
						"Compact preference must win");
				metrics.width -= 1;
				require(resolve(PresentationPreference::Spacious).layout ==
							PresentationLayout::Compact,
						"Spacious must fall back when narrow");
				metrics.width += 1;
				metrics.height -= 1;
				require(resolve().layout == PresentationLayout::Compact,
						"Short viewport must be Compact");
				metrics.height += 1;
				metrics.safe.left = 1;
				require(resolve().layout == PresentationLayout::Compact,
						"Safe area participates in fit");
				metrics.width += 1;
				require(resolve().layout == PresentationLayout::Spacious,
						"Inset-adjusted boundary fits");
				metrics.keyboardInset = 200 * scale;
				require(resolve().layout == PresentationLayout::Spacious,
						"Keyboard cannot change underlying layout");
				near(resolve().dialog.h, 280);
				require(resolve().minimumTargetHeight == (touch ? 48 : 32),
						"Target size follows capabilities");
				input.hover = false;
				require(resolve().layout == PresentationLayout::Spacious && !resolve().hover,
						"Hover cannot switch layout");
				input.hover = true;
			}
		}
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
		require(presentationOverride() == PresentationPreference::Spacious &&
					!phonePresentationRequested(),
				"Desktop override uses legacy controls");
		SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
		require(presentationOverride() == PresentationPreference::Compact &&
					phonePresentationRequested(),
				"Compact override uses adapted controls");
		SDL_setenv("GLOB2_MOBILE_UI", "touch-auto", 1);
		require(presentationOverride() == PresentationPreference::Automatic &&
					phonePresentationRequested(),
				"Touch automatic mode retains adapted controls");
		SDL_setenv("GLOB2_MOBILE_UI", "touch-spacious", 1);
		require(presentationOverride() == PresentationPreference::Spacious &&
					phonePresentationRequested(),
				"Touch spacious mode retains adapted controls");
		// An empty development override must behave like an absent override.
		SDL_setenv("GLOB2_MOBILE_UI", "", 1);
		require(!presentationOverride(), "No override uses saved preference and host capabilities");
		presentationPreference = PresentationPreference::Automatic;
		updatePresentation({800, 600, 1, {}, 0}, {false, true, true, true});
		require(!phonePresentationRequested(), "Automatic desktop presentation remains spacious");
		updatePresentation({320, 568, 1, {}, 0}, {true, false, false, false});
		require(phonePresentationRequested(), "Automatic phone presentation remains compact");
		MapCamera camera;
		camera.resize(960, 720, 4096, 4096);
		camera.originX = 4080.25;
		camera.originY = 4000.5;
		camera.setZoom(1.5, 317, 283);
		auto cameraCenter = camera.screenToWorld(480, 360);
		camera.resize(480, 600, 4096, 4096, 24, 48);
		auto cameraResized = camera.screenToWorld(264, 348);
		near(MapCamera::wrap(cameraCenter.first, 4096), MapCamera::wrap(cameraResized.first, 4096));
		near(MapCamera::wrap(cameraCenter.second, 4096),
			 MapCamera::wrap(cameraResized.second, 4096));
		require(camera.contains(24, 48) && !camera.contains(23, 48) && !camera.contains(504, 48),
				"Camera excludes safe areas and side panels");
		auto anchor = camera.screenToWorld(200, 300);
		camera.wheel(.3, 200, 300);
		auto zoomed = camera.screenToWorld(200, 300);
		near(MapCamera::wrap(anchor.first, 4096), MapCamera::wrap(zoomed.first, 4096));
		near(MapCamera::wrap(anchor.second, 4096), MapCamera::wrap(zoomed.second, 4096));
		ViewportTransform view({0, 48, 320, 472}, {4096, 4096});
		view.moveTo({4090, 4});
		auto at = view.screenToWorld({215, 110});
		view.zoom(2.3, {215, 110});
		auto after = view.screenToWorld({215, 110});
		near(at.x, after.x);
		near(at.y, after.y);
		auto screen = view.worldToScreen(after);
		near(screen.x, 215);
		near(screen.y, 110);
		auto center = view.position();
		view.resize({0, 48, 568, 224});
		near(center.x, view.position().x);
		near(center.y, view.position().y);
		view.zoom(100, {284, 160});
		near(view.zoom(), 3);
		view.zoom(0.01, {284, 160});
		near(view.zoom(), 0.5);
		view.pan({10000, -10000});
		require(view.position().x >= 0 && view.position().x < 4096, "Toroidal pan failed");
		for (auto dimensions :
			 {ViewPoint{320, 568}, {568, 320}, {360, 640}, {640, 360}, {1024, 768}})
			for (double scale : {1.0, 1.5, 2.0})
				for (double keyboard : {0.0, 260.0})
				{
					auto layout = MobileLayout::calculate(dimensions.x, dimensions.y,
														  {0, 24, 0, 20}, keyboard, scale, true);
					for (auto rect : {layout.status, layout.world, layout.actions, layout.panel})
					{
						require(rect.w >= 0 && rect.h >= 0, "Negative layout dimensions");
						if (rect.w && rect.h)
							require(rect.x >= 0 && rect.y >= 24 &&
										rect.x + rect.w <= dimensions.x &&
										rect.y + rect.h <=
											dimensions.y - std::max(20.0, keyboard) + 0.001,
									"Layout escapes safe area");
					}
					if (layout.persistentPanel)
						require(layout.world.w >= 480, "Persistent panel crowds map");
				}
		TouchInput touch;
		require(touch.down(1, 1, {20, 20}).empty(), "Tap selected on down");
		require(touch.move(1, 1, {25, 20}).empty(), "Tap slop became drag");
		auto actions = touch.up(1, 1, {25, 20});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::Select,
				"Tap not selected");
		touch.down(1, 1, {20, 20});
		actions = touch.move(1, 1, {28, 20});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::Pan,
				"Threshold did not start pan");
		near(actions[0].point.x, 8);
		actions = touch.up(1, 1, {28, 20}, 500);
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::PanEnd && actions[0].time == 500,
				"Pan release did not end the pan with its timestamp");
		touch.down(1, 1, {10, 10});
		touch.down(1, 2, {30, 10});
		actions = touch.move(1, 2, {50, 10});
		require(actions.size() == 2 && actions[1].kind == TouchActionKind::Zoom, "Pinch missing");
		near(actions[1].factor, 2);
		actions = touch.up(1, 2, {50, 10});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::PanEnd, "Lifting one of two fingers did not end the pan");
		require(touch.move(1, 1, {90, 90}).empty(), "Remaining finger moved world");
		require(touch.up(1, 1, {90, 90}).empty(), "Pinch ended as tap");
		touch.setMode(TouchMode::Placement);
		actions = touch.down(1, 1, {20, 20});
		require(actions[0].kind == TouchActionKind::Preview, "Placement not a preview");
		require(touch.up(1, 1, {20, 20}).empty(), "Placement committed on release");
		touch.setMode(TouchMode::Paint);
		actions = touch.down(1, 1, {20, 20});
		require(actions[0].kind == TouchActionKind::BeginStroke, "Paint did not start");
		actions = touch.down(1, 2, {40, 40});
		require(actions[0].kind == TouchActionKind::Cancel,
				"Pinch must cancel the unfinished stroke");
		touch.cancel();
		require(touch.move(1, 1, {30, 30}).empty(), "Canceled finger still active");
		touch.setMode(TouchMode::Navigate);
		touch.down(1, 1, {10, 10});
		touch.down(2, 1, {30, 10});
		actions = touch.move(2, 1, {40, 10});
		require(actions.size() == 2, "Device finger IDs collided");
		touch.down(1, 3, {20, 20});
		require(touch.move(2, 1, {50, 10}).empty(), "Third finger failed to cancel");
		// One-finger zoom: the second contact of a double-tap.
		touch.setMode(TouchMode::ZoomDrag);
		require(touch.down(1, 1, {100, 300}).empty(), "Zoom contact acted on down");
		require(touch.move(1, 1, {100, 295}).empty(), "Zoom slop changed zoom");
		require(!touch.zoomDragging(), "Zoom feedback shown inside slop");
		actions = touch.up(1, 1, {100, 295});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::ZoomReset,
				"Double-tap without travel must request the 1:1 reset");
		near(actions[0].point.x, 100);
		near(actions[0].point.y, 300);
		touch.setMode(TouchMode::ZoomDrag);
		touch.down(1, 1, {100, 300});
		actions = touch.move(1, 1, {100, 300 - TouchInput::zoomDoublingPoints / 2});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::Zoom,
				"Upward travel missing zoom");
		near(actions[0].factor, std::sqrt(2.0));
		near(actions[0].point.y, 300); // Anchored where the contact landed.
		require(touch.zoomDragging(), "Zoom feedback missing while dragging");
		actions = touch.move(1, 1, {100, 300 - TouchInput::zoomDoublingPoints});
		near(actions[0].factor, std::sqrt(2.0)); // Incremental, composing to 2x.
		require(touch.up(1, 1, {100, 120}).empty(), "Zoom drag released as a tap");
		touch.setMode(TouchMode::ZoomDrag);
		touch.down(1, 1, {100, 300});
		actions = touch.move(1, 1, {130, 290});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::Pan,
				"A sideways start after a tap must pan, not zoom");
		near(actions[0].point.x, 30);
		actions = touch.move(1, 1, {130, 200});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::Pan,
				"A pan that began sideways must not turn into a zoom");
		actions = touch.up(1, 1, {130, 200});
		require(actions.size() == 1 && actions[0].kind == TouchActionKind::PanEnd,
				"A sideways zoom contact ends as a pan, so momentum can follow, never as a tap");
		touch.setMode(TouchMode::ZoomDrag);
		touch.setZoomDragDirection(false);
		touch.down(1, 1, {100, 300});
		actions = touch.move(1, 1, {100, 300 + TouchInput::zoomDoublingPoints});
		near(actions[0].factor, 2);
		touch.setZoomDragDirection(true);
		actions = touch.move(1, 1, {100, 300 + 2 * TouchInput::zoomDoublingPoints});
		near(actions[0].factor, 0.5);
		actions = touch.down(1, 2, {200, 300});
		require(actions.empty() && !touch.zoomDragging(), "Second finger must end one-finger zoom");
		actions = touch.move(1, 2, {260, 300});
		require(actions.size() == 2 && actions[1].kind == TouchActionKind::Zoom,
				"Second finger did not hand over to a pinch");
		touch.up(1, 2, {260, 300});
		require(touch.move(1, 1, {100, 100}).empty(), "Zoom resumed after a pinch");
		require(touch.up(1, 1, {100, 100}).empty(), "Zoom contact ended as a tap after a pinch");
	}
}
}
