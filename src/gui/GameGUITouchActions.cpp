// SPDX-License-Identifier: GPL-3.0-or-later
#include "InGameTouchTheme.h"
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "UnitDisplayNames.h"
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;

Building *GameGUITouch::inspectedBuilding() const
{
	if (gui.selectionMode != GameGUI::BUILDING_SELECTION)
		return nullptr;
	return gui.selectionBuilding();
}
std::vector<GameGUITouch::BuildingAction> GameGUITouch::buildingActions() const
{
	std::vector<BuildingAction> result;
	auto *b = inspectedBuilding();
	if (!b)
		return result;
	if (b->owner != gui.localTeam || globalContainer->isViewingGame())
		return result;
	if (allocationBuilding())
	{
		result.push_back({"Assigned " + std::to_string(b->unitsWorking.size()) + " · Target " +
							  std::to_string(allocation && allocation->building == b->gid
												 ? allocation->requested
												 : gui.displayedMaxUnitWorking(*b)),
						  6});
		result.push_back({"Priority", 7});
	}
	if (b->type->defaultUnitStayRange)
		result.push_back({"Range: " + std::to_string(gui.displayedUnitStayRange(*b)), 8});
	auto tr = [](const char *key)
	{ return std::string(Toolkit::getStringTable()->getString(key)); };
	if (b->type->unitProductionTime)
	{
		result.push_back({"Production ratio", 9});
		const auto ratio = gui.displayedRatio(*b);
		for (int i = 0; i < NB_UNIT_TYPE; ++i)
			result.push_back({std::string(getUnitName(i)) + ": " + std::to_string(ratio[i]), 0, i});
	}
	if (b->type->type == "clearingflag")
	{
		for (int i = 0; i < BASIC_COUNT; ++i)
			if (i != STONE)
				result.push_back({getResourceName(i), 1, i, gui.displayedClearingResource(*b, i)});
	}
	if (b->type->type == "warflag")
	{
		for (int i = 0; i < NB_UNIT_LEVELS; ++i)
			result.push_back({tr("[Min required level:]") + " " + std::to_string(i + 1), 2, i,
							  gui.displayedMinLevelToFlag(*b) == i});
	}
	if (b->type->type == "explorationflag")
	{
		const char *names[] = {"[any explorer]", "[ground attack]"};
		for (int i = 0; i < EXPLORATION_FLAG_OPTION_COUNT; ++i)
			result.push_back({tr(names[i]), 2, i, gui.displayedMinLevelToFlag(*b) == i});
	}
	if (b->constructionResultState == Building::REPAIR)
		result.push_back({tr("[cancel repair]"), 3});
	else if (b->constructionResultState == Building::UPGRADE)
		result.push_back({tr("[cancel upgrade]"), 3});
	else if (b->buildingState == Building::ALIVE && !b->type->isBuildingSite)
	{
		if (b->hp < b->type->hpMax && b->type->regenerationSpeed == 0 &&
			b->isHardSpaceForBuildingSite(Building::REPAIR) &&
			gui.localTeam->maxBuildLevel() >= b->type->level)
			result.push_back({tr("[repair]"), 3});
		else if (b->hp == b->type->hpMax && b->type->nextLevel != -1 &&
				 b->isHardSpaceForBuildingSite(Building::UPGRADE) &&
				 gui.localTeam->maxBuildLevel() > b->type->level)
			result.push_back({tr("[upgrade]"), 3});
	}
	if (b->buildingState == Building::WAITING_FOR_DESTRUCTION)
		result.push_back({tr("[cancel destroy]"), 4});
	else if (b->buildingState == Building::ALIVE)
	{
		result.push_back({(confirmDestroy ? tr("[ok]") + ": " : "") + tr("[destroy]"), 4});
		if (confirmDestroy)
			result.push_back({tr("[Cancel]"), 5});
	}
	return result;
}
void GameGUITouch::drawBuildingActions()
{
	auto *gfx = globalContainer->gfx;
	const auto content = panelContent();
	const double unit = gfx->logicalUnitsPerPoint();
	const auto rows = buildingActions();
	if (rows.empty())
		drawPointLabel({content.x, content.y, content.w, 48 * unit}, "Read-only building", .9);
	for (size_t i = 0; i < rows.size(); ++i)
	{
		ViewRect rect{content.x,
					  content.y + (i * InGameTouchTheme::inspectorRow - actionScroll) * unit,
					  content.w, 48 * unit};
		if (rect.y < content.y || rect.y + rect.h > content.y + content.h)
			continue;
		const auto &row = rows[i];
		if (row.kind == 9)
		{
			gfx->drawHorzLine(int(rect.x + 8 * unit), int(rect.y + 4 * unit),
							  int(rect.w - 16 * unit), InGameTouchTheme::border);
			drawPointLabel(rect, row.label, .9, true);
			continue;
		}
		gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
							row.kind == 4  ? Color(81, 36, 60, 235)
							: row.selected ? InGameTouchTheme::selected
										   : InGameTouchTheme::field);
		if (row.kind == 7)
		{
			const char *labels[] = {"↓ Low", "= Normal", "↑ High"};
			for (int k = 0; k < 3; ++k)
			{
				const ViewRect button{rect.x + k * rect.w / 3, rect.y, rect.w / 3 - 2 * unit,
									  rect.h};
				gfx->drawFilledRect(int(button.x), int(button.y), int(button.w), int(button.h),
									gui.displayedPriority(*inspectedBuilding()) == k - 1
										? InGameTouchTheme::selected
										: InGameTouchTheme::field);
				drawPointLabel(button, labels[k], .8);
			}
		}
		else if (row.kind == 0 || row.kind == 6 || row.kind == 8)
		{
			drawPointLabel({rect.x, rect.y, 48 * unit, rect.h}, "−");
			drawPointLabel({rect.x + 48 * unit, rect.y, rect.w - 96 * unit,
							row.kind == 6 ? 26 * unit : rect.h},
						   row.label, row.kind == 6 ? .82 : 1.0);
			drawPointLabel({rect.x + rect.w - 48 * unit, rect.y, 48 * unit, rect.h}, "+");
			if (row.kind == 6)
			{
				const double fraction =
					(allocation ? allocation->requested
								: gui.displayedMaxUnitWorking(*inspectedBuilding())) /
					double(MAX_UNIT_WORKING);
				const double left = rect.x + 56 * unit, width = rect.w - 112 * unit,
							 y = rect.y + 36 * unit;
				gfx->drawFilledRect(int(left), int(y - 2 * unit), int(width), int(4 * unit),
									Color(29, 20, 43));
				gfx->drawFilledRect(int(left), int(y - 2 * unit), int(width * fraction),
									int(4 * unit), InGameTouchTheme::border);
				gfx->drawFilledRect(int(left + width * fraction - 5 * unit), int(y - 7 * unit),
									int(10 * unit), int(14 * unit), InGameTouchTheme::ink);
			}
		}
		else
			drawPointLabel(
				rect, (row.kind == 1 || row.kind == 2 ? (row.selected ? "[x] " : "[ ] ") : "") +
						  row.label);
	}
	const double extent = rows.size() * InGameTouchTheme::inspectorRow * unit;
	if (extent > content.h)
		gfx->drawFilledRect(int(content.x + content.w - 3 * unit),
							int(content.y + actionScroll * unit * content.h / extent),
							std::max(1, int(2 * unit)),
							std::max(1, int(content.h * content.h / extent)), Color(170, 185, 190));
}

