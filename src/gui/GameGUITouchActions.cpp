// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include <algorithm>
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
		result.push_back({GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString(
														 "[Assigned %0 · Target %1]"))
							  .arg(b->unitsWorking.size())
							  .arg(allocation && allocation->building == b->gid
									   ? allocation->requested
									   : gui.displayedMaxUnitWorking(*b)),
						  6});
		result.push_back({GAGCore::Toolkit::getStringTable()->getString("[priority]"), 7});
	}
	if (b->type->defaultUnitStayRange)
		result.push_back({GAGCore::FormattableString(
							  GAGCore::Toolkit::getStringTable()->getString("[Range: %0]"))
							  .arg(gui.displayedUnitStayRange(*b)),
						  8});
	auto tr = [](const char *key)
	{ return std::string(Toolkit::getStringTable()->getString(key)); };
	if (b->type->unitProductionTime)
	{
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
			result.push_back({GAGCore::FormattableString(tr("[Min required level %0]")).arg(i + 1),
							  2, i, gui.displayedMinLevelToFlag(*b) == i});
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
		result.push_back({confirmDestroy
							  ? GAGCore::FormattableString(tr("[Confirm %0]")).arg(tr("[destroy]"))
							  : tr("[destroy]"),
						  4});
		if (confirmDestroy)
			result.push_back({tr("[Cancel]"), 5});
	}
	return result;
}
// Local point-space boxes are the sole source of layout for input and drawing.
std::vector<ViewRect> GameGUITouch::buildingActionBoxes(double width) const
{
	const auto rows = buildingActions();
	std::vector<ViewRect> boxes;
	double y = 0;
	for (size_t i = 0; i < rows.size();)
	{
		size_t count = 1;
		const int kind = rows[i].kind;
		if (kind == 0 || kind == 1 || kind == 2)
		{
			while (i + count < rows.size() && rows[i + count].kind == kind)
				++count;
			count = std::min(count, size_t(kind == 0 ? 3 : 2));
		}
		else if (kind == 6 && width >= InGameTouchTheme::inspectorWide && i + 1 < rows.size() &&
				 rows[i + 1].kind == 7)
			count = 2;
		else if (kind >= 3 && kind <= 5)
		{
			// Confirmation adds Cancel beside the destructive action, never
			// below the visible panel.
			while (i + count < rows.size() && rows[i + count].kind >= 3 &&
				   rows[i + count].kind <= 5)
				++count;
		}
		const double height =
			kind == 0 ? InGameTouchTheme::ratioRow : InGameTouchTheme::inspectorRow;
		for (size_t k = 0; k < count; ++k)
			boxes.push_back({k * width / count, y, width / count, height});
		y += height;
		i += count;
	}
	return boxes;
}
double GameGUITouch::buildingActionsHeight(double width) const
{
	const auto boxes = buildingActionBoxes(width);
	return boxes.empty() ? InGameTouchTheme::inspectorRow : boxes.back().y + boxes.back().h;
}
ViewRect GameGUITouch::buildingActionRect(size_t index) const
{
	const auto content = panelContent();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const auto boxes = buildingActionBoxes(content.w / unit);
	if (index >= boxes.size())
		return {};
	const auto box = boxes[index];
	return {content.x + box.x * unit, content.y + (box.y - actionScroll) * unit, box.w * unit,
			box.h * unit};
}
void GameGUITouch::drawBuildingActions()
{
	auto *gfx = globalContainer->gfx;
	const auto content = panelContent();
	const double unit = gfx->logicalUnitsPerPoint();
	const auto rows = buildingActions();
	if (rows.empty())
		drawPointLabel({content.x, content.y, content.w, content.h},
					   GAGCore::Toolkit::getStringTable()->getString("[Read-only building]"), .9);
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const auto rect = buildingActionRect(i);
		if (rect.y < content.y || rect.y + rect.h > content.y + content.h)
			continue;
		const auto &row = rows[i];

		gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
							row.kind == 4  ? Color(81, 36, 60, 235)
							: row.selected ? InGameTouchTheme::selected
										   : InGameTouchTheme::field);
		if (row.kind == 7)
		{
			const std::string labels[] = {Toolkit::getStringTable()->getString("[↓ Low]"),
										  Toolkit::getStringTable()->getString("[= Normal]"),
										  Toolkit::getStringTable()->getString("[↑ High]")};
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
		else if (row.kind == 0)
		{
			drawPointLabel({rect.x, rect.y, rect.w, InGameTouchTheme::ratioLabel * unit}, row.label,
						   .78);
			const double top = rect.y + InGameTouchTheme::ratioLabel * unit;
			const double height = rect.h - InGameTouchTheme::ratioLabel * unit;
			drawPointLabel({rect.x, top, rect.w / 2, height}, "−");
			drawPointLabel({rect.x + rect.w / 2, top, rect.w / 2, height}, "+");
		}
		else if (row.kind == 6 || row.kind == 8)
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
	const double extent = buildingActionsHeight(content.w / unit) * unit;
	if (extent > content.h)
		gfx->drawFilledRect(int(content.x + content.w - 3 * unit),
							int(content.y + std::clamp(actionScroll * unit, 0.0, extent - content.h) * content.h / extent),
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
		auto content = panelContent();
		const auto rows = buildingActions();
		for (size_t i = 0; i < rows.size(); ++i)
			if (buildingActionRect(i).contains(point))
				content = buildingActionRect(i);
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
	const auto rows = buildingActions();
	if (!content.contains(point))
		return std::nullopt;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const auto rect = buildingActionRect(i);
		if (rect.y >= content.y && rect.y + rect.h <= content.y + content.h && rect.contains(point))
			return rows[i];
	}
	return std::nullopt;
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
	auto content = panelContent();
	const auto rows = buildingActions();
	for (size_t i = 0; i < rows.size(); ++i)
		if (buildingActionRect(i).contains(point))
			content = buildingActionRect(i);
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
		if (point.y < content.y + InGameTouchTheme::ratioLabel * unit)
			return;
		const int delta = point.x < content.x + content.w / 2 ? -1 : 1;
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
