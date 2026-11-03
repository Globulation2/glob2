// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <PerformanceTelemetry.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Presentation-only session statistics, shared by live and end-game screens.
class EnginePerformancePanel
{
  public:
	Glob2UI::Element compact(const Glob2UI::Presentation &, std::function<void()> open);
	Glob2UI::Element build(const Glob2UI::Presentation &, std::function<void()> changed);

  private:
	enum class View
	{
		Mean,
		Self,
		Total,
		Calls,
		PerTick,
		P50,
		P95,
		P99,
		Maximum,
		Deviation
	};
	unsigned scope = PerformanceTelemetry::ScopeCount;
	View view = View::Mean;
	bool realMinutes = false;
	unsigned group = 0;
	bool costsOpen = false, slowOpen = false, explorerOpen = false;
	bool recent = false;
	double cursorMinute = -1;
	struct Chart
	{
		std::string title, unit, explanation;
		std::vector<std::string> labels;
		std::vector<std::unique_ptr<EnginePerformancePanel>> series;
	};
	std::vector<Chart> charts;
	struct PlotBounds
	{
		int left = 0, right = 0;
	};
	std::array<PlotBounds, 7> chartPlots{};
	void initCharts();
	Glob2UI::Element explorer(const Glob2UI::Presentation &, std::function<void()> changed);
	Glob2UI::Element taskCosts(const Glob2UI::Presentation &);
	PlotBounds paintLines(Glob2UI::Canvas &, Glob2UI::Rect,
						  const std::vector<EnginePerformancePanel *> &,
						  const std::vector<std::string> &, const std::string &unit);
	unsigned selectedSlow = 0;
	std::uint64_t revision = ~std::uint64_t(0), session = 0;
	unsigned cachedScope = ~0u;
	View cachedView = View::Mean;
	bool cachedReal = false;
	struct Point
	{
		double x, value;
		std::uint64_t tickStart, tickEnd, windows;
		std::string mode;
		unsigned flags = 0;
		std::uint64_t budgetNs = 0, samples = 0;
	};
	std::vector<Point> points;
	PlotBounds explorerPlot;
	std::string viewUnit() const;
	void inspect(double localX, PlotBounds plot, const std::vector<Point> &data);
	void refresh();
	void paint(Glob2UI::Canvas &, Glob2UI::Rect);
};
