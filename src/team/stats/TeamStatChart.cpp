// SPDX-License-Identifier: GPL-3.0-or-later
// Team statistics charts, shared by the end-of-game results and the in-match
// statistics sheet.
#include "TeamStatChart.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "InGameTouchTheme.h"
#include "stats/MetricSeries.h"
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <functional>
using namespace GAGCore;

const Color TeamStatChart::background(34, 24, 49);
const Color TeamStatChart::ink(249, 232, 187);
const Color TeamStatChart::border(199, 165, 87);

namespace
{
const Color grid(68, 51, 82);
const Color muted(176, 160, 150);
//! Margin kept inside the chart's rectangle.
const int pad = 8;
//! Side of the colour square in front of a legend entry or panel title.
const int legendSwatch = 10;
// Bands of unrelated kinds: a fixed order of hues told apart with the common
// colour-vision deficiencies on the plot background. Catalog metrics have at
// most eight bands, so none is reused.
const Color categorical[] = {{57, 135, 229}, {217, 89, 38}, {25, 158, 112}, {201, 133, 0},
							 {213, 81, 129}, {0, 131, 0},	{144, 133, 233}, {230, 103, 103}};
// Bands that run from worst to best (starving ... fed).
const Color ordered[] = {{208, 59, 59}, {236, 131, 90}, {250, 178, 25}, {12, 163, 12}};

std::string tr(const std::string &key)
{
	return Toolkit::getStringTable()->getString(key.c_str());
}

Color blend(const Color &color, const Color &towards, int percent)
{
	auto mix = [percent](int a, int b) { return Uint8((a * (100 - percent) + b * percent) / 100); };
	return Color(mix(color.r, towards.r), mix(color.g, towards.g), mix(color.b, towards.b));
}

//! Draws and measures text in the chart's font, each string in its own colour.
struct Text
{
	DrawableSurface &surface;
	Font *font;
	int height;
	Text(DrawableSurface &surface, Font *font) : surface(surface), font(font), height(font->getStringHeight("0")) {}
	int width(const std::string &text) const { return font->getStringWidth(text); }
	void draw(int x, int y, const std::string &text, const Color &color) const
	{
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, color));
		surface.drawString(x, y, font, text);
		font->popStyle();
	}
	//! `text` cut to fit `available` pixels.
	std::string fit(std::string text, int available) const
	{
		while (!text.empty() && width(text) > available)
		{
			std::size_t last = text.size() - 1;
			while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80)
				--last;
			text.resize(last);
		}
		return text;
	}
};

//! A value as an axis or readout shows it, with its percent sign.
std::string valueLabel(double value, bool percent, bool decimals)
{
	return Stats::valueText(value, decimals) + (percent ? "%" : "");
}

//! Value of a series at `tick`, interpolated between its samples; false outside them.
bool valueAt(const std::vector<Uint32> &ticks, const std::vector<double> &values, double tick, double &value)
{
	if (ticks.empty() || tick < ticks.front() || tick > ticks.back())
		return false;
	const auto upper = std::lower_bound(ticks.begin(), ticks.end(), Uint32(std::ceil(tick)));
	const std::size_t hi = std::min(std::size_t(upper - ticks.begin()), ticks.size() - 1);
	const std::size_t lo = hi > 0 && ticks[hi] > tick ? hi - 1 : hi;
	const double span = double(ticks[hi]) - double(ticks[lo]);
	const double mu = span > 0 ? (tick - ticks[lo]) / span : 0;
	value = values[lo] * (1 - mu) + values[hi] * mu;
	return true;
}

//! Index of the sample nearest `tick`.
std::size_t nearest(const std::vector<Uint32> &ticks, double tick)
{
	std::size_t best = 0;
	for (std::size_t i = 1; i < ticks.size(); ++i)
		if (std::fabs(ticks[i] - tick) < std::fabs(ticks[best] - tick))
			best = i;
	return best;
}

