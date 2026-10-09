#include "render/ResourceSprites.h"
#include "BuildingPresentation.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include <algorithm>
#include <cmath>
// Presentation-only composition. Input and commands live in the coordinator,
// placement session, and action modules; no desktop composed screen is reused.
#include "GameGUITouch.h"
#include "HudUnitConversionIcon.h"
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

void GameGUITouch::drawNavigationIcon(int icon, ViewPoint center, bool selected, double points)
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint(), density = std::max(.25, double(gfx->getRasterScale()));
	const auto ink = icon == 9 ? Color(232, 194, 107) : InGameTouchTheme::ink();
	const Color accent(232, 194, 107);
	const auto packed = [](Color c) { return double((Uint32(c.r) << 24) | (Uint32(c.g) << 16) | (Uint32(c.b) << 8) | c.a); };
	const std::array<double, 4> key{unit, density, packed(InGameTouchTheme::ink()), packed(accent)};
	if (key != navigationIconKey)
	{
		for (auto &image : navigationIcons) image.reset();
		navigationIconKey = key;
	}
	if (!navigationIcons[icon])
	{
		// Evaluate strokes in backing pixels: rounded ends and smooth edges at
		// every display density, without scaling pixel-art desktop buttons.
		const int size = std::max(1, int(std::ceil(28 * unit * density)));
		auto *pixels = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888);
		if (!pixels) return;
		SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
		for (int row = 0; row < size; ++row)
			for (int col = 0; col < size; ++col)
			{
				const double x = (col + .5) / (density * unit) - 14;
				const double y = (row + .5) / (density * unit) - 14;
				double distance = 100;
				const auto line = [&](double ax, double ay, double bx, double by) {
					const double dx = bx - ax, dy = by - ay;
					const double t = std::clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0., 1.);
					distance = std::min(distance, std::hypot(x - ax - t * dx, y - ay - t * dy) - 1.05);
				};
				const auto circle = [&](double cx, double cy, double r) {
					distance = std::min(distance, std::abs(std::hypot(x - cx, y - cy) - r) - 1.05);
				};
				switch (icon)
				{
				case 0: // Hammer: construction.
					line(-7, 8, 3, -2); line(-5, -5, 0, -10); line(0, -10, 8, -2);
					line(8, -2, 3, 3); line(3, 3, -5, -5); break;
				case 1:
					line(-7, 10, -7, -10); line(-7, -9, 8, -9); line(8, -9, 4, -3);
					line(4, -3, 8, 2); line(8, 2, -7, 2); break;
				case 2: // Sliders, with gaps around the handles.
					for (int j = 0; j < 3; ++j) {
						const double yy = -7 + j * 7, xx = j == 1 ? 4 : -4;
						line(-10, yy, xx - 3, yy); line(xx + 3, yy, 10, yy); circle(xx, yy, 2.5);
					} break;
				case 3: circle(0, 0, 9); circle(0, 0, 4); line(0, 0, 9, -9); break;
				case 4:
					circle(0, -6, 3); circle(-8, -3, 2); circle(8, -3, 2);
					line(-5, 10, -5, 5); line(-5, 5, -2, 2); line(-2, 2, 2, 2);
					line(2, 2, 5, 5); line(5, 5, 5, 10); line(-5, 10, 5, 10);
					line(-11, 8, -11, 4); line(-11, 4, -8, 2);
					line(11, 8, 11, 4); line(11, 4, 8, 2); break;
				case 5: line(-9, -7, 9, -7); line(-9, 0, 9, 0); line(-9, 7, 9, 7); break;
				case 6: line(-5, -9, -5, 9); line(5, -9, 5, 9); break;
				case 7: line(-6, -9, 8, 0); line(8, 0, -6, 9); line(-6, 9, -6, -9); break;
				case 8:
					for (int j = 0; j < 2; ++j) { const double xx = -9 + j * 11;
						line(xx, -8, xx + 7, 0); line(xx + 7, 0, xx, 8); } break;
				case 9: // Trophy, with handles and a broad pedestal.
					line(-6, -9, 6, -9); line(-6, -9, -5, 0); line(-5, 0, 0, 4);
					line(0, 4, 5, 0); line(5, 0, 6, -9); line(0, 4, 0, 9);
					line(-5, 10, 5, 10); line(-6, -6, -10, -6); line(-10, -6, -10, -2);
					line(-10, -2, -5, 2); line(6, -6, 10, -6); line(10, -6, 10, -2); line(10, -2, 5, 2); break;

				}
				const Uint32 alpha = Uint8(std::round(255 * std::clamp(.5 - distance * density * unit, 0., 1.)));
				auto *scan = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + row * pixels->pitch);
				scan[col] = (alpha << 24) | (Uint32(ink.r) << 16) | (Uint32(ink.g) << 8) | ink.b;
			}
		navigationIcons[icon] = std::make_unique<DrawableSurface>(pixels, DrawableSurface::AdoptPixels{});
	}
	if (selected)
		gfx->drawFilledRect(int(center.x - 12 * unit), int(center.y - 16 * unit), int(24 * unit),
			std::max(1, int(2 * unit)), accent);
	gfx->drawSurface(float(center.x - points / 2 * unit), float(center.y - points / 2 * unit), float(points * unit),
		float(points * unit), navigationIcons[icon].get());
}

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
								i == int(gui.toolManager.getZoneType()) ? InGameTouchTheme::selected()
																		: InGameTouchTheme::field());
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
							InGameTouchTheme::paper());
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
	if (inspectingResource())
	{
		drawResourceInfo();
		return;
	}
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
						InGameTouchTheme::paper());
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
		const auto &map = gui.drawnScene().map;
		std::vector<BrushCoverage::Cell> centres;
		for (const auto &p : pending.points)
			centres.push_back({(int(p.x) >> 5) & map.getMaskW(), (int(p.y) >> 5) & map.getMaskH()});
		const auto area = world();
		const int zone = std::clamp(pending.zone, 0, 3);
		const Color fill = pending.mode == BrushTool::MODE_DEL ? InGameTouchTheme::erasePreview()
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
	const auto &colony = gui.drawnScene().panels.local.state();
	const auto &match = gui.drawnScene().panels.hud;
	stats.push_back({std::to_string(colony.prestige)});
	stats.push_back({""});
	// The last cell holds the speed chevrons and the simulation tick rate; where
	// the speed is fixed (network games) the rate has the cell to itself.
	const auto rate = gui.tickRate.rate();
	const bool chevrons = gui.canChangeGameSpeed();
	stats.push_back({rate ? TickRateMeter::format(*rate) : "-", -1, gui.tickRateShortfall() == 2});
	const auto paper = InGameTouchTheme::paper();
	const Color backdrop(paper.r, paper.g, paper.b, 245);
	gfx->drawFilledRect(int(hud.stats.x), int(hud.stats.y), int(hud.stats.w), int(hud.stats.h), backdrop);
	const auto drawUnitImage = [&](ViewRect box, int frame, Color color) {
		auto *sprite = globalContainer->unitmini;
		sprite->setBaseColor(color);
		const double scale = std::min(box.w / sprite->getW(frame), box.h / sprite->getH(frame));
		SDL_Rect clip{int(box.x), int(box.y), int(box.w), int(box.h)};
		gfx->setUITransform(scale, box.x + (box.w - sprite->getW(frame) * scale) / 2,
			box.y + (box.h - sprite->getH(frame) * scale) / 2, &clip);
		gfx->drawSprite(0, 0, sprite, frame);
		gfx->setUITransform(); gfx->setClipRect();
	};
	// Measure the icon and value as one group. Every value starts the same
	// distance after its icon; the cell centres the group, not the text alone.
	auto *font = globalContainer->standardFont;
	const double mainHeight = 40 * unit, valueGap = 6 * unit, iconGap = 4 * unit;
	double numericScale = std::min(.88, 24 * unit /
		(std::max(1, font->getStringHeight("Ag")) * gfx->textUnitsPerPoint()));
	double iconPoints = 24;
	const auto textWidth = [&](const std::string &text) {
		return font->getStringWidth(text) * gfx->textUnitsPerPoint();
	};
	const auto eachMetric = [&](const auto &visit) {
		for (int i : {0, 1, 2, 3, 5})
			visit(hud.cells[i], stats[i].text, false, i != 5 || chevrons);
		for (int side = 0; side < 2; ++side)
			visit(ViewRect{0, 0, hud.cells[4].w / 2, mainHeight},
				std::to_string(side == 0 ? colony.unitConversionGained : colony.unitConversionLost), true, true);
	};
	eachMetric([&](const ViewRect &r, const std::string &text, bool pair, bool hasIcon) {
		if (hasIcon)
			iconPoints = std::min(iconPoints, (r.w - 8 * unit - valueGap -
				(pair ? iconGap : 0) - textWidth(text) * numericScale) / ((pair ? 2 : 1) * unit));
	});
	iconPoints = std::clamp(iconPoints, 12., 24.);
	const auto iconWidth = [&](bool pair, bool hasIcon) {
		return hasIcon ? (pair ? 2 * iconPoints * unit + iconGap : iconPoints * unit) : 0.;
	};
	eachMetric([&](const ViewRect &r, const std::string &text, bool pair, bool hasIcon) {
		numericScale = std::min(numericScale, std::max(1., r.w - 8 * unit -
			iconWidth(pair, hasIcon) - (hasIcon ? valueGap : 0)) / std::max(1., textWidth(text)));
	});
	const auto groupLeft = [&](ViewRect r, const std::string &text, bool pair = false, bool hasIcon = true) {
		return r.x + (r.w - iconWidth(pair, hasIcon) - (hasIcon ? valueGap : 0) -
			textWidth(text) * numericScale) / 2;
	};
	const auto drawValue = [&](ViewRect r, double x, const std::string &text) {
		const double scale = numericScale * gfx->textUnitsPerPoint();
		SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
		InGameTouchTheme::TextStyle textStyle(font);
		gfx->setUITransform(scale, x, r.y + (mainHeight - font->getStringHeight("Ag") * scale) / 2, &clip);
		gfx->drawString(0, 0, font, text);
		gfx->setUITransform(); gfx->setClipRect();
	};
	const auto unitIcon = [&](ViewPoint center, int frame) {
		drawUnitImage({center.x - iconPoints / 2 * unit, center.y - iconPoints / 2 * unit, iconPoints * unit, iconPoints * unit}, frame, presentationColor(colony.color));
	};
	for (size_t i = 0; i < stats.size(); ++i)
	{
		const auto &stat = stats[i];
		const ViewRect r = statRect(hud, int(i));
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h), backdrop);
		if (stat.warning)
			gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), Color(230, 100, 95));
		const bool hasIcon = i != 5 || chevrons;
		const double left = groupLeft(r, stat.text, false, hasIcon);
		const ViewPoint center{left + iconPoints / 2 * unit, r.y + mainHeight / 2};
		if (i == 4)
		{
			for (int side = 0; side < 2; ++side)
			{
				const ViewRect part{r.x + side * r.w / 2, r.y, r.w / 2, r.h};
				const std::string text = std::to_string(side == 0 ? colony.unitConversionGained : colony.unitConversionLost);
				const double groupX = groupLeft(part, text, true);
				const double width = iconWidth(true, true), height = iconPoints * unit;
				gui.unitConversionIcon->draw(gfx, globalContainer->unitmini,
					{groupX, part.y + (mainHeight - height) / 2, width, height},
					presentationColor(colony.color), side == 0);
				drawValue(part, groupX + iconWidth(true, true) + valueGap, text);
			}
			continue;
		}
		if (stat.icon >= 0)
			unitIcon(center, stat.icon);
		else if (i == 3)
			drawNavigationIcon(9, center, false, iconPoints);
		else if (chevrons)
		{
			// The full speed symbol shares the same icon box as the
			// unit portraits and trophy, regardless of the active speed.
			const int lit = gui.litSpeedChevrons();
			const double pitch = iconPoints / GameSpeedControl::CHEVRONS;
			for (int c = 0; c < GameSpeedControl::CHEVRONS; ++c)
			{
				const Color color = c < lit ? Color(120, 230, 120) : Color(110, 95, 125);
				for (double stroke = 0; stroke < 1.5 * unit; stroke += .5)
				{
					const float x = float(center.x - iconPoints / 2 * unit + c * pitch * unit + stroke);
					gfx->drawLine(x, float(center.y - 5 * unit), float(x + 3 * unit), float(center.y), color);
					gfx->drawLine(float(x + 3 * unit), float(center.y), x, float(center.y + 5 * unit), color);
				}
			}
		}
		drawValue(r, left + iconWidth(false, hasIcon) + (hasIcon ? valueGap : 0), stat.text);
		if (i == 3)
		{
			// Secondary match progress stays below the common primary row.
			const double barY = r.y + 36 * unit, barW = r.w - 16 * unit;
			const double barX = r.x + 8 * unit;
			if (match.state().prestigeWinCondition && match.state().prestigeToReach > 0)
			{
				const double progress = std::clamp(double(match.totalPrestige()) / match.state().prestigeToReach, 0., 1.);
				gfx->drawFilledRect(int(barX), int(barY), int(barW), int(2 * unit), InGameTouchTheme::dialTrack());
				gfx->drawFilledRect(int(barX), int(barY), int(barW * progress), int(2 * unit), Color(232, 194, 107));
			}
		}
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
	const std::string labels[] = {globalContainer->replaying ? "[Pause]" : "[Build]",
		globalContainer->replaying ? "[Speed]" : "[Flags]", "[Editor tools]", "[Goals]", "[Teams]", "[Menu]"};
	gfx->drawFilledRect(int(ui.actions.x), int(ui.actions.y), int(ui.actions.w), int(ui.actions.h),
		InGameTouchTheme::paper());
	gfx->drawHorzLine(int(ui.actions.x), int(ui.actions.y), int(ui.actions.w), InGameTouchTheme::border());
	for (int i = 0; i < 6; ++i)
	{
		const double x = ui.actions.x + i * ui.actions.w / 6, width = ui.actions.w / 6;
		const bool selected = i == 0 ? panelOpen && !inspecting() && !inspectingReadOnly() && gui.displayMode == GameGUI::CONSTRUCTION_VIEW
			: i == 1 ? panelOpen && !inspecting() && !inspectingReadOnly() && gui.displayMode == GameGUI::FLAG_VIEW
			: i == 2 && (lensOpen || (panelOpen && gui.displayMode == GameGUI::STAT_TEXT_VIEW));
		const int icon = globalContainer->replaying && i < 2 ? (i == 1 ? 8 : gui.gamePaused ? 7 : 6) : i;
		drawNavigationIcon(icon, {x + width / 2, ui.actions.y + 17 * unit}, selected);
		drawPointLabel({x + 2 * unit, ui.actions.y + 32 * unit, width - 4 * unit, 16 * unit},
			Toolkit::getStringTable()->getString(labels[i]), .86);
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
	std::string text = gui.drawnScene().panels.hud.state().legacyScriptText;
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
						InGameTouchTheme::paper());
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
							int(48 * unit), InGameTouchTheme::selected());
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
	return building && building->owner().number == gui.localTeamNo &&
				   building->type->maxUnitWorking && building->state().buildingState == Building::ALIVE
			   ? building
			   : nullptr;
}
ViewRect GameGUITouch::allocationRect() const
{
	const auto ui = layout();
	auto rect = ui.panel;
	if ((!inspecting() && !inspectingReadOnly()) || rect.h <= 0)
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
		? (InGameTouchTheme::unitStatsTitle + (ui.persistentPanel ? InGameTouchTheme::inspectorHeader : 0)) *
			globalContainer->gfx->logicalUnitsPerPoint()
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
void GameGUITouch::drawSelectionHeader(ViewRect rect, Sprite *sprite, int frame,
	const std::string &text)
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const bool compact = !layout().persistentPanel;
	gfx->setClipRect();
	gfx->drawFilledRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::paper());
	const ViewRect icon{rect.x + 4 * unit, rect.y + 4 * unit,
		(compact ? 32 : 52) * unit, std::max(0.0, rect.h - 8 * unit)};
	if (sprite && frame >= 0 && frame < sprite->getFrameCount() &&
		sprite->getW(frame) > 0 && sprite->getH(frame) > 0)
	{
		const double factor = std::min({unit, icon.w / sprite->getW(frame), icon.h / sprite->getH(frame)});
		SDL_Rect clip{int(rect.x), int(rect.y), int(rect.w), int(rect.h)};
		gfx->setUITransform(factor, icon.x, icon.y + (icon.h - sprite->getH(frame) * factor) / 2, &clip);
		gfx->drawSprite(0, 0, sprite, frame);
		gfx->setUITransform();
		gfx->setClipRect();
	}
	else
	{
		gfx->drawFilledRect(int(icon.x), int(icon.y), int(icon.w), int(icon.h), 255, 0, 255);
		gfx->drawFilledRect(int(icon.x + icon.w / 4), int(icon.y + icon.h / 4),
			int(icon.w / 2), int(icon.h / 2), 0, 0, 0);
	}
	const ViewRect caption{rect.x + (compact ? 40 : 60) * unit, rect.y,
		std::max(0.0, rect.w - (compact ? 88 : 108) * unit), rect.h};
	double textScale = compact ? .8 : .85;
	// Long translated names and multiple material stocks must fit together.
	const double lineHeight = globalContainer->standardFont->getStringHeight("Ag") * gfx->textUnitsPerPoint();
	while (textScale > .45 && pointLines(text, caption.w, textScale).size() * lineHeight * textScale > rect.h - 4 * unit)
		textScale -= .05;
	drawPointLabel(caption, text, textScale);
	drawPointLabel({rect.x + rect.w - 48 * unit, rect.y, 48 * unit, rect.h}, "×", 1.2);
}

