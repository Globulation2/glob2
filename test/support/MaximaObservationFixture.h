// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <utility>

namespace glob2test
{
// Direct policy fixtures have the same immutable read boundary as a poll.
// Callers mutate live fixture state between invocations, never inside this phase.
template<class Context, class Function>
decltype(auto) withMaximaObservation(Context& context, Function&& function)
{
    auto observation=context.scopeOwnerObservation();
    return std::forward<Function>(function)();
}
}
