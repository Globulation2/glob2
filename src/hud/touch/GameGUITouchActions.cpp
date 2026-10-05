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

namespace
{
	//! The construction action a building offers (cancel repair/upgrade, repair,
	//! upgrade), or empty. One rule for the drawn panel and for input validation.
	std::string constructionActionLabel(int constructionResultState, int buildingState, const BuildingType *type,
										int hp, bool hardSpaceForRepair, bool hardSpaceForUpgrade, int maxBuildLevel, int maxHp)
	{
		auto tr = [](const char *key) { return std::string(Toolkit::getStringTable()->getString(key)); };
		if (constructionResultState == Building::REPAIR)
			return tr("[cancel repair]");
		if (constructionResultState == Building::UPGRADE)
			return tr("[cancel upgrade]");
		if (buildingState == Building::ALIVE && !type->isBuildingSite)
		{
			if (hp < maxHp && type->regenerationSpeed == 0 && hardSpaceForRepair && maxBuildLevel >= type->level)
				return tr("[repair]");
			if (hp == maxHp && type->nextLevel != -1 && hardSpaceForUpgrade && maxBuildLevel > type->level)
				return tr("[upgrade]");
		}
		return {};
	}

	//! The same, from the live building: input checks a held action against it.
	std::string liveConstructionActionLabel(Building &b, Team &local)
	{
		const bool offersConstruction = b.constructionResultState == Building::NO_CONSTRUCTION &&
			b.buildingState == Building::ALIVE && !b.type->isBuildingSite;
		return constructionActionLabel(b.constructionResultState, b.buildingState, b.type, b.hp,
			offersConstruction && b.isHardSpaceForBuildingSite(Building::REPAIR),
			offersConstruction && b.isUpgradeAvailable() && b.isHardSpaceForBuildingSite(Building::UPGRADE),
			local.maxBuildLevel(), b.getEffectiveMaxHp());
	}
}

bool GameGUITouch::inspecting() const
{
	return gui.selectionMode == GameGUI::BUILDING_SELECTION && std::holds_alternative<BuildingRef>(gui.selection);
}

