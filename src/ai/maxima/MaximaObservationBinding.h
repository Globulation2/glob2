// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ai/observation/ObservationQueries.h"
#include "Player.h"
#include "Game.h"

namespace AIMaximaRuntime
{
// A scoped borrow of canonical records. No simulation entity is reconstructed.
class ObservationBinding
{
    ::Player* owner;
    mutable std::shared_ptr<const AIEngine::AIWorldView> owned;
    const AIEngine::AIWorldView* borrowed=nullptr;
    mutable unsigned playerId=0,teamId=0;
public:
    explicit ObservationBinding(::Player* player):owner(player) {}
    const AIEngine::AIWorldView& get() const
    {
        if(borrowed) return *borrowed;
        if(!owned) throw std::logic_error("Maxima query requires a decision or owner observation scope");
        return *owned;
    }
    unsigned teamNumber() const {return teamId;}
    unsigned playerNumber() const {return playerId;}
    ::Player* getOwner() const {return owner;}
    void bindOwned(std::shared_ptr<const AIEngine::AIWorldView> observed,unsigned player,unsigned team)
    {owned=std::move(observed);borrowed=nullptr;playerId=player;teamId=team;}
    void borrow(const AIEngine::AIWorldView& world,unsigned player,unsigned team)
    {borrowed=&world;owned.reset();playerId=player;teamId=team;}
    void clear(){borrowed=nullptr;owned.reset();}
};
}
