// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "FertilityCalculator.h"
#include "FertilityField.h"
#include "Map.h"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
namespace FertilityCalculator {
struct Job::State {
    Map& map;
    std::optional<Fertility::Field> field;
    bool committed = false;
    explicit State(Map& map) : map(map) {}
};
Job::Job(Map& map) : state(std::make_unique<State>(map)) {}
Job::~Job() = default;
bool Job::ready() const { return state->field.has_value(); }
bool Job::advance(std::size_t operations) {
    if (operations && !ready()) state->field = Fertility::forMap(state->map);
    return ready();
}
float Job::progress() const { return ready() ? 1.f : 0.f; }
void Job::commit() {
    if (!ready()) throw std::logic_error("Cannot commit incomplete fertility");
    if (state->committed) return;
    auto& map = state->map;
    Uint16 maximum = 0;
    for (int x = 0; x < map.getW(); ++x)
        for (int y = 0; y < map.getH(); ++y) {
            const auto value = static_cast<Uint16>(std::min(state->field->at(x,y),
                std::uint32_t(std::numeric_limits<Uint16>::max())));
            map.getTile(x,y).fertility = value;
            maximum = std::max(maximum, value);
        }
    map.fertilityMaximum = maximum;
    state->committed = true;
}
void compute(Map& map, const ProgressCallback& progress) {
    if (progress) progress(0.f);
    Job job(map);
    job.advance(1);
    job.commit();
    if (progress) progress(1.f);
}
}
