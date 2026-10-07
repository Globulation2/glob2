// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AICabino.h"
#include "Player.h"

namespace glob2test
{
// Direct module fixtures borrow the same data-only projection as production.
// Refreshing the scope lets each assertion observe deliberate owner edits.
template<class Invoke>
decltype(auto) withCabinoObservation(Cabino::AICabino& ai, Game& game, Invoke invoke)
{
    const auto view=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
    const auto number=ai.player->teamNumber;
    ai.game=view.get();ai.map=view.get();ai.team=&view->teams[number];
    struct Reset { Cabino::AICabino& ai; ~Reset(){ai.game=nullptr;ai.team=nullptr;ai.map=nullptr;} } reset{ai};
    return invoke();
}
}
