// SPDX-License-Identifier: GPL-3.0-or-later
#include "InGameTouchTheme.h"
#include "GameGUITouch.h"
#include <TouchText.h>
#include <FormatableString.h>
#include <InterfacePresentation.h>
#include "MobileSafeArea.h"
#include <HostViewport.h>
#include "ThumbSide.h"
#include "BrushCoverage.h"
#include <map>
#include <set>
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "BuildingType.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"
#include "render/Minimap.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif
#if defined(__IPHONEOS__)
#include "mobile/ios/SafeArea.h"
#endif
using namespace GAGCore;

GameGUITouch::GameGUITouch(GameGUI &gui) : gui(gui)
{
	touchActive = phonePresentationRequested();
	hudMinimap = std::make_unique<Minimap>(globalContainer->runNoX, 128, 128, 0, 0, 128, 128,
										   Minimap::ShowFOW);
	hudMinimap->setGame(gui.game);
}
bool GameGUITouch::usesHUD() const
{
	return phonePresentationRequested();
}
MobileLayout GameGUITouch::layout() const
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const auto insets = presentationSafeInsets(gfx);
	auto input = presentationInput;
	input.touch = true;
	const auto resolved =
		resolvePresentation(presentationOverride().value_or(presentationPreference),
							{gfx->getW() / unit, gfx->getH() / unit, 1, insets}, input);
	auto result = MobileLayout::calculate(
		gfx->getW() / unit, gfx->getH() / unit, insets, 0, 1, panelOpen,
		resolved.layout == PresentationLayout::Spacious,
		resolved.layout == PresentationLayout::Spacious ? resolved.panelWidth : 288);
	// Persistent palettes float too: the map remains visible below their last
	// icon, and opening an inspector never changes the camera's framing.
	result.world = result.safe;
	result.world.h -= result.actions.h;
	result.status.h = 28;
	if ((result.persistentPanel || panelOpen) && showsBuildPalette())
	{
		const int columns = paletteColumns(result);
		const double stride = InGameTouchTheme::paletteCell + InGameTouchTheme::gap;
		const double rows = std::ceil(paletteItems().size() / double(columns));
		if (result.persistentPanel)
		{
			const double width = std::min(result.safe.w, columns * 60.0 + 8);
			const double height = std::min(result.world.h - 80, rows * 60 + 8);
			result.panel = {result.safe.x + result.safe.w - width, result.safe.y + 104, width, height};
		}
		else
		{
			// A rail opposite the thumb corner; taller palettes scroll.
			const double width = std::min(result.safe.w - InGameTouchTheme::railInset,
										  columns * stride + InGameTouchTheme::gap);
			const double height =
				std::min(result.world.h - 80,
						 std::min(rows, double(InGameTouchTheme::railMaximumRows)) * stride +
							 InGameTouchTheme::gap);
			result.panel = ThumbSide::corner(result.safe, width, height, InGameTouchTheme::railInset,
											 result.actions.y, ThumbSide::toolboxLeft());
		}
	}
	// Compact inspectors are the thumb dial; its bounds are set once the layout
	// is in drawable units below.
	const bool dial = (result.persistentPanel || panelOpen) && inspecting() &&
					  !result.persistentPanel && usesHUD();
	if ((result.persistentPanel || panelOpen) && inspecting() && !dial)
	{
		// Use horizontal room before introducing overflow. Ordinary inspectors
		// fit completely; only genuinely constrained/large-text views scroll.
		result.panel.w = std::min(result.safe.w, result.safe.w > result.safe.h
			? InGameTouchTheme::inspectorLandscapeWidth : InGameTouchTheme::inspectorPortraitWidth);
		result.panel.x = result.safe.x + result.safe.w - result.panel.w;
		const double available = result.actions.y - result.safe.y - (result.safe.h < 400 ? 80 : 104);
		const double height = std::min(available,
			InGameTouchTheme::inspectorHeader + buildingActionsHeight(result.panel.w));
		result.panel.y = result.persistentPanel ? result.safe.y + 104 : result.actions.y - height;
		result.panel.h = height;
	}
	if ((result.persistentPanel || panelOpen) && inspectingResource())
	{
		const double width = std::min(240.0, result.safe.w - 2 * InGameTouchTheme::railInset);
		result.panel = ThumbSide::corner(result.safe, width, 112, InGameTouchTheme::railInset,
			result.actions.y - 8, !ThumbSide::left());
	}
	if (gui.selectionMode == GameGUI::BRUSH_SELECTION)
		result.panel = {}; // Brush controls live in the bottom toolbar.
	// Independent HUD components reserve their bounds in the overlay layer.
	const double minimapBottom = result.safe.y + (result.safe.h < 400 ? 80 : 104);
	if (result.panel.w > 0 && result.panel.y < minimapBottom)
	{
		const double bottom = result.panel.y + result.panel.h;
		result.panel.y = minimapBottom;
		result.panel.h = std::max(0.0, bottom - minimapBottom);
	}
	for (auto *rect : {&result.safe, &result.status, &result.world, &result.actions, &result.panel})
	{
		rect->x *= unit;
		rect->y *= unit;
		rect->w *= unit;
		rect->h *= unit;
	}
	if (dial && gui.selectionMode != GameGUI::BRUSH_SELECTION)
		result.panel = dialLayout(result).bounds;
	// The compact lens strip replaces the tactical list's drawer.
	if (lensOpen && usesHUD() && !result.persistentPanel && gui.selectionMode == GameGUI::NO_SELECTION &&
		gui.displayMode == GameGUI::STAT_TEXT_VIEW && !globalContainer->isViewingGame() && !peekOpen && !statsOpen)
	{
		const auto rects = lensRects(result);
		double x0 = rects.front().x, y0 = rects.front().y, x1 = x0, y1 = y0;
		for (const auto &r : rects)
		{
			x0 = std::min(x0, r.x);
			y0 = std::min(y0, r.y);
			x1 = std::max(x1, r.x + r.w);
			y1 = std::max(y1, r.y + r.h);
		}
		result.panel = {x0, y0, x1 - x0, y1 - y0};
	}
	return result;
}
double GameGUITouch::tutorialMaximum() const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	return std::max(0.0, tutorialLines.size() * InGameTouchTheme::tutorialPitch() - tutorialRect().h / unit + 16 +
							 (gui.swallowSpaceKey ? 48 : 0));
}
void GameGUITouch::clampScroll()
{
	const auto content = panelContent();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	actionAxis.sync(actionScroll,
					std::max(0.0, buildingActionsHeight(content.w / unit) - content.h / unit),
					content.h / unit);
	const auto ui = layout();
	const int columns = paletteColumns(ui);
	const double height =
		showsBuildPalette()
			? std::ceil(paletteItems().size() / double(columns)) *
					  (paletteRail(ui) ? InGameTouchTheme::paletteCell + InGameTouchTheme::gap : 60) +
				  (paletteRail(ui) ? InGameTouchTheme::gap : 8)
			: inspectingResource() ? 0 : tacticalActions().size() * 56;
	panelAxis.sync(panelScroll, std::max(0.0, height - content.h / unit), content.h / unit);
	tutorialAxis.sync(tutorialScroll, tutorialMaximum(), tutorialRect().h / unit);
}
void GameGUITouch::stopScrolling()
{
	mapMotion.interrupt();
	panelAxis.axis.interrupt();
	actionAxis.axis.interrupt();
	tutorialAxis.axis.interrupt();
}
// A blank-map tap is a dismissal, not navigation back through inspector history.
void GameGUITouch::dismissMapPanels()
{
	const bool hadPanels = panelOpen || lensOpen || statsOpen || peekOpen || showStatistics || restorePalette;
	restorePalette = false;
	panelOpen = lensOpen = statsOpen = peekOpen = showStatistics = false;
	panelScroll = actionScroll = 0;
	keyboardFocus = -1;
	if (hadPanels) lastMapTapTicks.reset();
	gui.clearSelection();
	stopScrolling();
}

