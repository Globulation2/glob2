// SPDX-License-Identifier: GPL-3.0-or-later
#include "InGameTouchTheme.h"
#include "GameGUITouch.h"
#include <TouchText.h>
#include <InterfacePresentation.h>
#include "MobileSafeArea.h"
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
#ifdef __ANDROID__
#include <SDL_system.h>
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
		const int columns = gui.displayMode == GameGUI::FLAG_VIEW ? int(paletteItems().size()) :
			result.persistentPanel || result.safe.w > result.safe.h
								? 4
								: std::max(1, int(result.safe.w / 60));
		const double width = std::min(result.safe.w, columns * 60.0 + 8);
		const double height = std::min(result.world.h - 80,
									   std::ceil(paletteItems().size() / double(columns)) * 60 + 8);
		result.panel = {result.safe.x + result.safe.w - width, result.actions.y - height, width,
						height};
		if (result.persistentPanel)
			result.panel.y = result.safe.y + 104;
	}
	if ((result.persistentPanel || panelOpen) && inspectedBuilding())
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
	return result;
}
void GameGUITouch::clampScroll()
{
	const auto content = panelContent();
	actionScroll =
		std::clamp(actionScroll, 0.0,
				   std::max(0.0, buildingActionsHeight(content.w / globalContainer->gfx->logicalUnitsPerPoint()) -
									 content.h / globalContainer->gfx->logicalUnitsPerPoint()));
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const int columns = gui.displayMode == GameGUI::FLAG_VIEW ? int(paletteItems().size()) :
		std::max(1, int((content.w / unit - 8 + .01) / 60));
	const double height = showsBuildPalette()
							  ? std::ceil(paletteItems().size() / double(columns)) * 60 + 8
							  : tacticalActions().size() * 56;
	panelScroll = std::clamp(panelScroll, 0.0, std::max(0.0, height - content.h / unit));
}
GameGUITouch::~GameGUITouch() = default;
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
	allocation.reset();
	stroke.cancel();
	gui.toolManager.cancelDrag(gui.localTeamNo);
	gesture.cancel();
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
	if (gui.selectionMode == GameGUI::BRUSH_SELECTION && controls().contains(point))
		return 9;
	if (gui.selectionMode == GameGUI::TOOL_SELECTION && controls().contains(point))
		return point.x < controls().x + controls().w / 2 ? 1 : 2;
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
		if (layout().panel.contains(point))
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
		const auto rect = controls();
		targets = {{rect.x, rect.y, rect.w / 2, rect.h},
				   {rect.x + rect.w / 2, rect.y, rect.w / 2, rect.h}};
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
	else if (inspectedBuilding())
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
	if (usesHUD() && event.type == SDL_KEYDOWN)
	{
		const auto key = event.key.keysym.sym;
		if (key == SDLK_TAB)
		{
			const auto targets = keyboardTargets();
			if (!targets.empty())
				keyboardFocus = (keyboardFocus + ((event.key.keysym.mod & KMOD_SHIFT) ? -1 : 1) +
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
				pointer.type = SDL_MOUSEBUTTONDOWN;
				pointer.button.button = SDL_BUTTON_LEFT;
				pointer.button.x = int(rect.x + rect.w / 2);
				pointer.button.y = int(rect.y + rect.h / 2);
				process(pointer);
				pointer.type = SDL_MOUSEBUTTONUP;
				process(pointer);
			}
			return true;
		}
		if (key == SDLK_PAGEDOWN || key == SDLK_PAGEUP)
		{
			const double delta = key == SDLK_PAGEDOWN ? 144 : -144;
			panelScroll += delta;
			actionScroll += delta;
			clampScroll();
			return true;
		}
	}
	if ((event.type == SDL_MOUSEMOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
		((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
		 event.button.which == SDL_TOUCH_MOUSEID))
		return true;
	if (event.type == SDL_MOUSEBUTTONUP && swallowMouseRelease)
	{
		swallowMouseRelease = false;
		return true;
	}
	if (usesHUD() && event.type == SDL_MOUSEWHEEL)
	{
		int x, y;
		SDL_GetMouseState(&x, &y);
		GraphicContext::translateMouseCoordinates(x, y);
		const int region = interfaceRegion({double(x), double(y)});
		const double delta =
			event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -48 : 48);
		if (region == 3)
		{
			if (inspectedBuilding())
				actionScroll -= delta;
			else
				panelScroll -= delta;
			clampScroll();
			return true;
		}
	}
	if (usesHUD() && (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
					  event.type == SDL_MOUSEMOTION))
	{
		if (event.type != SDL_MOUSEMOTION && event.button.button != SDL_BUTTON_LEFT)
			return false;
		const bool motion = event.type == SDL_MOUSEMOTION;
		if (motion && !(event.motion.state & SDL_BUTTON_LMASK))
			return false;
		SDL_Event pointer{};
		pointer.type = motion                              ? SDL_FINGERMOTION
					   : event.type == SDL_MOUSEBUTTONDOWN ? SDL_FINGERDOWN
														   : SDL_FINGERUP;
		pointer.tfinger.touchId = -1;
		pointer.tfinger.fingerId = 0;
		pointer.tfinger.x =
			float(motion ? event.motion.x : event.button.x) / globalContainer->gfx->getW();
		pointer.tfinger.y =
			float(motion ? event.motion.y : event.button.y) / globalContainer->gfx->getH();
		return process(pointer);
	}
	if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
										  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
		cancel();
	if (event.type != SDL_FINGERDOWN && event.type != SDL_FINGERUP &&
		event.type != SDL_FINGERMOTION)
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
		const auto key = std::make_pair(event.tfinger.touchId, event.tfinger.fingerId);
		if (event.type == SDL_FINGERDOWN &&
			std::find(fingers.begin(), fingers.end(), key) == fingers.end())
			fingers.push_back(key);
		if (event.type == SDL_FINGERUP)
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
	if (!fingers.empty() && inspectedBuilding() &&
		(heldBuildingState != inspectedBuilding()->buildingState ||
		 heldConstructionState != inspectedBuilding()->constructionResultState))
	{
		cancel();
		return true;
	}
	ViewPoint point{event.tfinger.x * globalContainer->gfx->getW(),
					event.tfinger.y * globalContainer->gfx->getH()};
	const auto key = std::make_pair(event.tfinger.touchId, event.tfinger.fingerId);
	if (event.type == SDL_FINGERDOWN)
	{
		if (fingers.empty())
		{
			touchActive = true;
			gui.viewportSpeedX = gui.viewportSpeedY = 0;
			gui.lastMouseButtonState = 0;
			gui.selectionPushed = gui.panPushed = gui.miniMapPushed = false;
			scale = globalContainer->gfx->logicalUnitsPerPoint();
			ownerRegion = interfaceRegion(point);
			heldActionKind = -1;
			heldActionConfirmation = confirmDestroy;
			if (auto *building = inspectedBuilding())
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
			gesture.setMode(interfaceGesture                                ? TouchMode::Navigate
							: gui.selectionMode == GameGUI::TOOL_SELECTION  ? TouchMode::Placement
							: gui.selectionMode == GameGUI::BRUSH_SELECTION ? TouchMode::Paint
																			: TouchMode::Navigate);
		}
		if (fingers.empty() && ownerRegion == 0 && gui.selectionMode == GameGUI::TOOL_SELECTION)
			placementHold = TouchPlacementSession{key, point, gui.toolManager.getBuildingName(),
				true, {}, point, SDL_GetTicks64(), gui.localTeamNo};
		else
			placementHold.reset(); // A navigation gesture cannot resume edge panning.
		if (std::find(fingers.begin(), fingers.end(), key) == fingers.end())
			fingers.push_back(key);
		actions(gesture.down(key.first, key.second, {point.x / scale, point.y / scale}));
	}
	else if (event.type == SDL_FINGERMOTION)
	{
		if (placementHold && placementHold->pointer == key)
			placementHold->pointerPosition = point;
		actions(gesture.move(key.first, key.second, {point.x / scale, point.y / scale}));
	}
	else
	{
		if (placementHold && placementHold->pointer == key)
			placementHold.reset();
		auto changes = gesture.up(key.first, key.second, {point.x / scale, point.y / scale});
		if (changes.size() == 1 && changes.front().kind == TouchActionKind::Select &&
			!interfaceGesture && world().contains(point) && !controls().contains(point))
		{
			const Uint32 ticks = event.tfinger.timestamp;
			const double tapRadius = 24 * scale;
			if (lastMapTapTicks && ticks - *lastMapTapTicks <= 300 &&
				std::hypot(point.x - lastMapTapPoint.x, point.y - lastMapTapPoint.y) <= tapRadius)
			{
				gui.updateCamera();
				const bool zoomed = gui.zoomMap(std::log(1.0 / gui.camera.zoom) / std::log(1.1),
					int(point.x), int(point.y));
				lastMapTapTicks.reset();
				if (zoomed) changes.clear();
			}
			else { lastMapTapTicks = ticks; lastMapTapPoint = point; }
		}
		else lastMapTapTicks.reset();
		actions(changes);
		std::erase(fingers, key);
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
			stroke.cancel();
			preview.reset();
			gui.toolManager.cancelDrag(gui.localTeamNo);
			continue;
		}
		if (interfaceGesture)
		{
			if (usesHUD() && ownerRegion == 7 && action.kind == TouchActionKind::Pan)
			{
				const double unit = globalContainer->gfx->logicalUnitsPerPoint();
				tutorialScroll =
					std::clamp(tutorialScroll - point.y / unit, 0.0,
							   std::max(0.0, tutorialLines.size() * 24.0 - tutorialRect().h / unit +
												 16 + (gui.swallowSpaceKey ? 48 : 0)));
			}
			if (usesHUD() && ownerRegion == 3 && action.kind == TouchActionKind::Pan)
			{
				if (inspectedBuilding())
					actionScroll -= point.y / globalContainer->gfx->logicalUnitsPerPoint();
				else
					panelScroll -= point.y / globalContainer->gfx->logicalUnitsPerPoint();
				clampScroll();
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
		}
		else if (action.kind == TouchActionKind::Zoom)
		{
			lastMapTapTicks.reset();
			if (action.factor > 0)
				gui.zoomMap(std::log(action.factor) / std::log(1.1), int(point.x), int(point.y));
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
				{
					for (size_t i = 0; i < stroke.points.size(); ++i)
					{
						const auto p = stroke.points[i];
						if (i == 0)
							gui.toolManager.handleMouseDown(int(p.x), int(p.y), gui.localTeamNo, 0,
															0);
						else
							gui.toolManager.handleMouseDrag(int(p.x), int(p.y), gui.localTeamNo, 0,
															0);
					}
					gui.toolManager.finishPointerGesture(gui.localTeamNo);
				}
				stroke.cancel();
			}
		}
	}
}

