// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include <algorithm>
// Presentation-only composition. Input and commands live in the coordinator,
// placement session, and action modules; no desktop composed screen is reused.
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "TouchReadout.h"
#include "Brush.h"
#include "BrushCoverage.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include "TeamStat.h"
#include "Unit.h"
#include "UnitDisplayNames.h"
#include "TeamDisplay.h"
#include "render/Minimap.h"
#include <TouchText.h>
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;

void GameGUITouch::drawControls()
{
	if (usesHUD() && gui.selectionMode == GameGUI::BRUSH_SELECTION && !activeDialog())
	{
		// Zone choices then Done along the bar, Done under the thumb; brush size,
		// Paint/Erase, Pan and Undo on the rail above it.
		auto *gfx = globalContainer->gfx;
		auto *strings = GAGCore::Toolkit::getStringTable();
		std::vector<std::string> labels = {strings->getString("[Forbidden]"), strings->getString("[Guard]"),
										   strings->getString("[Clear]")};
		if (gui.toolManager.farmAreasAvailable())
			labels.push_back(strings->getString("[Farm]"));
		labels.push_back(strings->getString("[Done]"));
		const auto buttons = brushBarButtons();
		for (int i = 0; i < int(buttons.size()); ++i)
		{
			const auto &b = buttons[i];
			gfx->drawFilledRect(int(b.x), int(b.y), int(b.w) - 1, int(b.h),
								i == int(gui.toolManager.getZoneType()) ? InGameTouchTheme::selected
																		: InGameTouchTheme::field);
			drawPointLabel(b, labels[i], .9);
		}
		BrushHUD::State state;
		state.figure = gui.brush.getFigure();
		state.erase = gui.brush.getType() == BrushTool::MODE_DEL;
		state.pan = brushPan;
		state.touched = railTouched;
		state.modeLabel = state.erase ? strings->getString("[Erase]") : strings->getString("[Paint]");
		state.panLabel = strings->getString("[Pan]");
		state.undoLabel = strings->getString("[Undo stroke]");
		BrushHUD::draw(brushHUD(), state);
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
	const auto confirm = confirmRect(), cancelBox = cancelRect();
	gfx->setClipRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h));
	gfx->drawFilledRect(int(confirm.x), int(confirm.y), int(confirm.w), int(confirm.h),
						Color(preview ? 35 : 55, preview ? 90 : 55, 45, 245));
	gfx->drawFilledRect(int(cancelBox.x), int(cancelBox.y), int(cancelBox.w), int(cancelBox.h),
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
		const auto box = index ? cancelBox : confirm;
		const double factor = std::min(box.h * 0.42 / label->getH(), box.w * 0.8 / label->getW());
		const int width = int(label->getW() * factor), height = int(label->getH() * factor);
		gfx->drawSurface(int(box.x + (box.w - width) / 2), int(box.y + (box.h - height) / 2), width,
						 height, label);
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
	if (inspecting() && usesDial())
	{
		drawDial();
		drawAllocation();
		return;
	}
	if (lensVisible())
	{
		drawLenses();
		return;
	}
	gfx->drawFilledRect(int(panel.x), int(panel.y), int(panel.w), int(panel.h),
						InGameTouchTheme::paper);
	if (showsBuildPalette())
	{
		drawBuildPalette();
		return;
	}
	if (inspecting())
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
	if (inspectingResource())
	{
		drawResourceInfo();
		return;
	}
	if (gui.selectionMode == GameGUI::UNIT_SELECTION)
		drawUnitPanel();
	else
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
	// Pending brush cells are transient presentation state: painting the map
	// starts only after release (or after a held tap's window), so the preview
	// can vanish on cancellation. Cells match the zone orders exactly.
	const auto &pending = !stroke.points.empty() ? stroke
						  : deferredStroke     ? deferredStroke->stroke
											   : stroke;
	if (!pending.points.empty() && gui.selectionMode == GameGUI::BRUSH_SELECTION)
	{
		const auto &map = gui.game.map;
		std::vector<BrushCoverage::Cell> centres;
		for (const auto &p : pending.points)
			centres.push_back({(int(p.x) >> 5) & map.getMaskW(), (int(p.y) >> 5) & map.getMaskH()});
		const auto area = world();
		const int zone = std::clamp(pending.zone, 0, 3);
		const Color fill = pending.mode == BrushTool::MODE_DEL ? InGameTouchTheme::erasePreview
																 : InGameTouchTheme::zonePreview[zone];
		const int size = std::max(2, int(std::ceil(32 * gui.camera.zoom)));
		auto wrap = [](double value, double extent) { return value - std::floor(value / extent) * extent; };
		gfx->setClipRect(int(area.x), int(area.y), int(area.w), int(area.h));
		for (const auto &[x, y] : BrushCoverage::cells(pending.figure, centres))
		{
			const double sx = (wrap(x * 32.0 - gui.viewportX * 32, map.getW() * 32) - gui.camera.fractionX()) *
								  gui.camera.zoom +
							  gui.camera.offsetX,
						 sy = (wrap(y * 32.0 - gui.viewportY * 32, map.getH() * 32) - gui.camera.fractionY()) *
								  gui.camera.zoom +
							  gui.camera.offsetY;
			if (sx >= area.x + area.w || sy >= area.y + area.h || sx + size <= area.x || sy + size <= area.y)
				continue;
			gfx->drawFilledRect(int(sx), int(sy), size, size, fill);
			gfx->drawRect(int(sx), int(sy), size, size, InGameTouchTheme::zonePreviewEdge[zone]);
		}
		gfx->setClipRect();
	}
	const auto hud = hudLayout(ui);
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
			 .arg(gui.drawnScene().panels.local.prestige)
			 .arg(gui.drawnScene().panels.hud.totalPrestige)
			 .arg(gui.drawnScene().panels.hud.prestigeToReach)});
	stats.push_back({"+" + std::to_string(gui.drawnScene().panels.local.unitConversionGained) + " / −" +
					 std::to_string(gui.drawnScene().panels.local.unitConversionLost)});
	// The last cell holds the speed chevrons and the simulation tick rate; where
	// the speed is fixed (network games) the rate has the cell to itself.
	const auto rate = gui.tickRate.rate();
	const bool chevrons = gui.canChangeGameSpeed();
	stats.push_back({rate ? TickRateMeter::format(*rate) : "-", -1, gui.tickRateShortfall() == 2});
	for (size_t i = 0; i < stats.size(); ++i)
	{
		const auto &stat = stats[i];
		const ViewRect r = statRect(hud, int(i));
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::paper);
		if (stat.warning)
			gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), Color(230, 100, 95));
		double inset = 0;
		if (stat.icon >= 0)
		{
			SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
			gfx->setUITransform(unit, r.x + 2 * unit, r.y + 3 * unit, &clip);
			globalContainer->unitmini->setBaseColor(gui.drawnScene().panels.local.color);
			gfx->drawSprite(0, 0, globalContainer->unitmini, stat.icon);
			gfx->setUITransform();
			gfx->setClipRect();
			inset = 20 * unit;
		}
		if (chevrons && i + 1 == stats.size())
		{
			const int lit = gui.litSpeedChevrons();
			const double middle = r.y + r.h / 2;
			for (int c = 0; c < GameSpeedControl::CHEVRONS; ++c)
			{
				const Color color = c < lit ? Color(120, 230, 120) : Color(110, 95, 125);
				// Half-pixel steps fill a stroke two points thick.
				for (double stroke = 0; stroke < 2 * unit; stroke += .5)
				{
					const float x = float(r.x + (4 + c * 7.5) * unit + stroke);
					gfx->drawLine(x, float(middle - 5 * unit), float(x + 4.5 * unit), float(middle), color);
					gfx->drawLine(float(x + 4.5 * unit), float(middle), x, float(middle + 5 * unit), color);
				}
			}
			inset = 42 * unit;
		}
		drawPointLabel({r.x + inset, r.y, r.w - inset, r.h}, stat.text, .72);
	}
	if (!activeDialog())
	{
		drawTutorial();
		drawPanel();
	}
	if (statsOpen && !activeDialog())
		drawStats();
	drawMinimap();
	if (!activeDialog())
	{
		drawOverlayLegend();
		if (peekOpen)
			drawPeek();
	}
	if (gesture.zoomDragging())
		TouchReadout::draw(touchPoint, zoomReadout(), ui.safe);
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
							   : std::min(rect.h, (std::min<size_t>(3, tutorialLines.size()) *
														   InGameTouchTheme::tutorialPitch() +
													   16 + (gui.swallowSpaceKey ? 48 : 0)) *
													  unit);
	return rect;
}
void GameGUITouch::prepareTutorial()
{
	std::string text = gui.drawnScene().panels.hud.legacyScriptText;
	if (!gui.scriptText.empty())
	{
		if (!text.empty())
			text += '\n';
		text += gui.scriptText;
	}
	const double width =
		(std::min(layout().world.w / globalContainer->gfx->logicalUnitsPerPoint() - 16, 560.0) - 64) /
		InGameTouchTheme::textGrowth();
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
	// Lines are tutorialLine authored pixels apart and drawn at the text unit,
	// so they sit tutorialPitch() points apart.
	const double pitch = InGameTouchTheme::tutorialPitch();
	const size_t first = std::min(tutorialLines.size(), size_t(std::max(0.0, tutorialScroll) / pitch));
	gfx->setUITransform(gfx->textUnitsPerPoint(), rect.x + 8 * unit,
						rect.y + (8 - tutorialScroll + first * pitch) * unit, &clip);
	const size_t end = std::min(tutorialLines.size(), first + size_t(rect.h / unit / pitch) + 1);
	for (size_t i = first; i < end; ++i)
		gfx->drawString(0, int((i - first) * InGameTouchTheme::tutorialLine), globalContainer->standardFont,
						tutorialLines[i]);
	gfx->setUITransform();
	gfx->setClipRect();
	const double textHeight = std::max(1.0, rect.h - footerHeight),
				 content = tutorialLines.size() * pitch * unit;
	if (content > textHeight)
	{
		gfx->drawFilledRect(int(rect.x + rect.w - 3 * unit),
							int(rect.y + std::clamp(tutorialScroll * unit, 0.0, content - textHeight) * textHeight / content),
							std::max(1, int(2 * unit)), int(textHeight * textHeight / content),
							Color(170, 185, 190));
	}
	drawPointLabel({rect.x + rect.w - 48 * unit, rect.y, 48 * unit, 48 * unit}, "−", 1.2);
	if (gui.swallowSpaceKey)
	{
		gfx->drawFilledRect(int(rect.x), int(rect.y + rect.h - 48 * unit), int(rect.w),
							int(48 * unit), InGameTouchTheme::selected);
		SDL_Rect footer{int(rect.x), int(rect.y + rect.h - 48 * unit), int(rect.w), int(48 * unit)};
		// 16 points below the footer's top at the authored size, centred as text grows.
		gfx->setUITransform(gfx->textUnitsPerPoint(), rect.x + 12 * unit,
							rect.y + rect.h - (24 + 8 * InGameTouchTheme::textGrowth()) * unit, &footer);
		gfx->drawString(0, 0, globalContainer->standardFont,
						Toolkit::getStringTable()->getString("[ok]"));
		gfx->setUITransform();
		gfx->setClipRect();
	}
}