bool GameGUITouch::scrollAnimating() const
{
	return mapMotion.isAnimating() || panelAxis.axis.isAnimating() ||
		   actionAxis.axis.isAnimating() || tutorialAxis.axis.isAnimating();
}
Uint64 GameGUITouch::eventTime(const SDL_Event &event) const
{
	return (event.common.timestamp / SDL_NS_PER_MS);
}
void GameGUITouch::advanceScroll(Uint64 now)
{
	lastStepTime = now;
	if (mapMotion.isAnimating())
	{
		const auto [dx, dy] = mapMotion.stepDelta(now);
		if (dx != 0 || dy != 0)
		{
			gui.updateCamera();
			gui.camera.originX += dx / gui.camera.zoom;
			gui.camera.originY += dy / gui.camera.zoom;
			gui.camera.normalize();
			gui.viewportX = gui.camera.tileX();
			gui.viewportY = gui.camera.tileY();
		}
	}
	if (!usesHUD())
		return;
	for (auto *panel : {&panelAxis, &actionAxis, &tutorialAxis})
		if (panel->axis.isAnimating())
			panel->axis.step(now);
	clampScroll();
}
GameGUITouch::~GameGUITouch()
{
	if (!gestureExclusion.empty())
		hostGestureExclusion(globalContainer->gfx, {});
}
void GameGUITouch::syncGestureExclusion()
{
	std::vector<ViewRect> wanted;
	if (usesHUD() && !activeDialog())
	{
		const auto ui = layout();
		if (((showsBuildPalette() && paletteRail(ui)) || (inspecting() && !ui.persistentPanel) ||
			 lensVisible()) &&
			ui.panel.w > 0 && ui.panel.h > 0)
			wanted.push_back(ui.panel);
	}
	auto same = [](const ViewRect &a, const ViewRect &b)
	{ return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; };
	if (wanted.size() == gestureExclusion.size() &&
		std::equal(wanted.begin(), wanted.end(), gestureExclusion.begin(), same))
		return;
	gestureExclusion = std::move(wanted);
	++gestureExclusionUpdates;
	hostGestureExclusion(globalContainer->gfx, gestureExclusion);
}
ViewRect GameGUITouch::world() const
{
	if (usesHUD())
		return layout().world;
	return {0, 16, double(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH),
			double(globalContainer->gfx->getH() - 16)};
}
ViewRect GameGUITouch::controls() const
{
	if (usesHUD())
		return layout().actions;
	auto rect = world();
	rect.h = std::min(rect.h, 48 * globalContainer->gfx->logicalUnitsPerPoint());
	rect.y = globalContainer->gfx->getH() - rect.h;
	return rect;
}
// The phone HUD puts OK on the thumb's side of the bar; the legacy touch
// layout keeps OK on the left.
ViewRect GameGUITouch::confirmRect() const
{
	auto rect = controls();
	rect.w /= 2;
	if (usesHUD() && !ThumbSide::left())
		rect.x += rect.w;
	return rect;
}
ViewRect GameGUITouch::cancelRect() const
{
	auto rect = controls();
	rect.w /= 2;
	if (!(usesHUD() && !ThumbSide::left()))
		rect.x += rect.w;
	return rect;
}
ViewPoint GameGUITouch::previewCursor() const
{
	const auto &map = gui.game.map;
	auto wrap = [](double v, double n) { return v - std::floor(v / n) * n; };
	return {wrap(preview->x - gui.viewportX * 32, map.getW() * 32),
			wrap(preview->y - gui.viewportY * 32, map.getH() * 32)};
}
GAGGUI::ui::UIDialog *GameGUITouch::activeDialog() const
{
	return gui.activeDialog();
}

void GameGUITouch::cancel(bool preservePreview)
{
	if (placement)
	{
		gui.clearSelection();
		panelOpen = true;
	}
	placement.reset();
	placementHold.reset();
	releaseFlagDrag(true); // An interrupted carry returns the flag.
	strokeHold.reset();
	railTouched = -1;
	peekOpen = false;
	minimapPress.reset();
	allocation.reset();
	stroke.cancel();
	commitDeferredStroke(); // A completed tap is not undone by an interruption.
	gui.toolManager.cancelDrag(gui.localTeamNo);
	gesture.cancel();
	stopScrolling();
	lastMapTapTicks.reset();
	fingers.clear();
	ignoreTouchSequence = false;
	confirmDestroy = false;
	if (!preservePreview)
	{
		preview.reset();
		previewType.clear();
	}
	panX = panY = 0;
}

int GameGUITouch::interfaceRegion(ViewPoint point) const
{
	if (gui.inGameMenu || gui.typingInputScreen || gui.scrollableText)
		return 4;
	if (usesHUD() && peekOpen)
	{
		// The map peek owns every touch until it closes.
		if (peekRect().contains(point))
			return 40;
		const auto buttons = peekButtons();
		for (int i = 0; i < int(buttons.size()); ++i)
			if (buttons[i].contains(point))
				return 41 + i;
		return 44;
	}
	if (usesHUD() && statsOpen && !activeDialog())
	{
		const auto stats = statsLayout();
		if (stats.close.contains(point))
			return 45;
		if (stats.previous.contains(point))
			return 46;
		if (stats.next.contains(point))
			return 47;
		if (stats.sheet.contains(point))
			return 48;
	}
	if (gui.selectionMode == GameGUI::BRUSH_SELECTION && controls().contains(point))
		return 9;
	if (usesHUD() && gui.selectionMode == GameGUI::BRUSH_SELECTION &&
		BrushHUD::hit(brushHUD(), point).part != BrushHUD::Part::None)
		return 16;
	if (gui.selectionMode == GameGUI::TOOL_SELECTION && controls().contains(point))
		return confirmRect().contains(point) ? 1 : 2;
	if (usesHUD())
	{
		if (minimapRect().contains(point))
			return 8;
		if (layout().actions.contains(point))
			return 10 + std::min(5, int((point.x - layout().actions.x) / (layout().actions.w / 6)));
		const auto header = allocationRect();
		if (header.contains(point) &&
			point.x >= header.x + header.w - 48 * globalContainer->gfx->logicalUnitsPerPoint())
			return 38;
		// The dial answers only on its rings, chips and header; the map shows
		// (and stays tappable) between them.
		if (inspecting() && usesDial())
		{
			if (header.contains(point) || dialRegionAt(point))
				return 3;
		}
		else if (layout().panel.contains(point))
			return 3;
		if (tutorialRect().contains(point))
			return 7;
		return world().contains(point) ? 0 : 6;
	}
	if (point.x >= world().w)
		return 3;
	if (globalContainer->replaying && point.y >= REPLAY_BAR_Y)
		return 5;
	return world().contains(point) ? 0 : 6;
}

