// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "TutorialModel.h"
#include <ui/PositionedControls.h>
#include <sstream>
#include <iomanip>
#include <limits>
#include <algorithm>
#include <functional>
#include <set>

class GameGUI;
namespace Tutorial
{
void drawDestination(GameGUI &gui);
// A nonmodal, client-owned card. No callbacks enter the scenario interpreter.
class Guidance : public Glob2UI::InGameDialog
{
    std::shared_ptr<const Model> lesson;
    bool collapsed = false, help = false;
    std::set<std::pair<SDL_TouchID,SDL_FingerID>> capturedFingers;
    std::set<std::pair<SDL_MouseID,Uint8>> capturedMouseButtons;
    Glob2UI::Rect mobileArea;
    std::function<void(int)> navigate;
  public:
    explicit Guidance(std::function<void(int)> navigate) : navigate(std::move(navigate)) {}
    void mobileBounds(Glob2UI::Rect area)
    {
        if (mobileArea == area) return;
        mobileArea=area; invalidate();
    }
    void model(std::shared_ptr<const Model> next)
    {
        if (lesson==next) return;
        if (!lesson || lesson->lessonId!=next->lessonId || lesson->stepId!=next->stepId) {
            collapsed=false; help=false;
        }
        else if (!next->hint.empty() && lesson->hint!=next->hint) collapsed=false;
        lesson=std::move(next);invalidate();
    }
    Glob2UI::Element build(const Glob2UI::Presentation &p) override
    {
        // Stable control keys belong to this lesson step. A newly published
        // destination or restart map must not change a queued command's meaning.
        std::ostringstream context;
        context << "tutorial:";
        if(lesson)
        {
            const auto field=[&](const std::string &value){context << value.size() << ':' << value;};
            field(lesson->lessonId);field(lesson->stepId);field(lesson->restartMap);
            context << ':' << lesson->version << ':' << lesson->index;
            if(lesson->destination)
                context << ':' << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << lesson->destination->x << ':' << lesson->destination->y;
        }
        return GAGGUI::ui::semanticContext(content(p),context.str());
    }
    Glob2UI::Element content(const Glob2UI::Presentation &p)
    {
        using namespace Glob2UI;
        if (!lesson) return empty();
        const int current=lesson->index;
        const auto &recovery=lesson->hint;
        const std::string title=lesson->complete() ? tr(lesson->completionTitle) :
            std::to_string(current+1)+" / "+std::to_string(lesson->count)+"  "+tr(lesson->title);
        if (p.touch && !lesson->complete()) {
            // Keep actions outside the scroller: finding Help or the map target
            // must never require scrolling through the instructions first.
            const auto action=[&](const char *key, const char *name, UIIcon icon, std::function<void()> callback) {
                return width(p.pt(44),button(key,"",std::move(callback),{
                    .flat=true,.minHeight=44,.tooltip=tr(name),.accessibleLabel=tr(name),
                    .icon=uiIcon(icon),.iconSize=18}));
            };
            auto heading=row({expanded(paragraph(title,{FontRole::Support,true})),
                lesson->destination ? action("intro/destination","[intro show destination]",UIIcon::Map,[this]{navigate(ShowDestination);}) : Element{},
                action("intro/help","[intro help]",UIIcon::Info,[this]{help=!help; collapsed=false; invalidate();}),
                width(p.pt(44),button("intro/collapse",collapsed ? "+" : "−",[this]{collapsed=!collapsed; help=false; invalidate();},
                    {.flat=true,.role=FontRole::Support,.minHeight=44}))}, {p.pt(2),CrossAlign::Center});
            if (collapsed) return heading;
            auto instruction=paragraph(tr(lesson->touchInstruction),{FontRole::Support});
            instruction->key="intro/instruction";
            if (!help) {
                if (recovery.empty()) return column({heading,instruction},{p.pt(4)});
                // Inactivity/recovery hints must be visible without discovering
                // Help first. Prioritize the hint; Help retains the instruction.
                auto hintText=paragraph(tr(recovery),{FontRole::Support});
                hintText->key="tutorial/hint";
                return column({heading,height(p.pt(p.shortLandscape() ? 72 : 120),
                    scroll("tutorial/hint/"+lesson->stepId+"/"+recovery,hintText))},{p.pt(4)});
            }
            auto explanation=paragraph(tr(lesson->touchHelp),{FontRole::Support});
            explanation->key="intro/help-text";
            std::vector<Element> details{instruction,explanation};
            if (!recovery.empty()) details.push_back(paragraph(tr(recovery),{FontRole::Support}));
            return column({heading,height(p.pt(p.shortLandscape() ? 96 : 180),
                scroll("intro/body/"+std::to_string(current),column(std::move(details),{p.pt(8)})))},{p.pt(4)});
        }
        auto heading = row({expanded(p.touch ? paragraph(title,{FontRole::Support,true}) : label(title,{FontRole::Body,true})),
            button("intro/collapse", collapsed ? "+" : "−", [this] { collapsed=!collapsed; invalidate(); }, {.minHeight=44})});
        if (collapsed) return heading;
        std::vector<Element> body;
        if (lesson->complete()) {
            const auto font=p.touch ? FontRole::Support : FontRole::Body;
            const ButtonOptions options{.role=font,.minHeight=44};
            body.push_back(paragraph(tr(lesson->completionBody),{font}));
            body.push_back(button("intro/replay", tr("[intro play again]"), [this] { navigate(0); },options));
            body.push_back(button("tutorial/lessons",tr("[tutorial lessons]"),[this]{navigate(Lessons);},options));
            body.push_back(button("intro/menu", tr("[goto main menu]"), [this] { navigate(MainMenu); },options));
        } else {
            body.push_back(paragraph(tr(lesson->instruction)));
            if (lesson->destination) body.push_back(button("intro/destination",tr("[intro show destination]"),[this]{navigate(ShowDestination);}));
            if (!recovery.empty()) body.push_back(paragraph(tr(recovery)));
            body.push_back(button("intro/help", tr("[intro help]"), [this] { help=!help; invalidate(); }));
            if (help) { auto text=paragraph(tr(p.touch ? lesson->touchHelp : lesson->help)); text->key="intro/help-text"; body.push_back(std::move(text)); }
        }
        return column({heading, height(p.pt(p.touch ? (p.safe.h>p.pt(560) ? 196 : 112) : 160), scroll("intro/body/"+std::to_string(current), column(std::move(body))))});
    }
    void cancelInput()
    {
        capturedFingers.clear();capturedMouseButtons.clear();
        Glob2UI::InGameDialog::cancelInput();
    }
    bool handle(const SDL_Event &e)
    {
        if (e.type==SDL_EVENT_KEY_DOWN && (e.key.key==SDLK_TAB ||
            (host().keyboardFocused() && (e.key.key==SDLK_RETURN || e.key.key==SDLK_SPACE))))
            return eventLogical(e);
        if (e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_ESCAPE) {
            if (help) { help=false; invalidate(); return true; }
            // Leaving the nonmodal card gives Return/Space back to gameplay.
            host().focus("");
        }
        if (e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
            capturedFingers.clear(); capturedMouseButtons.clear(); cancelInput(); host().focus(""); return false;
        }
        if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP) {
            const auto key=std::make_pair(e.tfinger.touchID,e.tfinger.fingerID);
            const GAGGUI::ui::Point point{int(e.tfinger.x*presentation().viewport.w),int(e.tfinger.y*presentation().viewport.h)};
            if (e.type==SDL_EVENT_FINGER_DOWN) {
                capturedFingers.erase(key);
                if (panelBounds().contains(point)) capturedFingers.insert(key);
                else host().focus("");
            }
            if (!capturedFingers.contains(key)) return false;
            eventLogical(e);
            if (e.type==SDL_EVENT_FINGER_UP) capturedFingers.erase(key);
            return true;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const auto key=std::make_pair(e.button.which,e.button.button);
            const GAGGUI::ui::Point point{int(e.button.x),int(e.button.y)};
            if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
                capturedMouseButtons.erase(key);
                if (panelBounds().contains(point)) capturedMouseButtons.insert(key);
                else host().focus("");
            }
            if (!capturedMouseButtons.contains(key)) return false;
            eventLogical(e);
            if (e.type==SDL_EVENT_MOUSE_BUTTON_UP) capturedMouseButtons.erase(key);
            return true;
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            const bool captured=std::any_of(capturedMouseButtons.begin(),capturedMouseButtons.end(),
                [&](const auto &key){return key.first==e.motion.which;});
            // Hover belongs to the card, but a drag started on the map keeps
            // its whole sequence even when it crosses the card.
            if (!captured && (e.motion.state || !panelBounds().contains(GAGGUI::ui::Point{int(e.motion.x),int(e.motion.y)}))) return false;
            eventLogical(e); return true;
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL && panelBounds().contains(GAGGUI::ui::Point{int(e.wheel.mouse_x),int(e.wheel.mouse_y)})) {
            eventLogical(e); return true;
        }
        return false;
    }
  protected:
    GAGGUI::ui::Rect available(const Glob2UI::Presentation &p, const GAGGUI::ui::Metrics &m) override
    {
        if (p.touch && mobileArea.w>0) {
            auto r=mobileArea.inset(m.padding);
            r.w=std::min(r.w,p.pt(420)-2*m.padding);
            return r;
        }
        auto r=p.safe.inset(p.pt(8)+m.padding);
        r.y+=p.pt(p.touch ? 152 : 32);
        r.h=std::max(p.pt(48),p.safe.y+p.safe.h-r.y-p.pt(p.touch ? 100 : 16));
        r.w=std::min(r.w-p.pt(p.touch ? 0 : 176),p.pt(420));
        return r;
    }
    GAGGUI::ui::Rect place(GAGGUI::ui::Size size, GAGGUI::ui::Rect area) override
    { return {area.x,area.y,area.w,std::min(size.h,area.h)}; }
};
}
