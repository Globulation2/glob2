// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
// Presentation-only composition. Input and commands live in the coordinator,
// placement session, and action modules; no desktop composed screen is reused.
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include "TeamStat.h"
#include "render/Minimap.h"
#include <TouchText.h>
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;

void GameGUITouch::drawControls()
{
	if (usesHUD() && gui.selectionMode == GameGUI::BRUSH_SELECTION && !activeDialog())
	{
		const auto rect = controls();
		auto *gfx = globalContainer->gfx;
		const std::string zones[] = {GAGCore::Toolkit::getStringTable()->getString("[Forbidden]"),
									 GAGCore::Toolkit::getStringTable()->getString("[Guard]"),
									 GAGCore::Toolkit::getStringTable()->getString("[Clear]")};
		const std::string labels[] = {
			zones[gui.toolManager.getZoneType()],
			GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString("[Brush %0]"))
				.arg(gui.brush.getFigure() + 1),
			gui.brush.getType() == BrushTool::MODE_ADD
				? GAGCore::Toolkit::getStringTable()->getString("[Paint]")
				: GAGCore::Toolkit::getStringTable()->getString("[Erase]"),
			GAGCore::Toolkit::getStringTable()->getString("[Done]")};
		for (int i = 0; i < 4; ++i)
		{
			const ViewRect button{rect.x + i * rect.w / 4, rect.y, rect.w / 4 - 1, rect.h};
			gfx->drawFilledRect(int(button.x), int(button.y), int(button.w), int(button.h),
								InGameTouchTheme::field);
			drawPointLabel(button, labels[i], .9);
		}
		return;
	}
	if (!active() || gui.selectionMode != GameGUI::TOOL_SELECTION || gui.inGameMenu ||
		gui.typingInputScreen || gui.scrollableText)
		return;
	auto *gfx = globalContainer->gfx;
	auto rect = controls();
	if (placement && placement->dragging)
	{
		gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
							InGameTouchTheme::paper);
		drawPointLabel(
			rect,
			GAGCore::Toolkit::getStringTable()->getString("[Release on valid terrain to place]"),
			.9);
		return;
	}
	const int half = int(rect.w / 2);
	gfx->setClipRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h));
	gfx->drawFilledRect(int(rect.x), int(rect.y), half, int(rect.h),
						Color(preview ? 35 : 55, preview ? 90 : 55, 45, 245));
	gfx->drawFilledRect(int(rect.x) + half, int(rect.y), int(rect.w) - half, int(rect.h),
						Color(100, 35, 35, 245));
	if (!confirmLabel)
	{
		auto makeLabel = [](const char *key)
		{
			const std::string text = Toolkit::getStringTable()->getString(key);
			auto *font = globalContainer->menuFont;
			auto label = std::make_unique<DrawableSurface>(font->getStringWidth(text),
														   font->getStringHeight(text));
			label->drawFilledRect(0, 0, label->getW(), label->getH(), Color(0, 0, 0, 0));
			label->drawString(0, 0, font, text);
			return label;
		};
		confirmLabel = makeLabel("[ok]");
		cancelLabel = makeLabel("[Cancel]");
	}
	for (int index = 0; index < 2; ++index)
	{
		auto *label = index ? cancelLabel.get() : confirmLabel.get();
		const double factor = std::min(rect.h * 0.42 / label->getH(), half * 0.8 / label->getW());
		const int width = int(label->getW() * factor), height = int(label->getH() * factor);
		gfx->drawSurface(int(rect.x) + index * half + (half - width) / 2,
						 int(rect.y) + (int(rect.h) - height) / 2, width, height, label);
	}
	gfx->setClipRect();
}

void GameGUITouch::drawPanel()
{
	if (!usesHUD())
		return;
	const auto panel = layout().panel;
	if (panel.w <= 0 || panel.h <= 0)
		return;
	auto *gfx = globalContainer->gfx;
	gfx->setClipRect();
	gfx->drawFilledRect(int(panel.x), int(panel.y), int(panel.w), int(panel.h),
						InGameTouchTheme::paper);
	if (showsBuildPalette())
	{
		drawBuildPalette();
		return;
	}
	if (inspectedBuilding())
	{
		drawBuildingActions();
		drawAllocation();
		return;
	}
	if (gui.selectionMode == GameGUI::BRUSH_SELECTION)
	{
		drawPointLabel(
			{panel.x, panel.y, panel.w, 48 * gfx->logicalUnitsPerPoint()},
			GAGCore::Toolkit::getStringTable()->getString("[Paint on the map; two fingers move]"),
			.75);
		return;
	}
	drawTacticalPanel();
}