//! The plot rectangle and how ticks and values map onto its pixels.
struct Plot
{
	int x, y, w, h;
	Uint32 endTick;
	Stats::Axis axis;
	//! Too small to draw anything legible in.
	bool tooSmall() const { return w < 20 || h < 20; }
	int px(double tick) const { return x + int(tick * (w - 1) / std::max<Uint32>(1, endTick)); }
	int py(double value) const
	{
		return y + h - 1 - int(std::lround((value - axis.minimum) * (h - 1) / (axis.maximum - axis.minimum)));
	}
	double tickAt(int pixel) const { return double(pixel - x) * endTick / std::max(1, w - 1); }
	bool contains(int hx, int hy) const { return hx >= x && hx < x + w && hy >= y && hy < y + h; }
};

void paintTimeAxis(const Text &text, const Plot &plot)
{
	const int seconds = Stats::secondsAt(plot.endTick);
	const int step = Stats::timeStep(seconds, std::max(1, plot.w / (text.width("00:00") + 24)));
	for (int at = 0; at <= seconds; at += step)
	{
		const int x = plot.px(double(at) * GAME_TICKS_PER_SECOND);
		text.surface.drawVertLine(x, plot.y + plot.h, 4, muted);
		const std::string label = Stats::timeText(at);
		const int width = text.width(label);
		text.draw(std::clamp(x - width / 2, plot.x, plot.x + plot.w - width), plot.y + plot.h + 6, label, muted);
	}
}

//! A gridline's value: every label of an axis on the same scale.
std::string tickLabel(double value, const Stats::Axis &axis, bool percent, bool decimals)
{
	return Stats::tickText(value, std::max(std::fabs(axis.minimum), std::fabs(axis.maximum)), decimals) + (percent ? "%" : "");
}

//! Gridlines and their values to the right of the plot, in the width the caller
//! reserved with valueLabelWidth().
void paintValueAxis(const Text &text, const Plot &plot, bool percent, bool decimals)
{
	const int steps = int(std::lround((plot.axis.maximum - plot.axis.minimum) / plot.axis.step));
	// Every step has its gridline; a short plot labels only those with room.
	int labelled = plot.y + plot.h + text.height;
	for (int i = 0; i <= steps; ++i)
	{
		const double value = plot.axis.minimum + plot.axis.step * i;
		const int y = plot.py(value);
		const bool zero = std::fabs(value) < plot.axis.step / 1000;
		text.surface.drawHorzLine(plot.x, y, plot.w, zero && plot.axis.minimum < 0 ? muted : grid);
		const std::string label = tickLabel(value, plot.axis, percent, decimals);
		const int at = std::clamp(y - text.height / 2, plot.y, plot.y + plot.h - text.height);
		if (at + text.height > labelled)
			continue;
		text.draw(plot.x + plot.w + 6, at, label, muted);
		labelled = at;
	}
}

//! Width to reserve right of a plot for the labels paintValueAxis() draws.
int valueLabelWidth(const Text &text, const Stats::Axis &axis, bool percent, bool decimals)
{
	int width = 0;
	for (double value = axis.minimum; value <= axis.maximum + axis.step / 2; value += axis.step)
		width = std::max(width, text.width(tickLabel(value, axis, percent, decimals)));
	return width + 10;
}

struct ReadoutRow
{
	Color color;
	std::string name;
	double value;
};

