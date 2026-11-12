// SPDX-License-Identifier: GPL-3.0-or-later
#include "EnginePerformancePanel.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <tuple>
#include <vector>
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <FormatableString.h>
#include <RenderDevice.h>

namespace fe = Glob2UI;
namespace pt = PerformanceTelemetry;
namespace
{
const std::vector<std::string> overviewNames{"Tick computation: mean wall time (ms)",
											 "Tick computation: wall time per minute (ms/min)",
											 "Tick latency: approximate p95 wall time (ms)",
											 "Tick rate (ticks / real second)",
											 "Requested tick rate (ticks / second)",
											 "Presented frame rate (frames / real second)",
											 "Frame jitter: interval standard deviation (ms)",
											 "Frame jitter: mean consecutive change (ms)",
											 "Ticks waiting per minute",
											 "Ticks waiting (%)",
											 "Presentation budget misses (%)",
											 "Owner-loop budget misses (%)",
											 "Tick budget misses (%)",
											 "Longest tick overrun streak",
											 "Tick budget (ms)",
											 "Presentation budget (ms)"};
std::string nameOf(unsigned scope)
{
	return scope < pt::ScopeCount ? pt::descriptor(scope).name
								  : overviewNames.at(scope - pt::ScopeCount);
}
const char *unitOf(unsigned scope)
{
	static constexpr const char *overviewUnits[]{"ms", "ms/min", "ms",        "TPS", "TPS", "FPS",
												 "ms", "ms",     "ticks/min", "%",   "%",   "%",
												 "%",  "ticks",  "ms",        "ms"};
	return scope < pt::ScopeCount ? pt::unit(scope) : overviewUnits[scope - pt::ScopeCount];
}
constexpr int PlotLeft = 52, PlotRight = 12;
std::string boundaryText(unsigned flags)
{
	std::string text;
	for (const auto &[flag, label] : {std::pair{unsigned(pt::RendererChanged), "renderer changed"},
									  std::pair{unsigned(pt::PresentationGap), "long frame gap"},
									  std::pair{unsigned(pt::Suspended), "suspended"},
									  std::pair{unsigned(pt::Resumed), "resumed"}})
		if (flags & flag)
		{
			if (!text.empty())
				text += ", ";
			text += label;
		}
	return text.empty() ? "no transitions" : text;
}
std::string number(double value)
{
	if (!std::isfinite(value))
		return "Unavailable";
	std::ostringstream out;
	out << std::fixed << std::setprecision(2) << value;
	return out.str();
}
} // namespace
void EnginePerformancePanel::refresh()
{
	const auto &c = pt::collector();
	const auto *h = c.history.get();
	if (h && session == c.session && revision == h->revision && cachedScope == scope &&
		cachedView == view && cachedReal == realMinutes)
		return;
	session = c.session;
	revision = h ? h->revision : 0;
	cachedScope = scope;
	cachedView = view;
	cachedReal = realMinutes;
	points.clear();
	if (!h)
		return;
	points.reserve(pt::History::Capacity);
	for (std::size_t i = 0; i < h->size; ++i)
	{
		const auto &w = h->window(i);
		const auto &m = w.metrics[scope < pt::ScopeCount ? scope : unsigned(pt::Id::OwnerAdvance)];
		double v = std::numeric_limits<double>::quiet_NaN();
		const auto ticks = w.tickEnd - w.tickStart;
		const double minutes =
			realMinutes ? double(w.endNs - w.startNs) / 60e9 : double(ticks) / 1800.;
		const double scale = std::string_view(unitOf(scope)) == "ns" ? 1e6 : 1.;
		if (scope < pt::ScopeCount && view == View::Calls && m.calls && minutes > 0)
			v = double(m.calls) / minutes;
		if (scope < pt::ScopeCount && m.time.count)
			switch (view)
			{
			case View::Mean:
				v = m.time.mean / scale;
				break;
			case View::Self:
				if (std::string_view(unitOf(scope)) == "ns" && m.selfComplete &&
					pt::descriptor(scope).stride == 1)
					v = double(m.self) / m.time.count / scale;
				break;
			case View::Total:
				if (minutes > 0)
					v = double(m.time.total) / scale / minutes;
				break;
			case View::Calls:
				if (minutes > 0)
					v = double(m.calls) / minutes;
				break;
			case View::PerTick:
				if (ticks)
					v = double(m.time.total) / ticks / scale;
				break;
			case View::P50:
				v = m.time.percentile(.5) / scale;
				break;
			case View::P95:
				v = m.time.percentile(.95) / scale;
				break;
			case View::P99:
				v = m.time.percentile(.99) / scale;
				break;
			case View::Maximum:
				v = m.time.maximum / scale;
				break;
			case View::Deviation:
				v = std::sqrt(std::max(0., m.time.m2 / m.time.count)) / scale;
				break;
			}
		// These backend counters are frame deltas, not individual capture
		// observations. Only their ratio yields latency per completed capture.
		if (scope == unsigned(pt::Id::CaptureLatency))
		{
			const auto &completed = w.metrics[unsigned(pt::Id::CaptureCount)].time;
			v = NAN;
			if (m.time.count && completed.count)
				switch (view)
				{
				case View::Mean:
					if (completed.total)
						v = double(m.time.total) / completed.total / 1e6;
					break;
				case View::Calls:
					if (minutes > 0)
						v = double(completed.total) / minutes;
					break;
				case View::Total:
					if (minutes > 0)
						v = double(m.time.total) / minutes / 1e6;
					break;
				case View::PerTick:
					if (ticks)
						v = double(m.time.total) / ticks / 1e6;
					break;
				default:
					break; // Per-capture percentiles were not recorded.
				}
		}
		// Waits are event measurements: an advancing, covered interval with no
		// wait events is a measured zero, not an unsupported clock or missing data.
		const bool waitMetric =
			(scope >= unsigned(pt::Id::WaitAI) && scope <= unsigned(pt::Id::WaitClaims)) ||
			scope == unsigned(pt::Id::JoinWait);
		if (waitMetric && !m.time.count && ticks &&
			w.metrics[unsigned(pt::Id::OwnerAdvance)].time.count &&
			(view == View::Mean || view == View::PerTick ||
			 (minutes > 0 && (view == View::Total || view == View::Calls))))
			v = 0;
		if (scope >= pt::ScopeCount)
		{
			const auto &tick = w.metrics[unsigned(pt::Id::OwnerAdvance)];
			const auto &frames = w.metrics[unsigned(pt::Id::FrameInterval)];
			const auto seconds = double(w.endNs - w.startNs) / 1e9;
			switch (scope - pt::ScopeCount)
			{
			case 0:
				v = tick.time.count ? tick.time.mean / 1e6 : NAN;
				break;
			case 1:
				v = tick.time.count && minutes > 0 ? double(tick.time.total) / minutes / 1e6 : NAN;
				break;
			case 2:
				v = tick.time.count ? tick.time.percentile(.95) / 1e6 : NAN;
				break;
			case 3:
				v = seconds > 0 ? ticks / seconds : NAN;
				break;
			case 4:
				v = w.budgetNs ? 1e9 / w.budgetNs : NAN;
				break;
			case 5:
				v = frames.time.count && frames.time.total
						? frames.time.count * 1e9 / frames.time.total
						: NAN;
				break;
			case 6:
				v = frames.time.count
						? std::sqrt(std::max(0., frames.time.m2 / frames.time.count)) / 1e6
						: NAN;
				break;
			case 7:
			{
				const auto &jitter = w.metrics[unsigned(pt::Id::Jitter)];
				v = jitter.time.count ? jitter.time.mean / 1e6 : NAN;
				break;
			}
			case 8:
				v = minutes > 0 ? w.metrics[unsigned(pt::Id::TickWaited)].calls / minutes : NAN;
				break;
			case 9:
				v = ticks ? 100. * w.metrics[unsigned(pt::Id::TickWaited)].calls / ticks : NAN;
				break;
			case 10:
				v = w.frames.count ? 100. * w.frames.exceeded / w.frames.count : NAN;
				break;
			case 11:
				v = w.work.count ? 100. * w.work.exceeded / w.work.count : NAN;
				break;
			case 12:
				v = w.ticks.count ? 100. * w.ticks.exceeded / w.ticks.count : NAN;
				break;
			case 13:
				v = w.ticks.count ? double(w.ticks.longest) : NAN;
				break;
			case 14:
				v = w.budgetNs ? w.budgetNs / 1e6 : NAN;
				break;
			case 15:
				v = w.presentationBudgetNs ? w.presentationBudgetNs / 1e6 : NAN;
				break;
			}
		}
		points.push_back({double(w.tickEnd) / 1800., v, w.tickStart, w.tickEnd, w.windows,
						  w.mode.data(), w.flags, w.budgetNs, m.time.count});
	}
}
fe::Element EnginePerformancePanel::explorer(const fe::Presentation &p,
											 std::function<void()> changed)
{
	refresh();
	static const std::vector<std::string> categories{
		"Overview",  "Engine",       "Executor and waits",
		"Rendering", "Frame pacing", "Gradients and paths",
		"AI",        "Saves"};
	static constexpr const char *categoryIds[]{"overview", "engine",    "executor", "rendering",
											   "pacing",   "gradients", "ai",       "saves"};
	std::vector<std::string> metrics;
	std::vector<unsigned> indices;
	if (group == 0)
	{
		metrics = overviewNames;
		for (unsigned i = 0; i < overviewNames.size(); ++i)
			indices.push_back(pt::ScopeCount + i);
	}
	else
		for (unsigned i = 0; i < pt::ScopeCount; ++i)
			if (std::string_view(pt::category(i)) == categoryIds[group])
			{
				metrics.push_back(pt::descriptor(i).name);
				indices.push_back(i);
			}
	auto category =
		fe::choice("engine-performance/category", categories, int(group),
				   [this, changed](int selected)
				   {
					   group = unsigned(selected);
					   if (group == 0)
						   scope = pt::ScopeCount;
					   else
						   for (unsigned i = 0; i < pt::ScopeCount; ++i)
							   if (std::string_view(pt::category(i)) == categoryIds[group])
							   {
								   scope = i;
								   break;
							   }
					   view = View::Mean;
					   changed();
				   });
	const auto units = std::string_view(unitOf(scope));
	const std::string valueUnit = units == "ns" ? "ms" : std::string(units);
	std::vector<std::string> views{"Mean wall time / value (" + valueUnit + ")",
								   "Mean exclusive wall time (duration metrics only, ms)",
								   "Wall time / value per minute (" + valueUnit + "/min)",
								   "Calls / affected ticks per minute",
								   "Wall time / value per advancing tick (" + valueUnit + ")",
								   "Approximate p50 (" + valueUnit + ")",
								   "Approximate p95 (" + valueUnit + ")",
								   "Approximate p99 (" + valueUnit + ")",
								   "Maximum (" + valueUnit + ")",
								   "Standard deviation (" + valueUnit + ")"};
	if (scope == unsigned(pt::Id::CaptureLatency))
	{
		views[unsigned(View::Mean)] = "Mean latency per completed capture (ms/capture)";
		views[unsigned(View::Calls)] = "Completed captures per minute";
	}
	auto metric =
		fe::choice("engine-performance/metric", metrics,
				   int(std::find(indices.begin(), indices.end(), scope) - indices.begin()),
				   [this, changed, indices](int i)
				   {
					   scope = indices.at(std::size_t(i));
					   if (scope >= pt::ScopeCount)
					   {
						   changed();
						   return;
					   }
					   const std::string nameStorage = nameOf(scope);
					   const std::string_view name(nameStorage);
					   view = std::string_view(unitOf(scope)) == "count" ? View::Total
							  : (name.find("wait_tick") != name.npos ||
								 name.find("incomplete_deadline") != name.npos)
								  ? View::Calls
								  : View::Mean;
					   changed();
				   });
	auto mode = fe::choice("engine-performance/view", views, int(view),
						   [this, changed](int i)
						   {
							   view = View(i);
							   changed();
						   });
	auto clock = fe::choice("engine-performance/clock", {fe::tr("[engine performance per game minute]"), fe::tr("[engine performance per real minute]")},
							realMinutes ? 1 : 0,
							[this, changed](int i)
							{
								realMinutes = i != 0;
								changed();
							});
	fe::CanvasOptions options;
	options.accessibleText =
		std::string(nameOf(scope)) + "; " +
		(scope < pt::ScopeCount ? views[int(view)] : std::string("Session overview")) +
		"; latest " + (points.empty() ? "Unavailable" : number(points.back().value));
	auto inspectPoint = [this](fe::Point point)
	{
		refresh();
		inspect(point.x, explorerPlot, points);
	};
	options.hover = inspectPoint;
	options.tap = [inspectPoint](fe::Point point, fe::Host &) { inspectPoint(point); };
	std::vector<fe::Element> parts;
	parts.push_back(fe::label(fe::tr("[engine performance game engine performance]")));
	parts.push_back(std::move(category));
	parts.push_back(std::move(metric));
	if (scope == unsigned(pt::Id::CaptureLatency))
		parts.push_back(fe::hint("Mean latency uses total latency divided by completed captures. "
								 "Individual capture latency distributions are not recorded."));
	if (scope < pt::ScopeCount)
		parts.push_back(std::move(mode));
	if (scope < pt::ScopeCount && pt::descriptor(scope).stride > 1)
		parts.push_back(fe::hint("Duration summaries contain sampled observations (1 in " +
								 std::to_string(pt::descriptor(scope).stride) +
								 " calls); call rates count every call."));
	const bool rate = scope >= pt::ScopeCount
						  ? scope == pt::ScopeCount + 1 || scope == pt::ScopeCount + 8
						  : view == View::Total || view == View::Calls;
	if (rate)
		parts.push_back(std::move(clock));
	parts.push_back(fe::canvas(
		"engine-performance/explorer-chart", {p.pt(240), p.textPt(220)},
		[this](fe::Canvas &c, fe::Rect r, const fe::Frame &)
		{
			refresh();
			paint(c, r);
		},
		options));
	parts.push_back(fe::button("engine-performance/costs", fe::tr("[engine performance task wall-time costs]"),
							   [this, changed]
							   {
								   costsOpen = !costsOpen;
								   changed();
							   },
							   {.selected = costsOpen}));
	if (costsOpen)
		parts.push_back(taskCosts(p));
	parts.push_back(fe::button("engine-performance/slow", fe::tr("[engine performance inspect slow ticks]"),
							   [this, changed]
							   {
								   slowOpen = !slowOpen;
								   changed();
							   },
							   {.selected = slowOpen}));
	if (slowOpen && pt::collector().history)
	{
		std::vector<pt::SlowTick> slow;
		for (const auto &tick : pt::collector().history->slowTicks)
			if (tick.wallNs)
				slow.push_back(tick);
		std::sort(slow.begin(), slow.end(),
				  [](const auto &a, const auto &b) { return a.wallNs > b.wallNs; });
		if (!slow.empty())
		{
			selectedSlow = std::min(selectedSlow, unsigned(slow.size() - 1));
			std::vector<std::string> ticks;
			for (const auto &tick : slow)
				ticks.push_back(GAGCore::FormattableString(fe::tr("[engine performance tick %0 wall %1]"))
					.arg(tick.tick).arg(number(tick.wallNs / 1e6)));
			parts.push_back(fe::choice("engine-performance/slow-tick", ticks, int(selectedSlow),
									   [this, changed](int i)
									   {
										   selectedSlow = unsigned(i);
										   changed();
									   }));
			const auto &tick = slow[selectedSlow];
			parts.push_back(fe::paragraph(GAGCore::FormattableString(fe::tr("[engine performance wall time %0]")).arg(number(tick.wallNs / 1e6))));
			parts.push_back(fe::paragraph(GAGCore::FormattableString(fe::tr("[engine performance waits %0 joins %1 locks %2]"))
				.arg(number(tick.waitNs / 1e6)).arg(number(tick.ownerJoinNs / 1e6)).arg(number(tick.lockNs / 1e6))));
			static constexpr const char *phases[]{"[engine performance phase orders]",        "[engine performance phase units]",      "[engine performance phase buildings]",
												  "[engine performance phase map]",           "[engine performance phase statistics]", "[engine performance phase ai]",
												  "[engine performance phase world capture]", "[engine performance phase checksums]"};
			for (unsigned i = 0; i < tick.phaseWall.size(); ++i)
				parts.push_back(fe::paragraph(GAGCore::FormattableString(fe::tr("[engine performance phase %0 wall %1]"))
				.arg(fe::tr(phases[i])).arg(number(tick.phaseWall[i] / 1e6))));
		}
		else
			parts.push_back(fe::paragraph(fe::tr("[engine performance no advancing ticks recorded yet]")));
	}
	parts.push_back(fe::hint(
		"Local execution session. Game-time axis; approximate histogram percentiles. Inclusive "
		"timings overlap. Wall time includes blocking and descheduling. Hover shows mode and "
		"renderer/suspension transitions. Missing measurements are unavailable, not zero."));
	parts.push_back(fe::hint(
		"Worker queue delay does not identify a competing task. A tick can wait for multiple "
		"producers. Older windows are merged while preserving totals and distributions."));
	return fe::column(std::move(parts), {p.pt(5)});
}

