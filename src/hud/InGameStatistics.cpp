// SPDX-License-Identifier: GPL-3.0-or-later
#include "InGameStatistics.h"
#include "GameGUI.h"
#include "TeamStatChart.h"
#include "TeamDisplay.h"
#include "UnitConsts.h"
#include <FormatableString.h>
#include <GestureScroll.h>
#include <algorithm>
#include <sstream>

namespace fe = Glob2UI;
namespace {
const char *topicKeys[] = {"[stats topic overview]", "[stat page colony]", "[stats topic economy]", "[stat group military]", "[stats topic world]"};
std::string reading(const Stats::Metric &metric, const Stats::View &view, const TeamStats *stats, int team)
{
    if (!stats) return fe::tr("[stats waiting]");
    const auto h = Stats::historyOf(team, *stats);
    const auto value = Stats::latestReading(metric, view, h);
    if (!value.available) {
        const bool covered = std::any_of(h.points.begin(), h.points.end(), [&](const auto &point) {
            if (metric.sampled >= 0) return point.sampled != nullptr;
            return point.measurements && (!metric.extended || point.tick >= h.extendedCoverageStartTick) &&
                (!metric.labour || point.tick >= h.labourCoverageStartTick);
        });
        return fe::tr(h.points.size() < 2 || covered ? "[Not enough recorded history yet.]" : "[stat not recorded for this match]");
    }
    auto text = TeamStatChart::readingText(value);
    if (metric.kind == Stats::Metric::Counter && !view.total && !metric.ratioOf && metric.scale == 1)
        text += fe::tr("[stat per minute short]");
    return text;
}
}