void GameGUITouch::drawKeyboardFocus()
{
	if (!usesHUD() || keyboardFocus < 0)
		return;
	const auto targets = keyboardTargets();
	if (keyboardFocus >= int(targets.size()))
		return;
	const auto rect = targets[keyboardFocus];
	auto *gfx = globalContainer->gfx;
	gfx->setClipRect();
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), Color(180, 110, 20));
}

void GameGUITouch::drawHUD()
{
	if (!usesHUD())
		return;
	auto *gfx = globalContainer->gfx;
	const auto ui = layout();
	const double unit = gfx->logicalUnitsPerPoint();
	gfx->setClipRect();
	// A stroke preview is transient presentation state. Painting the actual
	// map starts only after release, so this trail can disappear on cancellation.
	if (stroke.points.size() > 1)
	{
		auto screenPoint = [&](ViewPoint p)
		{
			auto wrap = [](double value, double extent)
			{ return value - std::floor(value / extent) * extent; };
			return ViewPoint{(wrap(p.x - gui.viewportX * 32, gui.game.map.getW() * 32) -
							  gui.camera.fractionX()) *
									 gui.camera.zoom +
								 gui.camera.offsetX,
							 (wrap(p.y - gui.viewportY * 32, gui.game.map.getH() * 32) -
							  gui.camera.fractionY()) *
									 gui.camera.zoom +
								 gui.camera.offsetY};
		};
		for (size_t i = 1; i < stroke.points.size(); ++i)
		{
			const auto a = screenPoint(stroke.points[i - 1]), b = screenPoint(stroke.points[i]);
			if (world().contains(a) && world().contains(b) &&
				std::hypot(a.x - b.x, a.y - b.y) < world().w / 2)
				gfx->drawLine(int(a.x), int(a.y), int(b.x), int(b.y), InGameTouchTheme::border);
		}
	}
	const double available =
		std::min(ui.world.x + ui.world.w, minimapRect().x - 4 * unit) - ui.world.x;
	struct Stat
	{
		std::string text;
		int icon = -1;
		bool warning = false;
	};
	std::vector<Stat> stats;
	for (int i = 0; i < 3; ++i)
	{
		const int free =
			gui.teamStats->getFreeUnits(i) - (i == 0 ? gui.teamStats->getWorkersNeeded() : 0);
		stats.push_back(
			{std::to_string(free) + "/" + std::to_string(gui.teamStats->getTotalUnits(i)), i,
			 free < 0});
	}
	stats.push_back(
		{GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString("[P %0/%1/%2]"))
			 .arg(gui.localTeam->prestige)
			 .arg(gui.game.totalPrestige)
			 .arg(gui.game.prestigeToReach)});
	stats.push_back({"+" + std::to_string(gui.localTeam->unitConversionGained) + " / −" +
					 std::to_string(gui.localTeam->unitConversionLost)});
	int cpu = 0;
	for (auto value : gui.smoothedCPULoad)
		cpu += value;
	cpu /= GameGUI::SMOOTHED_CPU_SIZE;
	stats.push_back(
		{GAGCore::FormattableString(GAGCore::Toolkit::getStringTable()->getString("[CPU load %0]"))
			 .arg(cpu),
		 -1, cpu >= 75});
	const int columns = available / unit >= 600 ? 6 : 3;
	const double cell = std::min(120 * unit, available / columns);
	const double start = ui.world.x + (available - columns * cell) / 2;
	for (size_t i = 0; i < stats.size(); ++i)
	{
		const auto &stat = stats[i];
		const ViewRect r{start + (i % columns) * cell,
						 ui.safe.y + 4 * unit + (i / columns) * 28 * unit, cell - 3 * unit,
						 24 * unit};
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::paper);
		if (stat.warning)
			gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), Color(230, 100, 95));
		double inset = 0;
		if (stat.icon >= 0)
		{
			SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
			gfx->setUITransform(unit, r.x + 2 * unit, r.y + 3 * unit, &clip);
			globalContainer->unitmini->setBaseColor(gui.localTeam->color);
			gfx->drawSprite(0, 0, globalContainer->unitmini, stat.icon);
			gfx->setUITransform();
			gfx->setClipRect();
			inset = 20 * unit;
		}
		drawPointLabel({r.x + inset, r.y, r.w - inset, r.h}, stat.text, .72);
	}
	if (!activeDialog())
	{
		drawTutorial();
		drawPanel();
	}
	drawMinimap();
	if (activeDialog())
		return;
	if (gui.selectionMode == GameGUI::TOOL_SELECTION ||
		gui.selectionMode == GameGUI::BRUSH_SELECTION)
		return; // Active tools own this strip.
	const int icons[] = {globalContainer->replaying ? (gui.gamePaused ? 51 : 53) : 1,
						 globalContainer->replaying ? 55 : 29,
						 3,
						 47,
						 45,
						 6};
	for (int i = 0; i < 6; ++i)
	{
		const double x = ui.actions.x + i * ui.actions.w / 6, width = ui.actions.w / 6;
		gfx->drawFilledRect(int(x), int(ui.actions.y), int(width) - 1, int(ui.actions.h),
							InGameTouchTheme::paper);
		SDL_Rect clip{int(x), int(ui.actions.y), int(width), int(ui.actions.h)};
		gfx->setUITransform(.75 * unit, x + (width - 24 * unit) / 2, ui.actions.y + 2 * unit,
							&clip);
		gfx->drawSprite(0, 0, globalContainer->gamegui, icons[i]);
		gfx->setUITransform();
		gfx->setClipRect();
		const std::string labels[] = {globalContainer->replaying ? "[Pause]" : "[Build]",
									  globalContainer->replaying ? "[Speed]" : "[Flags]",
									  "[Info]",
									  "[Goals]",
									  "[Teams]",
									  "[Menu]"};
		drawPointLabel({x, ui.actions.y + 26 * unit, width, 22 * unit},
					   i == 2 ? GAGCore::Toolkit::getStringTable()->getString("[Editor tools]")
							  : Toolkit::getStringTable()->getString(labels[i]),
					   .82);
	}
}