void EnginePerformancePanel::initCharts()
{
	if (!charts.empty())
		return;
	auto add = [this](std::string title, std::string unit, std::string explanation,
					  std::initializer_list<std::tuple<const char *, unsigned, View>> specs)
	{
		Chart chart;
		chart.title = std::move(title);
		chart.unit = std::move(unit);
		chart.explanation = std::move(explanation);
		for (const auto &[label, id, selected] : specs)
		{
			auto series = std::make_unique<EnginePerformancePanel>();
			series->scope = id;
			series->view = selected;
			chart.labels.emplace_back(label);
			chart.series.push_back(std::move(series));
		}
		charts.push_back(std::move(chart));
	};
	auto id = [](pt::Id value) { return unsigned(value); };
	const auto o = pt::ScopeCount;
	add("Is the engine keeping up?", "TPS",
		"Achieved ticks per real second against the requested rate. Uncapped targets have no line.",
		{{"Achieved", o + 3, View::Mean}, {"Requested", o + 4, View::Mean}});
	add("How long does a tick take?", "ms",
		"Owner advancement, including required joins. p95 is approximate; budget follows game "
		"speed.",
		{{"Mean", o, View::Mean}, {"p95", o + 2, View::Mean}, {"Budget", o + 14, View::Mean}});
	// Every producer's wait, in ComputeProducer order. Only producers that
	// waited somewhere in the shown range are drawn (see visibleSeries).
	add("What holds up progress?", "ms/tick",
		"Exposed waiting per advancing tick for each background producer that waited in the "
		"shown range. A tick can wait for several producers at once.",
		{{"AI", id(pt::Id::WaitAI), View::PerTick},
		 {"Zone / resource gradients", id(pt::Id::WaitPeriodicGradient), View::PerTick},
		 {"Building gradients", id(pt::Id::WaitBuildingGradient), View::PerTick},
		 {"Resource growth", id(pt::Id::WaitResourceGrowth), View::PerTick},
		 {"Hiring", id(pt::Id::WaitHiring), View::PerTick},
		 {"Presentation", id(pt::Id::WaitPresentation), View::PerTick},
		 {"Other", id(pt::Id::WaitOther), View::PerTick},
		 {"Mixed", id(pt::Id::WaitMixed), View::PerTick},
		 {"Checksums", id(pt::Id::WaitChecksum), View::PerTick},
		 {"Team statistics", id(pt::Id::WaitTeamStatistics), View::PerTick},
		 {"Turret targeting", id(pt::Id::WaitTurret), View::PerTick},
		 {"Claims", id(pt::Id::WaitClaims), View::PerTick}});
	{
		// Stable colour per producer, so a producer keeps its colour as others
		// appear and disappear. Eight validated dark-surface hues; the four
		// producers that rarely wait reuse a hue drawn as dots instead of lines.
		const GAGCore::Color blue(57, 135, 229), orange(217, 89, 38), aqua(25, 158, 112),
			yellow(201, 133, 0), magenta(213, 81, 129), green(0, 131, 0), violet(144, 133, 233),
			red(230, 103, 103);
		auto &chart = charts.back();
		chart.nonZeroOnly = true;
		chart.styles = {{blue},   {orange},       {aqua},         {yellow},
						{magenta}, {violet, true}, {orange, true}, {aqua, true},
						{violet},  {red},          {yellow, true}, {green}};
	}
	add("How often is progress delayed?", "%",
		"Advancing ticks with any exposed wait, and ticks over budget. Producer counts can "
		"overlap.",
		{{"Ticks waiting", o + 9, View::Mean}, {"Ticks over budget", o + 12, View::Mean}});
	add("Are frames arriving smoothly?", "ms",
		"Presented frame intervals, with approximate p95 and the presentation budget. Includes "
		"fast-forward periods.",
		{{"Mean", id(pt::Id::FrameInterval), View::Mean},
		 {"p95", id(pt::Id::FrameInterval), View::P95},
		 {"Budget", o + 15, View::Mean}});
	add("How much does frame pacing vary?", "ms",
		"Interval standard deviation and mean change between consecutive intervals. Lower is "
		"steadier.",
		{{"Jitter", o + 6, View::Mean}, {"Interval change", o + 7, View::Mean}});
	add("Rendering work", "ms",
		"CPU-side rendering wall time includes blocking and descheduling. GL GPU frame timing and "
		"WebGPU render-pass sums per submission have different coverage; pass sums exclude copies. "
		"Absent samples remain unavailable.",
		{{"CPU-side wall", id(pt::Id::Render), View::Mean},
		 {"GPU frame", id(pt::Id::GPUTime), View::Mean},
		 {"GPU passes / submit", id(pt::Id::GPURenderPass), View::Mean}});
}