//! The box that names the values under the pointer: a heading, one row per
//! value, then notes. It sits at the top of the chart rectangle, beside
//! `anchorX` on the side with more room, and drops rows that do not fit.
void paintReadout(const Text &text, int left, int top, int width, int height, int anchorX, const std::string &heading,
				  const std::vector<ReadoutRow> &rows, bool percent, bool decimals, const std::vector<std::string> &notes)
{
	const int pad = 6, swatch = 8, line = text.height + 3;
	std::vector<std::string> values;
	int nameWidth = 0, valueWidth = 0;
	for (const auto &row : rows)
	{
		values.push_back(valueLabel(row.value, percent, decimals));
		valueWidth = std::max(valueWidth, text.width(values.back()));
		nameWidth = std::max(nameWidth, text.width(row.name));
	}
	nameWidth = std::min(nameWidth, std::max(40, width / 3));
	int boxW = std::max(text.width(heading), swatch + 5 + nameWidth + 10 + valueWidth);
	for (const auto &note : notes)
		boxW = std::max(boxW, text.width(note));
	boxW = std::min(width - 4, boxW + 2 * pad);
	const std::size_t shown = std::min(rows.size(), std::size_t(std::max(1, (height - 2 * pad - line * int(1 + notes.size())) / line)));
	const int boxH = 2 * pad + line * int(1 + shown + notes.size());
	// On the side of the pointer with more room, so that near the right-hand end
	// it does not cover the latest values and the names of the lines.
	const bool leftSide = anchorX > left + width / 2;
	int x = leftSide ? anchorX - 12 - boxW : anchorX + 12;
	x = std::clamp(x, left + 2, std::max(left + 2, left + width - 2 - boxW));
	const int y = top + 2;
	text.surface.drawFilledRect(x, y, boxW, boxH, Color(20, 13, 31, 235));
	text.surface.drawRect(x, y, boxW, boxH, TeamStatChart::border);
	int at = y + pad;
	text.draw(x + pad, at, heading, TeamStatChart::ink);
	at += line;
	for (std::size_t i = 0; i < shown; ++i, at += line)
	{
		text.surface.drawFilledRect(x + pad, at + (text.height - swatch) / 2, swatch, swatch, rows[i].color);
		text.draw(x + pad + swatch + 5, at, text.fit(rows[i].name, boxW - 2 * pad - swatch - 15 - valueWidth), TeamStatChart::ink);
		text.draw(x + boxW - pad - text.width(values[i]), at, values[i], TeamStatChart::ink);
	}
	for (const auto &note : notes)
	{
		text.draw(x + pad, at, text.fit(note, boxW - 2 * pad), muted);
		at += line;
	}
}

std::string markerText(const Stats::Marker &marker, const std::vector<TeamStatChart::Team> &teams)
{
	std::string name;
	for (const auto &team : teams)
		if (team.team == marker.team)
			name = team.name;
	return Stats::timeText(Stats::secondsAt(marker.tick)) + "  " + name + ": " + tr(marker.key);
}

//! One paint of a chart: what the parts drawn below the axis title share.
struct ChartPainter
{
	const Text &text;
	const TeamStatChart::Options &options;
	const Stats::Metric &metric;
	const Stats::Chart &chart;
	const std::vector<Stats::Marker> &markers;
	//! The chart's rectangle on the surface.
	int left, top, width, height;
	//! Right-hand end of the time axis: the match so far.
	Uint32 endTick;

	//! Room under a plot for its time labels.
	int bottomLabels() const { return text.height + 10; }
	//! The pointer on the surface, or -1 when it is not over the chart.
	int hoverX() const { return options.hoverX >= 0 ? left + options.hoverX : -1; }
	int hoverY() const { return options.hoverY >= 0 ? top + options.hoverY : -1; }
	bool namesTeams() const { return !chart.global && options.teams.size() > 1; }

	const TeamStatChart::Team &teamOf(int number) const
	{
		for (const auto &team : options.teams)
			if (team.team == number)
				return team;
		return options.teams.front();
	}

	//! The band legend of a split chart, wrapped from `y`; returns where it ends.
	int paintBandLegend(int y) const
	{
		int x = left + pad;
		for (std::size_t b = 0; b < chart.bandKeys.size(); ++b)
		{
			// A band no team ever has anything in is left out of the legend.
			bool used = false;
			for (const auto &team : chart.teams)
				used |= std::any_of(team.values[b].begin(), team.values[b].end(), [](double value) { return value > 0; });
			if (!used)
				continue;
			const std::string label = tr(chart.bandKeys[b]);
			const int need = legendSwatch + 4 + text.width(label) + 12;
			if (x + need > left + width - pad && x > left + pad)
			{
				x = left + pad;
				y += text.height + 3;
			}
			text.surface.drawFilledRect(x, y + (text.height - legendSwatch) / 2, legendSwatch, legendSwatch, TeamStatChart::bandColor(chart, b));
			text.draw(x + legendSwatch + 4, y, label, TeamStatChart::ink);
			x += need;
		}
		return y + text.height + 6;
	}

