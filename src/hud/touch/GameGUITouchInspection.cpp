// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include "GameGUIInternal.h"
#include "Order.h"
#include <cmath>
#include <algorithm>
#include <iterator>

using namespace GAGCore;
namespace ui = GAGGUI::ui;
namespace
{
ui::Rect bounds(ViewRect r)
{
	const int x = int(std::floor(r.x)), y = int(std::floor(r.y));
	return {x, y, std::max(0, int(std::ceil(r.x + r.w)) - x),
			std::max(0, int(std::ceil(r.y + r.h)) - y)};
}
ViewPoint point(ui::Point p)
{
	return {double(p.x), double(p.y)};
}
} // namespace
std::vector<ui::PositionedControl> GameGUITouch::describeControls()
{
	std::vector<ui::PositionedControl> result;
	if (activeDialog() || (!usesHUD() && (!active() || gui.selectionMode != GameGUI::TOOL_SELECTION)))
		return result;
	const auto l = layout();
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	auto add = [&](std::string key, std::string label, ViewRect r,
				   std::function<void(ViewPoint)> tap = {},
				   std::optional<ViewRect> clip = {}) -> ui::PositionedControl &
	{
		ui::PositionedControl control;
		control.key = "touch/" + key;
		control.label = std::move(label);
		control.bounds = bounds(r);
		if (clip)
			control.clip = bounds(*clip);
		if (tap)
			control.tap = [tap](ui::Point p) { tap(point(p)); };
		else
		{
			control.role = "group";
			control.actions.clear();
		}
		result.push_back(std::move(control));
		return result.back();
	};
	auto scroll = [&](const std::string &key, ViewRect area, int surface)
	{
		auto &control = add(key, "Scroll content", area);
		control.role = "scroll";
		control.value = int((surface == 3   ? scenarioMessageScroll
							 : surface == 2 ? actionScroll
											: panelScroll) *
							u);
		control.actions = {ui::ActionKind::Scroll};
		control.perform = [this, surface, u](const ui::SemanticAction &action)
		{
			const auto *delta = std::get_if<int>(&action.value);
			if (action.kind != ui::ActionKind::Scroll || !delta)
				return false;
			stopScrolling();
			auto &offset = surface == 3   ? scenarioMessageScroll
						   : surface == 2 ? actionScroll
										  : panelScroll;
			offset += double(*delta) / u;
			clampScroll();
			return true;
		};
	};
	auto placementControls = [&]
	{
		const std::string identity =
			"placement:" + gui.toolManager.getBuildingName() + ":" + previewType + ":" +
			std::to_string(gui.localTeamNo) + ":" +
			(preview ? std::to_string(int(preview->x)) + ":" + std::to_string(int(preview->y))
					 : "none");
		auto &confirm = add("placement/confirm", "Confirm construction", confirmRect(),
							[this](ViewPoint)
							{
								if (commitPlacement())
								{
									preview.reset();
									gui.clearSelection();
								}
							});
		confirm.enabled = preview.has_value() && !(placement && placement->dragging);
		confirm.identity = identity;
		add("placement/cancel", "Cancel construction", cancelRect(),
			[this](ViewPoint)
			{
				preview.reset();
				gui.clearSelection();
			})
			.identity = identity;
	};
	if (!usesHUD())
	{
		placementControls();
		return result;
	}
	if (peekOpen)
	{
		add("peek/backdrop", "Dismiss map tools", l.safe,
			[this](ViewPoint) { dismissMapPanels(); });
		add("minimap", "Map preview canvas", peekRect(), [this](ViewPoint p) { navigatePeek(p); })
			.role = "canvas";
		const auto buttons = peekButtons();
		const char *keys[] = {"done", "out", "in"};
		const char *labels[] = {"Done", "Zoom out", "Zoom in"};
		for (int i = 0; i < int(buttons.size()); ++i)
			add(std::string("peek/") + keys[i], labels[i], buttons[i],
				[this, i](ViewPoint)
				{
					if (!i)
					{
						peekOpen = false;
						return;
					}
					const auto area = world();
					gui.updateCamera();
					gui.zoomMap((i == 2 ? 1 : -1) * std::log(InGameTouchTheme::peekZoomStep) /
									std::log(1.1),
								int(area.x + area.w / 2), int(area.y + area.h / 2));
				});
		return result;
	}
	add("minimap", "Minimap canvas", minimapRect(), [this](ViewPoint p) { navigateMinimap(p); })
		.role = "canvas";
	if (speedRect().w > 0)
		add("speed", "Game speed", speedRect(), [this](ViewPoint) { gui.cycleGameSpeed(); }).value =
			gui.litSpeedChevrons();
	if (gui.selectionMode == GameGUI::TOOL_SELECTION)
	{
		placementControls();
	}
	else if (gui.selectionMode == GameGUI::BRUSH_SELECTION)
	{
		const auto buttons = brushBarButtons();
		const char *labels[] = {"Forbidden", "Guard", "Clear", "Farm"};
		for (int i = 0; i < int(buttons.size()); ++i)
			add("zone/" + std::to_string(i),
				i < gui.toolManager.zoneTypeCount() ? labels[i] : "Done", buttons[i],
				[this, i](ViewPoint)
				{
					stroke.cancel();
					if (i < gui.toolManager.zoneTypeCount())
						gui.toolManager.activateZoneTool(
							static_cast<GameGUIToolManager::ZoneType>(i));
					else
					{
						gui.clearSelection();
						panelOpen = true;
					}
				})
				.selected = i == int(gui.toolManager.getZoneType());
		const auto rail = brushHUD();
		for (size_t i = 0; i < rail.detents.size(); ++i)
			add("brush/shape/" + std::to_string(i),
				"Brush " + std::to_string(BrushTool::getBrushWidth(i)) + "x" +
					std::to_string(BrushTool::getBrushHeight(i)),
				rail.detents[i],
				[this, i](ViewPoint)
				{
					stroke.cancel();
					gui.brush.setFigure(unsigned(i));
				})
				.selected = gui.brush.getFigure() == i;
		if (rail.mode.w > 0)
			add("brush/mode", "Paint or erase", rail.mode,
				[this](ViewPoint)
				{
					stroke.cancel();
					gui.brush.setType(gui.brush.getType() == BrushTool::MODE_ADD
										  ? BrushTool::MODE_DEL
										  : BrushTool::MODE_ADD);
				})
				.selected = gui.brush.getType() == BrushTool::MODE_DEL;
		if (rail.pan.w > 0)
			add("brush/pan", "Pan", rail.pan,
				[this](ViewPoint)
				{
					stroke.cancel();
					brushPan = !brushPan;
				})
				.selected = brushPan;
		if (rail.undo.w > 0)
			add("brush/undo", "Undo stroke", rail.undo,
				[this](ViewPoint)
				{
					stroke.cancel();
					applyZoneUndo();
				});
	}
	else
	{
		const char *keys[] = {"buildings", "flags", "tools", "objectives", "alliance", "menu"};
		const char *labels[] = {"Buildings", "Flags", "Tools", "Objectives", "Alliances", "Menu"};
		for (int i = 0; i < 6; ++i)
		{
			auto &control =
				add(std::string("navigation/") + keys[i],
					globalContainer->replaying && i < 2 ? (i ? "Fast forward" : "Pause playback")
														: labels[i],
					navigationButtonRect(l, i),
					[this, i](ViewPoint) { activateNavigation(i); });
			control.selected =
				i == 0   ? (globalContainer->replaying
								? bool(gui.gamePaused)
								: panelOpen && gui.displayMode == GameGUI::CONSTRUCTION_VIEW)
				: i == 1 ? (globalContainer->replaying
								? bool(globalContainer->replayFastForward)
								: panelOpen && gui.displayMode == GameGUI::FLAG_VIEW)
						 : i == 2 && (lensOpen ||
									  (panelOpen && gui.displayMode == GameGUI::STAT_TEXT_VIEW));
			control.enabled = !((i == 0 && !globalContainer->replaying &&
								 (gui.hiddenGUIElements & GameGUI::HIDABLE_BUILDINGS_LIST)) ||
								(i == 1 && !globalContainer->replaying &&
								 (gui.hiddenGUIElements & GameGUI::HIDABLE_FLAGS_LIST)));
		}
	}
	// Event feed entries share their measured row model and callbacks with the
	// desktop HUD. Later overlays remove or cover them using ordinary ownership.
	if (gui.selectionMode != GameGUI::TOOL_SELECTION &&
		gui.selectionMode != GameGUI::BRUSH_SELECTION)
	{
		auto events = gui.describeEventControls();
		result.insert(result.end(), std::make_move_iterator(events.begin()),
					  std::make_move_iterator(events.end()));
	}
	const auto message = scenarioMessageRect();
	if (message.w > 0 && message.h > 0)
	{
		if (scenarioMessageCollapsed)
			add("scenario-message/expand", "Expand scenario message", message,
				[this](ViewPoint) { scenarioMessageCollapsed = false; });
		else
		{
			scroll("scenario-message/scroll", message, 3);
			const auto collapse = scenarioMessageCollapseRect();
			if (collapse.w > 0 && collapse.h > 0)
				add("scenario-message/collapse", "Collapse scenario message", collapse,
					[this](ViewPoint) { scenarioMessageCollapsed = true; });
			if (gui.swallowSpaceKey)
				add("scenario-message/continue", "Continue",
					scenarioMessageContinueRect(),
					[this](ViewPoint)
					{
						SDL_KeyboardEvent key{};
						key.key = SDLK_SPACE;
						gui.handleKey(key, true);
					});
		}
	}
	const auto content = panelContent();
	if (lensVisible())
	{
		const auto items = lenses();
		const auto rects = lensRects(l);
		for (size_t i = 0; i < items.size() && i < rects.size(); ++i)
			add("lens/" + std::to_string(items[i].action), items[i].label, rects[i],
				[this, action = items[i].action](ViewPoint) { menuAction(action); })
				.selected = items[i].selected;
	}
	else if (inspectingReadOnly())
	{
		add("inspector/close", "Close inspector", readOnlyCloseRect(),
			[this](ViewPoint) { dismissMapPanels(); });
		if (content.h > 0)
			scroll("inspector/scroll", content, 1);
	}
	else if (inspecting())
	{
		const auto header = allocationRect();
		add("inspector/close", "Close inspector",
			{header.x + header.w - 48 * u, header.y, 48 * u, header.h},
			[this](ViewPoint)
			{
				gui.clearSelection();
				panelOpen = previousPanelOpen;
				prepareDraw();
			});
		if (const auto *b = inspectedBuilding())
		{
			const std::string prefix = "building/" + std::to_string(b->state().gid) + "/" +
									   std::to_string(b->state().scriptIdentity) + "/";
			const std::string identity =
				prefix + std::to_string(gui.localTeamNo) + ":" + std::to_string(confirmDestroy);
			const bool editable =
				!globalContainer->isViewingGame() && b->owner().number == gui.localTeamNo;
			auto building = [&](std::string key, const BuildingAction &row, ViewRect box,
								std::optional<ViewRect> clip = {}) -> ui::PositionedControl &
			{
				auto &control = add(
					prefix + key, row.label, box, [this](ViewPoint p) { tapBuildingAction(p); },
					clip);
				control.identity = identity;
				control.enabled = editable;
				control.selected = row.selected;
				control.perform = [this, row](const ui::SemanticAction &action)
				{ return performBuildingAction(row, action); };
				if (row.kind == 0 || row.kind == 6 || row.kind == 7 || row.kind == 8)
				{
					control.role = row.kind == 7 ? "choice" : "slider";
					control.actions = {row.kind == 7 ? ui::ActionKind::Select
													 : ui::ActionKind::SetValue};
					control.value = row.kind == 0   ? gui.displayedRatio(*b)[row.value]
									: row.kind == 6 ? gui.displayedMaxUnitWorking(*b)
									: row.kind == 7 ? gui.displayedPriority(*b)
													: gui.displayedUnitStayRange(*b);
				}
				return control;
			};
			if (usesDial())
			{
				for (const auto &region : dialRegions())
				{
					const auto row = region.action;
					auto &control =
						building(std::to_string(row.kind) + "/" + std::to_string(row.value) + "/" +
									 std::to_string(int(region.part)),
								 row, dialRegionBounds(region));
					if (region.part == DialRegion::Arc)
						control.tap =
							{}; // Polar gesture path owns continuous arcs; no keyboard tap at a bounding-box centre.
					if (region.part == DialRegion::Minus || region.part == DialRegion::Plus)
					{
						control.actions = {ui::ActionKind::Activate};
						control.role = "button";
						control.label =
							(region.part == DialRegion::Minus ? "Decrease " : "Increase ") +
							row.label;
						control.perform =
							[this, row, delta = region.part == DialRegion::Minus ? -1 : 1,
							 maximum = region.maximum](const ui::SemanticAction &action)
						{
							const auto *b = inspectedBuilding();
							if (action.kind != ui::ActionKind::Activate || !b)
								return false;
							const int current = row.kind == 6   ? gui.displayedMaxUnitWorking(*b)
												: row.kind == 8 ? gui.displayedUnitStayRange(*b)
																: gui.displayedRatio(*b)[row.value];
							return performBuildingAction(row,
														 {ui::ActionKind::SetValue,
														  std::clamp(current + delta, 0, maximum)});
						};
					}
					else if (row.kind == 7)
					{
						control.actions = {ui::ActionKind::Activate};
						control.perform = [this, row](const ui::SemanticAction &action)
						{
							return action.kind == ui::ActionKind::Activate &&
								   performBuildingAction(row, {ui::ActionKind::Select, row.value});
						};
					}
					else if (region.part == DialRegion::RatioButton)
					{
						control.actions = {ui::ActionKind::Activate, ui::ActionKind::SetValue};
					}
				}
			}
			else
			{
				if (content.h > 0)
					scroll("inspector/scroll", content, 2);
				const auto rows = buildingActions();
				for (size_t i = 0; i < rows.size(); ++i)
				{
					const auto box = buildingActionRect(i);
					if (box.y < content.y || box.y + box.h > content.y + content.h)
						continue;
					building(std::to_string(rows[i].kind) + "/" + std::to_string(rows[i].value),
							 rows[i], box, content);
				}
			}
		}
	}
	else if ((l.persistentPanel || panelOpen) && showsBuildPalette())
	{
		scroll("palette/scroll", content, 1);
		const auto items = paletteItems();
		for (size_t i = 0; i < items.size(); ++i)
		{
			std::string label = items[i].name;
			auto &control = add(
				"palette/" + items[i].name, label, paletteItemRect(i),
				[this, item = items[i]](ViewPoint) { activatePalette(item); }, content);
			control.enabled = items[i].enabled;
		}
	}
	else if ((l.persistentPanel || panelOpen) && l.panel.h > 0 &&
			 gui.selectionMode != GameGUI::BRUSH_SELECTION &&
			 gui.selectionMode != GameGUI::TOOL_SELECTION)
	{
		scroll("tactical/scroll", l.panel, 1);
		const auto actions = tacticalActions();
		for (size_t i = 0; i < actions.size(); ++i)
			add("tactical/" + std::to_string(actions[i].second) + "/" + std::to_string(i),
				actions[i].first,
				{l.panel.x, l.panel.y + (i * 56 - panelScroll) * u, l.panel.w, 48 * u},
				actions[i].second == -2
					? std::function<void(ViewPoint)>{}
					: std::function<void(ViewPoint)>{[this, action = actions[i].second](ViewPoint)
													 { menuAction(action); }},
				l.panel);
	}
	// Later compact panels paint and receive input above scenario messages and
	// notifications. Conservatively retire a lower target when any visible part
	// overlaps, just as Host does for independent higher surfaces.
	if ((l.persistentPanel || panelOpen || lensVisible()) && l.panel.w > 0 && l.panel.h > 0)
	{
		const auto panel = bounds(l.panel);
		std::erase_if(result, [&](const auto &control)
		{
			const auto visible = control.clip ? control.bounds.intersect(*control.clip) : control.bounds;
			return (control.key.starts_with("touch/scenario-message/") || control.key.starts_with("game/events/")) && visible.intersects(panel);
		});
	}
	for (auto &control : result)
		if (control.key.starts_with("touch/scenario-message/"))
			control.identity = scenarioMessageText + ":" + std::to_string(gui.swallowSpaceKey);
	if (statsOpen)
	{
		const auto stats = statsLayout();
		// The sheet covers lower HUD controls; its embedded Host owns the body.
		std::erase_if(result, [&](const auto &control)
					  { return control.bounds.intersects(bounds(stats.sheet)); });
		const ViewRect header{stats.sheet.x, stats.sheet.y, stats.sheet.w,
            std::max(0., stats.body.y - stats.sheet.y)};
		auto &sheet = add("statistics/sheet", "Statistics sheet; pull header down to close", header);
		sheet.role = "canvas";
		sheet.actions = {ui::ActionKind::Scroll};
		sheet.perform = [this, u](const ui::SemanticAction &action)
		{
			const auto *delta = std::get_if<int>(&action.value);
			if (action.kind != ui::ActionKind::Scroll || !delta) return false;
			// A semantic scroll is a completed pull gesture, in logical UI pixels.
			statsDrag = 0;
			dragStatistics(double(*delta) / u);
			statsDrag = 0;
			return true;
		};
		add("statistics/close", "Close statistics", stats.close,
			[this](ViewPoint) { statsOpen = false; });
	}
	return result;
}

