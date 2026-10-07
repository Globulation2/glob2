// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

// Saved integer cost budgets. Parsing is outside the query/build hot paths;
// workers receive only the selected scalar budget, never this model or a team.
class BuildingGradientBudgetPolicy
{
    using Key = std::vector<std::string>;
    std::array<std::map<Key, int>, 3> levels;
public:
    static constexpr unsigned MaximumBytes = 1024 * 1024;
    explicit BuildingGradientBudgetPolicy(const std::string& source);
    int predict(const std::string& type, unsigned units, unsigned buildings,
                int route, int swim) const;
};