std::vector<ViewRect> GameGUITouch::keyboardTargets()
{
	std::vector<ViewRect> targets;
	if (activeDialog())
		return targets;
	const auto ui = layout();
	for (int i = 0; i < 6; ++i)
		targets.push_back(
			{ui.actions.x + i * ui.actions.w / 6, ui.actions.y, ui.actions.w / 6, ui.actions.h});
	if (gui.selectionMode == GameGUI::TOOL_SELECTION)
	{
		targets = {confirmRect(), cancelRect()};
	}
	if (peekOpen)
	{
		targets = peekButtons();
		targets.insert(targets.begin(), peekRect());
		return targets;
	}
	if (statsOpen)
	{
		const auto stats = statsLayout();
		return {stats.close, stats.previous, stats.next};
	}
	if (lensVisible())
		return lensRects(layout());
	if (gui.selectionMode == GameGUI::BRUSH_SELECTION)
	{
		targets = brushBarButtons();
		const auto rail = brushHUD();
		targets.insert(targets.end(), rail.detents.begin(), rail.detents.end());
		for (const auto &r : {rail.mode, rail.pan, rail.undo})
			if (r.w > 0)
				targets.push_back(r);
		return targets;
	}
	if (inspectingResource())
	{
		targets.push_back(resourceCloseRect());
		return targets;
	}
	const auto content = panelContent();
	if (showsBuildPalette())
	{
		for (size_t i = 0; i < paletteItems().size(); ++i)
		{
			const auto rect = paletteItemRect(i);
			if (content.contains({rect.x + rect.w / 2, rect.y + rect.h / 2}))
				targets.push_back(rect);
		}
	}
	else if (inspecting() && usesDial())
	{
		for (const auto &region : dialRegions())
			if (region.part != DialRegion::Arc && region.part != DialRegion::Proportions)
				targets.push_back(region.box);
	}
	else if (inspecting())
	{
		for (size_t i = 0; i < buildingActions().size(); ++i)
		{
			const auto rect = buildingActionRect(i);
			if (rect.y >= content.y && rect.y + rect.h <= content.y + content.h)
				targets.push_back(rect);
		}
	}
	return targets;
}

