// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "stats/MetricCatalog.h"
#include "render/scene/Scene.h"
#include <map>
#include <set>

class GameGUI;

// Presentation-only navigation. Catalog groups and metric IDs remain unchanged.
class InGameStatisticsPanel : public Glob2UI::InGameDialog
{
public:
    enum class Topic { Overview, Colony, Economy, Military, World };
    explicit InGameStatisticsPanel(GameGUI &gui);
    const char *recordingId() const override { return "in_game_statistics"; }
    Glob2UI::Element build(const Glob2UI::Presentation &p) override;
    void sync(const PresentationFrame &frame, int team, bool watching, bool chances);
    void setBounds(GAGGUI::ui::Rect bounds);
    void drawEmbedded(GAGCore::DrawableSurface &surface, Uint32 tick);
    void selectTopic(Topic value);
    Topic topic() const { return selectedTopic; }
    static Topic topicFor(Stats::Group group);
    void selectMetric(const std::string &id);
    std::string selectedMetric() const;
    void openMetric(const std::string &id);
    void restoreFocus();
    bool routeEvent(const SDL_Event &event);
    void suspend();
    void cancelEmbeddedInput();
    std::map<std::string, Stats::View> views;
    const std::vector<Stats::Metric> &metrics() const { return catalog; }
    const Stats::Metric *metric(const std::string &id) const;
    const TeamStats *history() const;
    int team() const { return teamId; }
    Uint32 tick() const { return sampleTick; }
    std::string teamName() const { return watchedName; }
    GAGCore::Color teamColor() const { return color; }
protected:
    GAGGUI::ui::Rect available(const Glob2UI::Presentation &, const GAGGUI::ui::Metrics &) override { return bounds; }
    GAGGUI::ui::Rect place(GAGGUI::ui::Size, GAGGUI::ui::Rect area) override { return area; }
    void paintPanel(GAGGUI::ui::Canvas &, GAGGUI::ui::Rect) override;
private:
    GameGUI &gui;
    Topic selectedTopic = Topic::Overview;
    std::map<Topic, std::string> selectedMetrics{{Topic::Overview, "population"}};
    int inlineHoverX = -1, inlineHoverY = -1;
    bool detailsOpen = false, watching = false, showChances = false;
    int teamId = -1;
    Uint32 sampleTick = 0;
    GAGGUI::ui::Rect bounds;
    std::shared_ptr<const SimulationSnapshot::History> recorded;
    std::shared_ptr<const TeamStats> latest;
    std::shared_ptr<const std::vector<BuildingType>> types;
    std::vector<Stats::Metric> catalog;
    std::vector<SceneHud::WinChance> chances;
    std::string watchedName, returnKey;
    std::set<SDL_FingerID> ownedFingers;
    bool mouseHeld = false;
    Uint64 scrollSequence = 0;
    GAGCore::Color color;
    Glob2UI::Element metricRow(const Stats::Metric &metric, const Glob2UI::Presentation &p, const Stats::Reading &value);
    Glob2UI::Element inlineHistory(const Glob2UI::Presentation &p);
    Glob2UI::Element unitDetails(const Glob2UI::Presentation &p);
};

class InGameStatisticsHistory : public Glob2UI::InGameDialog
{
public:
    InGameStatisticsHistory(InGameStatisticsPanel &panel, std::string metricId);
    const char *recordingId() const override { return "in_game_statistics_history"; }
    Glob2UI::Element build(const Glob2UI::Presentation &p) override;
    void setView(Stats::View view);
protected:
    void onEscape() override { finish(0); }
    void onUpdate(Uint32) override;
    bool onEvent(const SDL_Event &event) override;
    double maxWidth() const override { return 720; }

private:
    InGameStatisticsPanel &panel;
    std::string id;
    bool detailsOpen = false;
    const TeamStats *observedStats = nullptr;
    int observedTeam = -1, hoverX = -1, hoverY = -1;
    void paintChart(GAGGUI::ui::Canvas &canvas, GAGGUI::ui::Rect rect);
};