ViewRect GameGUITouch::tutorialRect() const
{
	if (tutorialLines.empty())
		return {};
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	auto rect = layout().world;
	rect.x += 8 * unit;
	rect.y = minimapRect().y + minimapRect().h + 8 * unit;
	rect.w = std::min(rect.w - 16 * unit, 560 * unit);
	rect.h = std::max(0.0, layout().actions.y - rect.y);
	rect.h = tutorialCollapsed ? 48 * unit
							   : std::min(rect.h, (std::min<size_t>(3, tutorialLines.size()) * 24 +
												   16 + (gui.swallowSpaceKey ? 48 : 0)) *
													  unit);
	return rect;
}
void GameGUITouch::prepareTutorial()
{
	std::string text = gui.game.sgslScript.isTextShown ? gui.game.sgslScript.textShown : "";
	if (!gui.scriptText.empty())
	{
		if (!text.empty())
			text += '\n';
		text += gui.scriptText;
	}
	const double width =
		std::min(layout().world.w / globalContainer->gfx->logicalUnitsPerPoint() - 16, 560.0) - 64;
	if (text == tutorialText && width == tutorialWidth)
		return;
	tutorialCollapsed = false;
	tutorialText = text;
	tutorialWidth = width;
	tutorialLines.clear();
	tutorialScroll = 0;
	std::string line;
	auto *font = globalContainer->standardFont;
	InGameTouchTheme::TextStyle textStyle(font);
	// Break at whitespace when possible, otherwise at UTF-8 character boundaries.
	for (size_t at = 0; at < text.size();)
	{
		size_t end = at + 1;
		while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
			++end;
		if (text[at] == '\n')
		{
			tutorialLines.push_back(line);
			line.clear();
			at = end;
			continue;
		}
		const std::string next = text.substr(at, end - at);
		if (!line.empty() && font->getStringWidth(line + next) * 1.0 > width)
		{
			const auto space = line.find_last_of(' ');
			if (space != std::string::npos)
			{
				tutorialLines.push_back(line.substr(0, space));
				line.erase(0, space + 1);
			}
			else
			{
				tutorialLines.push_back(line);
				line.clear();
			}
		}
		line += next;
		at = end;
	}
	if (!line.empty())
		tutorialLines.push_back(line);
}
void GameGUITouch::drawTutorial()
{
	InGameTouchTheme::TextStyle tutorialStyle(globalContainer->standardFont);
	auto rect = tutorialRect();
	if (rect.w <= 0 || rect.h <= 0)
		return;
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
						InGameTouchTheme::paper);
	if (tutorialCollapsed)
	{
		drawPointLabel(rect, GAGCore::Toolkit::getStringTable()->getString("[Tutorial ▸]"));
		return;
	}
	const double footerHeight = gui.swallowSpaceKey ? 48 * unit : 0;
	SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w - 48 * unit),
				  int(std::max(0.0, rect.h - footerHeight))};
	const size_t first = std::min(tutorialLines.size(), size_t(tutorialScroll / 24));
	gfx->setUITransform(1.0 * unit, rect.x + 8 * unit,
						rect.y + (8 - tutorialScroll + first * 24) * unit, &clip);
	const size_t end = std::min(tutorialLines.size(), first + size_t(rect.h / unit / 24) + 1);
	for (size_t i = first; i < end; ++i)
		gfx->drawString(0, int((i - first) * 24), globalContainer->standardFont, tutorialLines[i]);
	gfx->setUITransform();
	gfx->setClipRect();
	const double textHeight = std::max(1.0, rect.h - footerHeight),
				 content = tutorialLines.size() * 24 * unit;
	if (content > textHeight)
	{
		gfx->drawFilledRect(int(rect.x + rect.w - 3 * unit),
							int(rect.y + tutorialScroll * unit * textHeight / content),
							std::max(1, int(2 * unit)), int(textHeight * textHeight / content),
							Color(170, 185, 190));
	}
	drawPointLabel({rect.x + rect.w - 48 * unit, rect.y, 48 * unit, 48 * unit}, "−", 1.2);
	if (gui.swallowSpaceKey)
	{
		gfx->drawFilledRect(int(rect.x), int(rect.y + rect.h - 48 * unit), int(rect.w),
							int(48 * unit), InGameTouchTheme::selected);
		SDL_Rect footer{int(rect.x), int(rect.y + rect.h - 48 * unit), int(rect.w), int(48 * unit)};
		gfx->setUITransform(1.0 * unit, rect.x + 12 * unit, rect.y + rect.h - 32 * unit, &footer);
		gfx->drawString(0, 0, globalContainer->standardFont,
						Toolkit::getStringTable()->getString("[ok]"));
		gfx->setUITransform();
		gfx->setClipRect();
	}
}