bool GameGUITouch::process(SDL_Event &event)
{
	if (dispatching)
		return false;
	if (usesHUD() && event.type == SDL_EVENT_KEY_DOWN)
	{
		const auto key = event.key.key;
		if (key == SDLK_TAB)
		{
			const auto targets = keyboardTargets();
			if (!targets.empty())
				keyboardFocus = (keyboardFocus + ((event.key.mod & SDL_KMOD_SHIFT) ? -1 : 1) +
								 int(targets.size())) %
								int(targets.size());
			return true;
		}
		if ((key == SDLK_RETURN || key == SDLK_SPACE) && keyboardFocus >= 0 &&
			!gui.typingInputScreen && !event.key.repeat)
		{
			const auto targets = keyboardTargets();
			if (keyboardFocus < int(targets.size()))
			{
				const auto rect = targets[keyboardFocus];
				SDL_Event pointer{};
				pointer.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
				pointer.button.button = SDL_BUTTON_LEFT;
				pointer.button.x = int(rect.x + rect.w / 2);
				pointer.button.y = int(rect.y + rect.h / 2);
				process(pointer);
				pointer.type = SDL_EVENT_MOUSE_BUTTON_UP;
				process(pointer);
			}
			return true;
		}
		if (key == SDLK_PAGEDOWN || key == SDLK_PAGEUP)
		{
			const double delta = key == SDLK_PAGEDOWN ? 144 : -144;
			panelScroll += delta * paletteScrollSign();
			actionScroll += delta;
			clampScroll();
			return true;
		}
	}
	if ((event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
		((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
		 event.button.which == SDL_TOUCH_MOUSEID))
		return true;
	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && swallowMouseRelease)
	{
		swallowMouseRelease = false;
		return true;
	}
	if (usesHUD() && event.type == SDL_EVENT_MOUSE_WHEEL)
	{
		float x, y;
		SDL_GetMouseState(&x, &y);
		GraphicContext::translateMouseCoordinates(x, y);
		const int region = interfaceRegion({double(x), double(y)});
		const double delta =
			event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -48 : 48);
		if (region == 3)
		{
			if (inspecting())
				actionScroll -= delta;
			else
				panelScroll -= delta * paletteScrollSign();
			clampScroll();
			return true;
		}
	}
	if (usesHUD() && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
					  event.type == SDL_EVENT_MOUSE_MOTION))
	{
		if (event.type != SDL_EVENT_MOUSE_MOTION && event.button.button != SDL_BUTTON_LEFT)
			return false;
		const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
		if (motion && !(event.motion.state & SDL_BUTTON_LMASK))
			return false;
		SDL_Event pointer{};
		pointer.type = motion                              ? SDL_EVENT_FINGER_MOTION
					   : event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? SDL_EVENT_FINGER_DOWN
														   : SDL_EVENT_FINGER_UP;
		pointer.tfinger.timestamp = event.common.timestamp;
		pointer.tfinger.touchID = -1;
		pointer.tfinger.fingerID = 0;
		pointer.tfinger.x =
			float(motion ? event.motion.x : event.button.x) / globalContainer->gfx->getW();
		pointer.tfinger.y =
			float(motion ? event.motion.y : event.button.y) / globalContainer->gfx->getH();
		return process(pointer);
	}
	if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
										  event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED))
		cancel();
	if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_UP &&
		event.type != SDL_EVENT_FINGER_MOTION)
		return false;
	const ViewPoint pointerPoint{event.tfinger.x * globalContainer->gfx->getW(),
								 event.tfinger.y * globalContainer->gfx->getH()};
	if (usesHUD() && gui.inputState.hasFocus() && !ignoreTouchSequence &&
		processAllocationPointer(event, pointerPoint))
		return true;
	if (usesHUD() && gui.inputState.hasFocus() && !ignoreTouchSequence &&
		processPalettePointer(event, pointerPoint))
		return true;
	if (!gui.inputState.hasFocus())
		return true;
	if (ignoreTouchSequence)
	{
		const auto key = std::make_pair(event.tfinger.touchID, event.tfinger.fingerID);
		if (event.type == SDL_EVENT_FINGER_DOWN &&
			std::find(fingers.begin(), fingers.end(), key) == fingers.end())
			fingers.push_back(key);
		if (event.type == SDL_EVENT_FINGER_UP)
			std::erase(fingers, key);
		if (fingers.empty())
			ignoreTouchSequence = false;
		return true;
	}
	gui.checkSelection();
	if (!fingers.empty() &&
		(ownerSelection != gui.selectionMode || ownerMenu != gui.inGameMenu ||
		 ownerBuilding != (gui.selectionMode == GameGUI::BUILDING_SELECTION
							   ? gui.selectionBuilding()
							   : nullptr) ||
		 ownerDialog != activeDialog() || ownerTool != gui.toolManager.getBuildingName() ||
		 ownerOverlay != bool(gui.typingInputScreen || gui.scrollableText)))
	{
		cancel();
		return true;
	}
	// Input validates against the live building (authoritative state).
	Building *held = inspecting() ? gui.selectionBuilding() : nullptr;
	if (!fingers.empty() && held &&
		(heldBuildingState != held->buildingState ||
		 heldConstructionState != held->constructionResultState))
	{
		cancel();
		return true;
	}
	ViewPoint point{event.tfinger.x * globalContainer->gfx->getW(),
					event.tfinger.y * globalContainer->gfx->getH()};
	const auto key = std::make_pair(event.tfinger.touchID, event.tfinger.fingerID);
	const Uint64 time = eventTime(event);
	if (event.type == SDL_EVENT_FINGER_DOWN)
	{
		// A second finger while a flag is carried puts the flag back and ignores the
		// rest of the touch. Before the flag moved, the touch becomes a pinch.
		if (!fingers.empty() && flagDrag)
		{
			if (flagDrag->dragging)
			{
				releaseFlagDrag(true);
				if (std::find(fingers.begin(), fingers.end(), key) == fingers.end())
					fingers.push_back(key);
				ignoreTouchSequence = true;
				return true;
			}
			flagDrag.reset();
		}
		if (fingers.empty())
		{
			touchActive = true;
			// A touch catches coasting content where it is. Presets are re-read
			// here so the settings sliders apply to the next gesture.
			stopScrolling();
			fingerIsTouch = event.tfinger.touchID != -1;
			GAGCore::ScrollPhysicsConfig mapConfig = ScrollPresets::mapViewport();
			mapConfig.momentum = mapConfig.momentum && fingerIsTouch;
			mapMotion.setConfig(mapConfig);
			mapDragTravel = {};
			mapFlingArmed = false;
			const auto hud = fingerIsTouch ? ScrollPresets::hudPanel() : ScrollPresets::mouse();
			panelAxis.axis.setConfig(hud);
			actionAxis.axis.setConfig(hud);
			tutorialAxis.axis.setConfig(hud);
			gui.viewportSpeedX = gui.viewportSpeedY = 0;
			gui.lastMouseButtonState = 0;
			gui.selectionPushed = gui.panPushed = gui.miniMapPushed = false;
			scale = globalContainer->gfx->logicalUnitsPerPoint();
			ownerRegion = interfaceRegion(point);
			heldActionKind = -1;
			heldActionConfirmation = confirmDestroy;
			if (Building *building = inspecting() ? gui.selectionBuilding() : nullptr)
			{
				heldBuildingState = building->buildingState;
				heldConstructionState = building->constructionResultState;
				if (ownerRegion == 3)
					if (const auto row = actionAt(point))
					{
						heldActionKind = row->kind;
						heldActionValue = row->value;
						heldActionLabel = row->label;
					}
			}
			interfaceGesture = ownerRegion != 0;
			ownerSelection = gui.selectionMode;
			ownerMenu = gui.inGameMenu;
			ownerBuilding = gui.selectionMode == GameGUI::BUILDING_SELECTION
								? gui.selectionBuilding()
								: nullptr;
			ownerDialog = activeDialog();
			ownerOverlay = gui.typingInputScreen || gui.scrollableText;
			ownerTool = gui.toolManager.getBuildingName();
			touchStart = touchPoint = point;
			touchTravelled = false;
			const TouchMode natural = interfaceGesture                                ? TouchMode::Navigate
									  : gui.selectionMode == GameGUI::TOOL_SELECTION  ? TouchMode::Placement
									  : gui.selectionMode == GameGUI::BRUSH_SELECTION && !brushPan ? TouchMode::Paint
																					  : TouchMode::Navigate;
			// A contact on or near one of the player's flags carries the flag rather
			// than the map, even straight after a tap.
			Building *grabbed = !interfaceGesture && natural == TouchMode::Navigate ? grabbableFlag(point) : nullptr;
			if (grabbed)
				beginFlagDrag(*grabbed, key, point);
			else
				flagDrag.reset();
			// The second contact of a double-tap zooms; any other contact first
			// lets a waiting paint tap land, so nothing reorders the player's input.
			const bool zoomDrag = !interfaceGesture && !grabbed && natural != TouchMode::Placement &&
								  zoomTapArmed(event.tfinger.timestamp / SDL_NS_PER_MS, point);
			if (zoomDrag)
				deferredStroke.reset(); // That tap was the first half of the zoom.
			else
				commitDeferredStroke();
			lastMapTapTicks.reset();
			gesture.setMode(zoomDrag ? TouchMode::ZoomDrag : natural);
			gesture.setZoomDragDirection(globalContainer->settings.dragUpZoomsIn());
			// Touching a size on the brush rail selects it at once, so its magnified
			// preview follows the thumb while it scrubs along the rail.
			railTouched = -1;
			if (ownerRegion == 16)
			{
				const auto hit = BrushHUD::hit(brushHUD(), point);
				if (hit.part == BrushHUD::Part::Detent)
				{
					gui.brush.setFigure(unsigned(hit.index));
					railTouched = hit.index;
				}
			}
			statsDrag = 0;
			// A still press on the minimap opens the map peek (see prepareDraw).
			if (ownerRegion == 8)
				minimapPress = SDL_GetTicks();
			else
				minimapPress.reset();
			if (natural == TouchMode::Paint && !zoomDrag && ownerRegion == 0)
				strokeHold = TouchPlacementSession{key, point, {}, true, {}, point, SDL_GetTicks(), gui.localTeamNo};
			else
				strokeHold.reset();
		}
		else
			strokeHold.reset(); // A second finger navigates instead of painting.
		if (fingers.empty() && ownerRegion == 0 && gui.selectionMode == GameGUI::TOOL_SELECTION)
			placementHold = TouchPlacementSession{key, point, gui.toolManager.getBuildingName(),
				true, {}, point, SDL_GetTicks(), gui.localTeamNo};
		else
			placementHold.reset(); // A navigation gesture cannot resume edge panning.
		if (std::find(fingers.begin(), fingers.end(), key) == fingers.end())
			fingers.push_back(key);
		actions(gesture.down(key.first, key.second, {point.x / scale, point.y / scale}, time));
	}
	else if (event.type == SDL_EVENT_FINGER_MOTION)
	{
		if (flagDrag && flagDrag->pointer == key)
		{
			flagDrag->position = point;
			if (!flagDrag->dragging &&
				std::hypot(point.x - flagDrag->start.x, point.y - flagDrag->start.y) >= TouchInput::slop * scale)
			{
				// Past the tap threshold the finger carries the flag; the map stays put.
				flagDrag->dragging = true;
				gesture.cancel();
				stopScrolling();
				lastMapTapTicks.reset();
				minimapPress.reset();
			}
			if (flagDrag->dragging)
			{
				advanceFlagDrag();
				return true;
			}
		}
		if (placementHold && placementHold->pointer == key)
			placementHold->pointerPosition = point;
		if (strokeHold && strokeHold->pointer == key)
			strokeHold->pointerPosition = point;
		if (ownerRegion == 16 && railTouched >= 0 && fingers.size() == 1 && fingers.front() == key)
			if (const int detent = BrushHUD::detentAt(brushHUD(), point); detent >= 0)
			{
				gui.brush.setFigure(unsigned(detent));
				railTouched = detent;
			}
		// A single finger that landed on the minimap keeps steering the camera,
		// as a held mouse button does on desktop. Leaving the minimap clamps to
		// its edge so a fast sweep never drops the drag.
		if (usesHUD() && ownerRegion == 8 && fingers.size() == 1 && fingers.front() == key)
			navigateMinimap(minimapRect().clamp(point));
		if (usesHUD() && ownerRegion == 40 && peekOpen && fingers.size() == 1 && fingers.front() == key)
			navigatePeek(point);
		if (!fingers.empty() && fingers.front() == key)
		{
			touchPoint = point;
			touchTravelled = touchTravelled || std::hypot(point.x - touchStart.x, point.y - touchStart.y) >=
													TouchInput::slop * scale;
		}
		actions(gesture.move(key.first, key.second, {point.x / scale, point.y / scale}, time));
	}
	else
	{
		if (flagDrag && flagDrag->pointer == key)
		{
			if (flagDrag->dragging)
			{
				flagDrag->position = point;
				advanceFlagDrag();
				releaseFlagDrag(false);
				std::erase(fingers, key);
				lastMapTapTicks.reset();
				return true;
			}
			flagDrag.reset(); // A tap selects the flag as before.
		}
		if (placementHold && placementHold->pointer == key)
			placementHold.reset();
		if (strokeHold && strokeHold->pointer == key)
			strokeHold.reset();
		if (fingers.size() <= 1)
		{
			railTouched = -1;
			minimapPress.reset();
		}
		auto changes = gesture.up(key.first, key.second, {point.x / scale, point.y / scale}, time);
		// A completed tap on the world arms one-finger zoom for the next contact.
		const bool worldTap = changes.size() == 1 && !interfaceGesture && world().contains(point) &&
							  !controls().contains(point);
		const bool paintTap = worldTap && changes.front().kind == TouchActionKind::EndStroke &&
							  !touchTravelled && !stroke.points.empty() && strokeMatchesTool(stroke) &&
							  interfaceRegion(point) == 0;
		if (paintTap)
		{
			// Hold a painted tap for one double-tap window; see commitDeferredStroke.
			deferredStroke = TouchDeferredStroke{stroke, SDL_GetTicks()};
			stroke.cancel();
			changes.clear();
		}
		if (paintTap || (worldTap && changes.front().kind == TouchActionKind::Select))
		{
			lastMapTapTicks = event.tfinger.timestamp / SDL_NS_PER_MS;
			lastMapTapPoint = point;
		}
		else
			lastMapTapTicks.reset();
		actions(changes);
		std::erase(fingers, key);
		if (fingers.empty())
		{
			// A touch that stopped a bounce without dragging lets it finish.
			for (auto *panel : {&panelAxis, &actionAxis, &tutorialAxis})
				panel->axis.settle(time);
			if (usesHUD())
				clampScroll();
		}
	}
	return true;
}