	//! A split chart: one panel of stacked bands per team, from `y` down.
	void paintPanels(int y) const
	{
		const int count = int(chart.teams.size());
		const int areaH = top + height - y - pad;
		// The grid whose panels are largest, a panel counting as large when it is
		// both tall and (a chart over time) about twice as wide.
		int columns = 1;
		double best = 0;
		for (int candidate = 1; candidate <= count; ++candidate)
		{
			const double w = double(width - 2 * pad) / candidate, h = double(areaH) / ((count + candidate - 1) / candidate);
			const double size = std::min(w / 2, h);
			if (size > best)
			{
				best = size;
				columns = candidate;
			}
		}
		const int rows = (count + columns - 1) / columns;
		const int cellW = (width - 2 * pad) / columns, cellH = areaH / rows;
		// Every panel has the same axis, so the teams can be compared.
		const Stats::Axis axis = chart.percent ? Stats::Axis{0, 100, 25} : Stats::niceAxis(0, chart.high, std::max(2, cellH / 40), !chart.decimals);
		const int labelW = valueLabelWidth(text, axis, chart.percent, chart.decimals);
		std::function<void()> hovered;
		for (int i = 0; i < count; ++i)
		{
			const auto &series = chart.teams[std::size_t(i)];
			const TeamStatChart::Team &team = teamOf(series.team);
			const int cx = left + pad + (i % columns) * cellW, cy = y + (i / columns) * cellH;
			int titleH = 0;
			if (namesTeams())
			{
				text.surface.drawFilledRect(cx, cy + (text.height - legendSwatch) / 2, legendSwatch, legendSwatch, team.color);
				text.draw(cx + legendSwatch + 4, cy, text.fit(team.name, cellW - labelW - 20), TeamStatChart::ink);
				titleH = text.height + 3;
			}
			const Plot plot{cx, cy + titleH, cellW - labelW - 6, cellH - titleH - bottomLabels() - 4, endTick, axis};
			if (plot.tooSmall())
				continue;
			// Gridlines first: over the bands they would cut the areas into strips.
			paintValueAxis(text, plot, chart.percent, chart.decimals);
			paintBands(plot, series);
			text.surface.drawRect(plot.x, plot.y, plot.w, plot.h, grid);
			paintTimeAxis(text, plot);
			if (plot.contains(hoverX(), hoverY()))
				hovered = [this, plot, &series, &team] { paintPanelReadout(plot, series, team); };
		}
		// Last, so that no later panel's title is drawn over the readout.
		if (hovered)
			hovered();
	}

	//! The stacked areas of one team, filled a pixel column at a time, bottom
	//! band first.
	void paintBands(const Plot &plot, const Stats::TeamSeries &series) const
	{
		for (int px = plot.x; px < plot.x + plot.w; ++px)
		{
			const double tick = plot.tickAt(px);
			double base = 0;
			for (std::size_t b = 0; b < series.values.size(); ++b)
			{
				double value = 0;
				if (!valueAt(series.ticks, series.values[b], tick, value) || value <= 0)
					continue;
				const int from = plot.py(std::min(base, plot.axis.maximum)), to = plot.py(std::min(base + value, plot.axis.maximum));
				if (from > to)
					text.surface.drawVertLine(px, to, from - to, TeamStatChart::bandColor(chart, b));
				base += value;
			}
		}
	}

	//! The bands of one team at the sample nearest the pointer, top band first as
	//! they are stacked.
	void paintPanelReadout(const Plot &plot, const Stats::TeamSeries &series, const TeamStatChart::Team &team) const
	{
		const std::size_t at = nearest(series.ticks, plot.tickAt(hoverX()));
		const int x = plot.px(series.ticks[at]);
		text.surface.drawVertLine(x, plot.y, plot.h, TeamStatChart::ink);
		std::vector<ReadoutRow> rows;
		for (std::size_t b = series.values.size(); b-- > 0;)
			rows.push_back({TeamStatChart::bandColor(chart, b), tr(chart.bandKeys[b]), series.values[b][at]});
		paintReadout(text, left, top, width, height, x, Stats::timeText(Stats::secondsAt(series.ticks[at])) + "  " + team.name, rows,
					 chart.percent, chart.decimals, {});
	}

