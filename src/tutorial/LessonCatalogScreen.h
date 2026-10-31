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
    struct Entry { std::string map,title,briefing; };
    std::vector<Entry> entries;
    static Script::Value read(const std::string &path)
    {
        GAGCore::InputLineStream input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path));
        std::string text;
        while (!input.isEndOfStream()) {
            text+=input.readLine()+"\n";
            if(text.size()>Script::SourceLimit) throw std::runtime_error("Oversized lesson catalog");
        }
        return contentJson(text);
    }
    LessonCatalogScreen()
    {
        try {
            const auto catalog=read("scripts/tutorials/catalog.json");
            if(catalog.kind!=Script::Value::Array || catalog.items.size()>64) throw std::runtime_error("Invalid tutorial catalog");
            for(const auto &entry:catalog.items) {
                const auto path=entry.string("definition");
                if(!path.starts_with("scripts/tutorials/") || path.find("..")!=std::string::npos) throw std::runtime_error("Invalid lesson path");
                const auto definition=read(path);
                auto map=definition.string("map");
                if(!map.starts_with("campaigns/") || !map.ends_with(".map.gz") || map.find("..")!=std::string::npos) throw std::runtime_error("Invalid lesson map");
                entries.push_back({map,definition.string("title"),definition.string("briefing")});
            }
        } catch(const std::exception &) { entries.clear(); }
    }
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