void GameGUITouch::actions(const std::vector<TouchAction> &changes)
{
	for (const auto &action : changes)
	{
		const ViewPoint point{action.point.x * scale, action.point.y * scale};
		if (action.kind == TouchActionKind::Cancel)
		{
			placementHold.reset();
			strokeHold.reset();
			stroke.cancel();
			preview.reset();
			gui.toolManager.cancelDrag(gui.localTeamNo);
			stopScrolling();
			continue;
		}
		if (interfaceGesture)
		{
			if (usesHUD() && (ownerRegion == 7 || ownerRegion == 3) &&
				(action.kind == TouchActionKind::Pan || action.kind == TouchActionKind::PanEnd))
			{
				const double unit = globalContainer->gfx->logicalUnitsPerPoint();
				clampScroll();
				auto &panel = ownerRegion == 7 ? tutorialAxis : inspecting() ? actionAxis : panelAxis;
				if (action.kind == TouchActionKind::Pan)
					// The thumb rail fills from the bottom, so its content follows the
					// finger with the opposite sign.
					panel.axis.drag(action.time, -point.y / unit *
													 (&panel == &panelAxis ? paletteScrollSign() : 1));
				else
					panel.axis.endDrag(action.time);
				clampScroll();
			}
			if (usesHUD() && ownerRegion == 48 && action.kind == TouchActionKind::Pan)
			{
				// Pulling the statistics sheet down closes it.
				statsDrag += point.y / globalContainer->gfx->logicalUnitsPerPoint();
				if (statsDrag > InGameTouchTheme::target)
					statsOpen = false;
			}
			if (action.kind == TouchActionKind::Select && interfaceRegion(point) == ownerRegion)
				interfaceTap(point);
			continue;
		}
		if (action.kind == TouchActionKind::Pan)
		{
			lastMapTapTicks.reset();
			gui.updateCamera();
			gui.camera.originX -= point.x / gui.camera.zoom;
			gui.camera.originY -= point.y / gui.camera.zoom;
			gui.camera.normalize();
			const int oldX = gui.viewportX, oldY = gui.viewportY;
			gui.viewportX = gui.camera.tileX();
			gui.viewportY = gui.camera.tileY();
			gui.viewportChanged(oldX, gui.viewportX, oldY, gui.viewportY);
			// Accumulate displacement in screen points, not world or raster pixels.
			// Back-and-forth jitter inside the dead zone cannot arm a fling.
			mapDragTravel.x += action.point.x;
			mapDragTravel.y += action.point.y;
			mapFlingArmed |= std::hypot(mapDragTravel.x, mapDragTravel.y) >= InGameTouchTheme::mapFlingTravelPoints;
			// The same finger motion feeds the release velocity, in logical pixels.
			if (!mapMotion.isDragging())
				mapMotion.beginDrag(action.time);
			mapMotion.drag(action.time, -point.x, -point.y);
		}
		else if (action.kind == TouchActionKind::PanEnd)
		{
			mapMotion.endDrag(action.time);
			if (!mapFlingArmed)
				mapMotion.interrupt();
		}
		else if (action.kind == TouchActionKind::Zoom)
		{
			lastMapTapTicks.reset();
			if (action.factor > 0)
				gui.zoomMap(std::log(action.factor) / std::log(1.1), int(point.x), int(point.y));
		}
		else if (action.kind == TouchActionKind::DoubleTap)
		{
			// Without a zoomable renderer the second tap still selects, as before.
			if (!zoomIn(point) && world().contains(point))
				select(point);
		}
		else if (action.kind == TouchActionKind::Preview && world().contains(point) &&
				 !controls().contains(point))
		{
			preview = ViewPoint{double(gui.mapMouseX(point.x) + gui.viewportX * 32),
								double(gui.mapMouseY(point.y) + gui.viewportY * 32)};
			previewType = gui.toolManager.getBuildingName();
			prepareDraw();
		}
		else if (action.kind == TouchActionKind::Select && world().contains(point))
		{
			select(point);
		}
		else if (!globalContainer->isViewingGame() && gui.selectionMode == GameGUI::BRUSH_SELECTION)
		{
			if (action.kind == TouchActionKind::BeginStroke)
			{
				stroke.cancel();
				stroke.team = gui.localTeamNo;
				stroke.zone = gui.toolManager.getZoneType();
				stroke.figure = gui.brush.getFigure();
				stroke.mode = gui.brush.getType();
			}
			if (stroke.team != gui.localTeamNo || stroke.zone != gui.toolManager.getZoneType() ||
				stroke.figure != int(gui.brush.getFigure()) ||
				stroke.mode != int(gui.brush.getType()))
			{
				stroke.cancel();
				continue;
			}
			if ((action.kind == TouchActionKind::BeginStroke ||
				 action.kind == TouchActionKind::Stroke) &&
				interfaceRegion(point) == 0)
				stroke.points.push_back({double(gui.mapMouseX(point.x) + gui.viewportX * 32),
										 double(gui.mapMouseY(point.y) + gui.viewportY * 32)});
			else if (action.kind == TouchActionKind::EndStroke)
			{
				if (interfaceRegion(point) == 0 && !stroke.points.empty())
					replayStroke(stroke);
				stroke.cancel();
			}
		}
	}
}

bool GameGUITouch::zoomTapArmed(Uint32 ticks, ViewPoint point) const
{
	return lastMapTapTicks && Uint32(ticks - *lastMapTapTicks) <= InGameTouchTheme::doubleTapWindowMs &&
		   std::hypot(point.x - lastMapTapPoint.x, point.y - lastMapTapPoint.y) <=
			   InGameTouchTheme::doubleTapRadius * scale &&
		   world().contains(point) && !controls().contains(point);
}

bool GameGUITouch::zoomIn(ViewPoint point)
{
	return gui.zoomMap(std::log(InGameTouchTheme::doubleTapZoomFactor) / std::log(1.1), int(point.x), int(point.y));
}

std::string GameGUITouch::zoomReadout() const
{
	char value[16];
	std::snprintf(value, sizeof(value), "%.1f", gui.camera.zoom);
	return FormattableString(Toolkit::getStringTable()->getString("[zoom factor %0]")).arg(value);
}

bool GameGUITouch::strokeMatchesTool(const TouchStrokeSession &candidate) const
{
	return gui.selectionMode == GameGUI::BRUSH_SELECTION && !globalContainer->isViewingGame() &&
		   candidate.team == gui.localTeamNo && candidate.zone == gui.toolManager.getZoneType() &&
		   candidate.figure == int(gui.brush.getFigure()) && candidate.mode == int(gui.brush.getType());
}