	//! A chart that is not split: one line per team in a single plot from `y` down.
	void paintLines(int y) const
	{
		// A share or percentage that fills most of the range reads best against 0-100.
		const Stats::Axis axis = chart.percent && chart.high > 50 && chart.low >= 0
									 ? Stats::Axis{0, 100, 25}
									 : Stats::niceAxis(chart.low, chart.high, std::max(2, (top + height - y) / 44), !chart.decimals);
		const int labelW = valueLabelWidth(text, axis, chart.percent, chart.decimals);
		const Plot plot{left + pad, y, width - 2 * pad - labelW, top + height - y - bottomLabels() - pad, endTick, axis};
		if (plot.tooSmall())
			return;
		paintValueAxis(text, plot, chart.percent, chart.decimals);
		text.surface.drawRect(plot.x, plot.y, plot.w, plot.h, grid);
		paintTimeAxis(text, plot);

		// Moments worth noting: a dotted line up the plot and a tick on the time
		// axis, in the team's colour. Pointing at one says what it is.
		for (const auto &marker : markers)
		{
			const int x = plot.px(marker.tick);
			const Color faint = blend(teamOf(marker.team).color, TeamStatChart::background, 45);
			for (int dot = plot.y + 2; dot < plot.y + plot.h - 8; dot += 6)
				text.surface.drawVertLine(x, dot, 2, faint);
			text.surface.drawFilledRect(x - 2, plot.y + plot.h - 7, 5, 7, teamOf(marker.team).color);
			text.surface.drawRect(x - 2, plot.y + plot.h - 7, 5, 7, TeamStatChart::ink);
		}
		paintLineEndNames(plot, paintCurves(plot));
		if (plot.contains(hoverX(), hoverY()))
			paintLinesReadout(plot);
	}

	//! Where a team's line ends, for naming it there.
	struct LineEnd
	{
		int y;
		const TeamStatChart::Team *team;
	};

	//! The lines, the highlighted team last so that it lies on top.
	std::vector<LineEnd> paintCurves(const Plot &plot) const
	{
		std::vector<std::size_t> order;
		for (std::size_t i = 0; i < chart.teams.size(); ++i)
			if (chart.teams[i].team != options.highlighted)
				order.push_back(i);
		for (std::size_t i = 0; i < chart.teams.size(); ++i)
			if (chart.teams[i].team == options.highlighted)
				order.push_back(i);
		const bool anyHighlight = order.size() > 1 && chart.teams[order.back()].team == options.highlighted;
		// Teams with the same values would hide each other, so each line carries
		// markers, staggered from team to team.
		const int markerSpacing = 48;
		std::vector<LineEnd> ends;
		for (std::size_t n = 0; n < order.size(); ++n)
		{
			const auto &series = chart.teams[order[n]];
			const TeamStatChart::Team &team = teamOf(series.team);
			const bool strong = anyHighlight && series.team == options.highlighted;
			const Color color = chart.global ? TeamStatChart::ink : anyHighlight && !strong ? blend(team.color, TeamStatChart::background, 60) : team.color;
			const auto &values = series.values[0];
			const int phase = 8 + int(order[n]) * markerSpacing / std::max<int>(1, int(order.size()));
			int lastMarker = plot.x - markerSpacing + phase;
			for (std::size_t i = 0; i < series.ticks.size(); ++i)
			{
				// A gap: nothing to measure at this sample (no shots to count hits of).
				if (std::isnan(values[i]))
					continue;
				const int x = plot.px(series.ticks[i]), yv = plot.py(values[i]);
				if (i > 0 && !std::isnan(values[i - 1]))
				{
					// Two pixels thick; three for the highlighted team.
					const int x0 = plot.px(series.ticks[i - 1]), y0 = plot.py(values[i - 1]);
					text.surface.drawLine(x0, y0, x, yv, color);
					text.surface.drawLine(x0, y0 - 1, x, yv - 1, color);
					if (strong)
						text.surface.drawLine(x0, y0 + 1, x, yv + 1, color);
				}
				if (x - lastMarker >= markerSpacing && order.size() > 1 && !chart.global)
				{
					text.surface.drawFilledRect(x - 2, yv - 3, 5, 5, color);
					text.surface.drawRect(x - 3, yv - 4, 7, 7, TeamStatChart::background);
					lastMarker = x;
				}
			}
			if (!series.ticks.empty() && !std::isnan(values.back()))
				ends.push_back({plot.py(values.back()), &team});
		}
		return ends;
	}

