// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "TutorialModel.h"
#include "scripting/javascript/ScriptValue.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <Stream.h>
#include <stdexcept>
namespace Tutorial
{
// The same packaged catalog used by the bundler, not a C++ list of lessons.
class LessonCatalogScreen : public Glob2UI::Screen
{
  public:
    const char *recordingId() const override { return "tutorial_catalog"; }
    std::vector<CatalogEntry> entries=lessonCatalog();
    Glob2UI::Element build(const Glob2UI::Presentation &p) override
    {
        using namespace Glob2UI;
        std::vector<Element> rows;
        if(entries.empty()) rows.push_back(paragraph(tr("[tutorial unavailable]")));
        for(size_t i=0;i<entries.size();++i) rows.push_back(column({
            button("tutorial/lesson/"+std::to_string(i),tr(entries[i].title),[this,i]{endExecute(int(i)+1);}),
            hint(tr(entries[i].briefing))}));
        return page(tr("[tutorial lessons]"),scroll("tutorial/catalog",column(std::move(rows))),
            actions({{"tutorial/back",tr("[goto main menu]"),[this]{endExecute(0);},false,SDLK_ESCAPE}},p),p,640);
    }
  protected:
    void onEscape() override { endExecute(0); }
};
}