void GameGUITouch::interfaceTap(ViewPoint point)
{
	if (activeDialog())
		return;
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
		const auto rect = controls();
		const int button = std::clamp(int((point.x - rect.x) / (rect.w / 4)), 0, 3);
		stroke.cancel();
		if (button == 0)
			gui.toolManager.activateZoneTool(
				static_cast<GameGUIToolManager::ZoneType>((gui.toolManager.getZoneType() + 1) % 3));
		else if (button == 1)
			gui.brush.setFigure((gui.brush.getFigure() + 1) % BrushTool::BRUSH_COUNT);
		else if (button == 2)
			gui.brush.setType(gui.brush.getType() == BrushTool::MODE_ADD ? BrushTool::MODE_DEL
																		 : BrushTool::MODE_ADD);
		else
		{
			gui.clearSelection();
			panelOpen = true;
		}
		return;
	}
	if (!gui.inGameMenu && !gui.typingInputScreen && !gui.scrollableText &&
		gui.selectionMode == GameGUI::TOOL_SELECTION && controls().contains(point))
	{
		if (point.x < controls().x + controls().w / 2)
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
			SDL_Keysym key{};
			key.sym = SDLK_SPACE;
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
			if (button < 2)
			{
				const auto mode = button == 0 ? GameGUI::CONSTRUCTION_VIEW : GameGUI::FLAG_VIEW;
				panelOpen = !(panelOpen && gui.displayMode == mode &&
							  gui.selectionMode == GameGUI::NO_SELECTION);
				gui.clearSelection();
				gui.displayMode = mode;
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
		if (showsBuildPalette())
			tapBuildPalette(point);
		else if (inspectedBuilding())
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
	click.type = SDL_MOUSEBUTTONDOWN;
	click.button.button = SDL_BUTTON_LEFT;
	click.button.x = int(point.x);
	click.button.y = int(point.y);
	gui.processEvent(&click);
	click.type = SDL_MOUSEBUTTONUP;
	gui.processEvent(&click);
	dispatching = false;
	if (usesHUD() && (gui.selectionMode == GameGUI::TOOL_SELECTION ||
					  gui.selectionMode == GameGUI::BRUSH_SELECTION))
		panelOpen = false;
	gui.lastMouseButtonState = 0;
	gui.selectionPushed = gui.panPushed = gui.miniMapPushed = false;
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
	gui.view.mouseUnit = nullptr;
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
					gui.view.mouseUnit = unit;
			}
	const bool wasInspecting = inspectedBuilding() != nullptr;
	const bool wasOpen = panelOpen;
	const int oldDisplay = gui.displayMode;
	// Desktop selection deliberately sticks on empty terrain. A completed map
	// tap on touch dismisses the inspector; the shared picker can immediately
	// select the same building, another building, a unit or a resource instead.
	// Pan/cancel/UI gestures never reach this selection path.
	if (usesHUD() && wasInspecting) gui.clearSelection();
	gui.handleMapClick(int(screenPoint.x), int(screenPoint.y), SDL_BUTTON_LEFT);
	if (!wasInspecting && inspectedBuilding())
	{
		restorePalette = true;
		previousPanelOpen = wasOpen;
		previousDisplayMode = oldDisplay;
	}
	gui.selectionPushed = false;
	if (usesHUD() && gui.selectionMode != GameGUI::NO_SELECTION)
	{
		panelOpen = true;
		panelScroll = 144;
	}
}

void GameGUITouch::prepareDraw()
{
	if (!active())
		return;
	gui.checkSelection();
	advancePlacement();
	if (restorePalette && !inspectedBuilding())
	{
		panelOpen = previousPanelOpen;
		gui.displayMode = static_cast<GameGUI::DisplayMode>(previousDisplayMode);
		restorePalette = false;
	}
	if (inspectedBuilding() != lastInspectedBuilding)
	{
		confirmDestroy = false;
		actionScroll = 0;
		lastInspectedBuilding = inspectedBuilding();
	}
	if (usesHUD())
	{
		clampScroll();
		prepareTutorial();
	}
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
	gui.closeDialog();
	if (action >= 20 && action < 24)
	{
		bool *states[] = {&gui.showStarvingMap, &gui.showDamagedMap, &gui.showDefenseMap,
						  &gui.showFertilityMap};
		const bool enabled = !*states[action - 20];
		for (auto *state : states)
			*state = false;
		*states[action - 20] = enabled;
		const OverlayArea::OverlayType types[] = {OverlayArea::Starving, OverlayArea::Damage,
												  OverlayArea::Defence, OverlayArea::Fertility};
		gui.overlay.compute(gui.game, types[action - 20], gui.localTeamNo);
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