const SceneBuildingPanel *GameGUITouch::allocationBuilding() const
{
	if (gui.selectionMode != GameGUI::BUILDING_SELECTION || globalContainer->isViewingGame())
		return nullptr;
	auto *building = inspectedBuilding();
	return building && building->owner.teamNumber == gui.localTeamNo &&
				   building->type->maxUnitWorking && building->buildingState == Building::ALIVE
			   ? building
			   : nullptr;
}
ViewRect GameGUITouch::allocationRect() const
{
	const auto ui = layout();
	auto rect = ui.panel;
	if (!inspecting() || rect.h <= 0)
		return {};
	// Compact identity stays in the HUD even when its controls need row fallback.
	if (!ui.persistentPanel)
		return hudLayout(ui).identity;
	rect.h = InGameTouchTheme::inspectorHeader * globalContainer->gfx->logicalUnitsPerPoint();
	return rect;
}
ViewRect GameGUITouch::panelContent() const
{
	const auto ui = layout();
	auto rect = ui.panel;
	const double header = gui.selectionMode == GameGUI::UNIT_SELECTION
		? 48 * globalContainer->gfx->logicalUnitsPerPoint()
		: inspecting() && !ui.persistentPanel ? 0 : allocationRect().h;
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
						 width / (textScale * globalContainer->gfx->textUnitsPerPoint()) - 8);
}
void GameGUITouch::drawPointLabel(ViewRect rect, const std::string &text, double textScale,
								  bool leading)
{
	auto *gfx = globalContainer->gfx;
	const double unit = textScale * gfx->textUnitsPerPoint();
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
	sprite->setBaseColor(building->owner.color);
	const bool compact = !layout().persistentPanel;
	const double factor = compact ? std::min({unit, 32 * unit / sprite->getW(frame),
		(rect.h - 8 * unit) / sprite->getH(frame)}) : unit;
	const double iconY = compact ? (rect.h - sprite->getH(frame) * factor) / 2 : 4 * unit;
	gfx->setUITransform(factor, rect.x + 4 * unit, rect.y + iconY, &clip);
	gfx->drawSprite(0, 0, sprite, frame);
	gfx->setUITransform();
	gfx->setClipRect();
	const std::string name = Toolkit::getStringTable()->getString("[" + type->type + "]");
	drawPointLabel(
		{rect.x + (compact ? 40 : 60) * unit, rect.y,
		 std::max(0.0, rect.w - (compact ? 88 : 108) * unit), rect.h},
		GAGCore::FormattableString(Toolkit::getStringTable()->getString("[%0 · %1]"))
				.arg(name)
				.arg(type->level + 1) +
			"\n" +
			GAGCore::FormattableString(
				GAGCore::Toolkit::getStringTable()->getString("[%0 / %1 HP · %2]"))
				.arg(building->hp)
				.arg(type->hpMax)
				.arg(building->owner.teamNumber == gui.drawnScene().panels.local.teamNumber
						 ? std::string(
							   GAGCore::Toolkit::getStringTable()->getString("[Your colony]"))
						 : GAGCore::FormattableString(
							   GAGCore::Toolkit::getStringTable()->getString("[Team %0]"))
							   .arg(building->owner.teamNumber + 1)),
		compact ? .8 : .85);
	drawPointLabel({rect.x + rect.w - 48 * unit, rect.y, 48 * unit, rect.h}, "×", 1.2);
}

