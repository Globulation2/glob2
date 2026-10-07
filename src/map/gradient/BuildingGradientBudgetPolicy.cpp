// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingGradientBudgetPolicy.h"
#include "field/GradientCosts.h"
#include <nlohmann/json.hpp>
#include <bit>
#include <stdexcept>

BuildingGradientBudgetPolicy::BuildingGradientBudgetPolicy(const std::string& source)
{
    if (source.size() > MaximumBytes) throw std::invalid_argument("building gradient model is too large");
    const auto model = nlohmann::json::parse(source);
    const auto& tables = model.at("levels");
    if (!tables.is_array() || tables.size() != levels.size())
        throw std::invalid_argument("building gradient model requires three fallback levels");
    for (unsigned level = 0; level < levels.size(); ++level)
        for (const auto& row : tables[level])
        {
            const auto& features = row.at("features");
            const unsigned length = level == 0 ? 6 : level == 1 ? 4 : 3;
            if (!features.is_array() || features.size() != length)
                throw std::invalid_argument("invalid building gradient model features");
            Key key;
            for (unsigned i = 0; i < length; ++i)
            {
                if (level < 2 && i == 0) key.push_back(features[i].get<std::string>());
                else
                {
                    if (!features[i].is_number_integer()) throw std::invalid_argument("noninteger gradient model feature");
                    key.push_back(std::to_string(features[i].get<int>()));
                }
            }
            if (!row.at("cost").is_number_integer()) throw std::invalid_argument("noninteger gradient budget");
            const int cost = row.at("cost").get<int>();
            if (cost < 0 || cost > gradient_kernel::COST_LIMIT || !levels[level].emplace(key, cost).second)
                throw std::invalid_argument("invalid or duplicate building gradient budget");
        }
}

int BuildingGradientBudgetPolicy::predict(const std::string& type, unsigned units,
                                         unsigned buildings, int route, int swim) const
{
    const Key field{std::to_string(route), std::to_string(swim), "-1"};
    Key typed{type}; typed.insert(typed.end(), field.begin(), field.end());
    Key specific{type, std::to_string(std::bit_width(units)), std::to_string(std::bit_width(buildings))};
    specific.insert(specific.end(), field.begin(), field.end());
    const std::array<Key, 3> keys{specific, typed, field};
    for (unsigned level = 0; level < levels.size(); ++level)
        if (const auto it = levels[level].find(keys[level]); it != levels[level].end()) return it->second;
    return 0; // Unseen destinations retain lazy expansion rather than guessing.
}
