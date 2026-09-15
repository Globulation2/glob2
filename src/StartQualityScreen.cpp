// SPDX-License-Identifier: GPL-3.0-or-later
#include "StartQualityScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cstdio>

namespace
{
std::string tr(const std::string &s)
{
	return Toolkit::getStringTable()->getString("[" + s + "]");
}
std::string fixed(double v, int decimals = 2)
{
	char text[32];
	std::snprintf(text, sizeof text, "%.*f", decimals, v);
	return text;
}
} // namespace

StartQualityScreen::StartQualityScreen(const MapGeneration::StartQualityReport &report,
									   std::vector<std::string> colonyLabels,
									   std::vector<Color> colonyColors)
	: report(report), labels(std::move(colonyLabels)), colors(std::move(colonyColors))
{
	gfx = globalContainer->gfx;
	controls = new LobbyControls();
	controls->render = [this] { render(); };
	addWidget(controls);
}

StartQualityScreen::~StartQualityScreen() = default;

void StartQualityScreen::onAction(Widget *, Action, int, int) {}

void StartQualityScreen::onSDLEvent(SDL_Event *event)
{
	if (event->type == SDL_KEYDOWN && !controls->popup.open &&
		(event->key.keysym.sym == SDLK_ESCAPE || event->key.keysym.sym == SDLK_RETURN))
	{
		endExecute(BACK);
		return;
	}
	controls->handle(event);
}

// The table: what each colony was measured to have (walking steps to wheat and wood, the mean
// growth chance of its ground, the deposits within its catchment, its building sites, steps to the
// nearest rival) and how every factor scored between 0 and 1, then its weighted total; under it the
// map's worst and best totals, the fairness (worst over best) and the score (worst times fairness)
// the lobby keeps the best candidate roll by. The factor weights come from StartQualityWeights.
void StartQualityScreen::render()
{
	auto &ui = *controls;
	const int width = gfx->getW(), height = gfx->getH();
	const int w = std::min(width - 32, 1000), x = (width - w) / 2;
	const bool compact = width < 800;
	ui.setScreenPosition(0, 0);
	ui.setDimensions(width, height);
	ui.box({x - 8, 8, w + 16, height - 16}, Color(232, 237, 218), 8);
	ui.text(x + 8, 20, tr("Start quality"), compact ? "standard" : "menu", w - 16);
	const int subtitleY = compact ? 46 : 56;
	int yy = subtitleY + 8 +
			 ui.paragraph(x + 8, subtitleY, w - 16,
						  tr("Each colony's start is measured on the finished map: the walk to "
							 "wheat and to wood, how well its ground regrows, the deposits and "
							 "building sites within reach, and its distance from rivals. Every "
							 "factor scores from 0 to 1 against a fixed bar, the total weighs "
							 "them, fairness is the worst colony's total over the best, and the "
							 "score is the worst total times the fairness."));
	// Columns: the label, then a measured value with its score under it for each factor, then the
	// total. Widths share the row; the label column gets a little more.
	const char *heads[] = {"Colony", "Wheat", "Wood", "Fertility", "Deposits", "Room", "Isolation",
						   "Total"};
	const int columns = 8;
	const int labelW = compact ? 90 : 130;
	const int cellW = (w - 16 - labelW) / (columns - 1);
	const MapGeneration::StartQualityWeights weights;
	ui.beginRegion(40, {x, yy, w, height - yy - 70});
	int rowY = yy - ui.regions[40].offset;
	const int startY = rowY;
	for (int c = 0; c < columns; ++c)
		ui.text(x + 8 + (c == 0 ? 0 : labelW + (c - 1) * cellW), rowY, tr(heads[c]), "standard",
				c == 0 ? labelW - 8 : cellW - 6);
	rowY += 24;
	// The weights, so the reader sees what a factor counts for in the total.
	const double factorWeights[] = {weights.wheat,	weights.wood, weights.fertility,
									weights.depth,	weights.room, weights.isolation};
	ui.text(x + 8, rowY, tr("Weight"), "little", labelW - 8, true);
	for (int c = 0; c < 6; ++c)
		ui.text(x + 8 + labelW + c * cellW, rowY, fixed(factorWeights[c]), "little", cellW - 6,
				true);
	rowY += 22;
	const int rowH = 68;
	for (size_t i = 0; i < report.colonies.size(); ++i)
	{
		const auto &q = report.colonies[i];
		ui.box({x, rowY - 4, w - 12, rowH - 4}, ui.panel);
		if (i < colors.size())
			ui.box({x + 6, rowY + 2, 14, 14}, colors[i], 3);
		ui.text(x + 26, rowY, i < labels.size() ? labels[i] : std::to_string(i + 1), "little",
				labelW - 30);
		// Measured on the top line, the factor's score under it.
		const std::string measured[] = {
			q.wheatDistance < 0 ? "-" : std::to_string(q.wheatDistance),
			q.woodDistance < 0 ? "-" : std::to_string(q.woodDistance),
			fixed(q.meanFertility, 0),
			std::to_string(q.resourceAmount),
			std::to_string(q.buildSites),
			q.rivalDistance < 0 ? "-" : std::to_string(q.rivalDistance)};
		const double scores[] = {q.wheat, q.wood, q.fertility, q.depth, q.room, q.isolation};
		for (int c = 0; c < 6; ++c)
		{
			const int cx = x + 8 + labelW + c * cellW;
			ui.text(cx, rowY, measured[c], "little", cellW - 6);
			ui.text(cx, rowY + 18, fixed(scores[c]), "little", cellW - 6, true);
		}
		ui.text(x + 8 + labelW + 6 * cellW, rowY + 6, fixed(q.total), "standard", cellW - 6);
		const std::string stock = tr("Wheat") + " " +
			std::to_string(q.resources[WHEAT].catchmentAmount) + "  /  " + tr("Wood") +
			" " + std::to_string(q.resources[WOOD].catchmentAmount);
		ui.text(x + 26, rowY + 40, stock, "little", w - 42);
		rowY += rowH;
	}
	rowY += 8;
	const std::string totals = tr("Worst") + " " + fixed(report.worst) + "  /  " + tr("Best") +
							   " " + fixed(report.best) + "  /  " + tr("Fairness") + " " +
							   fixed(report.fairness) + "  /  " + tr("Score") + " " +
							   fixed(report.score);
	ui.text(x + 8, rowY, totals, "standard", w - 16);
	rowY += 30;
	ui.endRegion(rowY - startY);
	ui.button(
		"quality/back", {x, height - 55, 100, 34}, tr("Back"), [this] { endExecute(BACK); }, false,
		true, true);
}