Building *GameGUITouch::allocationBuilding() const
{
	if (gui.selectionMode != GameGUI::BUILDING_SELECTION || globalContainer->isViewingGame())
		return nullptr;
	auto *building = gui.selectionBuilding();
	return building && building->owner->teamNumber == gui.localTeamNo &&
				   building->type->maxUnitWorking && building->buildingState == Building::ALIVE
			   ? building
			   : nullptr;
}
ViewRect GameGUITouch::allocationRect() const
{
	auto rect = layout().panel;
	if (!inspectedBuilding() || rect.h <= 0)
		return {};
	rect.h = InGameTouchTheme::inspectorHeader * globalContainer->gfx->logicalUnitsPerPoint();
	return rect;
}
ViewRect GameGUITouch::panelContent() const
{
	auto rect = layout().panel;
	const double header = allocationRect().h;
	rect.y += header;
	rect.h = std::max(0.0, rect.h - header);
	return rect;
}
std::vector<std::string> GameGUITouch::pointLines(const std::string &text, double width,
												  double textScale) const
{
	auto *font = globalContainer->standardFont;
	InGameTouchTheme::TextStyle textStyle(font);
	return wrapTouchText(font, text,
						 width / (textScale * globalContainer->gfx->logicalUnitsPerPoint()) - 8);
}
void GameGUITouch::drawPointLabel(ViewRect rect, const std::string &text, double textScale,
								  bool leading)
{
	auto *gfx = globalContainer->gfx;
	const double unit = textScale * gfx->logicalUnitsPerPoint();
	auto clipped = rect;
	if (labelClip)
	{
		const double right = std::min(rect.x + rect.w, labelClip->x + labelClip->w),
					 bottom = std::min(rect.y + rect.h, labelClip->y + labelClip->h);
		clipped.x = std::max(rect.x, labelClip->x);
		clipped.y = std::max(rect.y, labelClip->y);
		clipped.w = std::max(0.0, right - clipped.x);
		clipped.h = std::max(0.0, bottom - clipped.y);
	}
	SDL_Rect clip{int(clipped.x), int(clipped.y), int(clipped.w), int(clipped.h)};
	auto *font = globalContainer->standardFont;
	InGameTouchTheme::TextStyle textStyle(font);
	const auto lines = pointLines(text, rect.w, textScale);
	const int height = font->getStringHeight("Ag");
	gfx->setUITransform(unit, rect.x,
						rect.y + std::max(0.0, (rect.h / unit - lines.size() * height) / 2) * unit,
						&clip);
	for (size_t i = 0; i < lines.size(); ++i)
		gfx->drawString(
			leading ? 8 : std::max(4, int((rect.w / unit - font->getStringWidth(lines[i])) / 2)),
			int(i) * height, font, lines[i]);
	gfx->setUITransform();
	gfx->setClipRect();
}
void GameGUITouch::drawAllocation()
{
	auto *building = inspectedBuilding();
	const auto rect = allocationRect();
	if (!building || rect.h <= 0)
		return;
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
						InGameTouchTheme::paper);
	const auto *type = building->type;
	auto *sprite = type->miniSpriteImage >= 0 ? type->miniSpritePtr : type->gameSpritePtr;
	const int frame = type->miniSpriteImage >= 0 ? type->miniSpriteImage : type->gameSpriteImage;
	SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w), int(rect.h)};
	sprite->setBaseColor(building->owner->color);
	gfx->setUITransform(unit, rect.x + 4 * unit, rect.y + 4 * unit, &clip);
	gfx->drawSprite(0, 0, sprite, frame);
	gfx->setUITransform();
	gfx->setClipRect();
	const std::string name = Toolkit::getStringTable()->getString("[" + type->type + "]");
	drawPointLabel(
		{rect.x + 60 * unit, rect.y, std::max(0.0, rect.w - 108 * unit), rect.h},
		GAGCore::FormattableString(Toolkit::getStringTable()->getString("[%0 · %1]"))
				.arg(name)
				.arg(type->level + 1) +
			"\n" +
			GAGCore::FormattableString(
				GAGCore::Toolkit::getStringTable()->getString("[%0 / %1 HP · %2]"))
				.arg(building->hp)
				.arg(type->hpMax)
				.arg(building->owner == gui.localTeam
						 ? std::string(
							   GAGCore::Toolkit::getStringTable()->getString("[Your colony]"))
						 : GAGCore::FormattableString(
							   GAGCore::Toolkit::getStringTable()->getString("[Team %0]"))
							   .arg(building->owner->teamNumber + 1)),
		.85);
	drawPointLabel({rect.x + rect.w - 48 * unit, rect.y, 48 * unit, rect.h}, "×", 1.2);
}