void GameGUITouch::replayStroke(const TouchStrokeSession &completed)
{
	// Record which displayed cells this stroke changes, so undo can revert
	// exactly those and nothing that was already painted before it.
	auto &map = gui.game.map;
	const bool adding = completed.mode == BrushTool::MODE_ADD;
	std::vector<BrushCoverage::Cell> centres;
	for (const auto &p : completed.points)
		centres.push_back({(int(p.x) >> 5) & map.getMaskW(), (int(p.y) >> 5) & map.getMaskH()});
	std::set<BrushCoverage::Cell> changed;
	for (const auto &[cx, cy] : BrushCoverage::cells(completed.figure, centres))
	{
		const int x = cx & map.getMaskW(), y = cy & map.getMaskH();
		const bool before = completed.zone == GameGUIToolManager::Forbidden ? map.isForbiddenInDisplayedView(x, y)
							: completed.zone == GameGUIToolManager::Guard	? map.isGuardAreaInDisplayedView(x, y)
																			: map.isClearAreaInDisplayedView(x, y);
		if (before != adding)
			changed.insert({x, y});
	}
	for (size_t i = 0; i < completed.points.size(); ++i)
	{
		const auto p = completed.points[i];
		if (i == 0)
			gui.toolManager.handleMouseDown(int(p.x), int(p.y), gui.localTeamNo, 0, 0);
		else
			gui.toolManager.handleMouseDrag(int(p.x), int(p.y), gui.localTeamNo, 0, 0);
	}
	gui.toolManager.finishPointerGesture(gui.localTeamNo);
	zoneUndo.reset();
	if (changed.empty())
		return;
	// Inverse orders in map-aligned blocks of at most 32x32 cells.
	ZoneUndo undo;
	undo.zone = completed.zone;
	undo.expires = SDL_GetTicks() + InGameTouchTheme::brushUndoMs;
	std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> blocks;
	for (const auto &[x, y] : changed)
	{
		blocks[{x / 32, y / 32}].push_back({x, y});
		undo.displayed.push_back({size_t(map.coordToIndex(x, y)), !adding});
	}
	const Uint8 inverse = adding ? BrushTool::MODE_DEL : BrushTool::MODE_ADD;
	for (const auto &[block, cells] : blocks)
	{
		const int left = block.first * 32, top = block.second * 32;
		const int width = std::min(32, map.getW() - left), height = std::min(32, map.getH() - top);
		Utilities::BitArray mask(size_t(width * height));
		for (const auto &[x, y] : cells)
			mask.set(size_t((y - top) * width + (x - left)), true);
		const Uint8 team = Uint8(gui.localTeamNo);
		if (completed.zone == GameGUIToolManager::Forbidden)
			undo.orders.push_back(std::make_shared<OrderAlterForbidden>(team, inverse, left, top, width, height, mask));
		else if (completed.zone == GameGUIToolManager::Guard)
			undo.orders.push_back(std::make_shared<OrderAlterGuardArea>(team, inverse, left, top, width, height, mask));
		else
			undo.orders.push_back(std::make_shared<OrderAlterClearArea>(team, inverse, left, top, width, height, mask));
	}
	zoneUndo = std::move(undo);
}

// The last stroke's changes are reverted by inverse orders sent after its own,
// and the displayed zones return to what the player saw before it.
void GameGUITouch::applyZoneUndo()
{
	if (!zoneUndo || globalContainer->isViewingGame())
		return;
	while (auto pending = gui.toolManager.getOrder())
		gui.orderQueue.push_back(pending);
	for (const auto &order : zoneUndo->orders)
		gui.orderQueue.push_back(order);
	auto &map = gui.game.map;
	auto &view = zoneUndo->zone == GameGUIToolManager::Forbidden ? map.displayedForbiddenView
				 : zoneUndo->zone == GameGUIToolManager::Guard	 ? map.displayedGuardAreaView
																 : map.displayedClearAreaView;
	for (const auto &[index, value] : zoneUndo->displayed)
		view.set(index, value);
	zoneUndo.reset();
}

BrushHUD::Layout GameGUITouch::brushHUD() const
{
	const auto ui = layout();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const auto mini = minimapRect();
	const double top = mini.y + mini.h + 8 * unit, inset = InGameTouchTheme::railInset * unit;
	return BrushHUD::layout({ui.safe.x + inset, top, ui.safe.w - 2 * inset, ui.actions.y - 8 * unit - top},
							ThumbSide::toolboxLeft(), unit, true, true, bool(zoneUndo));
}

// Zone choices then Done, with Done under the thumb.
std::vector<ViewRect> GameGUITouch::brushBarButtons() const
{
	const auto rect = controls();
	std::vector<ViewRect> buttons;
	for (int i = 0; i < 4; ++i)
	{
		const int slot = ThumbSide::left() ? 3 - i : i;
		buttons.push_back({rect.x + slot * rect.w / 4, rect.y, rect.w / 4, rect.h});
	}
	return buttons;
}

// A held paint tap lands when its double-tap window closes, when any other
// contact begins, or when input is interrupted. It is dropped only if the
// brush it was painted with is no longer the active tool.
void GameGUITouch::commitDeferredStroke()
{
	if (!deferredStroke)
		return;
	const auto held = std::move(deferredStroke->stroke);
	deferredStroke.reset();
	if (!held.points.empty() && strokeMatchesTool(held))
		replayStroke(held);
}

