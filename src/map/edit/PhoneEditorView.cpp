// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
// Touch editor presentation. Reads existing selection/value bindings; all edits
// return through named editor actions or ValueScrollBox::setValue.
#include "PhoneEditor.h"
#include "MapEdit.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include "BrushCoverage.h"
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
void surface(ViewRect r, Color color = InGameTouchTheme::paper)
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
	surface(close, InGameTouchTheme::field);
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
		surface(minus, InGameTouchTheme::field);
		surface(plus, InGameTouchTheme::field);
		label(minus, "-");
		label(plus, "+");
		surface(track, InGameTouchTheme::field);
		double fraction =
			double(property.value->currentValue()) / std::max(1, property.value->maximumValue());
		surface({track.x, track.y + track.h - 5 * u, track.w * fraction, 5 * u},
				InGameTouchTheme::selected);
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
				InGameTouchTheme::border);
	}
}
void PhoneEditor::drawBrushPanel()
{
	if (!brushOpen)
		return;
	auto *gfx = globalContainer->gfx;
	const double u = gfx->logicalUnitsPerPoint();
	surface(brushPanel);
	for (unsigned i = 0; i < BrushTool::BRUSH_COUNT; ++i)
	{
		ViewRect r{brushPanel.x + (i % 4) * brushPanel.w / 4, brushPanel.y + (i / 4) * 56 * u,
				   brushPanel.w / 4 - 2 * u, 54 * u};
		surface(r, editor.brush.getFigure() == i ? InGameTouchTheme::selected
												 : InGameTouchTheme::field);
		SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
		gfx->setUITransform(u, r.x + (r.w - 32 * u) / 2, r.y + 10 * u, &clip);
		gfx->drawSprite(0, 0, globalContainer->brush, 2 + i);
		gfx->setUITransform();
		gfx->setClipRect();
	}
	if (editor.selectionMode == MapEdit::PlaceZone)
	{
		const double y = brushPanel.y + 112 * u;
		ViewRect paint{brushPanel.x, y, brushPanel.w / 2 - 2 * u, 44 * u},
			erase{brushPanel.x + brushPanel.w / 2, y, brushPanel.w / 2 - 2 * u, 44 * u};
		surface(paint, editor.brush.getType() == BrushTool::MODE_ADD ? InGameTouchTheme::selected
																	 : InGameTouchTheme::field);
		surface(erase, editor.brush.getType() == BrushTool::MODE_DEL ? InGameTouchTheme::selected
																	 : InGameTouchTheme::field);
		label(paint, GAGCore::Toolkit::getStringTable()->getString("[Paint]"));
		label(erase, GAGCore::Toolkit::getStringTable()->getString("[Erase]"));
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
			if (editor.terrainType == TerrainSelector::Water)
				fill = Color(70, 145, 245, 130);
			else if (editor.terrainType == TerrainSelector::Grass)
				fill = Color(95, 220, 115, 130);
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