void GameGUITouch::drawAllocation()
{
	auto *building = inspectedBuilding();
	const auto rect = allocationRect();
	if (!building || rect.h <= 0)
		return;
	const auto *type = building->type;
	auto *sprite = type->miniSpriteImage >= 0 ? type->miniSpritePtr : type->gameSpritePtr;
	const int frame = type->miniSpriteImage >= 0 ? type->miniSpriteImage : type->gameSpriteImage;
	sprite->setBaseColor(presentationColor(building->owner().color));
	const std::string name = buildingDisplayName(*type);
	drawSelectionHeader(rect, sprite, frame,
		(building->showLevel ? std::string(GAGCore::FormattableString(Toolkit::getStringTable()->getString("[%0 · %1]"))
				.arg(name).arg(type->level + 1)) : name) +
			"\n" +
			GAGCore::FormattableString(
				GAGCore::Toolkit::getStringTable()->getString("[%0 / %1 HP · %2]"))
				.arg(building->state().hp)
				.arg(type->hpMax)
				.arg(building->owner().number == gui.drawnScene().panels.local.state().number
						 ? std::string(
							   GAGCore::Toolkit::getStringTable()->getString("[Your colony]"))
						 : GAGCore::FormattableString(
							   GAGCore::Toolkit::getStringTable()->getString("[Team %0]"))
							   .arg(building->owner().number + 1)));
}