GameGUITouch::HudLayout GameGUITouch::hudLayout(const MobileLayout &ui) const
{
	const auto safe = ui.safe;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	// Two stat rows need 56 points, then a 40-point identity bar and a gap.
	// Reserve it for compact building inspection; other tools keep their usual
	// minimap size. Wide landscapes have enough room for a single stat row.
	const bool identity = inspecting() && !ui.persistentPanel;
	const double side = (safe.h / unit < 400 && (!identity || safe.w / unit >= 680) ? 72 : 96) * unit;
	HudLayout hud;
	hud.minimap = {safe.x + safe.w - side - 4 * unit, safe.y + 4 * unit, side, side};
	const double available = std::max(0.0, hud.minimap.x - 4 * unit - ui.world.x);
	hud.columns = available / unit >= 600 ? 6 : 3;
	const double cell = std::min(120 * unit, available / hud.columns);
	hud.stats = {ui.world.x + (available - hud.columns * cell) / 2, safe.y + 4 * unit,
				 std::max(0.0, hud.columns * cell - 3 * unit), (6 / hud.columns * 28 - 4) * unit};
	const double header = InGameTouchTheme::inspectorHeader * unit;
	hud.identity = {hud.stats.x, hud.minimap.y + hud.minimap.h - header, hud.stats.w, header};
	return hud;
}