ViewRect GameGUITouch::minimapRect() const
{
	const auto safe = layout().safe;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double side = (safe.h / unit < 400 ? 72 : 96) * unit;
	return {safe.x + safe.w - side - 4 * unit, safe.y + 4 * unit, side, side};
}
void GameGUITouch::drawMinimap()
{
	const auto rect = minimapRect();
	auto *gfx = globalContainer->gfx;
	SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w), int(rect.h)};
	gfx->setUITransform(rect.w / 128, rect.x - (gfx->getW() - 128) * rect.w / 128, rect.y, &clip);
	hudMinimap->setMinimapMode(globalContainer->replaying && !globalContainer->replayShowFog
								   ? Minimap::HideFOW
								   : Minimap::ShowFOW);
	hudMinimap->draw(gui.localTeamNo, gui.viewportX, gui.viewportY,
					 int(std::ceil(gui.camera.visibleW() / 32)),
					 int(std::ceil(gui.camera.visibleH() / 32)));
	gfx->setUITransform();
	gfx->setClipRect();
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border);
}
void GameGUITouch::navigateMinimap(ViewPoint point)
{
	const auto rect = minimapRect();
	int x, y;
	hudMinimap->convertToMap(globalContainer->gfx->getW() - 128 +
								 int((point.x - rect.x) * 128 / rect.w),
							 int((point.y - rect.y) * 128 / rect.h), x, y);
	gui.updateCamera();
	const int oldX = gui.viewportX, oldY = gui.viewportY;
	gui.camera.originX = x * 32 - gui.camera.visibleW() / 2;
	gui.camera.originY = y * 32 - gui.camera.visibleH() / 2;
	gui.camera.normalize();
	gui.viewportX = gui.camera.tileX();
	gui.viewportY = gui.camera.tileY();
	gui.viewportChanged(oldX, gui.viewportX, oldY, gui.viewportY);
}
std::vector<std::pair<std::string, int>> GameGUITouch::tacticalActions() const
{
	if (showStatistics)
	{
		const auto *stats = gui.teamStats->getLatestStat();
		return {{GAGCore::Toolkit::getStringTable()->getString("[Back to tools]"), -1},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Units: %0]"))
					 .arg(stats->totalUnit),
				 -2},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Buildings: %0]"))
					 .arg(stats->totalBuilding),
				 -2},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Food: %0 / %1]"))
					 .arg(stats->totalFood)
					 .arg(stats->totalFoodCapacity),
				 -2},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Need food: %0]"))
					 .arg(stats->needFood),
				 -2},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Need healing: %0]"))
					 .arg(stats->needHeal),
				 -2},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Attack: %0]"))
					 .arg(stats->totalAttackPower),
				 -2},
				{GAGCore::FormattableString(
					 GAGCore::Toolkit::getStringTable()->getString("[Defense: %0]"))
					 .arg(stats->totalDefensePower),
				 -2}};
	}
	auto toggle = [](const std::string &label, bool selected)
	{ return std::string(selected ? "[x] " : "[ ] ") + label; };
	std::vector<std::pair<std::string, int>> result = {
		{GAGCore::Toolkit::getStringTable()->getString("[Statistics]"), 3},
		{toggle(GAGCore::Toolkit::getStringTable()->getString("[Starvation overlay]"),
				gui.showStarvingMap),
		 20},
		{toggle(GAGCore::Toolkit::getStringTable()->getString("[Damage overlay]"),
				gui.showDamagedMap),
		 21},
		{toggle(GAGCore::Toolkit::getStringTable()->getString("[Defense overlay]"),
				gui.showDefenseMap),
		 22},
		{toggle(GAGCore::Toolkit::getStringTable()->getString("[Fertility overlay]"),
				gui.showFertilityMap),
		 23},
		{toggle(GAGCore::Toolkit::getStringTable()->getString("[Health and food bars]"),
				gui.drawHealthFoodBar),
		 6},
		{GAGCore::Toolkit::getStringTable()->getString("[Message history]"), 4}};
	if (!globalContainer->isViewingGame())
	{
		result.push_back(
			{GAGCore::Toolkit::getStringTable()->getString("[Mark map for allies]"), 5});
		result.push_back({GAGCore::Toolkit::getStringTable()->getString("[Chat]"), 1});
	}
	else
	{
		std::vector<std::pair<std::string, int>> playback = {
			{toggle(GAGCore::Toolkit::getStringTable()->getString("[fog of war]"),
					globalContainer->replayShowFog),
			 31},
			{toggle(GAGCore::Toolkit::getStringTable()->getString("[combined vision]"),
					globalContainer->replayVisibleTeams == 0xffffffff),
			 32},
			{toggle(GAGCore::Toolkit::getStringTable()->getString("[Show zones]"),
					globalContainer->replayShowAreas),
			 33},
			{toggle(GAGCore::Toolkit::getStringTable()->getString("[show flags]"),
					globalContainer->replayShowFlags),
			 34}};
		for (int i = 0; i < gui.game.teamsCount(); ++i)
			playback.push_back(
				{toggle((GAGCore::FormattableString(
							 GAGCore::Toolkit::getStringTable()->getString("[View team %0]"))
							 .arg(i + 1))
							.c_str(),
						gui.localTeamNo == i),
				 40 + i});
		playback.insert(playback.end(), result.begin(), result.end());
		return playback;
	}
	return result;
}
void GameGUITouch::drawTacticalPanel()
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const auto panel = layout().panel;
	const auto rows = tacticalActions();
	labelClip = panel;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		ViewRect rect{panel.x, panel.y + (i * 56 - panelScroll) * unit, panel.w, 48 * unit};
		gfx->setClipRect(int(panel.x), int(panel.y), int(panel.w), int(panel.h));
		gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h),
							InGameTouchTheme::field);
		drawPointLabel(rect, rows[i].first, .9);
	}
	labelClip.reset();
	gfx->setClipRect();
}