GameGUITouch::HudLayout GameGUITouch::hudLayout(const MobileLayout &ui) const
{
	const auto safe = ui.safe;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	HudLayout hud;
	const bool wide = safe.w > safe.h || safe.w / unit >= 680;
	const bool identity = (inspecting() || inspectingReadOnly()) && !ui.persistentPanel;
	const bool stackedWide = wide && ((safe.w / unit - 92) * .25 - 4) / 2 < 82;
	const double side = (wide ? (safe.h / unit < 360 && !identity ? 72 : stackedWide ? 104 : 84) : 104) * unit, gap = 4 * unit;
	const double x = safe.x, y = safe.y + gap;
	hud.minimap = {safe.x + safe.w - side - gap, wide ? y : y + 44 * unit, side, side};
	const double available = std::max(0.0, hud.minimap.x - gap - x);
	if (wide)
	{
		// Prestige and conversion imagery get more room than simple counters.
		const double weights[] = {.14, .14, .14, .21, .25, .12};
		double left = x;
		for (int i = 0; i < 6; ++i)
		{
			const double width = available * weights[i];
			hud.cells[i] = {left, y, width - gap, (stackedWide ? 60 : 40) * unit};
			left += width;
		}
		hud.stats = {x, y, available - gap, (stackedWide ? 60 : 40) * unit};
	}
	else
	{
		// Keep speed above the minimap, so the second row can give conversions
		// a full visual composition instead of two tiny symbols in one cell.
		for (int i = 0; i < 3; ++i)
			hud.cells[i] = {x + i * available / 3, y, available / 3 - gap, 40 * unit};
		hud.cells[5] = {hud.minimap.x, y, side, 40 * unit};
		const double prestige = available * .42;
		hud.cells[3] = {x, y + 44 * unit, prestige - gap, 60 * unit};
		hud.cells[4] = {x + prestige, y + 44 * unit, available - prestige - gap, 60 * unit};
		hud.stats = {x, y, available - gap, 104 * unit};
	}
	const double header = InGameTouchTheme::inspectorHeader * unit;
	hud.identity = {hud.stats.x, hud.minimap.y + hud.minimap.h - header, hud.stats.w, header};
	return hud;
}