const SceneBuildingPanel *GameGUITouch::inspectedBuilding() const
{
	if (!inspecting())
		return nullptr;
	// The panel was extracted for the selection current when the frame was drawn.
	const SceneBuildingPanel &panel = gui.drawnScene().panels.building;
	const BuildingRef selected = std::get<BuildingRef>(gui.selection);
	if (!panel.valid || panel.gid != selected.gid || panel.generation != selected.generation)
		return nullptr;
	return &panel;
}
std::vector<GameGUITouch::BuildingAction> GameGUITouch::buildingActions() const
{
	std::vector<BuildingAction> result;
	auto *b = inspectedBuilding();
	if (!b)
		return result;
	if (b->owner.teamNumber != gui.drawnScene().panels.local.teamNumber || globalContainer->isViewingGame())
		return result;
	if (allocationBuilding())
	{
		result.push_back({GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString(
														 "[Assigned %0 · Target %1]"))
							  .arg(b->unitsWorking)
							  .arg(allocation && allocation->kind == 6 && allocation->building == b->gid
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
	const std::string construction = constructionActionLabel(b->constructionResultState, b->buildingState, b->type, b->hp,
		b->hardSpaceForRepair, b->hardSpaceForUpgrade, gui.drawnScene().panels.local.maxBuildLevel, b->effectiveMaxHp);
	if (!construction.empty())
		result.push_back({construction, 3});
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
							: row.selected ? InGameTouchTheme::selected()
										   : InGameTouchTheme::field());
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
										? InGameTouchTheme::selected()
										: InGameTouchTheme::field());
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
									int(4 * unit), InGameTouchTheme::border());
				gfx->drawFilledRect(int(left + width * fraction - 5 * unit), int(y - 7 * unit),
									int(10 * unit), int(14 * unit), InGameTouchTheme::ink());
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
	const TouchPlacementSession::Pointer pointer{event.tfinger.touchID, event.tfinger.fingerID};
	gui.checkSelection();
	// Input issues orders against the live building (see allocationBuilding for drawing).
	Building *building = allocationBuilding() ? gui.selectionBuilding() : nullptr;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	if (!allocation)
	{
		if (event.type != SDL_EVENT_FINGER_DOWN || !fingers.empty() || placement || activeDialog() ||
			!building)
			return false;
		if (usesDial())
		{
			// Dial sliders follow the thumb angle; the shared production arc
			// captures the nearer divider for the full gesture.
			const auto region = dialRegionAt(point);
			if (!region || (region->part != DialRegion::Arc && region->part != DialRegion::Proportions) ||
				(region->action.kind != 6 && region->action.kind != 0 && region->action.kind != 8))
				return false;
			TouchAllocationSession session{pointer, {}, building->gid, gui.localTeamNo};
			session.kind = region->action.kind;
			session.value = region->action.value;
			session.maximum = region->maximum;
			session.ring = region->ring;
			session.polar = true;
			session.sweepFrom = region->sliderFrom;
			session.sweepTo = region->sliderTo;
			session.requested = session.kind == 6   ? gui.displayedMaxUnitWorking(*building)
								: session.kind == 8 ? gui.displayedUnitStayRange(*building)
													: gui.displayedRatio(*building)[session.value];
			if (region->part == DialRegion::Proportions)
			{
				session.ratios = gui.displayedRatio(*building);
				session.initialRatios = TouchDial::shares(session.ratios, MAX_RATIO_RANGE);
				session.start = point;
				const auto g = dialLayout(layout()).geometry;
				const auto polar = TouchDial::polar(g, point);
				const int total = session.ratios[0] + session.ratios[1] + session.ratios[2];
				const double first = TouchDial::angleOf(session.ratios[0], region->from, region->to, std::max(1, total));
				const double second = TouchDial::angleOf(session.ratios[0] + session.ratios[1], region->from, region->to, std::max(1, total));
				const double d0 = std::abs(polar->angle - first), d1 = std::abs(polar->angle - second);
				session.divider = std::abs(d0 - d1) < 0.01
					? (polar->radius < g.rings[region->ring].middle() ? 0 : 1)
					: (d0 < d1 ? 0 : 1);
			}
			allocation = session;
		}
	}
	if (!allocation)
	{
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
		if (event.type == SDL_EVENT_FINGER_DOWN)
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
	if (allocation->polar)
	{
		const auto g = dialLayout(layout()).geometry;
		const auto polar = TouchDial::polar(g, point);
		allocation->position = point;
		if (allocation->divider >= 0)
		{
			allocation->moved |= std::hypot(point.x - allocation->start.x, point.y - allocation->start.y) >= 4 * unit;
			if (polar && allocation->moved)
			{
				const int at = TouchDial::value(polar->angle, allocation->sweepFrom, allocation->sweepTo, MAX_RATIO_RANGE);
				auto values = allocation->initialRatios;
				if (allocation->divider == 0)
				{
					const int pair = values[0] + values[1];
					values[0] = std::clamp(at, 0, pair);
					values[1] = pair - values[0];
				}
				else
				{
					values[1] = std::clamp(at - values[0], 0, MAX_RATIO_RANGE - values[0]);
					values[2] = MAX_RATIO_RANGE - values[0] - values[1];
				}
				allocation->ratios = values;
			}
		}
		else if (polar)
			allocation->requested =
				TouchDial::value(polar->angle, allocation->sweepFrom, allocation->sweepTo, allocation->maximum);
		if (event.type == SDL_EVENT_FINGER_UP)
		{
			// Commit once, if the thumb is still near its ring; a release
			// elsewhere abandons the preview.
			const auto released = dialRegionAt(point);
			if (polar && released && released->ring == allocation->ring)
			{
				if (allocation->divider >= 0)
				{
					if (allocation->moved)
						commitRatios(*building, allocation->ratios);
				}
				else if (allocation->kind == 6)
					gui.requestWorkerAllocation(*building, allocation->requested);
				else if (allocation->kind == 8)
					gui.requestFlagRange(*building, allocation->requested);
				else
					setRatio(*building, allocation->value, allocation->requested);
			}
			allocation.reset();
		}
		return true;
	}
	allocation->requested = int(std::lround(
		std::clamp((point.x - allocation->track.x) / std::max(1.0, allocation->track.w), 0.0, 1.0) *
		MAX_UNIT_WORKING));
	if (event.type == SDL_EVENT_FINGER_UP)
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
	if (usesDial())
	{
		if (inspecting())
			if (const auto region = dialRegionAt(point))
				return region->action;
		return std::nullopt;
	}
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
	// Input issues orders against the live building (see inspectedBuilding for drawing).
	Building *b = inspecting() ? gui.selectionBuilding() : nullptr;
	if (!b || b->owner != gui.localTeam || globalContainer->isViewingGame())
		return;
	if (usesDial())
	{
		const auto region = dialRegionAt(point);
		if (!region || region->action.kind != heldActionKind || region->action.value != heldActionValue ||
			heldActionConfirmation != confirmDestroy ||
			(region->action.kind == 3 && (region->action.label != heldActionLabel ||
										  liveConstructionActionLabel(*b, *gui.localTeam) != heldActionLabel)))
			return;
		tapDial(*b, *region, point);
		return;
	}
	const auto picked = actionAt(point);
	if (!picked || picked->kind != heldActionKind || picked->value != heldActionValue ||
		heldActionConfirmation != confirmDestroy)
		return;
	// The panel shows the last drawn state; commit only if the live building still offers it.
	if (picked->kind == 3 && (picked->label != heldActionLabel ||
							  liveConstructionActionLabel(*b, *gui.localTeam) != heldActionLabel))
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
		setRatio(*b, row.value, gui.displayedRatio(*b)[row.value] + delta);
	}
	else
		applyDiscreteAction(*b, row);
}
void GameGUITouch::setRatio(Building &building, int type, int value)
{
	auto values = gui.displayedRatio(building);
	const int next = std::clamp(value, 0, int(MAX_RATIO_RANGE));
	if (next == values[type])
		return;
	values[type] = next;
	commitRatios(building, values);
}
// Both inspector presentations share the pending UI value and send exactly one
// existing simulation order. Drawing and drag previews never mutate the building.
void GameGUITouch::commitRatios(Building &building, const std::array<int, 3> &values)
{
	if (values == gui.displayedRatio(building))
		return;
	gui.pendingFor(building.gid).pendingRatio = values;
	auto wire = values;
	gui.orderQueue.push_back(std::make_shared<OrderModifySwarm>(building.gid, wire.data()));
}

// Clearing resources, flag requirements, construction and destruction: the
// same orders from the row list and the dial.
void GameGUITouch::applyDiscreteAction(Building &building, const BuildingAction &row)
{
	auto *b = &building;
	if (row.kind == 1)
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