void GameGUITouch::interfaceTap(ViewPoint point)
{
	if (activeDialog())
		return;
	if (usesHUD() && statsOpen && !peekOpen)
	{
		const int region = interfaceRegion(point);
		if (region >= 45 && region <= 48)
		{
			if (region == 45)
				statsOpen = false;
			else if (region != 48)
				statsMetric = (statsMetric + (region == 47 ? 1 : EndOfGameStat::TYPE_NB_STATS - 1)) %
							  EndOfGameStat::TYPE_NB_STATS;
			return;
		}
	}
	if (usesHUD() && peekOpen)
	{
		const int region = interfaceRegion(point);
		if (region == 40)
			navigatePeek(point);
		else if (region == 42 || region == 43)
		{
			const auto area = world();
			gui.updateCamera();
			gui.zoomMap((region == 43 ? 1 : -1) * std::log(InGameTouchTheme::peekZoomStep) / std::log(1.1),
						int(area.x + area.w / 2), int(area.y + area.h / 2));
		}
		else if (region == 44)
			dismissMapPanels(); // Outside tap closes the whole transient UI.
		else
			peekOpen = false; // Explicit Done returns to the tools.
		return;
	}
	if (usesHUD() && interfaceRegion(point) == 38)
	{
		gui.clearSelection();
		panelOpen = previousPanelOpen;
		prepareDraw();
		return;
	}
	if (usesHUD() && minimapRect().contains(point))
	{
		navigateMinimap(point);
		return;
	}
	if (usesHUD() && gui.selectionMode == GameGUI::BRUSH_SELECTION && controls().contains(point))
	{
		const auto buttons = brushBarButtons();
		stroke.cancel();
		for (int button = 0; button < int(buttons.size()); ++button)
			if (buttons[button].contains(point))
			{
				if (button < 3)
					gui.toolManager.activateZoneTool(static_cast<GameGUIToolManager::ZoneType>(button));
				else
				{
					gui.clearSelection();
					panelOpen = true;
				}
			}
		return;
	}
	if (usesHUD() && gui.selectionMode == GameGUI::BRUSH_SELECTION && interfaceRegion(point) == 16)
	{
		const auto hit = BrushHUD::hit(brushHUD(), point);
		stroke.cancel();
		if (hit.part == BrushHUD::Part::Mode)
			gui.brush.setType(gui.brush.getType() == BrushTool::MODE_ADD ? BrushTool::MODE_DEL
																		 : BrushTool::MODE_ADD);
		else if (hit.part == BrushHUD::Part::Pan)
			brushPan = !brushPan;
		else if (hit.part == BrushHUD::Part::Detent)
			gui.brush.setFigure(unsigned(hit.index));
		else if (hit.part == BrushHUD::Part::Undo)
			applyZoneUndo();
		return;
	}
	if (!gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText &&
		gui.selectionMode == GameGUI::TOOL_SELECTION && controls().contains(point))
	{
		if (confirmRect().contains(point))
		{
			if (commitPlacement())
			{
				preview.reset();
				gui.clearSelection();
			}
		}
		else
		{
			preview.reset();
			gui.clearSelection();
		}
		return;
	}
	if (usesHUD() && !gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText &&
		tutorialRect().contains(point) && !layout().panel.contains(point))
	{
		if (tutorialCollapsed)
		{
			tutorialCollapsed = false;
			return;
		}
		const auto tutorial = tutorialRect();
		const double target = 48 * globalContainer->gfx->logicalUnitsPerPoint();
		if (point.x >= tutorial.x + tutorial.w - target && point.y < tutorial.y + target)
		{
			tutorialCollapsed = true;
			return;
		}
		if (point.y < tutorial.y + tutorial.h - 48 * globalContainer->gfx->logicalUnitsPerPoint())
			return;
		if (!gui.swallowSpaceKey)
		{
			return;
		}
		if (gui.swallowSpaceKey)
		{
			SDL_KeyboardEvent key{};
			key.key = SDLK_SPACE;
			gui.handleKey(key, true);
		}
		return;
	}
	const bool hudInput =
		usesHUD() && !gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText;
	if (hudInput && layout().actions.contains(point))
	{
		const int button =
			std::min(5, int((point.x - layout().actions.x) / (layout().actions.w / 6)));
		if (button == 2)
			showStatistics = false;
		if (globalContainer->replaying && button < 2)
		{
			if (button == 0)
				gui.gamePaused = !gui.gamePaused;
			else
			{
				globalContainer->replayFastForward = !globalContainer->replayFastForward;
				gui.gamePaused = false;
			}
			return;
		}
		if (button < 3)
		{
			if ((button == 0 && (gui.hiddenGUIElements & GameGUI::HIDABLE_BUILDINGS_LIST)) ||
				(button == 1 && (gui.hiddenGUIElements & GameGUI::HIDABLE_FLAGS_LIST)))
				return;
			if (inspectingResource())
			{
				// An explicit toolbox choice replaces resource inspection; do not
				// let the deferred inspector restoration override that choice.
				gui.clearSelection();
				restorePalette = false;
			}
			if (button < 2)
			{
				lensOpen = false;
				statsOpen = false;
				const auto mode = button == 0 ? GameGUI::CONSTRUCTION_VIEW : GameGUI::FLAG_VIEW;
				panelOpen = !(panelOpen && gui.displayMode == mode &&
							  gui.selectionMode == GameGUI::NO_SELECTION);
				gui.clearSelection();
				gui.displayMode = mode;
			}
			else if (gui.selectionMode == GameGUI::NO_SELECTION && !globalContainer->isViewingGame() &&
					 !layout().persistentPanel)
			{
				// Compact: Tools opens the lens strip opposite the thumb corner.
				lensOpen = !(lensOpen && !panelOpen && gui.displayMode == GameGUI::STAT_TEXT_VIEW);
				panelOpen = false;
				gui.displayMode = GameGUI::STAT_TEXT_VIEW;
				gui.replayDisplayMode = GameGUI::RDM_STAT_TEXT_VIEW;
			}
			else
			{
				panelOpen = !panelOpen || (gui.selectionMode == GameGUI::NO_SELECTION &&
										   gui.displayMode != GameGUI::STAT_TEXT_VIEW);
				if (gui.selectionMode == GameGUI::NO_SELECTION)
				{
					gui.displayMode = GameGUI::STAT_TEXT_VIEW;
					gui.replayDisplayMode = GameGUI::RDM_STAT_TEXT_VIEW;
				}
			}
			panelScroll = 0;
			clampScroll();
			return;
		}
		if (button == 4 && globalContainer->isViewingGame())
		{
			gui.clearSelection();
			gui.displayMode = GameGUI::STAT_TEXT_VIEW;
			panelOpen = true;
			panelScroll = 0;
			return;
		}
		// Touch opens dialogs by intent, never by synthesizing desktop pixels.
		if (button == 3)
			gui.openDialog(GameGUI::IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(&gui, false));
		else if (button == 4)
			gui.openDialog(GameGUI::IGM_ALLIANCE, std::make_unique<InGameAllianceScreen>(&gui));
		else
			gui.openMainMenu();
		return;
	}
	else if (hudInput && layout().panel.contains(point))
	{
		if (lensVisible())
		{
			const auto items = lenses();
			const auto rects = lensRects(layout());
			for (size_t i = 0; i < items.size() && i < rects.size(); ++i)
				if (rects[i].contains(point))
					menuAction(items[i].action);
			return;
		}
		if (inspectingResource())
		{
			if (resourceCloseRect().contains(point)) gui.clearSelection();
			return; // Resource readouts never dispatch tactical actions.
		}
		if (showsBuildPalette())
			tapBuildPalette(point);
		else if (inspecting())
			tapBuildingAction(point);
		else if (gui.selectionMode == GameGUI::BRUSH_SELECTION)
			return; // The brush toolbar owns painting commands, not this sidebar.
		else
		{
			const auto panel = layout().panel;
			const double unit = globalContainer->gfx->logicalUnitsPerPoint();
			const int index = int((point.y - panel.y) / unit + panelScroll) / 56;
			const auto actions = tacticalActions();
			if (index >= 0 && index < int(actions.size()))
				menuAction(actions[index].second);
		}
		return;
	}
	if (usesHUD())
		return;
	// Some shared panel hit tests read the current cursor instead of the event.
	gui.mouseX = gui.view.mouseX = gui.lastMouseX = int(point.x);
	gui.mouseY = gui.view.mouseY = gui.lastMouseY = int(point.y);
	dispatching = true;
	SDL_Event click{};
	click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	click.button.button = SDL_BUTTON_LEFT;
	click.button.x = int(point.x);
	click.button.y = int(point.y);
	gui.processEvent(&click);
	click.type = SDL_EVENT_MOUSE_BUTTON_UP;
	gui.processEvent(&click);
	dispatching = false;
	if (usesHUD() && (gui.selectionMode == GameGUI::TOOL_SELECTION ||
					  gui.selectionMode == GameGUI::BRUSH_SELECTION))
		panelOpen = false;
	gui.lastMouseButtonState = 0;
	gui.selectionPushed = gui.panPushed = gui.miniMapPushed = false;
}

// The visible unit drawn under a screen point, matching draw order: ground units
// first, then flying units, using their interpolated rectangles.
Unit *GameGUITouch::unitAt(ViewPoint screenPoint) const
{
	const ViewPoint point{double(gui.mapMouseX(int(screenPoint.x))), double(gui.mapMouseY(int(screenPoint.y)))};
	Unit *found = nullptr;
	const auto &map = gui.game.map;
	const int mx = int(point.x) / 32 + gui.viewportX, my = int(point.y) / 32 + gui.viewportY;
	const Uint32 visible =
		globalContainer->replaying ? globalContainer->replayVisibleTeams : gui.localTeam->me;
	const bool wholeMap = globalContainer->replaying && !globalContainer->replayShowFog;
	// Match draw order: ground units first, then flying units, using their interpolated rectangles.
	for (bool air : {false, true})
		for (int y = my - 1; y <= my + 1; ++y)
			for (int x = mx - 1; x <= mx + 1; ++x)
			{
				const Uint16 gid = air ? map.getAirUnit(x, y) : map.getGroundUnit(x, y);
				if (gid == NOGUID)
					continue;
				auto *unit = gui.game.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
				if (!unit)
					continue;
				if (!wholeMap && !map.isFOWDiscovered(x, y, visible) &&
					!map.isFOWDiscovered(x - unit->dx, y - unit->dy, visible))
					continue;
				int px, py;
				map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, gui.viewportX,
										 gui.viewportY);
				if (unit->action < BUILD)
				{
					px -= (unit->dx * (255 - unit->delta)) >> 3;
					py -= (unit->dy * (255 - unit->delta)) >> 3;
				}
				if (point.x > px && point.x < px + 32 && point.y > py && point.y < py + 32 &&
					(wholeMap || map.isFOWDiscovered(x, y, visible) ||
					 Unit::GIDtoTeam(gid) == gui.localTeamNo))
					found = unit;
			}
	return found;
}