fe::Element EnginePerformancePanel::compact(const fe::Presentation &p, std::function<void()> open)
{
	initCharts();
	// These readouts repaint from the published history, without rebuilding the
	// surrounding HUD or touching live simulation state.
	fe::CanvasOptions options;
	options.accessibleText = "Whole-engine performance summary";
	for (auto index : {0u, 1u, 3u})
	{
		auto &data = *charts[index].series[0];
		data.refresh();
		options.accessibleText +=
			"; " + charts[index].labels[0] + ": " +
			(data.points.empty() ? "Collecting" : number(data.points.back().value)) + " " +
			charts[index].unit;
	}
	return fe::column(
		{fe::label(fe::tr("[engine performance whole-engine performance]")),
		 fe::canvas(
			 "engine-performance/summary", {p.pt(160), p.pt(108)},
			 [this, p](fe::Canvas &c, fe::Rect r, const fe::Frame &)
			 {
				 c.fillRect(r, GAGCore::Color(18, 25, 35));
				 const std::pair<const char *, EnginePerformancePanel *> rows[]{
					 {"TPS", charts[0].series[0].get()},
					 {"Tick ms", charts[1].series[0].get()},
					 {"Waiting %", charts[3].series[0].get()}};
				 int y = r.y;
				 for (auto [label, series] : rows)
				 {
					 series->refresh();
					 c.text({r.x, y}, fe::FontRole::Support,
							std::string(label) + ": " +
								(series->points.empty() ? "Collecting…"
														: number(series->points.back().value)),
							GAGCore::Color(205, 215, 230));
					 y += p.pt(28);
				 }
			 },
			 options),
		 fe::button("engine-performance/open", fe::tr("[engine performance open performance charts]"), std::move(open)),
		 fe::hint("This session · all teams\nWall-clock measurements")},
		{p.pt(8)});
}