	//! Names the lines at their right-hand end. Names of lines that end close
	//! together are moved apart rather than dropped; with more teams than the
	//! plot has room to name, the legend above the chart is what names them.
	void paintLineEndNames(const Plot &plot, std::vector<LineEnd> ends) const
	{
		const int line = text.height + 1;
		if (!namesTeams() || plot.w <= 240 || int(ends.size()) * line > plot.h / 2)
			return;
		std::sort(ends.begin(), ends.end(), [](const LineEnd &a, const LineEnd &b) { return a.y < b.y; });
		const int lowest = plot.y + plot.h - text.height - 8;
		std::vector<int> at;
		for (const auto &end : ends)
			at.push_back(std::max(std::clamp(end.y - text.height - 2, plot.y + 1, lowest), at.empty() ? plot.y + 1 : at.back() + line));
		// Names pushed past the bottom move the ones above them up.
		for (std::size_t i = at.size(); i-- > 0;)
			at[i] = std::min(at[i], i + 1 < at.size() ? at[i + 1] - line : lowest);
		for (std::size_t i = 0; i < ends.size(); ++i)
		{
			const std::string name = text.fit(ends[i].team->name, plot.w / 4);
			text.draw(plot.x + plot.w - text.width(name) - 4, at[i], name, TeamStatChart::ink);
		}
	}

	//! Every team's value at the sample nearest the pointer, highest first, with
	//! the markers the pointer is on.
	void paintLinesReadout(const Plot &plot) const
	{
		// Snap to a sample of the team with the longest history.
		std::size_t longest = 0;
		for (std::size_t i = 1; i < chart.teams.size(); ++i)
			if (chart.teams[i].ticks.size() > chart.teams[longest].ticks.size())
				longest = i;
		const auto &reference = chart.teams[longest].ticks;
		const Uint32 snapped = reference[nearest(reference, plot.tickAt(hoverX()))];
		const int x = plot.px(snapped);
		text.surface.drawVertLine(x, plot.y, plot.h, TeamStatChart::ink);
		std::vector<ReadoutRow> rows;
		for (const auto &series : chart.teams)
		{
			if (series.ticks.empty() || snapped < series.ticks.front() || snapped > series.ticks.back())
				continue;
			const std::size_t at = nearest(series.ticks, snapped);
			if (std::isnan(series.values[0][at]))
				continue;
			const TeamStatChart::Team &team = teamOf(series.team);
			const Color color = chart.global ? TeamStatChart::ink : team.color;
			text.surface.drawFilledRect(x - 3, plot.py(series.values[0][at]) - 3, 7, 7, color);
			text.surface.drawRect(x - 3, plot.py(series.values[0][at]) - 3, 7, 7, TeamStatChart::ink);
			rows.push_back({color, chart.global ? TeamStatChart::title(metric) : team.name, series.values[0][at]});
		}
		std::stable_sort(rows.begin(), rows.end(), [](const ReadoutRow &a, const ReadoutRow &b) { return a.value > b.value; });
		std::vector<std::string> notes;
		for (const auto &marker : markers)
			if (std::abs(plot.px(marker.tick) - hoverX()) <= 6)
				notes.push_back(markerText(marker, options.teams));
		paintReadout(text, left, top, width, height, x, Stats::timeText(Stats::secondsAt(snapped)), rows, chart.percent, chart.decimals, notes);
	}
};
} // namespace

std::string TeamStatChart::title(const Stats::Metric &metric)
{
	return tr(metric.titleKey());
}

std::string TeamStatChart::about(const Stats::Metric &metric)
{
	return tr(metric.aboutKey());
}

std::string TeamStatChart::axisTitle(const Stats::Metric &metric, const Stats::View &requested)
{
	const Stats::View view = Stats::validView(metric, requested);
	const std::string unit = tr(metric.unitKey);
	const bool rate = metric.kind == Stats::Metric::Counter && !view.total;
	// A split metric's ratio is its headline in text panels, not what is charted.
	const bool ratio = metric.ratioOf && !view.split;
	std::string text;
	if (ratio && metric.mean)
		text = "%0";
	else if (ratio)
		text = tr("[stat axis percent of %0]");
	else if (view.share)
		text = tr("[stat axis share of all teams]");
	else if (view.relative && view.split)
		text = tr("[stat axis percent of %0]");
	else if (view.relative && metric.kind == Stats::Metric::Gauge)
		text = tr("[stat axis percent of units]");
	else if (view.relative)
		text = tr(rate ? "[stat axis %0 per minute per 100 units]" : "[stat axis %0 per 100 units]");
	else if (rate && metric.scale != 1)
		// Worker time: its scaled rate is a number of workers, not workers a minute.
		text = "%0";
	else if (rate)
		text = tr("[stat axis %0 per minute]");
	else if (metric.kind == Stats::Metric::Counter)
		text = tr("[stat axis %0 so far]");
	else
		text = "%0";
	// Not every wording names the unit, and arg() appends what has no place.
	if (text.find("%0") != std::string::npos)
		text = FormattableString(text).arg(unit);
	if (rate || metric.smoothed)
		text += ", " + std::string(FormattableString(tr("[stat axis %0 minute average]")).arg(Stats::windowMinutes(view.window)));
	return text;
}