bool GameGUITouch::controlAllowed(const std::string &key, ui::ActionKind kind)
{
	std::string error;
	return gui.hudInspectionHost().actionAllowed(key, kind, error);
}
bool GameGUITouch::inputBlocked(ViewPoint p)
{
	return gui.hudInspectionHost().inputBlockedAt({int(p.x), int(p.y)});
}
bool GameGUITouch::scrollAllowed(int surface)
{
	const char *key = surface == 3 ? "touch/scenario-message/scroll" :
		surface == 2 || inspectingReadOnly() ? "touch/inspector/scroll" :
		showsBuildPalette() ? "touch/palette/scroll" : "touch/tactical/scroll";
	return controlAllowed(key, ui::ActionKind::Scroll);
}
std::string GameGUITouch::buildingControlKey(const BuildingAction &row, std::optional<DialRegion::Part> part) const
{
	const auto *b = inspectedBuilding();
	if (!b) return {};
	return "touch/building/" + std::to_string(b->state().gid) + "/" + std::to_string(b->state().scriptIdentity) + "/" +
		std::to_string(row.kind) + "/" + std::to_string(row.value) + (part ? "/" + std::to_string(int(*part)) : "");
}
bool GameGUITouch::buildingControlAllowed(const BuildingAction &row, std::optional<DialRegion::Part> part)
{
	const bool value = !part || *part == DialRegion::Arc;
	const auto action = value && row.kind == 7 ? ui::ActionKind::Select :
		value && (row.kind == 0 || row.kind == 6 || row.kind == 8) ? ui::ActionKind::SetValue : ui::ActionKind::Activate;
	return controlAllowed(buildingControlKey(row, part), action);
}