void GameGUITouch::select(ViewPoint point)
{
	const auto screenPoint = point;
	point = {double(gui.mapMouseX(point.x)), double(gui.mapMouseY(point.y))};
	if (gui.putMark && !globalContainer->isViewingGame())
	{
		gui.orderQueue.push_back(std::make_shared<MapMarkOrder>(
			gui.localTeamNo, (int(point.x) / 32 + gui.viewportX) & gui.game.map.getMaskW(),
			(int(point.y) / 32 + gui.viewportY) & gui.game.map.getMaskH()));
		gui.putMark = false;
		return;
	}
	gui.view.mouseUnit = Game::refOf(unitAt(screenPoint));
	const bool wasInspecting = inspecting() || inspectingResource();
	const bool wasOpen = panelOpen;
	const int oldDisplay = gui.displayMode;
	// Desktop selection deliberately sticks on empty terrain. A completed map
	// tap on touch dismisses the inspector; the shared picker can immediately
	// select the same building, another building, a unit or a resource instead.
	// Pan/cancel/UI gestures never reach this selection path.
	if (usesHUD() && gui.selectionMode != GameGUI::TOOL_SELECTION &&
		gui.selectionMode != GameGUI::BRUSH_SELECTION) gui.clearSelection();
	gui.handleMapClick(int(screenPoint.x), int(screenPoint.y), SDL_BUTTON_LEFT);
	if (!wasInspecting && (inspecting() || inspectingResource()))
	{
		restorePalette = true;
		previousPanelOpen = wasOpen;
		previousDisplayMode = oldDisplay;
	}
	gui.selectionPushed = false;
	if (usesHUD() && gui.selectionMode == GameGUI::NO_SELECTION)
	{
		dismissMapPanels();
		return;
	}
	if (usesHUD() && gui.selectionMode != GameGUI::NO_SELECTION)
	{
		panelOpen = true;
		panelScroll = inspectingResource() ? 0 : 144;
	}
}

void GameGUITouch::prepareDraw()
{
	if (!active())
		return;
	gui.checkSelection();
	advancePlacement();
	advanceFlagDrag();
	if (deferredStroke &&
		SDL_GetTicks() - deferredStroke->ticks >= InGameTouchTheme::doubleTapWindowMs)
		commitDeferredStroke();
	if (minimapPress && (touchTravelled || fingers.size() != 1 || ownerRegion != 8))
		minimapPress.reset();
	if (minimapPress && SDL_GetTicks() - *minimapPress >= InGameTouchTheme::peekPressMs)
	{
		// The press became a request for the large map; its release does nothing.
		peekOpen = true;
		minimapPress.reset();
		ignoreTouchSequence = true;
	}
	if (gui.selectionMode != GameGUI::BRUSH_SELECTION)
	{
		zoneUndo.reset();
		brushPan = false;
	}
	else if (zoneUndo && SDL_GetTicks() >= zoneUndo->expires)
		zoneUndo.reset();
	// A stroke held at a map edge pans, and keeps painting under the still finger.
	if (strokeHold && !stroke.points.empty() && gui.selectionMode == GameGUI::BRUSH_SELECTION)
	{
		const auto point = strokeHold->pointerPosition;
		if (edgePan(point, strokeHold->lastUpdate) && interfaceRegion(point) == 0)
			stroke.points.push_back({double(gui.mapMouseX(int(point.x)) + gui.viewportX * 32),
									 double(gui.mapMouseY(int(point.y)) + gui.viewportY * 32)});
	}
	if (restorePalette && !inspecting() && !inspectingResource())
	{
		panelOpen = previousPanelOpen;
		gui.displayMode = static_cast<GameGUI::DisplayMode>(previousDisplayMode);
		restorePalette = false;
	}
	// Compare the selected building's identity (gid and generation).
	const BuildingRef inspected = inspecting() ? std::get<BuildingRef>(gui.selection) : BuildingRef();
	if (!(inspected == lastInspectedBuilding))
	{
		confirmDestroy = false;
		actionScroll = 0;
		lastInspectedBuilding = inspected;
	}
	if (usesHUD())
	{
		clampScroll();
		prepareTutorial();
	}
	syncGestureExclusion();
	if (gui.selectionMode != GameGUI::TOOL_SELECTION ||
		previewType != gui.toolManager.getBuildingName())
		preview.reset();
	if (preview)
	{
		const auto cursor = previewCursor();
		gui.view.mouseX = int(cursor.x);
		gui.view.mouseY = int(cursor.y);
		gui.mouseX =
			int((cursor.x - gui.camera.fractionX()) * gui.camera.zoom + gui.camera.offsetX);
		gui.mouseY =
			int((cursor.y - gui.camera.fractionY()) * gui.camera.zoom + gui.camera.offsetY);
	}
}

void GameGUITouch::menuAction(int action)
{
	if (action == -2)
		return;
	if (action == -1)
	{
		showStatistics = false;
		panelScroll = 0;
		return;
	}
	if (action == -10)
	{
		// No overlay.
		gui.showStarvingMap = gui.showDamagedMap = gui.showDefenseMap = gui.showFertilityMap = false;
		return;
	}
	if (action == 50)
	{
		peekOpen = true;
		return;
	}
	gui.closeDialog();
	if (action >= 20 && action < 24)
	{
		bool *states[] = {&gui.showStarvingMap, &gui.showDamagedMap, &gui.showDefenseMap,
						  &gui.showFertilityMap};
		const bool enabled = !*states[action - 20];
		for (auto *state : states)
			*state = false;
		*states[action - 20] = enabled;
		// The next frame's scene extraction computes the newly chosen overlay.
		return;
	}
	if (globalContainer->replaying && action >= 31 && action < 56)
	{
		if (action == 31)
			globalContainer->replayShowFog = !globalContainer->replayShowFog;
		else if (action == 32)
			globalContainer->replayVisibleTeams =
				globalContainer->replayVisibleTeams == 0xffffffff ? gui.localTeam->me : 0xffffffff;
		else if (action == 33)
			globalContainer->replayShowAreas = !globalContainer->replayShowAreas;
		else if (action == 34)
			globalContainer->replayShowFlags = !globalContainer->replayShowFlags;
		else if (action >= 40 && action - 40 < gui.game.teamsCount())
		{
			gui.clearSelection();
			gui.localTeamNo = action - 40;
			gui.adjustLocalTeam();
			for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); ++i)
				if (gui.game.players[i]->teamNumber == gui.localTeamNo)
				{
					gui.localPlayer = i;
					break;
				}
			if (globalContainer->replayVisibleTeams != 0xffffffff)
				globalContainer->replayVisibleTeams = gui.localTeam->me;
		}
		return;
	}
	switch (action)
	{
	case 0:
		if (globalContainer->replaying)
			gui.gamePaused = !gui.gamePaused;
		else if (!globalContainer->isViewingGame())
			gui.orderQueue.push_back(std::make_shared<PauseGameOrder>(!gui.gamePaused));
		break;
	case 1:
		gui.openChat();
		break;
	case 2:
		panelOpen = true;
		gui.clearSelection();
		panelScroll = 0;
		break;
	case 3:
		if (usesHUD() && !layout().persistentPanel && !globalContainer->isViewingGame())
		{
			statsOpen = true; // The compact statistics sheet.
			break;
		}
		showStatistics = true;
		panelOpen = true;
		gui.clearSelection();
		gui.replayDisplayMode = GameGUI::RDM_STAT_GRAPH_VIEW;
		gui.displayMode = GameGUI::STAT_GRAPH_VIEW;
		panelScroll = 0;
		break;
	case 4:
		gui.toggleHistory();
		break;
	case 5:
		gui.putMark = true;
		break;
	case 6:
		gui.drawHealthFoodBar = !gui.drawHealthFoodBar;
		break;
	case 30:
		globalContainer->replayFastForward = !globalContainer->replayFastForward;
		gui.gamePaused = false;
		break;
	}
}