fe::Element EnginePerformancePanel::build(const fe::Presentation &p, std::function<void()> changed)
{
	initCharts();
	std::vector<fe::Element> parts{fe::heading(fe::tr("[engine performance engine performance]"))};
	const auto *h = pt::collector().history.get();
	if (h && h->size)
		parts.push_back(fe::hint(
			"All teams · recorded game minutes " + number(h->window(0).tickStart / 1800.) + "–" +
			number(h->window(h->size - 1).tickEnd / 1800.) + " · loading starts a new session"));
	else
		parts.push_back(fe::hint("All teams · collecting this session’s first interval"));
	parts.push_back(fe::adaptive(
		[this, p, changed](const fe::LayoutContext &, fe::Size space)
		{
			auto range = fe::segments("engine-performance/range",
									  {"Whole session", "Last 5 game minutes"}, recent ? 1 : 0,
									  [this, changed](int i)
									  {
										  recent = i != 0;
										  cursorMinute = -1;
										  changed();
									  });
			auto follow = fe::button("engine-performance/follow-latest", fe::tr("[engine performance follow latest interval]"),
									 [this, changed]
									 {
										 cursorMinute = -1;
										 changed();
									 });
			return space.w < p.textPt(650) ? fe::column({range, follow}, {p.pt(5)})
										   : fe::row({fe::expanded(range), follow}, {p.pt(8)});
		}));
	std::vector<fe::Element> cards;
	for (std::size_t i = 0; i < charts.size(); ++i)
	{
		auto &chart = charts[i];
		fe::CanvasOptions options;
		options.accessibleText = chart.title + " (" + chart.unit + "). " + chart.explanation;
		for (auto &s : chart.series)
			s->refresh();
		const auto shown = visibleSeries(chart);
		for (const auto n : shown)
			options.accessibleText +=
				" " + chart.labels[n] + ": " +
				(chart.series[n]->points.empty() ? "Collecting"
												 : number(chart.series[n]->points.back().value));
		if (chart.nonZeroOnly && shown.empty())
			options.accessibleText += " No background waits in the shown range.";
		// Each legend row holds two series; grow the plot to keep its height.
		const int legendRows = int((std::max<std::size_t>(shown.size(), 4) + 1) / 2);
		auto inspect = [this, i](fe::Point at)
		{
			auto &data = *charts[i].series[0];
			data.refresh();
			this->inspect(at.x, chartPlots[i], data.points);
		};
		options.hover = inspect;
		options.tap = [inspect](fe::Point at, fe::Host &) { inspect(at); };
		const auto key =
			i == 0 ? "engine-performance/chart" : "engine-performance/chart/" + std::to_string(i);
		auto canvas = fe::canvas(
			key, {p.textPt(340), p.textPt(220 + 18 * (legendRows - 2))},
			[this, i](fe::Canvas &c, fe::Rect r, const fe::Frame &)
			{
				auto &chart = charts[i];
				for (auto &s : chart.series)
					s->refresh();
				std::vector<EnginePerformancePanel *> data;
				std::vector<std::string> labels;
				std::vector<SeriesStyle> styles;
				for (const auto n : visibleSeries(chart))
				{
					data.push_back(chart.series[n].get());
					labels.push_back(chart.labels[n]);
					if (n < chart.styles.size())
						styles.push_back(chart.styles[n]);
				}
				if (chart.nonZeroOnly && data.empty() && !chart.series.front()->points.empty())
				{
					chartPlots[i] = {};
					c.fillRect(r, GAGCore::Color(18, 25, 35));
					c.text({r.x + 10, r.y + 12}, fe::FontRole::Support,
						   "No background waits in the shown range.", GAGCore::Color(220, 225, 235));
					return;
				}
				// The hover position is mapped through the first series' windows,
				// which every series shares.
				if (data.empty())
					data.push_back(chart.series.front().get());
				chartPlots[i] = paintLines(c, r, data, labels.empty() ? chart.labels : labels,
										   chart.unit, styles);
			},
			options);
		cards.push_back(fe::card(fe::column({fe::label(chart.title),canvas,fe::hint(chart.explanation + (i==6 ? []{
            if(!globalContainer || !globalContainer->gfx) return std::string(" No GPU timing samples recorded.");
            const auto &gfx=*globalContainer->gfx;
            const auto backend=std::string(GAGCore::rendererBackendLabel(gfx.backendSwitchStatus().active));
            const auto *device=gfx.renderDevice();
            return !device || !device->capabilities().gpuTiming ? " GPU timing unavailable — "+backend+" renderer." : " GPU queries are sampled asynchronously; missing results have no samples.";
        }() : std::string()))},{p.pt(5)}),{.padding=p.pt(10),.shadow=false}));
	}
	parts.push_back(fe::wrap(std::move(cards),
							 {.gap = p.pt(10), .minChildWidth = p.textPt(380), .maxColumns = 2}));
	parts.push_back(fe::button("engine-performance/cost-summary", fe::tr("[engine performance compare task costs]"),
							   [this, changed]
							   {
								   costsOpen = !costsOpen;
								   changed();
							   },
							   {.selected = costsOpen}));
	if (costsOpen)
		parts.push_back(taskCosts(p));
	parts.push_back(fe::button("engine-performance/all-metrics", fe::tr("[engine performance all metrics and slow ticks]"),
							   [this, changed]
							   {
								   explorerOpen = !explorerOpen;
								   changed();
							   },
							   {.selected = explorerOpen}));
	if (explorerOpen)
		parts.push_back(explorer(p, changed));
	parts.push_back(
		fe::hint("Hover or tap any plot to inspect the same game time across charts. Shading marks "
				 "uncapped or mixed tick targets. Gold marks show mode changes or interruptions. "
				 "Coarse intervals retain their full coverage; gaps are not interpolated. Wall "
				 "time includes blocking and descheduling."));
	return fe::column(std::move(parts), {p.pt(10)});
}