bool GameGUITouch::processAllocationPointer(const SDL_Event &event, ViewPoint point)
{
	const TouchPlacementSession::Pointer pointer{event.tfinger.touchId, event.tfinger.fingerId};
	gui.checkSelection();
	auto *building = allocationBuilding();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (!allocation)
	{
		if (event.type != SDL_FINGERDOWN || !fingers.empty() || placement || activeDialog() ||
			!building)
			return false;
		const auto row = actionAt(point);
		const auto content = panelContent();
		if (!row || row->kind != 6 || point.x < content.x + 48 * unit ||
			point.x >= content.x + content.w - 48 * unit)
			return false;
		allocation = TouchAllocationSession{
			pointer,
			{content.x + 56 * unit, point.y - 24 * unit, content.w - 112 * unit, 48 * unit},
			building->gid,
			gui.localTeamNo,
			gui.displayedMaxUnitWorking(*building)};
	}
	if (pointer != allocation->pointer)
	{
		if (event.type == SDL_FINGERDOWN)
		{
			fingers = {allocation->pointer, pointer};
			allocation.reset();
			ignoreTouchSequence = true;
		}
		return true;
	}
	if (!building || building->gid != allocation->building || gui.localTeamNo != allocation->team ||
		activeDialog())
	{
		allocation.reset();
		return true;
	}
	allocation->requested = int(std::lround(
		std::clamp((point.x - allocation->track.x) / std::max(1.0, allocation->track.w), 0.0, 1.0) *
		MAX_UNIT_WORKING));
	if (event.type == SDL_FINGERUP)
	{
		const auto row = actionAt(point);
		if (row && row->kind == 6)
			gui.requestWorkerAllocation(*building, allocation->requested);
		allocation.reset();
	}
	return true;
}
std::optional<GameGUITouch::BuildingAction> GameGUITouch::actionAt(ViewPoint point) const
{
	const auto content = panelContent();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double y = (point.y - content.y) / unit + actionScroll;
	const auto rows = buildingActions();
	const int i = int(y / InGameTouchTheme::inspectorRow);
	if (!content.contains(point) || y < 0 || i >= int(rows.size()) ||
		y - i * InGameTouchTheme::inspectorRow >= 48)
		return std::nullopt;
	// Partially clipped rows are not actionable until scrolled fully into view.
	if (i * InGameTouchTheme::inspectorRow - actionScroll < 0 ||
		(i * InGameTouchTheme::inspectorRow - actionScroll + 48) * unit > content.h)
		return std::nullopt;
	return rows[i];
}
void GameGUITouch::tapBuildingAction(ViewPoint point)
{
	auto *b = inspectedBuilding();
	if (!b || b->owner != gui.localTeam || globalContainer->isViewingGame())
		return;
	const auto picked = actionAt(point);
	if (!picked || picked->kind != heldActionKind || picked->value != heldActionValue ||
		heldActionConfirmation != confirmDestroy)
		return;
	if (picked->kind == 3 && picked->label != heldActionLabel)
		return;
	const auto row = *picked;
	const auto content = panelContent();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (row.kind == 7)
	{
		gui.requestBuildingPriority(
			*b, std::clamp(int((point.x - content.x) / (content.w / 3)) - 1, -1, 1));
		return;
	}
	if (row.kind == 6 || row.kind == 8)
	{
		const int delta = point.x < content.x + 48 * unit                ? -1
						  : point.x >= content.x + content.w - 48 * unit ? 1
																		 : 0;
		if (row.kind == 6)
		{
			const int next = delta ? gui.displayedMaxUnitWorking(*b) + delta
								   : int(std::lround(std::clamp((point.x - content.x - 48 * unit) /
																	(content.w - 96 * unit),
																0.0, 1.0) *
													 MAX_UNIT_WORKING));
			gui.requestWorkerAllocation(*b, next);
		}
		else
			gui.requestFlagRange(*b, gui.displayedUnitStayRange(*b) + delta);
		return;
	}
	if (row.kind == 0)
	{
		int delta = point.x < content.x + 48 * unit                ? -1
					: point.x >= content.x + content.w - 48 * unit ? 1
																   : 0;
		auto values = gui.displayedRatio(*b);
		const int next = std::clamp(values[row.value] + delta, 0, int(MAX_RATIO_RANGE));
		if (next == values[row.value])
			return;
		values[row.value] = next;
		gui.pendingFor(b->gid).pendingRatio = values;
		gui.orderQueue.push_back(std::make_shared<OrderModifySwarm>(b->gid, values.data()));
	}
	else if (row.kind == 1)
	{
		std::array<bool, BASIC_COUNT> values;
		bool wire[BASIC_COUNT];
		for (int k = 0; k < BASIC_COUNT; ++k)
			values[k] = wire[k] = gui.displayedClearingResource(*b, k) ^ (k == row.value);
		gui.pendingFor(b->gid).pendingClearingResources = values;
		gui.orderQueue.push_back(std::make_shared<OrderModifyClearingFlag>(b->gid, wire));
	}
	else if (row.kind == 2)
	{
		if (gui.displayedMinLevelToFlag(*b) == row.value)
			return;
		gui.pendingFor(b->gid).pendingMinLevelToFlag = row.value;
		gui.orderQueue.push_back(std::make_shared<OrderModifyMinLevelToFlag>(b->gid, row.value));
	}
	else if (row.kind == 5)
		confirmDestroy = false;
	else
	{
		if (row.kind == 4 && b->buildingState == Building::ALIVE && !confirmDestroy)
		{
			confirmDestroy = true;
			return;
		}
		if (row.kind == 3)
			gui.requestBuildingConstruction(*b);
		else
			gui.requestBuildingDestruction(*b);
		confirmDestroy = false;
	}
}
