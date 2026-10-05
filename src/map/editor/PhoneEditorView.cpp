// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
// Touch editor presentation. Reads existing selection/value bindings; all edits
// return through named editor actions or ValueScrollBox::setValue.
#include "PhoneEditor.h"
#include "MapEdit.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include "BrushCoverage.h"
#include "render/Minimap.h"
#include "ThumbSide.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include "UnitDisplayNames.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <set>
using namespace GAGCore;
namespace
{
void surface(ViewRect r, Color color = InGameTouchTheme::paper())
{
	globalContainer->gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), color);
}
std::string translated(const char *key)
{
	return Toolkit::getStringTable()->getString(key);
}
} // namespace
bool PhoneEditor::inspecting() const
{
	return editor.panelMode == MapEdit::BuildingEditor || editor.panelMode == MapEdit::UnitEditor;
}
void PhoneEditor::prepareInspector()
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	const bool wide = safe.w >= 520 * u;
	const double w = wide ? 288 * u : safe.w;
	const double h = wide ? safe.h - 56 * u : std::min(280 * u, safe.h * .56);
	inspector = {safe.x + safe.w - w, safe.y + safe.h - h, w, h};
	inspectorBody = {inspector.x + 8 * u, inspector.y + 76 * u, w - 16 * u, h - 80 * u};
	content = wide ? ViewRect{safe.x, safe.y + 48 * u, safe.w - w - 4 * u, safe.h - 48 * u}
				   : ViewRect{safe.x, safe.y + 48 * u, safe.w, inspector.y - safe.y - 48 * u};
	const int identity = editor.panelMode == MapEdit::BuildingEditor
							 ? editor.selectedBuildingGID
							 : 65536 + editor.selectedUnitGID;
	if (identity != inspectorIdentity)
	{
		inspectorScroll = 0;
		inspectorIdentity = identity;
	}
	properties.clear();
	auto add = [&](const char *key, ValueScrollBox *value)
	{
		if (value->enabled)
			properties.push_back({value, translated(key), {}});
	};
	if (editor.panelMode == MapEdit::BuildingEditor)
	{
		add("[hp]", editor.buildingHPScrollBox);
		add("[Wheat]", editor.buildingFoodQuantityScrollBox);
		add("[assigned]", editor.buildingAssignedScrollBox);
		add("[Worker Ratio]", editor.buildingWorkerRatioScrollBox);
		add("[Explorer Ratio]", editor.buildingExplorerRatioScrollBox);
		add("[Warrior Ratio]", editor.buildingWarriorRatioScrollBox);
		add("[Cherry]", editor.buildingCherryScrollBox);
		add("[Orange]", editor.buildingOrangeScrollBox);
		add("[Prune]", editor.buildingPruneScrollBox);
		add("[Stone]", editor.buildingStoneScrollBox);
		add("[Bullets]", editor.buildingBulletsScrollBox);
		add("[Minimum Level To Flag]", editor.buildingMinimumLevelScrollBox);
		add("[range]", editor.buildingRadiusScrollBox);
	}
	else
	{
		add("[hp]", editor.unitHPScrollBox);
		add("[Walk]", editor.unitWalkLevelScrollBox);
		add("[Swim]", editor.unitSwimLevelScrollBox);
		add("[Build]", editor.unitBuildLevelScrollBox);
		add("[At. speed]", editor.unitAttackSpeedLevelScrollBox);
		add("[At. strength]", editor.unitAttackStrengthLevelScrollBox);
		add("[Magic At. Ground]", editor.unitMagicGroundAttackLevelScrollBox);
	}
	inspectorMaximum = std::max(0., properties.size() * 72 * u - inspectorBody.h);
	syncInspector();
	for (size_t i = 0; i < properties.size(); ++i)
		properties[i].rect = {inspectorBody.x, inspectorBody.y + i * 72 * u - inspectorScroll,
							  inspectorBody.w, 68 * u};
}
void PhoneEditor::drawInspector()
{
	auto *gfx = globalContainer->gfx;
	const double u = gfx->logicalUnitsPerPoint();
	surface({safe.x, safe.y, 88 * u, 44 * u});
	label({safe.x, safe.y, 88 * u, 44 * u}, translated("[menu]"));
	surface(inspector);
	const ViewRect close{inspector.x + inspector.w - 48 * u, inspector.y + 4 * u, 44 * u, 44 * u};
	surface(close, InGameTouchTheme::field());
	label(close, "X");
	std::string title, detail;
	MapEditorWidget *picture = nullptr;
	if (editor.panelMode == MapEdit::BuildingEditor)
	{
		auto *b = editor.game.teams[Building::GIDtoTeam(editor.selectedBuildingGID)]
					  ->myBuildings[Building::GIDtoID(editor.selectedBuildingGID)];
		if (!b)
		{
			clearTool();
			return;
		}
		title = translated(("[" + b->type->type + "]").c_str());
		detail = GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Team %0 / Level %1]"))
					 .arg(b->owner->teamNumber + 1)
					 .arg(b->type->level + 1);
		picture = editor.buildingPicture;
	}
	else
	{
		auto *unit = editor.game.teams[Unit::GIDtoTeam(editor.selectedUnitGID)]
						 ->myUnits[Unit::GIDtoID(editor.selectedUnitGID)];
		if (!unit)
		{
			clearTool();
			return;
		}
		title = getUnitName(unit->typeNum);
		detail =
			GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString("[Team %0]"))
				.arg(unit->owner->teamNumber + 1);
		picture = editor.unitPicture;
	}
	label({inspector.x + 64 * u, inspector.y + 4 * u, inspector.w - 116 * u, 30 * u}, title);
	label({inspector.x + 64 * u, inspector.y + 34 * u, inspector.w - 68 * u, 30 * u}, detail);
	const auto a = picture->area;
	SDL_Rect iconClip{int(inspector.x), int(inspector.y), int(62 * u), int(72 * u)};
	gfx->setUITransform(u, inspector.x + 4 * u - a.x * u, inspector.y + 10 * u - a.y * u,
						&iconClip);
	picture->draw();
	gfx->setUITransform();
	gfx->setClipRect();
	for (const auto &property : properties)
	{
		const auto r = property.rect;
		// Never expose a half-clipped actionable row at the sheet boundary.
		if (r.y < inspectorBody.y || r.y + r.h > inspectorBody.y + inspectorBody.h)
			continue;
		label({r.x, r.y, r.w, 22 * u}, property.caption);
		ViewRect minus{r.x, r.y + 24 * u, 44 * u, 44 * u},
			plus{r.x + r.w - 44 * u, r.y + 24 * u, 44 * u, 44 * u};
		ViewRect track{r.x + 48 * u, r.y + 24 * u, r.w - 96 * u, 44 * u};
		surface(minus, InGameTouchTheme::field());
		surface(plus, InGameTouchTheme::field());
		label(minus, "-");
		label(plus, "+");
		surface(track, InGameTouchTheme::field());
		double fraction =
			double(property.value->currentValue()) / std::max(1, property.value->maximumValue());
		surface({track.x, track.y + track.h - 5 * u, track.w * fraction, 5 * u},
				InGameTouchTheme::selected());
		label(track, std::to_string(property.value->currentValue()) + " / " +
						 std::to_string(property.value->maximumValue()));
	}
	if (inspectorMaximum > 0)
	{
		const double thumb = std::max(20 * u, inspectorBody.h * inspectorBody.h /
												  (inspectorMaximum + inspectorBody.h));
		surface({inspector.x + inspector.w - 3 * u,
				 inspectorBody.y + inspectorScroll / inspectorMaximum * (inspectorBody.h - thumb),
				 2 * u, thumb},
				InGameTouchTheme::border());
	}
}
void PhoneEditor::drawInteractionPreview()
{
	auto *gfx = globalContainer->gfx;
	const double u = gfx->logicalUnitsPerPoint();
	gfx->setClipRect(int(content.x), int(content.y), int(content.w), int(content.h));
	// A held tap stays visible while it waits to find out whether it starts a zoom.
	const auto &pending = !stroke.empty() ? stroke : deferred ? deferred->points : stroke;
	if (!pending.empty())
	{
		// Preview brush coverage without mutating terrain. The same brush mask
		// drives the committed editor operation; screen/world conversion wraps.
		const double corner = editor.selectionMode == MapEdit::PlaceTerrain &&
									  editor.terrainType <= TerrainSelector::Water
								  ? 16
								  : 0;
		std::vector<BrushCoverage::Cell> centres;
		for (auto p : pending)
		{
			auto [wx, wy] = editor.camera.screenToWorld(p.x, p.y);
			centres.push_back(BrushCoverage::cellAt(wx, wy, corner));
		}
		const auto cells = BrushCoverage::cells(editor.brush.getFigure(), centres);
		const bool erase = editor.brush.getType() == BrushTool::MODE_DEL ||
						   editor.selectionMode == MapEdit::RemoveObject;
		Color fill = erase ? Color(220, 80, 65, 115) : Color(240, 208, 110, 110);
		if (!erase && editor.selectionMode == MapEdit::PlaceTerrain)
		{
            if (TerrainSelector::isBaseTerrain(editor.terrainType))
            {
				const auto color =
					editor.game.map
						.terrainPresentation(TerrainSelector::baseTerrain(editor.terrainType))
						.preview;
				fill = Color(color.r, color.g, color.b, 130);
            }
		}
		const Color edge = erase ? Color(255, 128, 110) : Color(255, 235, 156);
		for (auto [x, y] : cells)
		{
			auto [sx, sy] = editor.camera.worldToScreen(x * 32, y * 32);
			const int size = std::max(2, int(32 * editor.camera.zoom));
			gfx->drawFilledRect(int(sx), int(sy), size, size, fill);
			gfx->drawRect(int(sx), int(sy), size, size, edge);
		}
	}
	if (drag && drag->moving)
	{
		bool valid = content.contains({double(editor.mouseX), double(editor.mouseY)});
		int width = 1, height = 1;
		if (editor.selectionMode == MapEdit::PlaceBuilding)
		{
			int type = globalContainer->buildingsTypes.getTypeNum(editor.selectionName,
																  editor.buildingLevel, false);
			if (!editor.isUpgradable(IntBuildingType::shortNumberFromType(editor.selectionName)))
				type = globalContainer->buildingsTypes.getTypeNum(editor.selectionName, 0, false);
			auto *b = globalContainer->buildingsTypes.get(type);
			width = b->width;
			height = b->height;
			int x, y, bx, by;
			editor.game.map.cursorToBuildingPos(editor.mapMouseX(editor.mouseX),
												editor.mapMouseY(editor.mouseY), width, height, &x,
												&y, editor.viewportX, editor.viewportY);
			valid =
				valid && editor.game.checkRoomForBuilding(x, y, b, &bx, &by, editor.team, false);
		}
		else if (editor.selectionMode == MapEdit::PlaceUnit)
		{
			int x, y;
			editor.game.map.displayToMapCaseAligned(editor.mapMouseX(editor.mouseX),
													editor.mapMouseY(editor.mouseY), &x, &y,
													editor.viewportX, editor.viewportY);
			int type = editor.placingUnit == MapEdit::Worker    ? WORKER
					   : editor.placingUnit == MapEdit::Warrior ? WARRIOR
																: EXPLORER;
			auto *unit =
				editor.game.teams[editor.team]->race.getUnitType(type, editor.placingUnitLevel);
			valid = valid && (unit->performance[FLY] ? editor.game.map.isFreeForAirUnit(x, y)
													 : editor.game.map.isFreeForGroundUnit(
														   x, y, unit->performance[SWIM],
														   Team::teamNumberToMask(editor.team)));
		}
		valid = valid && content.contains({double(editor.mouseX), editor.mouseY + 40 * u});
		const Color color = valid ? Color(140, 245, 170) : Color(255, 130, 110);
		const double w = std::max(32 * u, width * 32 * editor.camera.zoom),
					 h = std::max(32 * u, height * 32 * editor.camera.zoom);
		for (int i = 0; i < std::max(2, int(2 * u)); ++i)
			gfx->drawRect(editor.mouseX - int(w / 2) - i, editor.mouseY - int(h / 2) - i,
						  int(w) + 2 * i, int(h) + 2 * i, color);
		gfx->setClipRect();
		ViewRect status{content.x + std::max(0., (content.w - 180 * u) / 2), content.y + 4 * u,
						std::min(content.w, 180 * u), 28 * u};
		surface(status);
		label(status, valid ? GAGCore::Toolkit::getStringTable()->getString("[Release to place]")
							: GAGCore::Toolkit::getStringTable()->getString("[Blocked placement]"));
	}
	gfx->setClipRect();
}