InGameStatisticsPanel::InGameStatisticsPanel(GameGUI &owner) : gui(owner) {}
InGameStatisticsPanel::Topic InGameStatisticsPanel::topicFor(Stats::Group group)
{
    switch (group) {
    case Stats::Group::Population: case Stats::Group::Food: return Topic::Colony;
    case Stats::Group::Work: case Stats::Group::Materials: case Stats::Group::Buildings: return Topic::Economy;
    case Stats::Group::Military: return Topic::Military;
    default: return Topic::World;
    }
}
void InGameStatisticsPanel::selectTopic(Topic value)
{
    if (int(value) < 0 || int(value) > int(Topic::World)) return;
    selectedTopic = value;
    inlineHoverX = inlineHoverY = -1;
    invalidate();
}
std::string InGameStatisticsPanel::selectedMetric() const
{
    const auto it = selectedMetrics.find(selectedTopic);
    return it == selectedMetrics.end() ? std::string() : it->second;
}
void InGameStatisticsPanel::selectMetric(const std::string &id)
{
    const auto *m = metric(id);
    if (!m) return;
    if (selectedTopic != Topic::Overview && topicFor(m->group) != selectedTopic) selectTopic(topicFor(m->group));
    selectedMetrics[selectedTopic] = id;
    views.try_emplace(id, Stats::defaultView(*m));
    inlineHoverX = inlineHoverY = -1;
    invalidate();
}
void InGameStatisticsPanel::setBounds(fe::Rect value)
{
    if (bounds.x == value.x && bounds.y == value.y && bounds.w == value.w && bounds.h == value.h) return;
    bounds = value;
    host().relayout();
}
void InGameStatisticsPanel::paintPanel(fe::Canvas &canvas, fe::Rect)
{
    canvas.fillRect(bounds, theme().palette.panel);
}
void InGameStatisticsPanel::drawEmbedded(GAGCore::DrawableSurface &surface, Uint32 tick)
{
    // This host is part of the HUD, not a recording/dialog phase. Only the
    // separately hosted history dialog uses UIDialog::draw().
    refreshPresentation();
    host().layoutIfNeeded();
    surface.setClipRect();
    fe::SurfaceCanvas canvas(surface, theme(), presentation());
    paintPanel(canvas, bounds);
    host().paint(canvas, tick);
}
void InGameStatisticsPanel::sync(const PresentationFrame &frame, int team, bool isWatching, bool winChances)
{
    // A team switch can precede the next published frame. Never label the old
    // team's compact snapshot as the newly requested team in that interval.
    const bool matching = frame.request.localTeam == team;
    const auto nextHistory = matching ? frame.world.history : nullptr;
    const auto nextLatest = matching ? frame.panels.localStats : nullptr;
    const bool changed = recorded != nextHistory || latest != nextLatest || teamId != team ||
                         types != frame.buildingTypes || watching != isWatching || showChances != winChances;
    if (catalog.empty() || types != frame.buildingTypes) {
        types = frame.buildingTypes;
        catalog = types ? Stats::catalogForBuildings(*types) : Stats::catalog();
        // This catalog metric uses hidden opponent state and remains post-game only.
        std::erase_if(catalog, [](const auto &m) { return m.winProbability; });
        for (const auto &m : catalog) selectedMetrics.try_emplace(topicFor(m.group), m.id);
    }
    if (teamId != team) inlineHoverX = inlineHoverY = -1;
    teamId = team;
    sampleTick = frame.tick;
    recorded = nextHistory;
    latest = nextLatest;
    watching = isWatching;
    showChances = winChances;
    chances = winChances ? frame.panels.hud.winChances : std::vector<SceneHud::WinChance>{};
    watchedName = watching && frame.world.teams && team >= 0 && std::size_t(team) < frame.world.teams->values.size()
        ? displayPlayerName(frame.world.teams->values[team].firstPlayerName) : std::string();
    if (frame.world.teams && team >= 0 && std::size_t(team) < frame.world.teams->values.size())
        color = presentationColor(frame.world.teams->values[team].color);
    if (changed) invalidate();
}
const TeamStats *InGameStatisticsPanel::history() const
{
    if (!recorded || teamId < 0 || std::size_t(teamId) >= recorded->teams.size()) return nullptr;
    return recorded->teams[teamId].get();
}
const Stats::Metric *InGameStatisticsPanel::metric(const std::string &id) const
{
    const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &m) { return id == m.id; });
    return it == catalog.end() ? nullptr : &*it;
}
void InGameStatisticsPanel::openMetric(const std::string &id)
{
    if (!metric(id)) return;
    selectMetric(id);
    returnKey = "stats/metric/" + id;
    host().closePopup();
    gui.openDialog(GameGUI::IGM_STATISTICS, std::make_unique<InGameStatisticsHistory>(*this, id));
}
void InGameStatisticsPanel::cancelEmbeddedInput()
{
    host().closePopup(); cancelInput();
    ownedFingers.clear(); mouseHeld = false; scrollSequence = 0;
}
void InGameStatisticsPanel::suspend()
{
    recorded.reset(); latest.reset();
    cancelEmbeddedInput();
}
bool InGameStatisticsPanel::routeEvent(const SDL_Event &event)
{
    bool inside = host().popupOpen();
    const auto area = host().rootBounds();
    if (const auto scroll = GAGCore::scrollGesture(event)) {
        inside = inside || area.contains(fe::Point{int(scroll->x), int(scroll->y)});
        if (scroll->phase == GAGCore::ScrollGesturePhase::Began && inside) scrollSequence = scroll->sequence;
        inside = inside || (scrollSequence && scrollSequence == scroll->sequence);
    }
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        inside = inside || area.contains(fe::Point{int(event.button.x), int(event.button.y)}) || mouseHeld;
        if (event.button.button == SDL_BUTTON_LEFT) mouseHeld = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && inside;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        inside = inside || mouseHeld || area.contains(fe::Point{int(event.motion.x), int(event.motion.y)}); break;
    case SDL_EVENT_MOUSE_WHEEL:
        inside = inside || area.contains(fe::Point{int(event.wheel.mouse_x), int(event.wheel.mouse_y)}); break;
    case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_UP: {
        const auto &viewport = presentation().viewport;
        inside = inside || ownedFingers.count(event.tfinger.fingerID) ||
            (event.type == SDL_EVENT_FINGER_DOWN && area.contains(fe::Point{int(event.tfinger.x * viewport.w), int(event.tfinger.y * viewport.h)}));
        if (event.type == SDL_EVENT_FINGER_DOWN && inside) ownedFingers.insert(event.tfinger.fingerID);
        if (event.type == SDL_EVENT_FINGER_UP) ownedFingers.erase(event.tfinger.fingerID);
        break;
    }
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        inside = inside || host().keyboardFocused() || event.key.key == SDLK_TAB; break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        cancelInput(); ownedFingers.clear(); mouseHeld = false; scrollSequence = 0; return false;
    default: break;
    }
    if (!inside) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_FINGER_DOWN) host().focus("", false);
        return false;
    }
    const bool consumed = eventLogical(event);
    return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ? consumed : true;
}
void InGameStatisticsPanel::restoreFocus()
{
    if (!returnKey.empty()) host().focus(returnKey, true);
    invalidate();
}
fe::Element InGameStatisticsPanel::metricRow(const Stats::Metric &m, const fe::Presentation &p, const Stats::Reading &value)
{
    const std::string id = m.id;
    std::string number = value.available ? TeamStatChart::readingText(value) : "—";
    const auto view = Stats::defaultView(m);
    if (value.available && m.kind == Stats::Metric::Counter && !view.total && !m.ratioOf && m.scale == 1)
        number += fe::tr("[stat per minute short]");
    const auto name = TeamStatChart::title(m);
    auto action = fe::button("stats/metric/" + id, "", [this, id] { selectMetric(id); },
        {.selected = selectedMetric() == id, .flat = true, .alignLeft = true,
         .tooltip = value.available ? TeamStatChart::about(m) : reading(m, view, history(), teamId),
         .accessibleLabel = name + "\n" + number});
    auto label = fe::paragraph(name);
    auto numberText = fe::paragraph(number, {fe::FontRole::Body, false, fe::TextAlign::Right});
    const fe::ToolkitTextMeasurer textMeasure(theme(), p.touch, p.textUnit);
    const int numberWidth = p.touch ? p.textPt(96) : textMeasure.width(fe::FontRole::Body, number);
    // Keep short readings on one line, using the full label width when a
    // translation or larger text would squeeze either column.
    const bool stacked = p.touch ? bounds.w < p.textPt(280)
        : textMeasure.width(fe::FontRole::Body, name) + numberWidth + p.pt(44) > bounds.w;
    auto columns = stacked
        ? fe::column({label, numberText}, {p.pt(2)})
        : fe::row({fe::expanded(label), fe::width(numberWidth, numberText)}, {p.pt(8), fe::CrossAlign::Center});
    auto text = fe::padding(fe::Insets::symmetric(p.pt(8), p.pt(6)), columns);
    // The transparent text layer keeps the whole row one focusable action,
    // while its label and value retain independent alignment and wrapping.
    return fe::stack({action, text});
}
fe::Element InGameStatisticsPanel::inlineHistory(const fe::Presentation &p)
{
    const auto id = selectedMetric();
    const auto *m = metric(id);
    if (!m) return fe::empty();
    fe::CanvasOptions options;
    options.accessibleText = TeamStatChart::title(*m) + ": " + fe::tr("[stats history]");
    options.hover = [this](fe::Point pt) { inlineHoverX = pt.x; inlineHoverY = pt.y; };
    options.tap = [this](fe::Point pt, fe::Host &) { inlineHoverX = pt.x; inlineHoverY = pt.y; };
    return fe::column({fe::caption(TeamStatChart::title(*m)),
        fe::canvas("stats/inline/chart", {p.pt(240), p.pt(150)}, [this, id, inset = p.pt(4)](fe::Canvas &canvas, fe::Rect rect, const fe::Frame &) {
            auto *surface = canvas.surface();
            const auto *m = metric(id);
            if (!surface || !m) return;
            canvas.fillRect(rect, TeamStatChart::background);
            canvas.pushClip(rect);
            if (const auto *stats = history()) {
                const double unit = presentation().unit;
                canvas.transformed(unit, {rect.x, rect.y}, rect, [&] {
                    TeamStatChart::Options options;
                    options.metric = m;
                    const auto chosen = views.find(id);
                    options.view = chosen == views.end() ? Stats::defaultView(*m) : chosen->second;
                    options.teams.push_back({teamId, color, ""});
                    options.hoverX = inlineHoverX < 0 ? -1 : int(inlineHoverX/unit);
                    options.hoverY = inlineHoverY < 0 ? -1 : int(inlineHoverY/unit);
                    TeamStatChart::paint({Stats::historyOf(teamId, *stats)}, sampleTick, *surface,
                        0, 0, int(rect.w/unit), int(rect.h/unit), options);
                });
            } else canvas.text({rect.x + inset, rect.y + inset}, fe::FontRole::Support,
                fe::tr("[stats waiting]"), TeamStatChart::ink);
            canvas.popClip();
        }, options),
        fe::button("stats/full-history/" + id, fe::tr("[stats full history]"), [this, id] { openMetric(id); },
            {.flat = true, .role = fe::FontRole::Support})
    }, {p.pt(4)});
}
fe::Element InGameStatisticsPanel::unitDetails(const fe::Presentation &p)
{
    std::vector<fe::Element> rows;
    rows.push_back(fe::button("stats/unit-details", fe::tr("[stats unit details]"), [this] { detailsOpen = !detailsOpen; invalidate(); },
                             {.selected = detailsOpen, .alignLeft = true}));
    if (!detailsOpen) return fe::column(std::move(rows));
    if (!latest) { rows.push_back(fe::paragraph(fe::tr("[stats waiting]"))); return fe::column(std::move(rows)); }
    const auto &s = *latest->getLatestStat();
    auto line = [&](const std::string &label, const std::string &value) { rows.push_back(fe::paragraph(label + ": " + value)); };
    auto count = [&](const char *key, int value) { line(fe::tr(key), std::to_string(value)); };
    count("[Units]", s.totalUnit);
    const char *unitKeys[] = {"[workers]", "[explorers]", "[warriors]"};
    for (int type = 0; type < NB_UNIT_TYPE; ++type) {
        if (type < 3) {
            count(unitKeys[type], s.numberUnitPerType[type]);
            const int free = type == WORKER ? std::max(0, s.isFree[type] - s.totalNeeded) : s.isFree[type];
            count("[free]", free);
            if (type == WORKER) count("[seeking a job]", std::min(s.totalNeeded, s.isFree[type]));
        }
    }
    count("[are ok]", s.needNothing); count("[are hungry]", s.needFood);
    count("[are dying hungry]", s.needFoodCritical); count("[are wonded]", s.needHeal);
    const std::pair<const char *, int> abilities[] = {{"[Walk]", WALK}, {"[Swim]", SWIM}, {"[Build]", BUILD},
        {"[Harvest]", HARVEST}, {"[At. speed]", ATTACK_SPEED}, {"[At. strength]", ATTACK_STRENGTH}};
    for (auto [key, ability] : abilities) {
        std::string values;
        for (int level = ability == SWIM ? 1 : 0; level < NB_UNIT_LEVELS; ++level) {
            if (!values.empty()) values += " / ";
            values += std::to_string(s.upgradeState[ability][level]);
        }
        line(fe::tr(key), values);
    }
    for (int level = 0; level < 3; ++level)
        line(fe::tr("[level]") + " " + std::to_string(level + 1) + " " + fe::tr("[jobs]"), std::to_string(s.totalNeededPerLevel[level]));
    std::string happiness;
    for (int i = 0; i <= HAPPINESS_COUNT; ++i) { if (i) happiness += " / "; happiness += std::to_string(s.happiness[i]); }
    line(fe::tr("[Happyness]"), happiness);
    return fe::column(std::move(rows), {p.pt(4)});
}
fe::Element InGameStatisticsPanel::build(const fe::Presentation &p)
{
    std::vector<fe::Element> topicRows, buttons;
    const bool narrow = bounds.w < p.textPt(270);
    const int columns = narrow ? 2 : 3;
    for (int i = 0; i < 5; ++i) {
        buttons.push_back(fe::expanded(fe::button("stats/topic/" + std::to_string(i), fe::tr(topicKeys[i]),
            [this, i] { selectTopic(Topic(i)); }, {.selected = selectedTopic == Topic(i), .role = fe::FontRole::Support})));
        if ((narrow && i == 0) || int(buttons.size()) == columns || i == 4) {
            topicRows.push_back(fe::row(std::move(buttons), {p.pt(4)}));
            buttons.clear();
        }
    }
    std::vector<fe::Element> content;
    if (!p.touch) content.push_back(fe::label(fe::tr("[Statistics]")));
    if (watching) content.push_back(fe::label(fe::tr("[watching:]") + " " + watchedName));
    content.push_back(fe::column(std::move(topicRows), {p.pt(4)}));
    std::vector<fe::Element> body;
    bool missing = false;
    const auto *stats = history();
    const auto joined = stats ? Stats::historyOf(teamId, *stats) : Stats::TeamHistory{};
    auto addMetric = [&](const Stats::Metric &m) {
        const auto value = stats ? Stats::latestReading(m, Stats::defaultView(m), joined) : Stats::Reading{};
        missing |= !value.available;
        body.push_back(metricRow(m, p, value));
    };
    auto header = [&](const std::string &name) {
        if (!body.empty()) body.push_back(fe::spacer(p.pt(8)));
        body.push_back(fe::caption(name, false));
    };
    if (selectedTopic == Topic::Overview) {
        struct Section { const char *key; const char *first; const char *second; };
        const Section sections[] = {
            {"[stat page colony]", "population", "net growth"},
            {"[stat group food]", "hunger", "health"},
            {"[stats work construction]", "idle workers", "blocked buildings"},
            {"[stats defence]", "warriors", "intruders"}};
        for (const auto &s : sections) {
            header(fe::tr(s.key));
            for (auto id : {s.first, s.second}) if (const auto *m = metric(id)) addMetric(*m);
            if (selectedMetric() == s.first || selectedMetric() == s.second) body.push_back(inlineHistory(p));
        }
    } else {
        auto previous = Stats::Group::Count;
        bool selectedGroup = false;
        for (const auto &m : catalog) if (topicFor(m.group) == selectedTopic) {
            if (previous != m.group) {
                if (selectedGroup) body.push_back(inlineHistory(p));
                header(fe::tr(Stats::groupKey(m.group)));
                previous = m.group; selectedGroup = false;
            }
            addMetric(m);
            selectedGroup |= selectedMetric() == m.id;
        }
        if (selectedGroup) body.push_back(inlineHistory(p));
        if (selectedTopic == Topic::Colony) body.push_back(unitDetails(p));
        if (selectedTopic == Topic::World && showChances) {
            header(fe::tr("[Win chance]"));
            for (const auto &chance : chances) body.push_back(fe::paragraph(chance.name + ": " + (chance.alive ? std::to_string(chance.permille / 10) + "%" : "—")));
        }
    }
    if (missing) body.push_back(fe::hint(fe::tr("[stats missing readings]")));
    content.push_back(fe::expanded(fe::scroll("stats/scroll/" + std::to_string(int(selectedTopic)),
        fe::column(std::move(body), {p.pt(3)}), {.shrinkToContent = false})));
    return fe::pad(p.pt(6), fe::column(std::move(content), {p.pt(6)}));
}

