// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace Script { struct Value; }
namespace SimulationSnapshot { struct Session; }
namespace Tutorial
{
// Immutable data published by the scenario. No live game or UI pointers.
struct Destination { double x=0,y=0; std::string label; };
struct Model
{
    std::string lessonId,stepId,restartMap,title,instruction,touchInstruction,help,touchHelp;
    std::string completionTitle,completionBody,hint;
    int version=1,index=0,count=0;
    std::vector<std::string> highlights;
    std::optional<Destination> destination;
    bool complete() const { return index==count; }
};
inline constexpr int MainMenu=702, ShowDestination=703, Lessons=704;
inline constexpr const char *IntroductionMap="campaigns/introduction.map.gz";
// Strict validation is part of the map callback's atomic effect transaction.
Script::Value contentJson(const std::string &text);
std::shared_ptr<const Model> parse(const Script::Value &value);
// Only a scenario's published presentation can activate tutorial guidance.
std::shared_ptr<const Model> view(const SimulationSnapshot::Session &session);
}