fe::Element EnginePerformancePanel::taskCosts(const fe::Presentation &p)
{
	using I = pt::Id;
	// Owning containers: a nested initializer_list's inner arrays are temporaries
	// that die once `groups` is initialised, leaving the loop below reading freed
	// stack memory (a crash on "Compare task costs").
	const std::vector<std::pair<const char *, std::vector<std::pair<I, const char *>>>>
		groups{{"Owner work",
				{{I::Units, "Units"},
				 {I::Buildings, "Buildings"},
				 {I::Tasks, "Tasks"},
				 {I::Map, "Map"},
				 {I::Fog, "Fog"},
				 {I::Scripts, "Scripts"},
				 {I::Projects, "Construction"},
				 {I::CaptureWorld, "World capture"},
				 {I::AIAdmission, "AI admission"}}},
			   {"Compute callbacks (workers or owner)",
				{{I::ComputeAI, "AI decisions"},
				 {I::ComputePeriodicGradient, "Zone / resource gradients"},
				 {I::ComputeBuildingGradient, "Building gradients"},
				 {I::ComputeHiring, "Hiring"},
				 {I::ComputeTurret, "Turret targeting"},
				 {I::ComputeChecksum, "Checksums"},
				 {I::ComputeTeamStatistics, "Team statistics"},
				 {I::ComputeResourceGrowth, "Resource growth"},
				 {I::ComputeClaims, "Claims"},
				 {I::ComputeScene, "Scene preparation"},
				 {I::ComputeMaintenance, "Maintenance"}}},
			   {"Rendering and supporting work",
				{{I::Render, "Rendering (CPU-side wall)"},
				 {I::SaveHash, "Save checksums"},
				 {I::SaveWrite, "Save writing"}}}};
	std::vector<fe::Element> sections;
	sections.push_back(
		fe::hint("Inclusive wall durations · groups and rows overlap · these are work costs, not a "
				 "critical-path ranking. Range matches the charts."));
	const auto *h = pt::collector().history.get();
	double ticks = 0;
	std::array<double, pt::ScopeCount> totals{};
	if (h && h->size)
	{
		const double start = recent ? h->window(h->size - 1).tickEnd / 1800. - 5. : -1.;
		for (std::size_t n = 0; n < h->size; ++n)
		{
			const auto &w = h->window(n);
			if (w.tickEnd / 1800. < start)
				continue;
			ticks += w.tickEnd - w.tickStart;
			for (unsigned j = 0; j < pt::ScopeCount; ++j)
				totals[j] += w.metrics[j].time.total / 1e6;
		}
	}
	for (auto &[title, tasks] : groups)
	{
		std::vector<std::pair<double, std::pair<I, const char *>>> sorted;
		for (auto task : tasks)
			sorted.push_back({totals[unsigned(task.first)], task});
		std::sort(sorted.begin(), sorted.end(),
				  [](const auto &a, const auto &b) { return a.first > b.first; });
		std::vector<fe::Element> rows{fe::label(title),
									  fe::row({fe::expanded(fe::hint("Task")),
											   fe::width(p.textPt(100), fe::hint("ms / tick")),
											   fe::width(p.textPt(110), fe::hint("Total ms"))})};
		for (auto &[wall, task] : sorted)
			rows.push_back(fe::row(
				{fe::expanded(fe::label(task.second, {fe::FontRole::Support})),
				 fe::width(p.textPt(100),
						   fe::label(ticks ? (wall > 0 && wall / ticks < .01 ? "<0.01"
																			 : number(wall / ticks))
										   : "—",
									 {fe::FontRole::Support})),
				 fe::width(p.textPt(110), fe::label(wall > 0 && wall < .01 ? "<0.01" : number(wall),
													{fe::FontRole::Support}))},
				{p.pt(4)}));
		sections.push_back(fe::card(fe::column(std::move(rows), {p.pt(4)}),
									{.padding = p.pt(8), .shadow = false}));
	}
	return fe::column(std::move(sections), {p.pt(8)});
}