ViewRect GameGUITouch::statRect(const HudLayout &hud, int index) const
{
	return hud.cells[index];
}

ViewRect GameGUITouch::speedRect() const
{
	if (!gui.canChangeGameSpeed())
		return {};
	const auto hud = hudLayout(layout());
	auto rect = statRect(hud, 5);
	// A thumb-sized target reaches below the cell unless the identity bar sits there.
	if (!((inspecting() || inspectingReadOnly()) && !layout().persistentPanel))
		rect.h = std::max(rect.h, InGameTouchTheme::target * globalContainer->gfx->logicalUnitsPerPoint());
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
	gfx->drawRect(int(rect.x), int(rect.y), int(rect.w), int(rect.h), InGameTouchTheme::border());
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
	const auto id = static_cast<ResourceId>(r.type);
	const auto& catalog = gui.drawnScene().map.resourceRegistry();
	ResourceInfo info;
	info.resource = r.type;
	info.name = getResourceDisplayName(catalog.presentation(id).name);
	info.sprite = catalog.presentation(id).frame(r.amount, 0, 0);
	for (unsigned m=0; m<MaterialCount; ++m)
	{
		const auto& yield = catalog.yields(id)[m];
		if (!yield.capacity) continue;
		if (!info.amount.empty()) info.amount += "\n";
		const auto amount = gui.drawnScene().map.materialAmountAt(size_t(gui.selectionResource()),m);
		info.amount += getMaterialName(m) + ": " + (yield.consumption == ResourceConsumption::Infinite && amount > 0 ? std::string("∞") :
			std::to_string(amount)+"/"+std::to_string(yield.capacity));
	}
	return info;
}

