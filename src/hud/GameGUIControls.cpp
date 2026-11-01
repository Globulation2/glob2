// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "InGameStatistics.h"
#include "GameGUITouch.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "MapZoomControls.h"
#include "PanelButtonHit.h"
#include "TeamDisplay.h"
#include "UnitDisplayNames.h"
#include "GameGUIEventLayout.h"
#include "hive/HiveDialog.h"
#include "ConnectionOverlay.h"
#include "tutorial/TutorialGuidance.h"
#include <ui/Host.h>
#include <algorithm>
#include <limits>

using namespace GAGGUI::ui;
namespace
{
std::optional<int> scrollBoxValue(int local, int current, int maximum)
{
	if (local < SCROLLBOX_ARROW_WIDTH)
		return current > 0 ? std::optional<int>(current - 1) : std::nullopt;
	if (local < SCROLLBOX_BAR_WIDTH - SCROLLBOX_ARROW_WIDTH)
		return (local - SCROLLBOX_ARROW_WIDTH) * maximum /
			   (SCROLLBOX_BAR_WIDTH - 2 * SCROLLBOX_ARROW_WIDTH);
	return current < maximum ? std::optional<int>(current + 1) : std::nullopt;
}
} // namespace
Host &GameGUI::hudInspectionHost()
{
	updateCamera();
	if (!hudHost)
		hudHost = std::make_unique<Host>(
			Glob2UI::inGameTheme(),
			[this](const Presentation &)
			{
				return semanticContext(positionedControls(describeHudControls()),
									   "hud:" + std::to_string(drawnScene().map.identity()) + ":" +
										   std::to_string(localPlayer) + ":" +
										   std::to_string(localTeamNo) + ":" +
										   std::to_string(globalContainer->replaying) + ":" +
										   std::to_string(globalContainer->liveSpectating));
			});
	hudHost->setPresentation(
		Presentation::forSurface(globalContainer->gfx->getW(), globalContainer->gfx->getH()));
	hudHost->invalidate();
	hudHost->layoutIfNeeded();
	prepareHudOwnership();
	hudHost->focus(touch ? touch->focusedControlKey() : std::string{}, true);
	return *hudHost;
}
void GameGUI::prepareHudOwnership()
{
	if (!globalContainer->gfx)
		return;
	const Rect viewport{0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH()};
	if (connectionOverlay)
		(void)connectionOverlay->describeControls(touch && touch->usesHUD(),
												  connectionOverlayArea(),
												  globalContainer->gfx->logicalUnitsPerPoint());
	const bool modal = activeDialog() || (connectionOverlay && connectionOverlay->blocksGameplay());
	std::vector<Rect> hudAbove, guideAbove, statisticsAbove;
	if (modal) {
		hudAbove.push_back(viewport);
		statisticsAbove.push_back(viewport);
	}
	const bool hiveVisible = hiveCardsVisible();
	if (hiveCards)
	{
		auto &host = hiveCards->host();
		host.layoutIfNeeded();
		if (!hiveVisible)
			hiveCards->cancelInput();
		host.setBlockedRegions(!hiveVisible ? std::vector<Rect>{viewport}
													   : std::vector<Rect>{});
		if (!modal && hiveVisible)
		{
			hudAbove.push_back(hiveCards->panelBounds());
			guideAbove.push_back(hiveCards->panelBounds());
			statisticsAbove.push_back(hiveCards->panelBounds());
		}
	}
	if (tutorialGuidance)
	{
		auto &host = tutorialGuidance->host();
		host.layoutIfNeeded();
		if (modal || (touch && touch->obscuresTutorial()))
		{
			tutorialGuidance->cancelInput();
			guideAbove = {viewport};
		}
		else if (connectionOverlay)
		{
			const auto r = connectionOverlay->panelBounds();
			guideAbove.push_back({r.x, r.y, r.w, r.h});
		}
		host.setBlockedRegions(guideAbove);
		if (!modal && (!touch || !touch->obscuresTutorial())) {
			hudAbove.push_back(tutorialGuidance->panelBounds());
			statisticsAbove.push_back(tutorialGuidance->panelBounds());
		}
	}
    if (connectionOverlay && !modal) {
        const auto r = connectionOverlay->panelBounds();
        statisticsAbove.push_back({r.x, r.y, r.w, r.h});
    }
    if (statisticsVisible()) {
        auto &host = statisticsHost();
        host.setBlockedRegions(std::move(statisticsAbove));
        if (!modal)
            hudAbove.push_back(host.popupOpen() ? viewport : statisticsBounds());
    } else if (statisticsPanel) {
        statisticsPanel->cancelEmbeddedInput();
        statisticsPanel->host().setBlockedRegions({viewport});
    }
	// Connection cards live in the positioned HUD tree itself. Its current tree
	// contains only their controls, so a network modal must not block that host.
	if (connectionOverlay && connectionOverlay->blocksGameplay() && !activeDialog())
		hudAbove.clear();
	if (hudHost)
		hudHost->setBlockedRegions(std::move(hudAbove));
}
bool GameGUI::activateHudControlAt(Point point, const std::string &prefix)
{
	const auto controls = describeHudControls();
	for (auto it = controls.rbegin(); it != controls.rend(); ++it)
		if ((prefix.empty() || it->key.starts_with(prefix)) && it->contains(point) &&
			(it->tap || std::find(it->actions.begin(), it->actions.end(), ActionKind::Activate) !=
							it->actions.end()))
		{
			// The standard host now owns human targeting as well as semantic
			// validation, including clipping and higher-surface occlusion.
			hudInspectionHost().tapAt(point);
			return true;
		}
	return false;
}
GameGUI::HudControlPaint::HudControlPaint(GameGUI &owner)
	: gui(owner), previous(owner.hudPaintControls)
{
	if (!previous)
	{
		snapshot = gui.describeHudControls();
		gui.hudPaintControls = &snapshot;
	}
}
GameGUI::HudControlPaint::~HudControlPaint()
{
	gui.hudPaintControls = previous;
}
Rect GameGUI::hudControlBounds(const std::string &key, Rect fallback)
{
	const auto fallbackControls =
		hudPaintControls ? std::vector<PositionedControl>{} : describeHudControls();
	const auto &controls = hudPaintControls ? *hudPaintControls : fallbackControls;
	for (const auto &c : controls)
		if (c.key == key || (key.starts_with("building/") && c.key.starts_with("building/") &&
							 c.key.ends_with(key.substr(8))))
			return c.bounds;
	return fallback;
}
std::vector<PositionedControl> GameGUI::describeEventControls()
{
	std::vector<PositionedControl> controls;
	const auto start = eventFeedStartY();
	if (!start)
		return controls;
	const auto layout = eventFeedLayout(32, *start);
	for (const auto &entry : layout.rows)
	{
		PositionedControl control;
		control.key = "game/events/" + std::to_string(entry.event.id);
		control.identity = std::to_string(entry.event.x) + ":" + std::to_string(entry.event.y);
		control.label = entry.event.label;
		if (entry.event.count > 1)
			control.label += " ×" + std::to_string(entry.event.count);
		control.bounds = entry.bounds;
		if (layout.mobile)
			control.clip = Rect{int(layout.area.x), int(layout.area.y), int(layout.area.w),
								int(layout.area.h)};
		control.tap = [this, x = entry.event.x, y = entry.event.y](Point)
		{ centerViewportOn(x, y); };
		controls.push_back(std::move(control));
	}
	return controls;
}
SDL_Rect GameGUI::connectionOverlayArea() const
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const int sidebar = touch && touch->usesHUD() ? 0 : RIGHT_MENU_WIDTH;
	SDL_Rect area{0, 16, gfx->getW() - sidebar, gfx->getH() - 16};
	if (touch && touch->usesHUD())
	{
		const auto ui = touch->hudLayout();
		area = {int(ui.world.x), int(ui.status.y + ui.status.h), int(ui.world.w),
				int(ui.world.y + ui.world.h - ui.status.y - ui.status.h)};
		if (ui.world.h > ui.world.w)
		{
			area.y += int(28 * unit);
			area.h -= int(28 * unit);
		}
	}
	return area;
}
std::vector<PositionedControl> GameGUI::describeHudControls()
{
	std::vector<PositionedControl> controls;
	if (!globalContainer->gfx)
		return controls;
	auto connectionControls = connectionOverlay
								  ? connectionOverlay->describeControls(
										touch && touch->usesHUD(), connectionOverlayArea(),
										globalContainer->gfx->logicalUnitsPerPoint())
								  : std::vector<PositionedControl>{};
	if (connectionOverlay && connectionOverlay->blocksGameplay())
		return connectionControls;
	auto compose = [&]()
	{
		// The network panel owns its visible pixels before the HUD receives input.
		// Omit overlapping underlying actions rather than exposing unreachable targets.
		for (const auto &overlay : connectionControls)
			controls.erase(
				std::remove_if(
					controls.begin(), controls.end(), [&](const auto &c)
					{ return !c.actions.empty() && c.bounds.intersects(overlay.bounds); }),
				controls.end());
		controls.insert(controls.end(), std::make_move_iterator(connectionControls.begin()),
						std::make_move_iterator(connectionControls.end()));
		return std::move(controls);
	};
	const int width = globalContainer->gfx->getW(), height = globalContainer->gfx->getH();
	const int left = width - RIGHT_MENU_WIDTH, content = width - RIGHT_MENU_RIGHT_OFFSET;
	auto button = [&](std::string key, std::string label, Rect bounds, std::function<void()> action,
					  bool enabled = true, bool selected = false)
	{
		PositionedControl c;
		c.key = std::move(key);
		c.label = std::move(label);
		c.bounds = bounds;
		c.enabled = enabled;
		c.selected = selected;
		c.tap = [action](Point) { action(); };
		controls.push_back(std::move(c));
	};
	// World presentation deliberately stays a canvas. Filtered world records are
	// provided by Hive, never copied into the UI tree.
	PositionedControl map;
	map.key = "game/map";
	map.label = "Map";
	map.role = "canvas";
	map.bounds = {0, 16, std::max(0, left), std::max(0, height - 16)};
	map.actions.clear();
	controls.push_back(map);
	if (touch && touch->usesHUD())
	{
		const auto world = touch->worldBounds();
		controls.front().bounds = {int(world.x), int(world.y), int(world.w), int(world.h)};
		auto compact = touch->describeControls();
		controls.insert(controls.end(), std::make_move_iterator(compact.begin()),
						std::make_move_iterator(compact.end()));
		return compose();
	}
	auto eventControls = describeEventControls();
	controls.insert(controls.end(), std::make_move_iterator(eventControls.begin()),
					std::make_move_iterator(eventControls.end()));
	PositionedControl mini;
	mini.key = "game/minimap";
	mini.label = "Minimap";
	mini.role = "canvas";
	minimap.setMapSize(drawnScene().map.getW(), drawnScene().map.getH());
	mini.bounds = minimap.imageBounds();
	mini.actions.clear();
	controls.push_back(mini);
	auto zoom = mapZoomControls(
		camera, "game/zoom",
		[this](int i)
		{
			const auto r = mapZoomControlBounds(camera, true, true)[i];
			const auto p = r.center();
			const bool ring = torusView.enabled();
			clickMapZoomControls(camera, p.x, p.y, true, true,
								 ring ? (globalContainer->gfx->getW() - RIGHT_MENU_WIDTH) / 2.0
									  : -1,
								 ring ? (globalContainer->gfx->getH() + 16) / 2.0 : -1);
			viewportX = camera.tileX();
			viewportY = camera.tileY();
			torusView.rebaseViewport(viewportX, viewportY);
		},
		true, true);
	for (auto &c : zoom)
		c.enabled = c.enabled && (torusView.enabled() || !torusView.active());
	controls.insert(controls.end(), std::make_move_iterator(zoom.begin()),
					std::make_move_iterator(zoom.end()));
	button("game/menu", "Game menu",
		   {left - IGM_ICON_HEIGHT / 2, IGM_MAIN_MENU_ICON_Y, IGM_ICON_HEIGHT, IGM_ICON_HEIGHT},
		   [this] { openMainMenu(); });
	if (!(hiddenGUIElements & HIDABLE_ALLIANCE))
		button("game/alliance", "Alliance",
			   {left - IGM_ICON_HEIGHT / 2, IGM_ALLIANCE_ICON_Y, IGM_ICON_HEIGHT, IGM_ICON_HEIGHT},
			   [this] { openDialog(IGM_ALLIANCE, std::make_unique<InGameAllianceScreen>(this)); });
	button("game/objectives", "Objectives",
		   {left - IGM_ICON_HEIGHT / 2, IGM_OBJECTIVES_ICON_Y, IGM_ICON_HEIGHT, IGM_ICON_HEIGHT},
		   [this]
		   { openDialog(IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(this, false)); });
	if (canChangeGameSpeed())
	{
		PositionedControl speed;
		speed.key = "game/speed";
		speed.label = "Game speed";
		speed.role = "stepper";
		speed.bounds = {topBarSpeedX() - 4, 0, TOP_BAR_SPEED_WIDTH, 16};
		speed.value = globalContainer->settings.gameSpeed;
		speed.actions = {ActionKind::Activate, ActionKind::SetValue};
		speed.tap = [this](Point) { cycleGameSpeed(true); };
		speed.perform = [this](const SemanticAction &a)
		{
			if (a.kind == ActionKind::Activate)
			{
				cycleGameSpeed(true);
				return true;
			}
			const auto *v = std::get_if<int>(&a.value);
			if (a.kind != ActionKind::SetValue || !v || *v < Settings::GAME_SPEED_MINIMUM ||
				*v > Settings::GAME_SPEED_MAXIMUM)
				return false;
			setGameSpeed(*v);
			return true;
		};
		controls.push_back(std::move(speed));
	}
	if (globalContainer->replaying)
	{
		PositionedControl track;
		track.key = "replay/progress";
		track.role = "canvas";
		track.label = "Replay progress";
		track.bounds = {0, REPLAY_BAR_Y, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT};
		track.actions.clear();
		controls.push_back(std::move(track));
		const auto bounds = replayPlaybackControlBounds();
		const char *keys[] = {"play", "pause", "fast-forward"};
		const char *labels[] = {"Play", "Pause", "Fast forward"};
		for (int i = 0; i < 3; ++i)
			button(std::string("replay/") + keys[i], labels[i], bounds[i],
				   [this, r = bounds[i]]
				   {
					   const auto p = r.center();
					   handleReplayProgressBarClick(p.x, p.y, SDL_BUTTON_LEFT);
				   });
	}
	const bool viewing = globalContainer->isViewingGame();
	const int count = viewing ? int(RDM_NB_VIEWS) : int(NB_VIEWS);
	const char *playLabels[] = {"Construction", "Flags and areas", "Statistics", "Graphs"};
	const char *watchLabels[] = {"Spectator options", "Statistics", "Graphs"};
	for (int i = 0; i < count; ++i)
	{
		if (!viewing && ((1 << i) & hiddenGUIElements))
			continue;
		button(
			"game/panel/" + std::to_string(i), viewing ? watchLabels[i] : playLabels[i],
			{left + (RIGHT_MENU_WIDTH - count * PANEL_BUTTON_WIDTH) / 2 + i * PANEL_BUTTON_WIDTH,
			 YPOS_BASE_DEFAULT - 32, PANEL_BUTTON_WIDTH, 32},
			[this, i, viewing]
			{
				if (viewing)
					replayDisplayMode = ReplayDisplayMode(i);
				else
					displayMode = DisplayMode(i);
				clearSelection();
			},
			true, viewing ? int(replayDisplayMode) == i : int(displayMode) == i);
	}
	if (selectionMode == BUILDING_SELECTION)
	{
		const auto *b = inputBuildingPanel();
		if (!b)
			return compose();
		const BuildingRef ref = b->state().identity;
		const std::string prefix =
			"building/" + std::to_string(ref.gid) + "/" + std::to_string(ref.generation) + "/";
		const auto *type = b->type;
		const bool own = !viewing && b->owner().number == localTeamNo;
		const bool allied = (b->owner().allies & Team::teamNumberToMask(localTeamNo)) != 0;
		const Rect clip{
			left, YPOS_BASE_BUILDING, RIGHT_MENU_WIDTH,
			std::max(0, height - BOTTOM_BUTTON_PRIMARY_YOFFSET - 4 - YPOS_BASE_BUILDING)};
		auto current = [this, ref]() -> const SceneBuildingPanel *
		{
			const auto *p = inputBuildingPanel();
			return p && p->state().identity == ref ? p : nullptr;
		};
		auto numeric = [&](std::string key, std::string label, Rect rect, int value, int minimum,
						   int maximum, std::function<int(const SceneBuildingPanel &)> readValue,
						   std::function<bool(const SceneBuildingPanel &, int)> change)
		{
			PositionedControl c;
			c.key = prefix + key;
			c.identity = std::to_string(b->state().typeNum) + ":" + std::to_string(minimum) + ":" +
						 std::to_string(maximum);
			c.role = "slider";
			c.label = std::move(label);
			c.bounds = rect;
			c.clip = clip;
			c.value = value;
			c.enabled = own;
			c.actions = {ActionKind::SetValue};
			auto apply = [this, current, readValue, change, minimum, maximum,
						  typeNum = b->state().typeNum, state = b->state().buildingState](int next)
			{
				if (next < minimum || next > maximum)
					return false;
				const auto *p = current();
				if (!p || globalContainer->isViewingGame() || p->owner().number != localTeamNo ||
					p->state().typeNum != typeNum || p->state().buildingState != state)
					return false;
				// A valid setter succeeds even when the ordinary request helper
				// deduplicates it. Keep live binding/permission checks above this.
				if (next == readValue(*p)) return true;
				return change(*p, next);
			};
			c.perform = [apply](const SemanticAction &a)
			{
				const auto *next = std::get_if<int>(&a.value);
				return a.kind == ActionKind::SetValue && next && apply(*next);
			};
			c.tap = [apply, value, maximum, rect](Point p)
			{
				if (const auto next = scrollBoxValue(p.x - rect.x, value, maximum))
					apply(*next);
			};
			controls.push_back(std::move(c));
		};
		auto option = [&](std::string key, std::string label, Rect rect, bool selected,
						  std::function<void(const SceneBuildingPanel &)> change)
		{
			button(
				prefix + key, std::move(label), rect,
				[current, change]
				{
					if (const auto *p = current())
						change(*p);
				},
				own, selected);
			controls.back().role = "option";
			controls.back().clip = clip;
		};
		int y = YPOS_BASE_BUILDING + YOFFSET_NAME + YOFFSET_ICON + YOFFSET_B_SEP +
				buildingExtraHeaderHeight(*type);
		auto bar = [&]
		{
			return Rect{content, y + YOFFSET_TEXT_BAR - buildingInfoScroll, SCROLLBOX_BAR_WIDTH,
						16};
		};
		if (type->maxUnitWorking)
		{
			if (allied && b->state().buildingState == Building::ALIVE)
				numeric("workers", "Assigned workers", bar(), displayedMaxUnitWorking(*b), 0,
						type->semantics.assignmentLimit,
						[this](const auto &p) { return displayedMaxUnitWorking(p); },
						[this](const auto &p, int v) { return requestWorkerAllocation(p, v); });
			y += YOFFSET_BAR + YOFFSET_B_SEP;
			y += YOFFSET_B_SEP;
			if (allied && b->state().buildingState == Building::ALIVE)
				for (int i = -1; i <= 1; ++i)
					option("priority/" + std::to_string(i),
						   i < 0 ? "Low priority"
						   : i   ? "High priority"
								 : "Medium priority",
						   {content + (i + 1) * (128 / 3), y + 16 - buildingInfoScroll, 12, 12},
						   displayedPriority(*b) == i,
						   [this, i](const auto &p) { requestBuildingPriority(p, i); });
			y += YOFFSET_BAR + YOFFSET_B_SEP;
		}
		if (type->maxUnitStayRange > 0 &&
			(type->zonable[WORKER] || type->zonable[EXPLORER] || type->zonable[WARRIOR]))
		{
			if (allied)
				numeric("range", "Flag range", bar(), displayedUnitStayRange(*b), 0,
						type->maxUnitStayRange,
						[this](const auto &p) { return displayedUnitStayRange(p); },
						[this](const auto &p, int v) { return requestFlagRange(p, v); });
			y += YOFFSET_BAR + YOFFSET_B_SEP;
		}
		auto rows = [&](int n, auto visible, auto add)
		{
			y += YOFFSET_B_SEP + YOFFSET_TEXT_PARA;
			for (int i = 0; i < n; ++i)
				if (visible(i))
				{
					add(i, Rect{content + 10, y - buildingInfoScroll, 12, YOFFSET_TEXT_PARA});
					y += YOFFSET_TEXT_PARA;
				}
		};
		if (allied && type->zonable[WORKER])
		{
			rows(
				MaterialCount, [&](int i) { return drawnScene().clearableMaterial(i); },
				[&](int i, Rect r)
				{
					option("clear/" + std::to_string(i), getMaterialName(i), r,
						   displayedClearingResource(*b, i),
						   [this, i](const auto &p)
						   {
							   std::array<bool, MaterialCount> next;
							   bool wire[MaterialCount];
							   for (int k = 0; k < MaterialCount; ++k)
								   next[k] = wire[k] = displayedClearingResource(p, k) ^ (k == i);
							   pendingFor(p.state().gid).pendingClearingResources = next;
							   enqueueOrder(
								   std::make_shared<OrderModifyClearingFlag>(p.state().gid, wire));
						   });
				});
			rows(
				NB_UNIT_LEVELS, [](int) { return true; },
				[&](int i, Rect r)
				{
					option("worker-level/" + std::to_string(i),
						   "Minimum worker level " + std::to_string(i + 1), r,
						   displayedMinWorkerLevelToFlag(*b) == i,
						   [this, i](const auto &p)
						   {
							   pendingFor(p.state().gid).pendingMinWorkerLevelToFlag = i;
							   enqueueOrder(std::make_shared<OrderModifyMinLevelToFlag>(
								   p.state().gid, i, 2));
						   });
				});
		}
		if (allied && type->zonable[WARRIOR])
			rows(
				NB_UNIT_LEVELS, [](int) { return true; },
				[&](int i, Rect r)
				{
					option("warrior-level/" + std::to_string(i),
						   "Minimum warrior level " + std::to_string(i + 1), r,
						   displayedMinLevelToFlag(*b) == i,
						   [this, i](const auto &p)
						   {
							   pendingFor(p.state().gid).pendingMinLevelToFlag = i;
							   enqueueOrder(
								   std::make_shared<OrderModifyMinLevelToFlag>(p.state().gid, i));
						   });
				});
		if (allied && type->zonable[EXPLORER])
			rows(
				EXPLORATION_FLAG_OPTION_COUNT, [](int) { return true; },
				[&](int i, Rect r)
				{
					option("explorer/" + std::to_string(i),
						   i ? "Ground attack explorers" : "Any explorer", r,
						   displayedExplorersRequireBombing(*b) == bool(i),
						   [this, i](const auto &p)
						   {
							   pendingFor(p.state().gid).pendingExplorersRequireBombing = bool(i);
							   enqueueOrder(std::make_shared<OrderModifyMinLevelToFlag>(
								   p.state().gid, i, 1));
						   });
				});
		if (type->armor)
			y += YOFFSET_TEXT_LINE;
		if (type->maxUnitInside)
			y += YOFFSET_INFOS;
		y += buildingProjectileStatsHeight(*type);
		if (allied && buildingServiceProgressTimeout(*type))
			y += YOFFSET_PROGRESS_BAR;
		y += YOFFSET_B_SEP;
		if (type->canExchange && (b->owner().exchangeVision & Team::teamNumberToMask(localTeamNo)))
			y += (1 + HAPPINESS_COUNT) * YOFFSET_TEXT_PARA;
		for (unsigned i = 0; i < MaterialCount; ++i)
			if (!(type->canExchange && i >= HAPPINESS_BASE &&
				  i < HAPPINESS_BASE + HAPPINESS_COUNT) &&
				type->maxMaterial[i] && drawnScene().materialVisible(i))
				y += YOFFSET_RESOURCE_LINE;
		if (type->maxBullets)
			y += YOFFSET_RESOURCE_LINE;
		y += YOFFSET_RESOURCE_SECTION_PAD;
		if (allied && type->semantics.production.enabledUnitMask)
		{
			y += YOFFSET_SWARM_PROGRESS_BAR;
			for (int i = 0; i < NB_UNIT_TYPE; ++i)
				if (type->semantics.production.recipes[i].enabled)
				{
					numeric("production/" + std::to_string(i),
							std::string(getUnitName(i)) + " production ratio",
							{content, y - buildingInfoScroll, SCROLLBOX_BAR_WIDTH, 16},
							displayedRatio(*b)[i], 0, MAX_RATIO_RANGE,
							[this, i](const auto &p) { return displayedRatio(p)[i]; },
							[this, i](const auto &p, int v)
							{
								auto values = displayedRatio(p);
								if (values[i] == v)
									return false;
								values[i] = v;
								pendingFor(p.state().gid).pendingRatio = values;
								enqueueOrder(std::make_shared<OrderModifySwarm>(p.state().gid,
																				values.data()));
								return true;
							});
					y += YOFFSET_SWARM_RATIO_LINE;
				}
		}
		PositionedControl scroll;
		scroll.key = prefix + "scroll";
		scroll.role = "scroll";
		scroll.label = "Building details";
		scroll.bounds = clip;
		scroll.value = buildingInfoScroll;
		scroll.actions = {ActionKind::Scroll};
		scroll.perform = [this](const SemanticAction &a)
		{
			const auto *d = std::get_if<int>(&a.value);
			if (a.kind != ActionKind::Scroll || !d)
				return false;
			buildingInfoScroll =
				int(std::clamp(static_cast<long long>(buildingInfoScroll) + *d, 0LL,
							   static_cast<long long>(buildingInfoScrollMaximum)));
			return true;
		};
		controls.insert(controls.begin(), std::move(scroll));
		if (allied && b->owner().number == drawnScene().panels.local.state().number)
		{
			std::string primary;
			if (b->state().constructionResultState == Building::REPAIR)
				primary = "Cancel repair";
			else if (b->state().constructionResultState == Building::UPGRADE)
				primary = "Cancel upgrade";
			else if (b->state().constructionResultState == Building::NO_CONSTRUCTION &&
					 b->state().buildingState == Building::ALIVE && !type->isBuildingSite)
			{
				if (b->state().hp < b->state().maxHp)
				{
					if (type->semantics.repairable && b->hardSpaceForRepair)
						primary = "Repair";
				}
				else if (type->nextLevel != -1 && b->hardSpaceForUpgrade)
					primary = "Upgrade";
			}
			if (!primary.empty())
				button(
					prefix + "construction", primary,
					{content, height - BOTTOM_BUTTON_PRIMARY_YOFFSET, 128, BOTTOM_BUTTON_HEIGHT},
					[this, current]
					{
						if (const auto *p = current())
							requestBuildingConstruction(*p);
					},
					own);
			if (b->state().buildingState == Building::ALIVE ||
				b->state().buildingState == Building::WAITING_FOR_DESTRUCTION)
				button(
					prefix + "destroy",
					b->state().buildingState == Building::ALIVE ? "Destroy" : "Cancel destroy",
					{content, height - BOTTOM_BUTTON_SECONDARY_YOFFSET, 128, BOTTOM_BUTTON_HEIGHT},
					[this, current]
					{
						if (const auto *p = current())
							requestBuildingDestruction(*p);
					},
					own);
		}
		return compose();
	}
	if (!viewing &&
		(selectionMode == NO_SELECTION || selectionMode == TOOL_SELECTION ||
		 selectionMode == BRUSH_SELECTION) &&
		(displayMode == CONSTRUCTION_VIEW || displayMode == FLAG_VIEW))
	{
		const bool flags = displayMode == FLAG_VIEW;
		const int top = flags ? YPOS_BASE_FLAG : YPOS_BASE_CONSTRUCTION, columns = flags ? 3 : 2;
		const auto &names = flags ? flagsChoiceName : buildingsChoiceName;
		const auto &states = flags ? flagsChoiceState : buildingsChoiceState;
		const int offset = flags ? flagChoiceRow : buildingChoiceRow;
		const int rows = choiceVisibleRows(top, columns);
		for (size_t i = 0; i < names.size(); ++i)
		{
			auto r = choiceControlBounds(top, i, columns);
			button(
				std::string(flags ? "game/flags/" : "game/buildings/") + names[i], names[i], r,
				[this, name = names[i]] { setSelection(TOOL_SELECTION, (void *)name.c_str()); },
				states[i]);
			controls.back().clip = Rect{left, top, RIGHT_MENU_WIDTH, rows * 46};
		}
		if (flags)
		{
			const int zoneCount = toolManager.zoneTypeCount();
			const char *names[] = {"Forbidden area", "Guard area", "Clearing area", "Farm area"};
			for (int i = 0; i < zoneCount; ++i)
				button(
					"game/zone/" + std::to_string(i), names[i],
					{left + zoneStripButtonX(i, zoneCount), YPOS_BASE_FLAG + YOFFSET_BRUSH,
					 ZONE_STRIP_BUTTON_SIZE, ZONE_STRIP_BUTTON_SIZE},
					[this, i]
					{
						setSelection(BRUSH_SELECTION);
						brush.defaultSelection();
						toolManager.activateZoneTool(GameGUIToolManager::ZoneType(i));
					},
					true,
					brush.getType() != BrushTool::MODE_NONE && int(toolManager.getZoneType()) == i);
			const int bx = left + (RIGHT_MENU_WIDTH - BrushTool::WIDTH) / 2,
					  by = YPOS_BASE_FLAG + YOFFSET_BRUSH + ZONE_STRIP_HEIGHT;
			auto brushButton = [&](std::string key, std::string label, Rect r, bool selected)
			{
				button(
					std::move(key), std::move(label), r,
					[this, r, bx, by]
					{
						setSelection(BRUSH_SELECTION);
						const auto p = r.center();
						brush.handleClick(p.x - bx, p.y - by);
						toolManager.activateZoneTool();
					},
					true, selected);
			};
			for (int i = 0; i < BrushTool::MODE_BUTTON_COUNT; ++i)
				brushButton("game/brush/mode/" + std::to_string(i), i ? "Erase area" : "Paint area",
							{bx + i * BrushTool::MODE_BUTTON_WIDTH, by,
							 BrushTool::MODE_BUTTON_WIDTH, BrushTool::MODE_ROW_HEIGHT},
							brush.getType() == unsigned(i + 1));
			for (unsigned i = 0; i < BrushTool::BRUSH_COUNT; ++i)
				brushButton("game/brush/figure/" + std::to_string(i),
							"Brush " + std::to_string(i + 1),
							{bx + int(i % BrushTool::FIGURE_COLUMNS) * BrushTool::FIGURE_CELL_SIZE,
							 by + BrushTool::MODE_ROW_HEIGHT +
								 int(i / BrushTool::FIGURE_COLUMNS) * BrushTool::FIGURE_CELL_SIZE,
							 BrushTool::FIGURE_CELL_SIZE, BrushTool::FIGURE_CELL_SIZE},
							brush.getFigure() == i);
		}
		PositionedControl scroll;
		scroll.key = flags ? "game/flags/scroll" : "game/buildings/scroll";
		scroll.role = "scroll";
		scroll.label = flags ? "Flags palette" : "Building palette";
		scroll.bounds = {left, top, RIGHT_MENU_WIDTH, rows * 46};
		scroll.value = offset;
		scroll.actions = {ActionKind::Scroll};
		scroll.perform =
			[this, flags, columns, rows, count = int(names.size())](const SemanticAction &a)
		{
			const auto *d = std::get_if<int>(&a.value);
			if (a.kind != ActionKind::Scroll || !d)
				return false;
			int &offset = flags ? flagChoiceRow : buildingChoiceRow;
			const long long delta =
				*d == 0 ? 0 : (*d > 0 ? std::max(1, *d / 46) : std::min(-1, *d / 46));
			offset = int(std::clamp(
				static_cast<long long>(offset) + delta, 0LL,
				static_cast<long long>(std::max(0, (count + columns - 1) / columns - rows))));
			return true;
		};
		controls.insert(controls.begin(), std::move(scroll));
	}
	const bool graphs =
		selectionMode == NO_SELECTION &&
		(viewing ? replayDisplayMode == RDM_STAT_GRAPH_VIEW : displayMode == STAT_GRAPH_VIEW);
	if (graphs)
	{
		const char *keys[] = {"starving", "damaged", "defense", "fertility"};
		const char *labels[] = {"Starving Map", "Damaged Map", "Defense Map", "Fertility Map"};
		const bool values[] = {showStarvingMap, showDamagedMap, showDefenseMap, showFertilityMap};
		for (int i = 0; i < 4; ++i)
			button(
				std::string("game/overlay/") + keys[i], labels[i],
				{left + 8, YPOS_BASE_STAT + 28 + i * 24 + (viewing ? 15 : 0), 16, 16},
				[this, i, selected = values[i]]
				{
					showStarvingMap = i == 0 && !selected;
					showDamagedMap = i == 1 && !selected;
					showDefenseMap = i == 2 && !selected;
					showFertilityMap = i == 3 && !selected;
				},
				true, values[i]);
	}
	if (viewing && replayDisplayMode == RDM_REPLAY_VIEW)
	{
		const int x = left + REPLAY_PANEL_XOFFSET, y = REPLAY_PANEL_YOFFSET,
				  inc = REPLAY_PANEL_SPACE_BETWEEN_OPTIONS;
		auto toggle = [&](std::string key, std::string label, int row, bool value,
						  std::function<void(bool)> apply)
		{
			PositionedControl c;
			c.key = "spectator/" + key;
			c.role = "toggle";
			c.label = std::move(label);
			c.bounds = {x, y + row * inc, 20, 20};
			c.value = value;
			c.selected = value;
			c.actions = {ActionKind::Activate, ActionKind::SetValue};
			c.tap = [value, apply](Point) { apply(!value); };
			c.perform = [value, apply](const SemanticAction &a)
			{
				if (a.kind == ActionKind::Activate)
				{
					apply(!value);
					return true;
				}
				const auto *v = std::get_if<bool>(&a.value);
				if (a.kind != ActionKind::SetValue || !v)
					return false;
				apply(*v);
				return true;
			};
			controls.push_back(std::move(c));
		};
		toggle("fog", "Fog of war", 1, globalContainer->replayShowFog,
			   [this](bool v)
			   {
				   globalContainer->replayShowFog = v;
				   minimap.setMinimapMode(v ? Minimap::ShowFOW : Minimap::HideFOW);
			   });
		toggle("vision", "Combined vision", 2, globalContainer->replayVisibleTeams == 0xFFFFFFFF,
			   [this](bool v)
			   {
				   globalContainer->replayVisibleTeams =
					   v ? 0xFFFFFFFF : Team::teamNumberToMask(localTeamNo);
			   });
		toggle("areas", "Show areas", 3, globalContainer->replayShowAreas,
			   [](bool v) { globalContainer->replayShowAreas = v; });
		toggle("flags", "Show flags", 4, globalContainer->replayShowFlags,
			   [](bool v) { globalContainer->replayShowFlags = v; });
		for (int i = 0; i < drawnScene().entities.teamCount; ++i)
			button(
				"spectator/team/" + std::to_string(i),
				displayPlayerName(drawnScene().entities.teams[i].firstPlayerName),
				{x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i + 1) * inc, 20, 20},
				[this, i] { selectViewedTeam(i); }, true, localTeamNo == i);
	}
	if (touch && !touch->usesHUD())
	{
		auto placement = touch->describeControls();
		controls.insert(controls.end(), std::make_move_iterator(placement.begin()),
						std::make_move_iterator(placement.end()));
	}
	return compose();
}