bool PhoneEditor::showsMapButton() const
{
	return !peekOpen && !inspecting() && !editor.hasDialog() && editor.panelMode != MapEdit::Teams;
}
// In the content's bottom corner away from the thumb, clear of the brush rail.
ViewRect PhoneEditor::mapButton() const
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint(), w = 88 * u, h = InGameTouchTheme::target * u;
	return {ThumbSide::left() ? content.x + content.w - 8 * u - w : content.x + 8 * u, content.y + content.h - 8 * u - h,
			w, h};
}
ViewRect PhoneEditor::peekRect() const
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	const double column = InGameTouchTheme::peekButtonColumn * u, gap = 8 * u;
	if (content.w > content.h)
	{
		const double side = std::min({InGameTouchTheme::peekSide * u, content.h - 16 * u, content.w - column - 5 * gap});
		const double x = content.x + (content.w - side - gap - column) / 2;
		return {ThumbSide::left() ? x + column + gap : x, content.y + (content.h - side) / 2, side, side};
	}
	const double buttons = InGameTouchTheme::target * u + gap;
	const double side = std::min({InGameTouchTheme::peekSide * u, safe.w - 32 * u, content.h - buttons - 16 * u});
	return {safe.x + (safe.w - side) / 2, content.y + (content.h - side - buttons) / 2, side, side};
}
std::vector<ViewRect> PhoneEditor::peekButtons() const
{
	const auto map = peekRect();
	const double u = globalContainer->gfx->logicalUnitsPerPoint(), gap = 8 * u;
	std::vector<ViewRect> buttons;
	if (content.w > content.h)
	{
		const double w = InGameTouchTheme::peekButtonColumn * u, h = (map.h - 2 * gap) / 3;
		const double x = ThumbSide::left() ? map.x - gap - w : map.x + map.w + gap;
		for (int i = 0; i < 3; ++i) // Done, zoom out, zoom in (lowest).
			buttons.push_back({x, map.y + i * (h + gap), w, h});
		return buttons;
	}
	const double h = InGameTouchTheme::target * u, w = (map.w - 2 * gap) / 3;
	for (int i = 0; i < 3; ++i)
	{
		const int slot = ThumbSide::left() ? 2 - i : i;
		buttons.push_back({map.x + slot * (w + gap), map.y + map.h + gap, w, h});
	}
	return buttons;
}
void PhoneEditor::navigatePeek(ViewPoint point)
{
	if (!peekMinimap)
		return;
	const int size = InGameTouchTheme::peekMinimapSize;
	const auto rect = peekRect();
	point = rect.clamp(point);
	int x, y;
	peekMinimap->convertToMap(globalContainer->gfx->getW() - size + int((point.x - rect.x) * size / rect.w),
							  int((point.y - rect.y) * size / rect.h), x, y);
	editor.updateCamera();
	auto &map = editor.game.map;
	editor.viewportX = (x - int(editor.camera.visibleW() / 64)) & map.wMask;
	editor.viewportY = (y - int(editor.camera.visibleH() / 64)) & map.hMask;
	editor.updateCamera();
}
void PhoneEditor::drawPeek()
{
	auto *gfx = globalContainer->gfx;
	const double u = gfx->logicalUnitsPerPoint();
	gfx->setClipRect();
	if (showsMapButton())
	{
		const auto button = mapButton();
		surface(button, InGameTouchTheme::field());
		gfx->drawRect(int(button.x), int(button.y), int(button.w), int(button.h), InGameTouchTheme::border());
		label(button, translated("[Minimap]"));
	}
	if (!peekOpen)
		return;
	const int size = InGameTouchTheme::peekMinimapSize;
	if (!peekMinimap)
	{
		peekMinimap = std::make_unique<Minimap>(globalContainer->runNoX, size, size, 0, 0, size, size,
												Minimap::HideFOW);
		peekMinimap->setGame(editor.game);
	}
	surface(content, Color(0, 0, 0, 120));
	const auto rect = peekRect();
	const auto buttons = peekButtons();
	{
		double x0 = rect.x, y0 = rect.y, x1 = rect.x + rect.w, y1 = rect.y + rect.h;
		for (const auto &b : buttons)
		{
			x0 = std::min(x0, b.x);
			x1 = std::max(x1, b.x + b.w);
			y1 = std::max(y1, b.y + b.h);
		}
		surface({x0 - 6 * u, y0 - 6 * u, x1 - x0 + 12 * u, y1 - y0 + 12 * u});
	}
	SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w), int(rect.h)};
	editor.updateCamera();
	gfx->setUITransform(rect.w / size, rect.x - (gfx->getW() - size) * rect.w / size, rect.y, &clip);
	peekMinimap->draw(editor.view.drawnScene(), editor.team, editor.viewportX, editor.viewportY, int(std::ceil(editor.camera.visibleW() / 32)),
					  int(std::ceil(editor.camera.visibleH() / 32)));
	gfx->setUITransform();
	gfx->setClipRect();
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border());
	const std::string labels[] = {translated("[Done]"), "−", "+"};
	for (int i = 0; i < 3; ++i)
	{
		surface(buttons[i], InGameTouchTheme::field());
		gfx->drawRect(int(buttons[i].x), int(buttons[i].y), int(buttons[i].w), int(buttons[i].h),
					  InGameTouchTheme::border());
		label(buttons[i], labels[i]);
	}
}
