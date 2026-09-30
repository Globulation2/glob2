// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include "InGameTouchTheme.h"
#include "gui/TouchReadout.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "MobileSafeArea.h"
#include "GlobalContainer.h"
#include <TouchText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <cmath>
#include <cstdio>
using namespace GAGCore;
namespace
{
// Point-based presentation policy. Gesture tuning must not change editor rules.
constexpr double headerHeight = 48;
constexpr double modeHeight = 44;
constexpr double paletteHeight = 60;
constexpr double dragThreshold = 8;
constexpr double previewLift = 40;
} // namespace

PhoneEditor::PhoneEditor(MapEdit &editor) : editor(editor) {}
PhoneEditor::~PhoneEditor() = default;
bool PhoneEditor::hasOverlay() const
{
	return editor.hasDialog();
}
void PhoneEditor::cancel()
{
	if (drag && drag->moving)
		editor.performAction("unselect");
	brushOpen = false;
	touch.cancel();
	stopScrolling();
	held = -1;
	onMap = false;
	drag.reset();
	commitDeferred(); // A completed tap is not undone by an interruption.
	lastTapTicks.reset();
	stroke.clear();
	quarantined.clear();
	if (auto *dialog = editor.activeDialog())
		dialog->cancelInput();
}
// All rectangles are independent editor presentation bounds. Palette widgets
// contribute only their individual artwork and named selection actions.
void PhoneEditor::prepare()
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	safe = mobileDialogSafe(gfx);
	const double dock = (modeHeight + (tools ? paletteHeight : 0)) * unit;
	content = {safe.x, safe.y + headerHeight * unit, safe.w,
			   std::max(0., safe.h - headerHeight * unit - dock)};
	modeBar = {safe.x, safe.y + safe.h - dock, safe.w, modeHeight * unit};
	tray = {safe.x, modeBar.y + modeBar.h, safe.w, tools ? paletteHeight * unit : 0};
	rows.clear();
	brushPanel = {safe.x + std::max(0., (safe.w - 240 * unit) / 2), safe.y + 48 * unit,
				  std::min(safe.w, 240 * unit),
				  (editor.selectionMode == MapEdit::PlaceZone ? 156 : 112) * unit};
	if (inspecting())
	{
		prepareInspector();
		return;
	}
	if (editor.panelMode == MapEdit::Teams)
		return;
	if (!tools)
		return;
	// External keyboard/gallery actions can select an editor mode as well.
	if (editor.panelMode == MapEdit::AddBuildings)
		paletteMode = 2;
	else if (editor.panelMode == MapEdit::AddFlagsAndZones)
		paletteMode = 3;
	else if (editor.panelMode == MapEdit::Terrain && paletteMode >= 2)
		paletteMode = 0;
	std::vector<MapEditorWidget *> items;
	if (paletteMode == 0)
		items = {editor.grass,        editor.sand,         editor.water,
				 editor.deleteButton, editor.areasButton,  editor.noResourceGrowthButton,
				 editor.areaNumber,   editor.areaNameLabel};
	else if (paletteMode == 1)
		items = {editor.wheat,   editor.trees,  editor.stone,  editor.algae,
				 editor.papyrus, editor.orange, editor.cherry, editor.prune};
	else if (paletteMode == 2)
		items = {editor.swarm,        editor.inn,      editor.hospital, editor.racetrack,
				 editor.swimmingpool, editor.barracks, editor.school,   editor.defencetower,
				 editor.stonewall,    editor.market};
	else
		items = {editor.explorationflag, editor.warflag,   editor.clearingflag,
				 editor.forbiddenZone,   editor.guardZone, editor.clearingZone,
				 editor.worker,          editor.explorer,  editor.warrior};
	double extent = 4 * unit;
	for (auto *w : items)
	{
		w->area.updateWindowWidth(gfx->getW());
		const auto a = w->area;
		const double width = std::max(56., std::min(200., double(a.width) + 16)) * unit;
		const double scale = std::min(
			{unit, (width - 12 * unit) / std::max(1, a.width), 44 * unit / std::max(1, a.height)});
		rows.push_back({w, {extent, tray.y + 2 * unit, width, 56 * unit}, scale});
		extent += width + 4 * unit;
	}
	maximum = std::max(0., extent - tray.w);
	syncTray();
	for (auto &row : rows)
		row.rect.x += tray.x - offset;
}
void PhoneEditor::syncTray() { trayAxis.sync(offset, maximum, tray.w); }
void PhoneEditor::syncInspector() { inspectorAxis.sync(inspectorScroll, inspectorMaximum, inspectorBody.h); }
void PhoneEditor::stopScrolling()
{
	mapMotion.interrupt();
	trayAxis.axis.interrupt();
	inspectorAxis.axis.interrupt();
}
bool PhoneEditor::animating() const
{
	return mapMotion.isAnimating() || trayAxis.axis.isAnimating() || inspectorAxis.axis.isAnimating();
}
void PhoneEditor::advance(Uint32 tick)
{
	lastTick = tick;
	if (mapMotion.isAnimating())
	{
		const auto [dx, dy] = mapMotion.stepDelta(tick);
		if (dx != 0 || dy != 0)
		{
			editor.updateCamera();
			editor.camera.originX += dx / editor.camera.zoom;
			editor.camera.originY += dy / editor.camera.zoom;
			editor.camera.normalize();
			editor.viewportX = editor.camera.tileX() & editor.game.map.wMask;
			editor.viewportY = editor.camera.tileY() & editor.game.map.hMask;
		}
	}
	if (trayAxis.axis.isAnimating())
	{
		trayAxis.axis.step(tick);
		trayAxis.publish(offset);
	}
	if (inspectorAxis.axis.isAnimating())
	{
		inspectorAxis.axis.step(tick);
		inspectorAxis.publish(inspectorScroll);
	}
}
void PhoneEditor::clearTool()
{
	const bool visible = tools;
	chooseMode(paletteMode);
	tools = visible;
	pan = false;
}
void PhoneEditor::chooseMode(int mode)
{
	cancel();
	paletteMode = mode;
	offset = 0;
	tools = true;
	const char *modes[] = {"switch to terrain view", "switch to terrain view",
						   "switch to building view", "switch to flag view"};
	editor.performAction(modes[mode]);
}
int PhoneEditor::hit(ViewPoint p) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (!safe.contains(p))
		return -1;
	if (brushOpen)
	{
		if (!brushPanel.contains(p))
			return -29;
		if (p.y >= brushPanel.y + 112 * unit)
			return p.x < brushPanel.x + brushPanel.w / 2 ? -120 : -121;
		return -100 - int((p.x - brushPanel.x) * 4 / brushPanel.w) -
			   4 * int((p.y - brushPanel.y) / (56 * unit));
	}
	if (inspecting())
	{
		if (ViewRect{inspector.x + inspector.w - 48 * unit, inspector.y + 4 * unit, 44 * unit,
					 44 * unit}
				.contains(p))
			return -20;
		if (inspectorBody.contains(p))
			for (size_t i = 0; i < properties.size(); ++i)
			{
				auto r = properties[i].rect;
				if (r.y < inspectorBody.y || r.y + r.h > inspectorBody.y + inspectorBody.h)
					continue;
				if (r.contains(p) && p.y >= r.y + 24 * unit)
					return 1000 + int(i) * 3 +
						   (p.x < r.x + 44 * unit          ? 0
							: p.x >= r.x + r.w - 44 * unit ? 2
														   : 1);
			}
		if (inspector.contains(p))
			return -22;
		return ViewRect{safe.x, safe.y, 88 * unit, 44 * unit}.contains(p) ? -2 : -1;
	}
	if (editor.panelMode == MapEdit::Teams && tray.contains(p))
		return -21;
	if (p.y < safe.y + headerHeight * unit)
		return -2 - std::clamp(int((p.x - safe.x) * 4 / safe.w), 0, 3);
	if (!tools && modeBar.contains(p))
		return -6;
	if (tools && modeBar.contains(p))
		return -10 - std::clamp(int((p.x - modeBar.x) * 4 / modeBar.w), 0, 3);
	if (tools && tray.contains(p))
		for (size_t i = 0; i < rows.size(); ++i)
			if (rows[i].rect.contains(p))
				return int(i);
	return -1;
}
void PhoneEditor::placeAt(ViewPoint p)
{
	editor.mouseX = int(p.x);
	editor.mouseY = int(p.y);
	editor.updateCamera();
	if (editor.selectionMode == MapEdit::PlaceBuilding)
		editor.performAction("place building");
	else if (editor.selectionMode == MapEdit::PlaceUnit)
		editor.performAction("place unit");
	else
	{
		editor.performAction("select map unit");
		editor.performAction("select map building");
	}
}
void PhoneEditor::paintStroke()
{
	if (stroke.empty())
		return;
	const char *prefix = editor.selectionMode == MapEdit::PlaceTerrain   ? "terrain"
						 : editor.selectionMode == MapEdit::PlaceZone    ? "zone"
						 : editor.selectionMode == MapEdit::RemoveObject ? "delete"
						 : editor.selectionMode == MapEdit::ChangeAreas
							 ? "area"
							 : "no ressource growth area";
	editor.updateCamera();
	for (size_t i = 0; i < stroke.size(); ++i)
	{
		editor.mouseX = int(stroke[i].x);
		editor.mouseY = int(stroke[i].y);
		editor.performAction(std::string(prefix) + (i ? " drag motion" : " drag start"));
	}
	editor.performAction(std::string(prefix) + " drag end");
	stroke.clear();
}
bool PhoneEditor::zoomArmed(Uint32 ticks, ViewPoint point) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	return lastTapTicks && Uint32(ticks - *lastTapTicks) <= InGameTouchTheme::doubleTapWindowMs &&
		   std::hypot(point.x - lastTapPoint.x, point.y - lastTapPoint.y) <=
			   InGameTouchTheme::doubleTapRadius * unit &&
		   content.contains(point);
}
bool PhoneEditor::deferredMatchesTool(const DeferredStroke &candidate) const
{
	return candidate.selection == int(editor.selectionMode) &&
		   candidate.terrain == int(editor.terrainType) &&
		   candidate.figure == int(editor.brush.getFigure()) &&
		   candidate.type == int(editor.brush.getType());
}
// A held paint tap lands when its window closes, when another contact begins,
// or on interruption; it is dropped only if its brush is no longer active.
void PhoneEditor::commitDeferred()
{
	if (!deferred)
		return;
	auto held = std::move(*deferred);
	deferred.reset();
	if (held.points.empty() || !deferredMatchesTool(held))
		return;
	auto unfinished = std::move(stroke);
	stroke = std::move(held.points);
	paintStroke();
	stroke = std::move(unfinished);
}
void PhoneEditor::drawZoomReadout()
{
	if (!touch.zoomDragging())
		return;
	char value[16];
	std::snprintf(value, sizeof(value), "%.1f", editor.camera.zoom);
	TouchReadout::draw(touchPoint,
					   GAGCore::FormattableString(Toolkit::getStringTable()->getString("[zoom factor %0]")).arg(value),
					   safe);
}
void PhoneEditor::act(const TouchAction &action)
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	ViewPoint p{action.point.x * unit, action.point.y * unit};
	if (action.kind == TouchActionKind::Cancel)
	{
		stroke.clear();
		stopScrolling();
		return;
	}
	if (action.kind == TouchActionKind::PanEnd)
	{
		mapMotion.endDrag(action.time);
		trayAxis.axis.endDrag(action.time);
		inspectorAxis.axis.endDrag(action.time);
		syncTray();
		syncInspector();
		return;
	}
	if (action.kind == TouchActionKind::BeginStroke || action.kind == TouchActionKind::Stroke ||
		action.kind == TouchActionKind::EndStroke)
	{
		if (!onMap)
			return;
		if (!content.contains(p))
		{
			stroke.clear();
			onMap = false;
			return;
		}
		editor.mouseX = int(p.x);
		editor.mouseY = int(p.y);
		stroke.push_back(p);
		if (action.kind == TouchActionKind::EndStroke)
		{
			if (!touchTravelled)
			{
				deferred = DeferredStroke{std::move(stroke), int(editor.selectionMode), int(editor.terrainType),
										  int(editor.brush.getFigure()), int(editor.brush.getType()),
										  SDL_GetTicks64()};
				stroke.clear();
				lastTapTicks = eventTicks;
				lastTapPoint = p;
			}
			else
				paintStroke();
		}
		return;
	}
	if (action.kind == TouchActionKind::Pan)
	{
		if (inspecting() && !onMap)
		{
			syncInspector();
			inspectorAxis.axis.drag(action.time, -p.y);
			syncInspector();
			return;
		}
		if (!onMap && held >= 0)
		{
			syncTray();
			trayAxis.axis.drag(action.time, -p.x);
			syncTray();
		}
		else if (onMap)
		{
			// Subpixel panning through the camera, as in the game; the tile
			// viewport is derived from it.
			editor.updateCamera();
			editor.camera.originX -= p.x / editor.camera.zoom;
			editor.camera.originY -= p.y / editor.camera.zoom;
			editor.camera.normalize();
			editor.viewportX = editor.camera.tileX() & editor.game.map.wMask;
			editor.viewportY = editor.camera.tileY() & editor.game.map.hMask;
			if (!mapMotion.isDragging())
				mapMotion.beginDrag(action.time);
			mapMotion.drag(action.time, -p.x, -p.y);
		}
		return;
	}
	// MapCamera::wheel steps by 1.1, so a gesture factor converts with that base.
	if (action.kind == TouchActionKind::Zoom && onMap)
	{
		editor.zoomMap(std::log(action.factor) / std::log(1.1), p.x, p.y);
		return;
	}
	if (action.kind == TouchActionKind::ZoomReset && onMap)
	{
		editor.updateCamera();
		if (!editor.zoomMap(std::log(1.0 / editor.camera.zoom) / std::log(1.1), p.x, p.y) && !pan &&
			content.contains(p))
			placeAt(p); // Without a zoomable renderer the second tap acts as before.
		return;
	}
	if (action.kind != TouchActionKind::Select || hit(p) != held)
		return;
	if (held == -20)
	{
		clearTool();
		return;
	}
	if (held == -21)
	{
		editor.suspendInput();
		editor.performAction("open teams editor");
		return;
	}
	if (held == -29)
	{
		brushOpen = false;
		return;
	}
	if (held <= -100 && held > -108)
	{
		editor.brush.setFigure(-100 - held);
		brushOpen = false;
		return;
	}
	if (held == -120 || held == -121)
	{
		editor.brush.setType(held == -120 ? BrushTool::MODE_ADD : BrushTool::MODE_DEL);
		brushOpen = false;
		return;
	}
	if (held >= 1000 && inspecting())
	{
		const int index = (held - 1000) / 3, part = (held - 1000) % 3;
		if (index < int(properties.size()))
		{
			auto &property = properties[index];
			auto *value = property.value;
			int requested = value->currentValue() + (part == 0 ? -1 : 1);
			if (part == 1)
				requested = int(
					std::clamp((p.x - property.rect.x - 48 * unit) / (property.rect.w - 96 * unit),
							   0., 1.) *
						value->maximumValue() +
					.5);
			value->setValue(requested);
			editor.hasMapBeenModified = true;
		}
		return;
	}
	if (held == -6)
	{
		tools = true;
		return;
	}
	if (held <= -10 && held >= -13)
	{
		if (paletteMode == -10 - held)
		{
			tools = false;
			return;
		}
		chooseMode(-10 - held);
		return;
	}
	if (held == -2)
	{
		editor.suspendInput();
		editor.performAction("open menu screen");
		return;
	}
	const bool zone = editor.selectionMode == MapEdit::PlaceZone;
	if (held == -5)
	{
		if (editor.selectionMode != MapEdit::PlaceNothing)
			clearTool();
		else
		{
			cancel();
			pan = !pan;
		}
		return;
	}
	const bool objects = paletteMode >= 2;
	if (held == -3)
	{
		if (objects)
			editor.selectActiveTeam((editor.team + 1) % editor.game.teamsCount());
		else
			brushOpen = !brushOpen;
		return;
	}
	if (held == -4)
	{
		if (zone)
		{
			brushOpen = !brushOpen;
			return;
		}
		if (objects)
		{
			if (editor.selectionMode == MapEdit::PlaceUnit)
				editor.performAction("select unit level " +
									 std::to_string((editor.placingUnitLevel + 1) % 4 + 1));
			else
				editor.performAction("switch to building level " +
									 std::to_string((editor.buildingLevel + 1) % 3 + 1));
		}
		else if (editor.selectionMode != MapEdit::RemoveObject &&
				 !(editor.selectionMode == MapEdit::PlaceTerrain &&
				   editor.terrainType <= TerrainSelector::Water))
			editor.brush.setType(editor.brush.getType() == BrushTool::MODE_DEL
									 ? BrushTool::MODE_ADD
									 : BrushTool::MODE_DEL);
		return;
	}
	if (held >= 0 && held < int(rows.size()))
	{
		auto &row = rows[held];
		auto *w = row.widget;
		if (dynamic_cast<ValueScrollBox *>(w))
		{
			const double part = (p.x - row.rect.x) / row.rect.w;
			w->handleClick(part < .25 ? 0 : part > .75 ? 111 : 10 + int(part * 92), 8);
		}
		else if (dynamic_cast<NumberCycler *>(w) || dynamic_cast<Checkbox *>(w))
		{
			// These small shared widgets own their value change; their click
			// does not dispatch through a composed desktop sidebar.
			w->handleClick(0, 0);
		}
		else
			editor.performAction(w->action);
		pan = false;
		return;
	}
	if (onMap && content.contains(p))
	{
		// Placement taps never arm zoom, so repeated placement stays reliable.
		const bool placing = editor.selectionMode == MapEdit::PlaceBuilding ||
							 editor.selectionMode == MapEdit::PlaceUnit;
		if (!pan)
			placeAt(p);
		if (!placing)
		{
			lastTapTicks = eventTicks;
			lastTapPoint = p;
		}
	}
}
bool PhoneEditor::event(SDL_Event event)
{
	if (editor.hasDialog())
	{
		editor.delegateMenu(event);
		return true;
	}
	if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
										  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
	{
		cancel();
		return false;
	}
	prepare();
	ViewPoint p;
	int phase = -1;
	Sint64 device = -1, id = 0;
	switch (event.type)
	{
	case SDL_FINGERDOWN:
	case SDL_FINGERMOTION:
	case SDL_FINGERUP:
		p = {event.tfinger.x * globalContainer->gfx->getW(),
			 event.tfinger.y * globalContainer->gfx->getH()};
		device = event.tfinger.touchId;
		id = event.tfinger.fingerId;
		phase = event.type == SDL_FINGERDOWN ? 0 : event.type == SDL_FINGERUP ? 2 : 1;
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		if (event.button.which == SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT)
			return true;
		p = {double(event.button.x), double(event.button.y)};
		phase = event.type == SDL_MOUSEBUTTONDOWN ? 0 : 2;
		break;
	case SDL_MOUSEMOTION:
		if (event.motion.which == SDL_TOUCH_MOUSEID)
			return true;
		p = {double(event.motion.x), double(event.motion.y)};
		phase = 1;
		break;
	case SDL_MOUSEWHEEL:
		if (inspecting())
		{
			inspectorScroll = std::clamp(
				inspectorScroll - event.wheel.y * 72 * globalContainer->gfx->logicalUnitsPerPoint(),
				0., inspectorMaximum);
			return true;
		}
		if (tools)
			offset = std::clamp(offset - event.wheel.y * 56 *
											 globalContainer->gfx->logicalUnitsPerPoint(),
								0., maximum);
		return true;
	case SDL_KEYDOWN:
		if (event.key.keysym.sym == SDLK_ESCAPE)
		{
			cancel();
			tools = !tools;
			return true;
		}
		return false;
	default:
		return false;
	}
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const auto key = std::make_pair(device, id);
	const Uint64 time = widenTicks(event.common.timestamp, lastTick);
	if (phase == 0 && !touch.hasPointers() && !drag)
	{
		// A touch catches coasting content where it is; presets are re-read so
		// the settings sliders apply to the next gesture.
		stopScrolling();
		fingerIsTouch = device != -1;
		ScrollPhysicsConfig mapConfig = ScrollPresets::mapViewport();
		mapConfig.momentum = mapConfig.momentum && fingerIsTouch;
		mapMotion.setConfig(mapConfig);
		trayAxis.axis.setConfig(fingerIsTouch ? ScrollPresets::editorTray() : ScrollPresets::mouse());
		inspectorAxis.axis.setConfig(fingerIsTouch ? ScrollPresets::hudPanel() : ScrollPresets::mouse());
	}
	if (!quarantined.empty())
	{
		if (phase == 0)
			quarantined.insert(key);
		if (phase == 2)
			quarantined.erase(key);
		return true;
	}
	if (drag)
	{
		if (key != std::make_pair(drag->device, drag->finger))
		{
			if (phase == 0)
			{
				quarantined.insert(key);
				quarantined.insert({drag->device, drag->finger});
				drag.reset();
				editor.performAction("unselect");
			}
			return true;
		}
		if (!drag->moving &&
			std::hypot(p.x - drag->start.x, p.y - drag->start.y) >= dragThreshold * unit)
		{
			// Horizontal motion browses the tray; moving into the map starts placement.
			if (content.contains(p))
			{
				drag->moving = true;
				editor.performAction(drag->widget->action);
				pan = false;
			}
			else
			{
				drag->browsing = true;
				syncTray();
				if (!trayAxis.axis.isDragging())
					trayAxis.axis.beginDrag(time);
				trayAxis.axis.drag(time, -(p.x - drag->start.x));
				syncTray();
				drag->start = p;
			}
		}
		if (drag->moving)
		{
			editor.mouseX = int(p.x);
			editor.mouseY = int(p.y - previewLift * unit);
		}
		if (phase == 2)
		{
			if (drag->moving)
			{
				if (content.contains(p) && content.contains({p.x, p.y - previewLift * unit}))
					placeAt({p.x, p.y - previewLift * unit});
			}
			else if (!drag->browsing && hit(p) >= 0 && rows[hit(p)].widget == drag->widget)
			{
				editor.performAction(drag->widget->action);
				pan = false;
			}
			if (drag->browsing)
			{
				trayAxis.axis.endDrag(time);
				syncTray();
			}
			drag.reset();
		}
		return true;
	}
	std::vector<TouchAction> actions;
	if (phase == 0)
	{
		if (!touch.hasPointers())
		{
			held = hit(p);
			onMap = held == -1 && content.contains(p);
			const bool placing = editor.selectionMode == MapEdit::PlaceBuilding ||
								 editor.selectionMode == MapEdit::PlaceUnit;
			const bool zoomDrag = onMap && !placing && zoomArmed(event.common.timestamp, p);
			if (zoomDrag)
				deferred.reset(); // That tap was the first half of the zoom.
			else
				commitDeferred();
			lastTapTicks.reset();
			touchKey = key;
			touchStart = touchPoint = p;
			touchTravelled = false;
			if (held >= 0 && held < int(rows.size()) &&
				(dynamic_cast<BuildingSelectorWidget *>(rows[held].widget) ||
				 dynamic_cast<UnitSelector *>(rows[held].widget)))
			{
				drag = Drag{device, id, rows[held].widget, p};
				return true;
			}
			const bool paint = editor.selectionMode == MapEdit::PlaceTerrain ||
							   editor.selectionMode == MapEdit::PlaceZone ||
							   editor.selectionMode == MapEdit::RemoveObject ||
							   editor.selectionMode == MapEdit::ChangeAreas ||
							   editor.selectionMode == MapEdit::ChangeNoResourceGrowthAreas;
			touch.setMode(zoomDrag ? TouchMode::ZoomDrag
						  : onMap && !pan && paint ? TouchMode::Paint
												   : TouchMode::Navigate);
			touch.setZoomDragDirection(globalContainer->settings.dragUpZoomsIn());
		}
		actions = touch.down(device, id, {p.x / unit, p.y / unit}, time);
	}
	else if (phase == 1)
	{
		if (key == touchKey)
		{
			touchPoint = p;
			touchTravelled = touchTravelled || std::hypot(p.x - touchStart.x, p.y - touchStart.y) >=
												   TouchInput::slop * unit;
		}
		actions = touch.move(device, id, {p.x / unit, p.y / unit}, time);
	}
	else
	{
		eventTicks = event.common.timestamp;
		actions = touch.up(device, id, {p.x / unit, p.y / unit}, time);
	}
	for (const auto &a : actions)
		act(a);
	if (phase == 2 && !touch.hasPointers())
	{
		// A touch that stopped a bounce without dragging lets it finish.
		trayAxis.axis.settle(time);
		inspectorAxis.axis.settle(time);
		syncTray();
		syncInspector();
	}
	return true;
}
void PhoneEditor::label(ViewRect r, const std::string &text)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double scale = gfx->logicalUnitsPerPoint();
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255, 249, 229)));
	SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
	gfx->setUITransform(scale, r.x + 4 * scale, r.y + (r.h - 16 * scale) / 2, &clip);
	gfx->drawString(0, 0, font, text, std::max(1, int(r.w / scale - 8)));
	gfx->setUITransform();
	gfx->setClipRect();
	font->popStyle();
}
void PhoneEditor::draw()
{
	if (deferred && SDL_GetTicks64() - deferred->ticks >= InGameTouchTheme::doubleTapWindowMs)
		commitDeferred();
	if (editor.hasDialog())
	{
		editor.drawDialog();
		return;
	}
	prepare();
	if (inspecting())
	{
		drawInspector();
		drawZoomReadout();
		return;
	}
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const bool objects = paletteMode >= 2;
	std::string labels[] = {
		GAGCore::Toolkit::getStringTable()->getString("[Menu]"),
		objects
			? GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString("[Team %0]"))
				  .arg(editor.team + 1)
			: GAGCore::FormattableString(
				  GAGCore::Toolkit::getStringTable()->getString("[Brush %0x%1]"))
				  .arg(BrushTool::getBrushWidth(editor.brush.getFigure()))
				  .arg(BrushTool::getBrushHeight(editor.brush.getFigure())),
		objects ? GAGCore::FormattableString(
					  GAGCore::Toolkit::getStringTable()->getString("[Level %0]"))
					  .arg((editor.selectionMode == MapEdit::PlaceUnit ? editor.placingUnitLevel
																	   : editor.buildingLevel) +
						   1)
				: (editor.brush.getType() == BrushTool::MODE_DEL
					   ? GAGCore::Toolkit::getStringTable()->getString("[Erase]")
					   : GAGCore::Toolkit::getStringTable()->getString("[Paint]")),
		pan ? GAGCore::Toolkit::getStringTable()->getString("[Pan]")
		: editor.selectionMode != MapEdit::PlaceNothing
			? GAGCore::Toolkit::getStringTable()->getString("[Done]")
			: GAGCore::Toolkit::getStringTable()->getString("[Select]")};
	if (editor.selectionMode == MapEdit::PlaceTerrain &&
		editor.terrainType <= TerrainSelector::Water)
	{
		const std::string materials[] = {GAGCore::Toolkit::getStringTable()->getString("[grass]"),
										 GAGCore::Toolkit::getStringTable()->getString("[sand]"),
										 GAGCore::Toolkit::getStringTable()->getString("[Water]")};
		labels[2] = materials[editor.terrainType];
	}
	else if (editor.selectionMode == MapEdit::RemoveObject)
		labels[2] = GAGCore::Toolkit::getStringTable()->getString("[delete]");
	// Zones need both an owner and brush controls. Two fingers still pan,
	// so the last toolbar slot can expose Paint/Erase while this tool is active.
	if (editor.selectionMode == MapEdit::PlaceZone)
	{
		labels[2] = GAGCore::FormattableString(
						GAGCore::Toolkit::getStringTable()->getString("[Brush %0x%1]"))
						.arg(BrushTool::getBrushWidth(editor.brush.getFigure()))
						.arg(BrushTool::getBrushHeight(editor.brush.getFigure()));
		labels[3] = GAGCore::Toolkit::getStringTable()->getString("[Done]");
	}
	for (int i = 0; i < 4; ++i)
	{
		ViewRect r{safe.x + i * safe.w / 4, safe.y, safe.w / 4 - unit, 44 * unit};
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::paper);
		label(r, labels[i]);
		if (i == 1 && objects)
			gfx->drawFilledRect(int(r.x + 4 * unit), int(r.y + r.h - 5 * unit), int(r.w - 8 * unit),
								int(3 * unit), editor.game.teams[editor.team]->color);
	}
	drawInteractionPreview();
	drawZoomReadout();
	if (!tools)
	{
		gfx->drawFilledRect(int(modeBar.x), int(modeBar.y), int(modeBar.w), int(modeBar.h),
							InGameTouchTheme::paper);
		label(modeBar, GAGCore::Toolkit::getStringTable()->getString("[Show palette]"));
		return;
	}
	gfx->drawFilledRect(int(modeBar.x), int(modeBar.y), int(modeBar.w), int(modeBar.h + tray.h),
						InGameTouchTheme::paper);
	const std::string modes[] = {Toolkit::getStringTable()->getString("[Terrain]"),
								 Toolkit::getStringTable()->getString("[Resources]"),
								 Toolkit::getStringTable()->getString("[Buildings]"),
								 Toolkit::getStringTable()->getString("[Flags]")};
	for (int i = 0; i < 4; ++i)
	{
		ViewRect r{modeBar.x + i * modeBar.w / 4, modeBar.y, modeBar.w / 4 - unit, modeBar.h};
		if (i == paletteMode && editor.panelMode != MapEdit::Teams)
			gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::selected);
		label(r, modes[i]);
	}
	if (editor.panelMode == MapEdit::Teams)
		label(tray, GAGCore::FormattableString(
						GAGCore::Toolkit::getStringTable()->getString("[Manage teams (%0)]"))
						.arg(editor.game.teamsCount()));
	for (const auto &row : rows)
	{
		const auto r = row.rect;
		const auto a = row.widget->area;
		const double left = std::max(r.x, tray.x), right = std::min(r.x + r.w, tray.x + tray.w);
		if (right <= left)
			continue;
		SDL_Rect clip{int(left), int(tray.y), int(right - left), int(tray.h)};
		gfx->setClipRect(clip.x, clip.y, clip.w, clip.h);
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::field);
		gfx->setUITransform(row.scale, r.x + (r.w - a.width * row.scale) / 2 - a.x * row.scale,
							r.y + (r.h - a.height * row.scale) / 2 - a.y * row.scale, &clip);
		row.widget->draw();
		gfx->setUITransform();
		gfx->setClipRect();
	}
	if (maximum > 0)
		gfx->drawFilledRect(
			int(tray.x + offset / (maximum + tray.w) * tray.w), int(tray.y + tray.h - 2 * unit),
			int(tray.w * tray.w / (maximum + tray.w)), int(2 * unit), InGameTouchTheme::border);
	drawBrushPanel();
}