ViewRect GameGUITouch::statRect(const HudLayout &hud, int index) const
{
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double cell = (hud.stats.w + 3 * unit) / hud.columns;
	return {hud.stats.x + (index % hud.columns) * cell,
			hud.stats.y + (index / hud.columns) * 28 * unit, cell - 3 * unit, 24 * unit};
}

ViewRect GameGUITouch::speedRect() const
{
	if (!gui.canChangeGameSpeed())
		return {};
	const auto hud = hudLayout(layout());
	auto rect = statRect(hud, 5);
	// A thumb-sized target reaches below the cell unless the identity bar sits there.
	if (!(inspecting() && !layout().persistentPanel))
		rect.h = InGameTouchTheme::target * globalContainer->gfx->logicalUnitsPerPoint();
	return rect;
}

ViewRect GameGUITouch::minimapRect() const
{
	return hudLayout(layout()).minimap;
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
	hudMinimap->draw(gui.view.drawnScene(), gui.localTeamNo, gui.viewportX, gui.viewportY,
					 int(std::ceil(gui.camera.visibleW() / 32)),
					 int(std::ceil(gui.camera.visibleH() / 32)));
	gfx->setUITransform();
	gfx->setClipRect();
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border);
}
void GameGUITouch::navigateMinimap(ViewPoint point)
{
	navigateMinimapIn(*hudMinimap, minimapRect(), 128, point);
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
		for (int i = 0; i < gui.drawnScene().entities.teamCount; ++i)
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
bool GameGUITouch::inspectingResource() const
{
	return gui.selectionMode == GameGUI::RESOURCE_SELECTION;
}

std::optional<GameGUITouch::ResourceInfo> GameGUITouch::resourceInfo() const
{
	if (!inspectingResource()) return {};
	const auto &r = gui.drawnScene().map.getResource(size_t(gui.selectionResource()));
	if (r.type == NO_RES_TYPE) return {};
	const auto *type = globalContainer->resourcesTypes.get(r.type);
	ResourceInfo info;
	info.name = getResourceName(r.type);
	info.sprite = type->gfxId + r.variety * type->sizesCount + r.amount - (type->eternal ? 0 : 1);
	if (type->granular)
		info.amount = std::to_string(r.amount) + "/" + std::to_string(type->sizesCount);
	return info;
}

bool GameGUITouch::inspectingReadOnly() const
{
	return inspectingResource() || gui.selectionMode == GameGUI::UNIT_SELECTION;
}

ViewRect GameGUITouch::readOnlyCloseRect() const
{
	const auto panel = layout().panel;
	const double target = 48 * globalContainer->gfx->logicalUnitsPerPoint();
	return {panel.x + panel.w - target, panel.y, target, target};
}

void GameGUITouch::drawResourceInfo()
{
	const auto info = resourceInfo();
	if (!info) return;
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const auto panel = layout().panel;
	drawPointLabel({panel.x + 8 * unit, panel.y, panel.w - 56 * unit, 48 * unit}, info->name, .9);
	drawPointLabel(readOnlyCloseRect(), "×");
	const ViewRect icon{panel.x + 16 * unit, panel.y + 52 * unit, 48 * unit, 48 * unit};
	auto *sprite = globalContainer->resources;
	const double factor = std::min(icon.w / sprite->getW(info->sprite), icon.h / sprite->getH(info->sprite));
	SDL_Rect clip{int(panel.x), int(panel.y), int(panel.w), int(panel.h)};
	gfx->setUITransform(factor, icon.x + (icon.w - sprite->getW(info->sprite) * factor) / 2,
		icon.y + (icon.h - sprite->getH(info->sprite) * factor) / 2, &clip);
	gfx->drawSprite(0, 0, sprite, info->sprite);
	gfx->setUITransform();
	gfx->setClipRect();
	if (!info->amount.empty())
		drawPointLabel({icon.x + icon.w + 8 * unit, icon.y, panel.w - 88 * unit, icon.h}, info->amount);
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

std::vector<std::string> GameGUITouch::unitInfoRows() const
{
	const auto &u = gui.drawnScene().panels.unit;
	if (!u.valid) return {};
	auto *strings = Toolkit::getStringTable();
	std::vector<std::string> rows{displayPlayerName(u.owner.firstPlayerName)};
	auto value = [&](const char *key, std::string text) {
		rows.push_back(std::string(strings->getString(key)) + ": " + text);
	};
	value("[hp]", std::to_string(u.hp) + " / " + std::to_string(u.performance[HP]));
	value("[food]", std::to_string(u.hungry * 100 / Unit::HUNGRY_MAX) + "% (" + std::to_string(u.fruitCount) + ")");
	value("[current speed]", std::to_string(u.speed));
	if (u.performance[ARMOR]) value("[armor]", std::to_string(u.realArmor));
	if (u.performance[HARVEST]) {
		if (u.carriedResource < 0) rows.push_back(strings->getString("[don't carry anything]"));
		else value("[carry]", getResourceName(u.carriedResource));
	}
	const std::pair<int, const char *> abilities[] = {{WALK,"[Walk]"}, {SWIM,"[Swim]"}, {BUILD,"[Build]"},
		{HARVEST,"[Harvest]"}, {ATTACK_SPEED,"[At. speed]"}, {ATTACK_STRENGTH,"[At. strength]"},
		{MAGIC_ATTACK_AIR,"[Magic At. Air]"}, {MAGIC_ATTACK_GROUND,"[Magic At. Ground]"}};
	for (const auto &[ability,key] : abilities)
		if (u.performance[ability]) {
			const bool attack = ability == ATTACK_STRENGTH || ability == MAGIC_ATTACK_AIR || ability == MAGIC_ATTACK_GROUND;
			const int strength = (u.performance[ability] + (attack ? u.experienceLevel : 0)) *
				(ability == ATTACK_STRENGTH ? u.glassCannonScale : 1);
			value(key, "(" + std::to_string(u.level[ability] + (ability == SWIM ? 0 : 1)) + ") " + std::to_string(strength));
		}
	if (u.performance[ATTACK_STRENGTH] || u.performance[MAGIC_ATTACK_AIR] || u.performance[MAGIC_ATTACK_GROUND])
		rows.push_back("XP: " + std::to_string(u.experience) + " / " + std::to_string(u.nextLevelThreshold));
	return rows;
}

void GameGUITouch::drawUnitPanel()
{
	const auto panel = layout().panel;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	const double row = 48 * unit * InGameTouchTheme::textGrowth();
	const auto rows = unitInfoRows();
	labelClip = panelContent();
	globalContainer->gfx->setClipRect(int(labelClip->x), int(labelClip->y), int(labelClip->w), int(labelClip->h));
	for (size_t i = 0; i < rows.size(); ++i)
		drawPointLabel({panel.x, panel.y + 48 * unit + i * row - panelScroll * unit, panel.w, row}, rows[i], .9, true);
	labelClip.reset();
	globalContainer->gfx->setClipRect();
	if (gui.drawnScene().panels.unit.valid)
		drawPointLabel({panel.x, panel.y, panel.w - 48 * unit, 48 * unit}, getUnitName(gui.drawnScene().panels.unit.typeNum), 1.0, true);
	drawPointLabel(readOnlyCloseRect(), "×", 1.2);
}
