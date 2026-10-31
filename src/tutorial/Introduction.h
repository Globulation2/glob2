// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GameObjectives.h"
#include "GameHints.h"
#include "sim/snapshot/ObservationRecords.h"
#include <array>
#include <string>

// Compatibility keys for the original embedded prototype, plus its launch path.
// New lessons use TutorialModel and scripts/tutorials/*.json.
namespace Introduction
{
inline constexpr const char *Map = "campaigns/introduction.map.gz";
inline constexpr const char *Briefing = "[intro briefing]";
inline constexpr std::array<const char*, 5> Goals = {
    "[intro goal scout]", "[intro goal build]", "[intro goal supply]",
    "[intro goal grow]", "[intro goal attack]"};
inline constexpr std::array<const char*, 5> Instructions = {
    "[intro instruction scout]", "[intro instruction build]", "[intro instruction supply]",
    "[intro instruction grow]", "[intro instruction attack]"};
inline constexpr std::array<const char*, 5> Help = {
    "[intro help scout]", "[intro help build]", "[intro help supply]",
    "[intro help grow]", "[intro help attack]"};
inline constexpr std::array<const char*, 5> TouchHelp = {
    "[intro help touch scout]", "[intro help touch build]", "[intro help touch supply]",
    "[intro help touch grow]", "[intro help touch attack]"};
inline constexpr std::array<const char*, 5> TouchInstructions = {
    "[intro compact scout]", "[intro compact build]", "[intro compact supply]",
    "[intro compact grow]", "[intro compact attack]"};
inline bool active(const SimulationSnapshot::Session &s)
{
    if (!s.missionBriefing || *s.missionBriefing != Briefing || !s.objectives ||
        s.objectives->getNumberOfObjectives() != int(Goals.size())) return false;
    for (int i=0; i<int(Goals.size()); ++i)
        if (s.objectives->getGameObjectiveText(i) != Goals[i]) return false;
    return true;
}
inline int stage(const SimulationSnapshot::Session &s)
{
    if (!active(s)) return -1;
    for (int i=0; i<int(Goals.size()); ++i)
        if (!s.objectives->isObjectiveComplete(i)) return i;
    return int(Goals.size());
}
}
