// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared_runtime/RuntimeObservation.h"
#include "Player.h"
#include "Game.h"

namespace AIMaximaRuntime
{
// Live ownership exists only for construction/load and synchronous diagnostics.
// During an engine decision every query resolves through the borrowed records.
class ObservationBinding
{
    ::Player* owner;
    mutable std::shared_ptr<const AIEngine::AIWorldView> view;
    mutable std::shared_ptr<const AISharedRuntime::Read::Catalog> catalog;
    mutable std::shared_ptr<const AIEngine::AIWorldView::Catalog> catalogSource;
    mutable std::unique_ptr<AISharedRuntime::Read::World> world;
    mutable AISharedRuntime::Read::Player value;
    AISharedRuntime::Read::Player* borrowed=nullptr;
public:
    explicit ObservationBinding(::Player* player):owner(player) {}
    AISharedRuntime::Read::Player* get() const
    {
        if(borrowed)return borrowed;
        if(!world){view=AIEngine::AIWorldView::capture(*owner->game,AIEngine::AIWorldView::captureCatalog(*owner->game));bindOwned(view,owner->number,owner->team->teamNumber);}
        return &value;
    }
    ::Player* getOwner() const {return owner;}
    void bindOwned(std::shared_ptr<const AIEngine::AIWorldView> observed,unsigned player,unsigned team) const
    {
        view=std::move(observed);
        if(!catalog || catalogSource!=view->catalog) {
            catalogSource=view->catalog;
            catalog=std::make_shared<const AISharedRuntime::Read::Catalog>(*catalogSource);
        }
        world=std::make_unique<AISharedRuntime::Read::World>(*view,catalog);
        value={player,world.get(),world->teams[team],&world->map};
    }
    void borrow(AISharedRuntime::Read::Player* player){borrowed=player;world.reset();view.reset();}
    void clear(){borrowed=nullptr;world.reset();view.reset();value={};}
    void refreshOwner(){if(!borrowed){clear();get();}}
};
}