InGameStatisticsHistory::InGameStatisticsHistory(InGameStatisticsPanel &owner, std::string metricId) : panel(owner), id(std::move(metricId))
{
    if (const auto *m = panel.metric(id)) panel.views.try_emplace(id, Stats::defaultView(*m));
}
void InGameStatisticsHistory::setView(Stats::View view)
{
    if (const auto *m = panel.metric(id)) panel.views[id] = Stats::validView(*m, view);
    invalidate();
}
bool InGameStatisticsHistory::onEvent(const SDL_Event &event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_AC_BACK) { finish(0); return true; }
    return false;
}
void InGameStatisticsHistory::onUpdate(Uint32)
{
    if (observedStats != panel.history() || observedTeam != panel.team()) {
        if (observedTeam != panel.team()) hoverX = hoverY = -1;
        observedStats = panel.history(); observedTeam = panel.team(); invalidate();
    }
}
fe::Element InGameStatisticsHistory::build(const fe::Presentation &p)
{
    const auto *m = panel.metric(id);
    if (!m) return fe::column({fe::paragraph(fe::tr("[stats waiting]")), fe::button("stats/history/close", fe::tr("[Close]"), [this] { finish(0); })});
    const auto view = panel.views.at(id);
    std::vector<fe::Element> parts{fe::heading(TeamStatChart::title(*m))};
    if (!panel.teamName().empty()) parts.push_back(fe::paragraph(panel.teamName()));
    parts.push_back(fe::paragraph(reading(*m, view, panel.history(), panel.team())));
    std::vector<fe::Element> controls;
    if (Stats::canTotal(*m)) controls.push_back(fe::segments("stats/history/measure", {fe::tr("[stat view per minute]"), fe::tr("[stat view total]")}, view.total ? 1 : 0,
        [this](int n) { auto v = panel.views.at(id); v.total = n == 1; setView(v); }));
    if ((m->kind == Stats::Metric::Counter && !view.total) || m->smoothed) {
        std::vector<std::string> windows; int chosen = 0;
        for (int n = 0; n < 3; ++n) { windows.push_back(GAGCore::FormattableString(fe::tr("[stat view %0 min]")).arg(Stats::windowMinutes(Stats::RATE_WINDOWS[n]))); if (view.window == Stats::RATE_WINDOWS[n]) chosen = n; }
        controls.push_back(fe::segments("stats/history/window", windows, chosen, [this](int n) { auto v = panel.views.at(id); v.window = Stats::RATE_WINDOWS[n]; setView(v); }));
    }
    auto controlRow = fe::column(std::move(controls), {p.pt(6)});
    if (!p.shortLandscape()) parts.push_back(controlRow);
    fe::CanvasOptions options;
    options.accessibleText = TeamStatChart::title(*m) + ": " + TeamStatChart::axisTitle(*m, view);
    options.hover = [this](fe::Point pt) { hoverX = pt.x; hoverY = pt.y; };
    options.tap = [this](fe::Point pt, fe::Host &) { hoverX = pt.x; hoverY = pt.y; };
    parts.push_back(fe::canvas("stats/history/chart", {p.pt(560), p.pt(p.shortLandscape() ? 150 : 280)}, [this](fe::Canvas &c, fe::Rect r, const fe::Frame &) { paintChart(c, r); }, options));
    if (p.shortLandscape()) parts.push_back(controlRow);
    parts.push_back(fe::button("stats/history/details", fe::tr("[Details]"), [this] { detailsOpen = !detailsOpen; invalidate(); },
        {.selected = detailsOpen, .alignLeft = true, .icon = fe::uiIcon(fe::UIIcon::Info), .iconSize = 16}));
    if (detailsOpen) parts.push_back(fe::paragraph(TeamStatChart::about(*m)));
    return fe::footer(fe::scroll("stats/history/body", fe::column(std::move(parts), {p.pt(8)})),
        dialogActions({{"stats/history/close", fe::tr("[Close]"), [this] { finish(0); }, false, SDLK_ESCAPE}}, p));
}
void InGameStatisticsHistory::paintChart(fe::Canvas &canvas, fe::Rect rect)
{
    auto *surface = canvas.surface();
    const auto *m = panel.metric(id);
    if (!surface || !m) return;
    canvas.fillRect(rect, TeamStatChart::background);
    canvas.pushClip(rect);
    if (const auto *stats = panel.history()) {
        const double unit = presentation().unit;
        // Legacy chart text and hit coordinates use the same point-space transform on both hosts.
        canvas.transformed(unit, {rect.x, rect.y}, rect, [&] {
            TeamStatChart::Options o;
            o.metric = m; o.view = panel.views.at(id);
            o.teams.push_back({panel.team(), panel.teamColor(), panel.teamName()});
            o.hoverX = hoverX < 0 ? -1 : int(hoverX / unit); o.hoverY = hoverY < 0 ? -1 : int(hoverY / unit);
            TeamStatChart::paint({Stats::historyOf(panel.team(), *stats)}, panel.tick(), *surface, 0, 0, int(rect.w / unit), int(rect.h / unit), o);
        });
    } else canvas.text({rect.x + 8, rect.y + 8}, fe::FontRole::Body, fe::tr("[stats waiting]"), TeamStatChart::ink);
    canvas.popClip();
}
