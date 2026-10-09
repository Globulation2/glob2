// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include "InGameTouchTheme.h"
#include "TouchReadout.h"
#include "ThumbSide.h"
#include "BrushCoverage.h"
#include "Map.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "MobileSafeArea.h"
#include "GlobalContainer.h"
#include <TouchText.h>
#include <ApplicationHost.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <cmath>
#include <cstdio>
#include <set>
using namespace GAGCore;
namespace
{
// Point-based presentation policy. Gesture tuning must not change editor rules.
constexpr double headerHeight = 48;
constexpr double modeHeight = 44;
constexpr double dragThreshold = 8;
constexpr double previewLift = 40;
} // namespace

PhoneEditor::PhoneEditor(MapEdit &editor) : editor(editor) {}
PhoneEditor::~PhoneEditor()
{
	if (!publishedControls.empty())
		ApplicationHost::controlsChanged(this, nullptr);
}
bool PhoneEditor::hasOverlay() const
{
	return editor.hasDialog();
}
void PhoneEditor::cancel()
{
	if (drag && drag->moving)
		editor.performAction("unselect");
	railTouched = -1;
	peekOpen = false;
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
// All rectangles are independent editor presentation bounds. Tray cards come
// from the brush catalogue (PhoneEditorTray.cpp).
void PhoneEditor::prepare()
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	safe = mobileDialogSafe(gfx);
	// External keyboard/gallery actions can select an editor mode as well.
	if (editor.panelMode == MapEdit::AddBuildings)
		paletteMode = 2;
	else if (editor.panelMode == MapEdit::AddFlagsAndZones)
		paletteMode = 3;
	else if (editor.panelMode == MapEdit::Teams)
		paletteMode = 4;
	else if (editor.panelMode == MapEdit::Terrain && paletteMode >= 2)
		paletteMode = 0;
	const bool cards = tools && !inspecting() && paletteMode < 4;
	if (cards)
		layoutTray(unit);
	else
	{
		trayLayoutKey.clear();
		rows.clear();
		chips.clear();
	}
	// Short landscape phones keep more of the map: the strips tighten.
	const bool tight = safe.h < 480 * unit;
	const double modes = tight ? 38 : modeHeight, cardsHeight = tight ? cardHeight - 6 : cardHeight;
	const double chipsHeight = cards && chips.size() > 1 ? (tight ? chipHeight - 4 : chipHeight) : 0;
	const double dock = (modes + (tools ? cardsHeight + chipsHeight : 0)) * unit;
	content = {safe.x, safe.y + headerHeight * unit, safe.w,
			   std::max(0., safe.h - headerHeight * unit - dock)};
	modeBar = {safe.x, safe.y + safe.h - dock, safe.w, modes * unit};
	tray = {safe.x, modeBar.y + modeBar.h, safe.w, tools ? (cardsHeight + chipsHeight) * unit : 0};
	chipBar = {tray.x, tray.y, tray.w, chipsHeight * unit};
	cardBar = {tray.x, tray.y + chipBar.h, tray.w, tray.h - chipBar.h};
	if (inspecting())
	{
		prepareInspector();
		return;
	}
	if (cards)
		prepareTray(unit);
}
void PhoneEditor::syncTray()
{
	if (nativeScrolling && nativeSurface == 1 && trayAxis.externallyMoved(offset))
	{ nativeScroll.cancel(trayAxis.axis); nativeScrolling = false; }
	trayAxis.sync(offset, maximum, tray.w);
}
void PhoneEditor::syncInspector()
{
	if (nativeScrolling && nativeSurface == 2 && inspectorAxis.externallyMoved(inspectorScroll))
	{ nativeScroll.cancel(inspectorAxis.axis); nativeScrolling = false; }
	inspectorAxis.sync(inspectorScroll, inspectorMaximum, inspectorBody.h);
}
void PhoneEditor::stopScrolling()
{
	if (nativeScrolling)
	{
		nativeScroll.cancel(nativeAxis().axis);
		nativeAxis().publish(nativeOffset());
		nativeScrolling = false;
	}
	mapMotion.interrupt();
	trayAxis.axis.interrupt();
	inspectorAxis.axis.interrupt();
}
bool PhoneEditor::animating() const
{
	return (nativeScrolling && nativeScroll.needsFrames()) || mapMotion.isAnimating() || trayAxis.axis.isAnimating() || inspectorAxis.axis.isAnimating();
}
void PhoneEditor::advance(Uint32 tick)
{
	lastTick = tick;
	if (nativeScrolling && (editor.hasDialog() || (nativeSurface == 2 && !inspecting()) || (nativeSurface == 1 && !tools))) stopScrolling();
	if (nativeScrolling)
	{
		if (nativeSurface == 2) syncInspector(); else syncTray();
		nativeScroll.update(tick, nativeAxis().axis);
		nativeAxis().publish(nativeOffset());
	}
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
	undo.reset();
	paletteMode = mode;
	offset = 0;
	tools = true;
	const char *modes[modeCount] = {"switch to terrain view", "switch to terrain view",
									"switch to building view", "switch to flag view",
									"switch to teams view"};
	editor.performAction(modes[mode]);
}
int PhoneEditor::hit(ViewPoint p) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (!safe.contains(p))
		return -1;
	if (peekOpen)
	{
		// The map peek owns every touch until it closes.
		if (peekRect().contains(p))
			return -300;
		const auto buttons = peekButtons();
		for (int i = 0; i < int(buttons.size()); ++i)
			if (buttons[i].contains(p))
				return -301 - i;
		return -304;
	}
	if (showsMapButton() && mapButton().contains(p))
		return -305;
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
		return -10 - std::clamp(int((p.x - modeBar.x) * modeCount / modeBar.w), 0, modeCount - 1);
	if (tools && chipBar.contains(p))
	{
		for (size_t i = 0; i < chips.size(); ++i)
			if (chips[i].rect.contains(p))
				return -400 - int(i);
		return -23; // Between chips: inert.
	}
	if (tools && tray.contains(p))
		for (size_t i = 0; i < rows.size(); ++i)
			if (rows[i].rect.contains(p))
				return int(i);
	if (paintMode())
	{
		const auto railHit = BrushHUD::hit(rail(), p);
		if (railHit.part == BrushHUD::Part::Detent)
			return -200 - railHit.index;
		if (railHit.part == BrushHUD::Part::Pan)
			return -210;
		if (railHit.part == BrushHUD::Part::Mode)
			return -211;
		if (railHit.part == BrushHUD::Part::Undo)
			return -213;
	}
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
bool PhoneEditor::paintMode() const
{
	const auto mode = editor.selectionMode;
	return !inspecting() && editor.panelMode != MapEdit::Teams &&
		   (mode == MapEdit::PlaceTerrain || mode == MapEdit::PlaceZone || mode == MapEdit::RemoveObject ||
			mode == MapEdit::ChangeAreas || mode == MapEdit::ChangeNoResourceGrowthAreas);
}
BrushHUD::Layout PhoneEditor::rail() const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint(), inset = 8 * unit;
	// Paint/Erase applies to every brush except deletion: zones, areas,
	// no-growth areas, resources and terrain (Erase reverts it to grass).
	const bool mode = editor.selectionMode != MapEdit::RemoveObject;
	return BrushHUD::layout({content.x + inset, content.y + inset, content.w - 2 * inset, content.h - 2 * inset},
							ThumbSide::left(), unit, mode, true, bool(undo));
}
// Zone, area and no-growth strokes only change tile masks, so their tiles (and
// the displayed zone bits) are restored exactly. Terrain and delete strokes
// remove units, buildings and resources and are not undoable.
void PhoneEditor::applyUndo()
{
	if (!undo)
		return;
	auto &map = editor.game.map;
	for (size_t i = 0; i < undo->cells.size(); ++i)
	{
		const auto [x, y] = undo->cells[i];
		map.replaceTile(x, y, undo->tiles[i]);
		if (undo->zoneView)
			undo->zoneView->set(size_t(map.w * y + x), undo->view[i]);
	}
	editor.game.regenerateDiscoveryMap();
	editor.hasMapBeenModified = true;
	undo.reset();
}
void PhoneEditor::paintStroke()
{
	if (stroke.empty())
		return;
	auto &map = editor.game.map;
	const bool undoable = editor.selectionMode == MapEdit::PlaceZone ||
						  editor.selectionMode == MapEdit::ChangeAreas ||
						  editor.selectionMode == MapEdit::ChangeNoResourceGrowthAreas;
	EditorUndo snapshot;
	if (undoable)
	{
		// Snapshot the covered cells with a one-cell margin.
		std::vector<BrushCoverage::Cell> centres;
		for (const auto &p : stroke)
		{
			const auto [wx, wy] = editor.camera.screenToWorld(p.x, p.y);
			centres.push_back(BrushCoverage::cellAt(wx, wy));
		}
		std::set<std::pair<int, int>> cells;
		for (const auto &[cx, cy] : BrushCoverage::cells(editor.brush.getFigure(), centres))
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					cells.insert({(cx + dx) & map.wMask, (cy + dy) & map.hMask});
		if (editor.selectionMode == MapEdit::PlaceZone)
			snapshot.zoneView = &editor.areaBrushTarget().view;
		for (const auto &cell : cells)
		{
			snapshot.cells.push_back(cell);
			snapshot.tiles.push_back(map.getTile(cell.first, cell.second));
			if (snapshot.zoneView)
				snapshot.view.push_back(snapshot.zoneView->get(size_t(map.w * cell.second + cell.first)));
		}
	}
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
	undo.reset();
	bool changed = false;
	for (size_t i = 0; i < snapshot.cells.size() && !changed; ++i)
	{
		const auto &now = map.getTile(snapshot.cells[i].first, snapshot.cells[i].second);
		const auto &was = snapshot.tiles[i];
		changed = now.forbidden != was.forbidden || now.guardArea != was.guardArea ||
				  now.clearArea != was.clearArea || now.farmArea != was.farmArea || now.scriptAreas != was.scriptAreas ||
				  now.canResourcesGrow != was.canResourcesGrow;
	}
	if (changed)
	{
		snapshot.expires = SDL_GetTicks() + InGameTouchTheme::brushUndoMs;
		undo = std::move(snapshot);
	}
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
										  SDL_GetTicks()};
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
	if (action.kind == TouchActionKind::DoubleTap && onMap)
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
	if (held == -300)
	{
		navigatePeek(p);
		return;
	}
	if (held == -302 || held == -303)
	{
		editor.zoomMap((held == -303 ? 1 : -1) * std::log(InGameTouchTheme::peekZoomStep) / std::log(1.1),
					   content.x + content.w / 2, content.y + content.h / 2);
		return;
	}
	if (held == -301 || held == -304)
	{
		peekOpen = false; // Done, or a tap outside the peek.
		return;
	}
	if (held == -305)
	{
		cancel();
		peekOpen = true;
		return;
	}
	if (held <= -200 && held > -200 - int(BrushTool::BRUSH_COUNT))
	{
		editor.brush.setFigure(unsigned(-200 - held));
		return;
	}
	if (held == -210)
	{
		pan = !pan;
		return;
	}
	if (held == -211)
	{
		editor.brush.setType(editor.brush.getType() == BrushTool::MODE_DEL ? BrushTool::MODE_ADD
																			: BrushTool::MODE_DEL);
		return;
	}
	if (held == -213)
	{
		applyUndo();
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
	if (held <= -400 && held > -400 - int(chips.size()))
	{
		jumpToChip(-400 - held);
		return;
	}
	if (held <= -10 && held > -10 - modeCount)
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
		// Brush modes show their size here; the rail changes it.
		if (objects)
			editor.selectActiveTeam((editor.team + 1) % editor.view.scene->entities.teamCount);
		return;
	}
	if (held == -4)
	{
		if (objects)
		{
			if (editor.selectionMode == MapEdit::PlaceUnit)
				editor.performAction("select unit level " +
									 std::to_string((editor.placingUnitLevel + 1) % 4 + 1));
			else
				editor.performAction("switch to building level " +
									 std::to_string((editor.buildingLevel + 1) % editor.buildingLevelCount + 1));
		}
		else if (editor.selectionMode != MapEdit::RemoveObject &&
				 !(editor.selectionMode == MapEdit::PlaceTerrain &&
				   TerrainSelector::isBaseTerrain(editor.terrainType)))
			editor.brush.setType(editor.brush.getType() == BrushTool::MODE_DEL
									 ? BrushTool::MODE_ADD
									 : BrushTool::MODE_DEL);
		return;
	}
	if (held >= 0 && held < int(rows.size()))
	{
		const Row row = rows[held]; // Activation may rebuild the tray.
		activateRow(row, p);
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
		if (nativeScrolling) stopScrolling();
		GAGCore::GraphicContext::translateMouseEvent(&event);
		editor.delegateMenu(event);
		return true;
	}
	if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
										  event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED))
	{
		cancel();
		return false;
	}
	prepare();
	if (auto sample = scrollGesture(event))
	{
		if (!editor.inputState.hasFocus()) return true;
		GraphicContext::translateMouseEvent(&event);
		sample = scrollGesture(event);
		if (sample->phase == ScrollGesturePhase::Began && sample->sequence != nativeSequence)
		{
			const int previousSurface = nativeScrolling ? nativeSurface : 0;
			const auto previousAxis = nativeAxis().axis;
			stopScrolling();
			nativeSequence = sample->sequence;
			nativeSurface = 0;
			const ViewPoint point{sample->x, sample->y};
			if (inspecting() && inspectorBody.contains(point)) nativeSurface = 2;
			else if (tools && tray.contains(point)) nativeSurface = 1;
			if (nativeSurface)
			{
				if (nativeSurface == previousSurface)
				{
					nativeAxis().axis = previousAxis;
					nativeAxis().publish(nativeOffset());
				}
				auto &axis = nativeAxis().axis;
				nativeScroll.begin(*sample, axis, nativeOffset(), axis.maximum(), nativeSurface == 2 ? inspectorBody.h : tray.w, nativeSurface == 1);
				nativeScrolling = true;
			}
		}
		if (sample->sequence != nativeSequence) return true;
		if (!nativeSurface)
		{
			// Over the map a trackpad or wheel gesture zooms about the pointer.
			const ViewPoint at{sample->x, sample->y};
			if (content.contains(at) && !peekOpen)
			{
				const auto wheel = gestureWheelFallback(*sample);
				const double delta = wheel.wheel.y * (wheel.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
				if (delta != 0)
					editor.zoomMap(delta, int(at.x), int(at.y));
			}
			return true;
		}
		if (nativeScrolling)
		{
			nativeScroll.handle(*sample, nativeAxis().axis);
			if (nativeSurface == 2) syncInspector(); else syncTray();
		}
		return true;
	}
	if (nativeScrolling && (event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_FINGER_DOWN)) stopScrolling();
	// Pointer positions arrive in window coordinates; the tray works in logical
	// pixels like the rest of the editor. Fingers are normalized already.
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
		event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL)
		GraphicContext::translateMouseEvent(&event);
	ViewPoint p;
	int phase = -1;
	SDL_TouchID device = SDL_MOUSE_TOUCHID;
	SDL_FingerID id = 0;
	switch (event.type)
	{
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_MOTION:
	case SDL_EVENT_FINGER_UP:
		p = {event.tfinger.x * globalContainer->gfx->getW(),
			 event.tfinger.y * globalContainer->gfx->getH()};
		device = event.tfinger.touchID;
		id = event.tfinger.fingerID;
		phase = event.type == SDL_EVENT_FINGER_DOWN ? 0 : event.type == SDL_EVENT_FINGER_UP ? 2 : 1;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event.button.which == SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT)
			return true;
		p = {double(event.button.x), double(event.button.y)};
		phase = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0 : 2;
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if (event.motion.which == SDL_TOUCH_MOUSEID)
			return true;
		p = {double(event.motion.x), double(event.motion.y)};
		phase = 1;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
	{
		// The wheel acts on what the pointer is over: the inspector or tray
		// scrolls, the map zooms about the pointer as on the desktop.
		const ViewPoint at{event.wheel.mouse_x, event.wheel.mouse_y};
		const double u = globalContainer->gfx->logicalUnitsPerPoint();
		const double flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
		const double steps = (event.wheel.y - event.wheel.x) * flip;
		if (inspecting() && inspector.contains(at))
		{
			inspectorScroll = std::clamp(inspectorScroll - event.wheel.y * flip * 72 * u, 0., inspectorMaximum);
			return true;
		}
		if (tools && cardBar.contains(at))
		{
			offset = std::clamp(offset - steps * 56 * u, 0., maximum);
			return true;
		}
		if (content.contains(at) && !peekOpen && event.wheel.y != 0)
			editor.zoomMap(event.wheel.y * flip, int(at.x), int(at.y));
		return true;
	}
	case SDL_EVENT_KEY_DOWN:
		if (event.key.key == SDLK_ESCAPE)
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
	const Uint64 time = (event.common.timestamp / SDL_NS_PER_MS);
	if (phase == 0 && !touch.hasPointers() && !drag)
	{
		// A touch catches coasting content where it is; presets are re-read so
		// the settings sliders apply to the next gesture.
		stopScrolling();
		fingerIsTouch = device != SDL_MOUSE_TOUCHID;
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
				editor.performAction(drag->action);
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
			else if (!drag->browsing && hit(p) >= 0 && rows[hit(p)].id == drag->id)
			{
				editor.performAction(drag->action);
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
			const bool zoomDrag = onMap && !placing && zoomArmed(event.common.timestamp / SDL_NS_PER_MS, p);
			if (zoomDrag)
				deferred.reset(); // That tap was the first half of the zoom.
			else
				commitDeferred();
			lastTapTicks.reset();
			touchKey = key;
			touchStart = touchPoint = p;
			touchTravelled = false;
			// Touching a rail size selects it at once; the thumb can then scrub.
			railTouched = held <= -200 && held > -200 - int(BrushTool::BRUSH_COUNT) ? -200 - held : -1;
			if (railTouched >= 0)
				editor.brush.setFigure(unsigned(railTouched));
			if (held >= 0 && held < int(rows.size()) && dragPlaces(rows[held]))
			{
				drag = Drag{device, id, rows[held].id, editor.findBrush(rows[held].id)->action, p};
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
		if (key == touchKey && held == -300 && peekOpen)
			navigatePeek(p);
		if (key == touchKey && railTouched >= 0)
			if (const int detent = BrushHUD::detentAt(rail(), p); detent >= 0)
			{
				editor.brush.setFigure(unsigned(detent));
				railTouched = detent;
			}
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
		eventTicks = event.common.timestamp / SDL_NS_PER_MS;
		actions = touch.up(device, id, {p.x / unit, p.y / unit}, time);
		if (key == touchKey)
			railTouched = -1;
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
	const double unit = gfx->logicalUnitsPerPoint(), scale = gfx->textUnitsPerPoint();
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255, 249, 229)));
	SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
	gfx->setUITransform(scale, r.x + 4 * unit, r.y + (r.h - 16 * scale) / 2, &clip);
	gfx->drawString(0, 0, font, text, std::max(1, int((r.w - 8 * unit) / scale)));
	gfx->setUITransform();
	gfx->setClipRect();
	font->popStyle();
}
void PhoneEditor::centredLabel(ViewRect r, const std::string &text)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double scale = gfx->textUnitsPerPoint();
	const double width = font->getStringWidth(text) * scale;
	if (width + 4 * gfx->logicalUnitsPerPoint() > r.w)
	{
		label(r, text); // Too long to centre: start at the edge and clip.
		return;
	}
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255, 249, 229)));
	SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
	gfx->setUITransform(scale, r.x + (r.w - width) / 2, r.y + (r.h - font->getStringHeight(text) * scale) / 2, &clip);
	gfx->drawString(0, 0, font, text);
	gfx->setUITransform();
	gfx->setClipRect();
	font->popStyle();
}
void PhoneEditor::draw()
{
	if (deferred && SDL_GetTicks() - deferred->ticks >= InGameTouchTheme::doubleTapWindowMs)
		commitDeferred();
	if (editor.hasDialog())
	{
		publishControls(false);
		editor.drawDialog();
		return;
	}
	prepare();
	publishControls(tools && !inspecting());
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
		TerrainSelector::isBaseTerrain(editor.terrainType))
	{
		const auto type = TerrainSelector::baseTerrain(editor.terrainType);
		const auto *label = editor.view.scene->map.terrainPresentation(type).label;
		labels[2] = unsigned(type) < TERRAIN_COUNT
						? GAGCore::Toolkit::getStringTable()->getString(label)
						: label;
	}
	else if (editor.selectionMode == MapEdit::RemoveObject)
		labels[2] = GAGCore::Toolkit::getStringTable()->getString("[delete]");
	// Zones show Paint/Erase beside the size; the rail also changes both, and
	// its Pan toggle moves the map with one finger.
	if (editor.selectionMode == MapEdit::PlaceZone)
	{
		labels[2] = editor.brush.getType() == BrushTool::MODE_DEL
						? GAGCore::Toolkit::getStringTable()->getString("[Erase]")
						: GAGCore::Toolkit::getStringTable()->getString("[Paint]");
		labels[3] = GAGCore::Toolkit::getStringTable()->getString("[Done]");
	}
	for (int i = 0; i < 4; ++i)
	{
		ViewRect r{safe.x + i * safe.w / 4, safe.y, safe.w / 4 - unit, 44 * unit};
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::paper());
		label(r, labels[i]);
		if (i == 1 && objects)
			gfx->drawFilledRect(int(r.x + 4 * unit), int(r.y + r.h - 5 * unit), int(r.w - 8 * unit),
								int(3 * unit), presentationColor(editor.view.scene->entities.teams[editor.team].color));
	}
	drawInteractionPreview();
	if (undo && (!paintMode() || SDL_GetTicks() >= undo->expires))
		undo.reset();
	if (paintMode())
	{
		auto *strings = Toolkit::getStringTable();
		BrushHUD::State state;
		state.figure = editor.brush.getFigure();
		state.erase = editor.brush.getType() == BrushTool::MODE_DEL;
		state.pan = pan;
		state.touched = railTouched;
		state.modeLabel = state.erase ? strings->getString("[Erase]") : strings->getString("[Paint]");
		state.panLabel = strings->getString("[Pan]");
		state.undoLabel = strings->getString("[Undo stroke]");
		BrushHUD::draw(rail(), state);
	}
	drawZoomReadout();
	drawPeek(); // The Map button, or the open peek over everything.
	if (!tools)
	{
		gfx->drawFilledRect(int(modeBar.x), int(modeBar.y), int(modeBar.w), int(modeBar.h),
							InGameTouchTheme::paper());
		label(modeBar, GAGCore::Toolkit::getStringTable()->getString("[Show palette]"));
		drawStatusToast();
		return;
	}
	// The mode strip and tray are opaque: cards read against one backdrop.
	Color backdrop = InGameTouchTheme::paper();
	backdrop.a = 255;
	gfx->drawFilledRect(int(modeBar.x), int(modeBar.y), int(modeBar.w), int(modeBar.h + tray.h), backdrop);
	const std::string modes[modeCount] = {Toolkit::getStringTable()->getString("[Terrain]"),
										  Toolkit::getStringTable()->getString("[Resources]"),
										  Toolkit::getStringTable()->getString("[Buildings]"),
										  Toolkit::getStringTable()->getString("[Flags]"),
										  Toolkit::getStringTable()->getString("[Teams]")};
	for (int i = 0; i < modeCount; ++i)
	{
		ViewRect r{modeBar.x + i * modeBar.w / modeCount, modeBar.y, modeBar.w / modeCount - unit, modeBar.h};
		if (i == paletteMode)
			gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::selected());
		centredLabel(r, modes[i]);
	}
	if (editor.panelMode == MapEdit::Teams)
	{
		// One wide card opens the teams editor.
		const ViewRect card{tray.x + 8 * unit, tray.y + 6 * unit, tray.w - 16 * unit, tray.h - 14 * unit};
		gfx->drawFilledRect(int(card.x), int(card.y), int(card.w), int(card.h), InGameTouchTheme::field());
		for (int i = 0; i < editor.view.scene->entities.teamCount && i < 16; ++i)
			gfx->drawFilledRect(int(card.x + (10 + 22 * i) * unit), int(card.y + card.h - 12 * unit), int(16 * unit),
								int(5 * unit), presentationColor(editor.view.scene->entities.teams[i].color));
		centredLabel({card.x, card.y, card.w, card.h - 10 * unit},
					 GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString("[Manage teams (%0)]"))
						 .arg(editor.view.scene->entities.teamCount));
	}
	drawTray();
	drawStatusToast();
}