std::string TeamStatChart::readingText(const Stats::Reading &reading)
{
	if (!reading.available)
		return tr("[Stats unavailable]");
	return valueLabel(reading.value, reading.percent, reading.decimals);
}

Color TeamStatChart::bandColor(const Stats::Chart &chart, std::size_t index)
{
	if (chart.ordered)
		return ordered[std::min(index, std::size(ordered) - 1)];
	// "Other" is the same neutral grey wherever it appears, whatever its position.
	if (index < chart.bandKeys.size() && chart.bandKeys[index] == "[stat band other]")
		return Color(150, 148, 160);
	return categorical[index % std::size(categorical)];
}

void TeamStatChart::paintReadings(DrawableSurface &surface, int left, int top, int width, const TeamStats &stats, Stats::Group group)
{
	Text text(surface, globalContainer->littleFont);
	const auto history = Stats::historyOf(0, stats);
	const std::string perMinute = tr("[stat per minute short]");
	int y = top;
	for (const auto &metric : Stats::catalog())
	{
		if (metric.group != group)
			continue;
		const Stats::View view = Stats::defaultView(metric);
		const auto reading = Stats::latestReading(metric, view, history);
		std::string value = readingText(reading);
		if (reading.available && metric.kind == Stats::Metric::Counter && !metric.ratioOf && metric.scale == 1)
			value += perMinute;
		const int valueWidth = text.width(value);
		surface.drawString(left + 4, y, text.font, text.fit(title(metric), width - 12 - valueWidth));
		surface.drawString(left + width - 4 - valueWidth, y, text.font, value);
		y += text.height + 1;
	}
}

void TeamStatChart::paint(const Game &game, DrawableSurface &surface, int left, int top, int width, int height,
						  const Options &options)
{
	if (!options.metric || width < 40 || height < 40)
		return;
	const Stats::Metric &metric = *options.metric;
	const Stats::View view = Stats::validView(metric, options.view);
	const Text text(surface, globalContainer->littleFont);

	std::vector<Stats::TeamHistory> histories;
	std::vector<Stats::Marker> markers;
	for (const auto &team : options.teams)
	{
		histories.push_back(Stats::historyOf(team.team, game.teams[team.team]->stats));
		const auto found = Stats::markers(histories.back());
		markers.insert(markers.end(), found.begin(), found.end());
	}
	const Stats::Chart chart = Stats::buildChart(metric, view, histories);
	std::size_t samples = 0;
	for (const auto &team : chart.teams)
		samples = std::max(samples, team.ticks.size());
	if (samples < 2)
	{
		// Either the match was too short, or this save predates the measurement.
		const bool recorded = std::any_of(histories.begin(), histories.end(), [](const auto &h) { return h.points.size() > 1; });
		text.draw(left + pad, top + pad, tr(recorded ? "[stat not recorded for this match]" : "[Not enough recorded history yet.]"), TeamStatChart::ink);
		return;
	}

	if (!chart.any)
	{
		// Recorded, and nothing to draw: no trade, no prestige, nobody stranded.
		text.draw(left + pad, top + pad, tr("[stat nothing to show]"), TeamStatChart::ink);
		return;
	}
	// Markers belong to teams; a map-wide series has none.
	if (chart.global)
		markers.clear();

	ChartPainter painter{text, options, metric, chart, markers, left, top, width, height, std::max<Uint32>(1, game.stepCounter)};
	int y = top + pad;
	text.draw(left + pad, y, text.fit(axisTitle(metric, view), width - 2 * pad), muted);
	y += text.height + 4;
	if (chart.stacked)
		painter.paintPanels(painter.paintBandLegend(y));
	else
		painter.paintLines(y);
}