std::string GameGUITouch::pointerControlKey(ViewPoint p)
{
	if (!peekOpen && !statsOpen && inspecting() && usesDial())
		if (const auto region = dialRegionAt(p)) return buildingControlKey(region->action, region->part);
	const ui::Point at{int(p.x), int(p.y)};
	const auto controls = describeControls();
	for (auto it = controls.rbegin(); it != controls.rend(); ++it)
	{
		if (it->key.starts_with("touch/building/") && usesDial()) continue; // Exact polar test above owns rings, not bounding-box holes.
		if (it->contains(at)) return it->key;
	}
	return {};
}

std::string GameGUITouch::focusedControlKey()
{
	if (!usesHUD() || keyboardFocus < 0) return {};
	const auto targets = keyboardTargets();
	if (keyboardFocus >= int(targets.size())) return {};
	const auto &r = targets[keyboardFocus];
	return pointerControlKey({r.x + r.w / 2, r.y + r.h / 2});
}

bool GameGUITouch::pointerControlAllowed(ViewPoint p)
{
	const auto key = pointerControlKey(p);
	if (key.empty()) return true;
	const auto controls = describeControls();
	for (const auto &control : controls)
		if (control.key == key)
			return control.enabled && !control.actions.empty() && controlAllowed(key, control.actions.front());
	return false;
}