std::vector<std::size_t> EnginePerformancePanel::visibleSeries(const Chart &chart) const
{
	std::vector<std::size_t> shown;
	for (std::size_t n = 0; n < chart.series.size(); ++n)
	{
		if (!chart.nonZeroOnly)
		{
			shown.push_back(n);
			continue;
		}
		// Match paintLines' range: the last five game minutes or the whole session.
		const auto &points = chart.series[n]->points;
		if (points.empty())
			continue;
		const double xmin = std::max(points.front().tickStart / 1800.,
									 recent ? points.back().x - 5. : 0.);
		for (const auto &point : points)
			if (point.x >= xmin && std::isfinite(point.value) && point.value > 0)
			{
				shown.push_back(n);
				break;
			}
	}
	return shown;
}

std::string EnginePerformancePanel::viewUnit() const
{
	if (scope >= pt::ScopeCount)
		return unitOf(scope);
	if (scope == unsigned(pt::Id::CaptureLatency))
	{
		if (view == View::Mean)
			return "ms/capture";
		if (view == View::Calls)
			return "captures/min";
	}
	const std::string base = std::string_view(unitOf(scope)) == "ns" ? "ms" : unitOf(scope);
	switch (view)
	{
	case View::Calls:
		return (scope >= unsigned(pt::Id::WaitTickAI) &&
				scope <= unsigned(pt::Id::WaitTickClaims)) ||
					   scope == unsigned(pt::Id::TickWaited)
				   ? "ticks/min"
				   : "calls/min";
	case View::Total:
		return base + "/min";
	case View::PerTick:
		return base + "/tick";
	case View::Self:
		return "ms";
	default:
		return base;
	}
}
void EnginePerformancePanel::inspect(double localX, PlotBounds plot, const std::vector<Point> &data)
{
	if (data.empty() || plot.right <= plot.left)
		return;
	const double end = data.back().x;
	const double start = std::max(data.front().tickStart / 1800., recent ? end - 5. : 0.);
	cursorMinute =
		start + std::clamp((localX - plot.left) / (plot.right - plot.left), 0., 1.) * (end - start);
}
void EnginePerformancePanel::paint(fe::Canvas &c, fe::Rect r)
{
	refresh();
	explorerPlot = paintLines(c, r, {this}, {"Value"}, viewUnit());
}
EnginePerformancePanel::PlotBounds
EnginePerformancePanel::paintLines(fe::Canvas &c, fe::Rect r,
								   const std::vector<EnginePerformancePanel *> &data,
								   const std::vector<std::string> &labels, const std::string &unit,
								   const std::vector<SeriesStyle> &styles)
{
	const GAGCore::Color ink(220, 225, 235), muted(150, 166, 183), grid(48, 61, 75),
		gold(220, 171, 81);
	const GAGCore::Color palette[]{GAGCore::Color(87, 208, 235), GAGCore::Color(244, 181, 91),
								   GAGCore::Color(186, 157, 240), GAGCore::Color(127, 218, 160)};
	auto style = [&](std::size_t n)
	{ return n < styles.size() ? styles[n] : SeriesStyle{palette[n % 4]}; };
	c.fillRect(r, GAGCore::Color(18, 25, 35));
	c.pushClip(r);
	if (data.empty() || data.front()->points.empty())
	{
		c.text({r.x + 10, r.y + 12}, fe::FontRole::Support, "Collecting the first interval…", ink);
		c.popClip();
		return {};
	}
	const auto &base = data.front()->points;
	const double xmax = base.back().x;
	const double xmin = std::max(base.front().tickStart / 1800., recent ? xmax - 5. : 0.);
	const double span = std::max(.001, xmax - xmin);
	double maximum = 0;
	for (auto *s : data)
		for (const auto &point : s->points)
			if (point.x >= xmin && std::isfinite(point.value))
				maximum = std::max(maximum, point.value);
	const double power = std::pow(10., std::floor(std::log10(maximum > 0 ? maximum : 1.)));
	maximum = maximum > 0 ? std::ceil(maximum / power / 2.) * power * 2. : 1.;
	const auto &measurer = c.measurer();
	const int lineHeight = std::max(1, measurer.lineHeight(fe::FontRole::Support));
	const int legendStep = lineHeight + 4;
	const int statusY = r.y + 6 + int((data.size() + 1) / 2) * legendStep + 4;
	const int left =
		r.x + std::max({PlotLeft, measurer.width(fe::FontRole::Support, number(maximum)) + 10,
						measurer.width(fe::FontRole::Support, unit) + 10});
	const int right = r.right() - PlotRight;
	const int top = statusY + lineHeight + lineHeight / 2 + 8;
	const int bottom = r.bottom() - (2 * lineHeight + 14);
	auto x = [&](double minute)
	{ return left + int((minute - xmin) / span * std::max(1, right - left)); };

	auto y = [&](double v) { return bottom - int(v / maximum * std::max(1, bottom - top)); };
	bool uncapped = false, uncappedLabel = false;
	int uncappedStart = left;
	for (const auto &point : base)
	{
		if (point.x < xmin)
			continue;
		if (!point.budgetNs && point.tickEnd > point.tickStart)
		{
			const int start = std::max(left, x(point.tickStart / 1800.));
			if (!uncapped)
			{
				uncappedStart = start;
				uncappedLabel = false;
			}
			c.fillRect({start, top, std::max(0, x(point.x) - start), bottom - top},
					   GAGCore::Color(40, 34, 27));
			if (!uncappedLabel && x(point.x) - uncappedStart > 180)
			{
				c.text({uncappedStart + 4, top + 4}, fe::FontRole::Support,
					   "Uncapped / mixed target", gold);
				uncappedLabel = true;
			}
			uncapped = true;
		}
		else
			uncapped = false;
	}
	c.text({r.x + 5, statusY}, fe::FontRole::Support, unit, muted);
	const int divisions = std::clamp((bottom - top) / (lineHeight + 4), 1, 4);
	for (int n = 0; n <= divisions; ++n)
	{
		double v = maximum * n / divisions;
		const int py = y(v);
		c.line({left, py}, {right, py}, grid);
		c.text({r.x + 4, py - lineHeight / 2}, fe::FontRole::Support, number(v), muted);
	}
	for (int n = 0; n <= 4; ++n)
	{
		const double minute = xmin + span * n / 4.;
		const int px = x(minute);
		c.line({px, top}, {px, bottom}, grid);
		if (n % 2 == 0 || right - left > 430)
		{
			const auto label = number(minute);
			const int width = measurer.width(fe::FontRole::Support, label);
			c.text({std::clamp(px - width / 2, left, std::max(left, right - width)), bottom + 5},
				   fe::FontRole::Support, label, muted);
		}
	}
	const int axisWidth = measurer.width(fe::FontRole::Support, "game min");
	c.text({std::max(left, right - axisWidth), bottom + lineHeight + 8}, fe::FontRole::Support,
		   "game min", muted);

	std::size_t selectedIndex = base.size() - 1;
	const bool inspecting = cursorMinute >= xmin && cursorMinute <= xmax;
	if (inspecting)
	{
		double nearest = std::numeric_limits<double>::infinity();
		for (std::size_t i = 0; i < base.size(); ++i)
		{
			const double distance = std::abs(base[i].x - cursorMinute);
			// Paused intervals can share a game tick: prefer the latest one.
			if (distance <= nearest)
			{
				nearest = distance;
				selectedIndex = i;
			}
		}
		c.line({x(base[selectedIndex].x), top}, {x(base[selectedIndex].x), bottom}, ink);
	}
	const auto &selected = base[selectedIndex];
	std::string lastMode;
	std::uint64_t lastBudget = 0;
	bool havePrevious = false;
	for (const auto &point : base)
	{
		if (point.x < xmin)
			continue;
		if ((havePrevious && (lastMode != point.mode || lastBudget != point.budgetNs)) ||
			(point.flags & (pt::Suspended | pt::Resumed | pt::PresentationGap)))
			c.line({x(point.x), top}, {x(point.x), bottom}, gold);
		if (point.windows > 1)
			c.line({std::max(left, x(point.tickStart / 1800.)), bottom - 2},
				   {x(point.x), bottom - 2}, gold);
		lastMode = point.mode;
		lastBudget = point.budgetNs;
		havePrevious = true;
	}
	for (std::size_t n = 0; n < data.size(); ++n)
	{
		const auto &series = data[n]->points;
		bool connected = false;
		fe::Point previous;
		std::string mode;
		for (const auto &point : series)
		{
			if (point.x < xmin)
				continue;
			if (!std::isfinite(point.value))
			{
				connected = false;
				continue;
			}
			const fe::Point at{x(point.x), y(point.value)};
			const auto look = style(n);
			if (connected && mode == point.mode && !look.dotted)
				c.line(previous, at, look.color);
			else
				c.fillRect({at.x - 1, at.y - 1, 3, 3}, look.color);
			previous = at;
			connected = true;
			mode = point.mode;
		}
		// Every series projects the same ordered history windows. Tick identity
		// alone is insufficient because paused/final intervals can repeat it.
		const auto *point = selectedIndex < series.size() ? &series[selectedIndex] : nullptr;
		const bool available = point && std::isfinite(point->value);
		std::string value = available ? number(point->value) + " " + unit : "no samples";
		if (data[n]->scope == unsigned(pt::Id::GPUTime) ||
			data[n]->scope == unsigned(pt::Id::GPURenderPass))
		{
			if (available)
				value += " (n=" + std::to_string(point->samples) + ")";
			else
				value = "no timing samples";
		}
		const int column = int(n % 2), row = int(n / 2);
		const bool single = n + 1 == data.size() && column == 0;
		const int availableWidth = (single ? r.w : r.w / 2) - 16;
		const std::string suffix = ": " + value;
		const int labelWidth =
			std::max(0, availableWidth - measurer.width(fe::FontRole::Support, suffix));
		auto legend =
			fe::ellipsize(measurer, fe::FontRole::Support, labels[n], labelWidth) + suffix;
		legend = fe::ellipsize(measurer, fe::FontRole::Support, legend, availableWidth);
		c.text({r.x + 8 + column * r.w / 2, r.y + 6 + row * legendStep}, fe::FontRole::Support,
			   legend, style(n).color);
	}
	std::string status = std::string(inspecting ? "Selected: " : "Latest interval: ") +
						 number(selected.tickStart / 1800.) + "–" + number(selected.x) + " min · " +
						 selected.mode;
	if (!selected.budgetNs)
		status += " · no tick target";
	if (selected.windows > 1)
		status += " · " + std::to_string(selected.windows) + " merged intervals";
	if (selected.flags)
		status += " · " + boundaryText(selected.flags);
	c.text({left, statusY}, fe::FontRole::Support,
		   fe::ellipsize(measurer, fe::FontRole::Support, status, std::max(1, right - left)),
		   muted);
	c.popClip();
	return {left - r.x, right - r.x};
}
