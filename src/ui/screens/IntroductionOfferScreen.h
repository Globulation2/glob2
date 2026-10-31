// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "GlobalContainer.h"
#include <ApplicationHost.h>

// Recommendation and preflight share layout, but only the recommendation saves
// a local preference. A failed persistence never blocks access to the game.
class IntroductionOfferScreen : public Glob2UI::Screen
{
    bool offer, failed=false;
    int pending=-1;
    std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
    void choose(int value)
    {
        if (persistence) return;
        if (!offer || failed) { endExecute(value); return; }
        pending=value;
        globalContainer->settings.introductionOffered=true;
        try {
            if (GAGCore::ApplicationHost::storageRestoreFailed() || !globalContainer->settings.save()) { failure(); return; }
            persistence=GAGCore::ApplicationHost::persistStorage();
            if (!persistence) failure();
        } catch (const std::exception &) { failure(); }
        invalidate();
    }
    void failure() { persistence.reset(); failed=true; invalidate(); }
  public:
    explicit IntroductionOfferScreen(bool firstRun) : offer(firstRun) {}
    Glob2UI::Element build(const Glob2UI::Presentation &p) override
    {
        using namespace Glob2UI;
        auto body=column({paragraph(tr("[intro preflight]")),hint(tr("[intro preflight save]")),
            failed ? paragraph(tr("[intro preference failed]")) : empty()});
        std::vector<MenuAction> choices;
        if (failed) choices.push_back({"intro/continue",tr("[continue]"),[this]{endExecute(pending);},true});
        else {
            choices.push_back({"intro/start",tr(offer ? "[intro try]" : "[intro start]"),[this]{choose(1);},true,SDLK_RETURN,!persistence});
            if (!offer) choices.push_back({"tutorial/lessons",tr("[tutorial lessons]"),[this]{choose(2);},false,SDLK_UNKNOWN,!persistence});
            choices.push_back({"intro/menu",tr("[goto main menu]"),[this]{choose(0);},false,SDLK_ESCAPE,!persistence});
        }
        return page(tr("[intro menu]"),scroll("intro/preflight",body),actions(std::move(choices),p),p,560);
    }
    void onTimer(Uint32) override
    {
        if (!persistence) return;
        const auto state=persistence->state();
        if (state==GAGCore::ApplicationHost::PersistenceState::Failed) failure();
        else if (state==GAGCore::ApplicationHost::PersistenceState::Succeeded) { persistence.reset(); endExecute(pending); }
    }
  protected:
    void onEscape() override { if (!persistence) choose(failed ? pending : 0); }
};