bool GameGUITouch::inspectingReadOnly() const
{
	return inspectingResource() || gui.selectionMode == GameGUI::UNIT_SELECTION;
}

ViewRect GameGUITouch::readOnlyCloseRect() const
{
	const auto panel = inspectingReadOnly() ? allocationRect() : layout().panel;
	const double target = 48 * globalContainer->gfx->logicalUnitsPerPoint();
	return {panel.x + panel.w - target, panel.y, target, inspectingReadOnly() ? panel.h : target};
}

void GameGUITouch::drawResourceInfo()
{
	const auto info = resourceInfo();
	const auto rect = allocationRect();
	if (!info || rect.h <= 0) return;
	auto *sprite = ResourceSprites::resolve(gui.drawnScene().map.frozenResourceRegistry()).sprites[info->resource];
	drawSelectionHeader(rect, sprite, info->sprite,
		info->name + (info->amount.empty() ? "" : "\n" + info->amount));
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
							InGameTouchTheme::field());
		drawPointLabel(rect, rows[i].first, .9);
	}
	labelClip.reset();
	gfx->setClipRect();
}

std::vector<std::pair<std::string, std::string>> GameGUITouch::unitInfoRows() const
{
	const auto &u = gui.drawnScene().panels.unit;
	if (!u.valid) return {};
	auto *strings = Toolkit::getStringTable();
	std::vector<std::pair<std::string, std::string>> rows;
	auto value = [&](const char *key, std::string text) {
		rows.emplace_back(strings->getString(key), std::move(text));
	};
	value("[hp]", std::to_string(u.state().hp) + " / " + std::to_string(u.state().performance[HP]));
	value("[food]", std::to_string(u.state().hungry * 100 / Unit::HUNGRY_MAX) + "% (" + std::to_string(u.state().fruitCount) + ")");
	value("[current speed]", std::to_string(u.state().speed));
	if (u.state().performance[ARMOR]) value("[armor]", std::to_string(u.realArmor));
	if (u.state().performance[HARVEST]) {
		if (u.state().carriedMaterial < 0) value("[carry]", strings->getString("[don't carry anything]"));
		else value("[carry]", getMaterialName(u.state().carriedMaterial));
	}
	const std::pair<int, const char *> abilities[] = {{WALK,"[Walk]"}, {SWIM,"[Swim]"}, {BUILD,"[Build]"},
		{HARVEST,"[Harvest]"}, {ATTACK_SPEED,"[At. speed]"}, {ATTACK_STRENGTH,"[At. strength]"},
		{MAGIC_ATTACK_AIR,"[Magic At. Air]"}, {MAGIC_ATTACK_GROUND,"[Magic At. Ground]"}};
	for (const auto &[ability,key] : abilities)
		if (u.state().performance[ability]) {
			const bool attack = ability == ATTACK_STRENGTH || ability == MAGIC_ATTACK_AIR || ability == MAGIC_ATTACK_GROUND;
			const int strength = (u.state().performance[ability] + (attack ? u.state().experienceLevel : 0)) *
				(ability == ATTACK_STRENGTH ? u.glassCannonScale : 1);
			value(key, std::to_string(strength) + " · " + strings->getString("[level]") + " " +
				std::to_string(u.state().level[ability] + (ability == SWIM ? 0 : 1)));
		}
	if (u.state().performance[ATTACK_STRENGTH] || u.state().performance[MAGIC_ATTACK_AIR] || u.state().performance[MAGIC_ATTACK_GROUND])
		rows.emplace_back("XP", std::to_string(u.state().experience) + " / " + std::to_string(u.nextLevelThreshold));
	return rows;
}

void GameGUITouch::drawUnitPanel()
{
	auto *gfx = globalContainer->gfx;
	const auto ui = layout();
	const auto panel = ui.panel;
	const double unit = gfx->logicalUnitsPerPoint();
	const double rowHeight = InGameTouchTheme::unitStatRow * unit * InGameTouchTheme::textGrowth();
	const auto rows = unitInfoRows();
	const auto &selected = gui.drawnScene().panels.unit;
	if (selected.valid)
	{
		auto *sprite = globalContainer->unitmini;
		sprite->setBaseColor(presentationColor(selected.owner().color));
		drawSelectionHeader(allocationRect(), sprite, selected.state().typeNum,
			std::string(getUnitName(selected.state().typeNum)) + "\n" + displayPlayerName(selected.owner().firstPlayerName));
	}
	// A solid surface keeps the map from competing with the unit's numbers.
	auto background = InGameTouchTheme::paper();
	background.a = 255;
	const double top = panel.y + (ui.persistentPanel ? InGameTouchTheme::inspectorHeader * unit : 0);
	gfx->drawFilledRect(float(panel.x), float(top), float(panel.w), float(panel.y + panel.h - top), background);
	drawPointLabel({panel.x + 8 * unit, top, panel.w - 16 * unit, InGameTouchTheme::unitStatsTitle * unit},
		Toolkit::getStringTable()->getString("[Statistics]"), .85, true);
	const auto content = panelContent();
	labelClip = content;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const ViewRect row{panel.x + 8 * unit, content.y + i * rowHeight - panelScroll * unit,
			panel.w - 16 * unit, rowHeight - 2 * unit};
		gfx->setClipRect(int(content.x), int(content.y), int(content.w), int(content.h));
		auto fill = i % 2 ? InGameTouchTheme::readout() : InGameTouchTheme::field();
		fill.a = 255;
		gfx->drawFilledRect(float(row.x), float(row.y), float(row.w), float(row.h), fill);
		drawPointLabel({row.x, row.y, row.w * .45, row.h}, rows[i].first, .8, true);
		drawPointLabel({row.x + row.w * .45, row.y, row.w * .55, row.h}, rows[i].second, .9);
	}
	labelClip.reset();
	gfx->setClipRect();
	gfx->drawRect(int(panel.x), int(top), int(panel.w), int(panel.y + panel.h - top), InGameTouchTheme::border());
}
