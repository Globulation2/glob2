// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include "BuildingType.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>

using namespace GAGCore;

void GameGUITouch::setTutorialSource(std::string_view source)
{
    const int recognized = TouchTutorial::chapter(source);
    tutorialChapter = recognized;
    tutorialCursors.clear();
    const auto &stories = gui.game.sgslScript.presentationStories();
    if (recognized && stories.size() == 1)
    {
        int ordinal = 0;
        const TouchTutorial::Message *message = nullptr;
        const auto &line = stories[0].line;
        for (size_t n = 0; n < line.size(); ++n)
        {
            if (line[n].type == SGSLToken::S_SHOW && n + 2 < line.size())
            {
                if (line[n + 2].type != SGSLToken::LANG)
                {
                    ++ordinal;
                    int count = 0;
                    for (const auto &entry : TouchTutorial::messages())
                        if (entry.chapter == recognized && ++count == ordinal) { message = &entry; break; }
                }
                tutorialCursors.emplace_back(int(n + 1), message);
            }
            else if (line[n].type == SGSLToken::S_HIDE)
                tutorialCursors.emplace_back(int(n + 1), nullptr);
        }
    }
    tutorialOriginal.clear();
    tutorialMessage = nullptr;
    tutorialPage = 0;
    tutorialAcknowledged = false;
    tutorialCollapsed = false;
    tutorialText.clear();
    tutorialWidth = 0;
}
const TouchTutorial::Message *GameGUITouch::tutorialCursorMessage() const
{
    const int cursor = gui.drawnScene().panels.hud.state().legacyScriptCursor;
    const TouchTutorial::Message *message = nullptr;
    for (const auto &[at, entry] : tutorialCursors)
    {
        if (at > cursor) break;
        message = entry;
    }
    return message;
}
void GameGUITouch::tutorialAcknowledgmentChanged(bool value)
{
    // SGSL can show identical translated text at two successive Space waits.
    // Watch the FIFO acknowledgment cycle, even when several simulation ticks
    // are consumed between draws; text comparison alone would soft-lock it.
    if (value && !tutorialWasSwallowing && tutorialAcknowledged)
    {
        // A repeated source string still represents a new script message.
        if (usesHUD() && tutorialMessage) gui.previousSGSLText.clear();
        tutorialAcknowledged = false;
        tutorialPage = 0;
        tutorialWidth = 0;
        tutorialScroll = 0;
        tutorialCollapsed = false;
    }
    tutorialWasSwallowing = value;
}
std::string GameGUITouch::tutorialPageText(const TouchTutorial::Page &page) const
{
    // Until selection, compact guidance describes the ring. On selection the
    // real fit policy chooses ring versus rows; this can change on rotation.
    const bool rows = inspecting() ? !usesDial() : layout().persistentPanel;
    return Toolkit::getStringTable()->getString(rows && *page.rowsKey ? page.rowsKey : page.key);
}
std::string GameGUITouch::tutorialHistoryText(const std::string &original) const
{
    if (!gui.scriptText.empty()) return original;
    const auto *cursorMessage = tutorialCursorMessage();
    const auto *message = usesHUD() ? (original.empty() || TouchTutorial::matches(cursorMessage, original)
        ? cursorMessage : TouchTutorial::find(tutorialChapter, original)) : nullptr;
    if (!message) return original;
    std::string result;
    for (const auto &page : message->pages)
    {
        if (!result.empty()) result += '\n';
        result += tutorialPageText(page);
    }
    return result;
}
std::string GameGUITouch::tutorialHistoryIdentity(const std::string &original) const
{
    if (!gui.scriptText.empty()) return original;
    const auto *cursorMessage = tutorialCursorMessage();
    const auto *message = usesHUD() ? (original.empty() || TouchTutorial::matches(cursorMessage, original)
        ? cursorMessage : TouchTutorial::find(tutorialChapter, original)) : nullptr;
    return message ? message->id : original;
}
double GameGUITouch::tutorialFooterHeight() const
{
    return (tutorialMessage || gui.swallowSpaceKey) ? 48 * globalContainer->gfx->logicalUnitsPerPoint() : 0;
}
ViewRect GameGUITouch::tutorialBackRect() const
{
    if (!tutorialMessage || tutorialPage == 0 || tutorialCollapsed) return {};
    const auto rect = tutorialRect();
    return {rect.x, rect.y + rect.h - tutorialFooterHeight(), rect.w / 2, tutorialFooterHeight()};
}
ViewRect GameGUITouch::tutorialNextRect() const
{
    const auto rect = tutorialRect(), back = tutorialBackRect();
    return {rect.x + back.w, rect.y + rect.h - tutorialFooterHeight(), rect.w - back.w, tutorialFooterHeight()};
}
bool GameGUITouch::tapTutorial(ViewPoint point)
{
    const auto rect = tutorialRect();
    if (!rect.contains(point)) return false;
    if (tutorialCollapsed) { tutorialCollapsed = false; return true; }
    const double target = 48 * globalContainer->gfx->logicalUnitsPerPoint();
    if (point.x >= rect.x + rect.w - target && point.y < rect.y + target)
    { tutorialCollapsed = true; return true; }
    if (tutorialBackRect().contains(point))
    {
        --tutorialPage;
        tutorialWidth = 0;
        tutorialScroll = 0;
        return true;
    }
    if (!tutorialNextRect().contains(point) || tutorialAcknowledged) return true;
    if (tutorialMessage && tutorialPage + 1 < tutorialMessage->pages.size())
    {
        ++tutorialPage;
        tutorialWidth = 0;
        tutorialScroll = 0;
    }
    else if (gui.swallowSpaceKey && !gui.isSpaceSet())
    {
        tutorialAcknowledged = true;
        SDL_KeyboardEvent key{};
        key.key = SDLK_SPACE;
        gui.handleKey(key, true);
    }
    return true;
}

std::vector<ViewRect> GameGUITouch::tutorialTargets() const
{
    using Target = TouchTutorial::Target;
    if (!tutorialMessage || tutorialAcknowledged || activeDialog() || peekOpen || statsOpen) return {};
    const auto &page = tutorialMessage->pages[tutorialPage];
    const auto ui = layout();
    const double unit = globalContainer->gfx->logicalUnitsPerPoint();
    auto toolbar = [&](int button) {
        // Placement and painting replace the toolbar. Never highlight their
        // buttons as if they were Build/Flags/Menu.
        if (gui.selectionMode == GameGUI::TOOL_SELECTION || gui.selectionMode == GameGUI::BRUSH_SELECTION)
            return std::vector<ViewRect>{};
        return std::vector<ViewRect>{{ui.actions.x + button * ui.actions.w / 6, ui.actions.y, ui.actions.w / 6, ui.actions.h}};
    };
    switch (page.target)
    {
    case Target::None: return {};
    case Target::Menu: return toolbar(5);
    case Target::Build: return toolbar(0);
    case Target::Flags: return toolbar(1);
    case Target::Tools: return toolbar(2);
    case Target::Statistics: return {hudLayout(ui).stats};
    case Target::History:
        if (lensVisible())
        {
            const auto choices = lenses();
            const auto boxes = lensRects(ui);
            for (size_t i = 0; i < choices.size(); ++i)
                if (choices[i].action == 4) return {boxes[i]};
        }
        else if (panelOpen && gui.displayMode == GameGUI::STAT_TEXT_VIEW && !inspecting())
        {
            const auto choices = tacticalActions();
            const auto content = panelContent();
            for (size_t i = 0; i < choices.size(); ++i)
                if (choices[i].second == 4)
                    return {{content.x, content.y + (i * 48 - panelScroll) * unit, content.w, 48 * unit}};
        }
        return toolbar(2);
    case Target::Brush:
        if (gui.selectionMode == GameGUI::BRUSH_SELECTION) return {brushHUD().rail};
        return toolbar(1);
    case Target::Palette:
    {
        if (gui.selectionMode == GameGUI::TOOL_SELECTION) return {confirmRect()};
        if (gui.selectionMode == GameGUI::BRUSH_SELECTION && std::string_view(page.choice).starts_with("zone:"))
            return {brushHUD().mode};
        const bool flag = std::string_view(page.choice).find("flag") != std::string_view::npos ||
                          std::string_view(page.choice).starts_with("zone:");
        if ((panelOpen || ui.persistentPanel) && showsBuildPalette() &&
            gui.displayMode == (flag ? GameGUI::FLAG_VIEW : GameGUI::CONSTRUCTION_VIEW))
        {
            const auto items = paletteItems();
            const auto content = panelContent();
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].enabled && (items[i].name == page.choice ||
                    items[i].name.find(std::string(page.choice) + ".") != std::string::npos))
                {
                    const auto rect = paletteItemRect(i);
                    // An entirely visible entry gets its own outline; otherwise
                    // outline the scroll viewport without changing its offset.
                    if (rect.x >= content.x && rect.y >= content.y &&
                        rect.x + rect.w <= content.x + content.w && rect.y + rect.h <= content.y + content.h)
                        return {rect};
                    return {content};
                }
        }
        return toolbar(flag ? 1 : 0);
    }
    default: break;
    }
    const auto *building = inspectedBuilding();
    const bool matches = building && (!*page.choice || building->type->type == page.choice);
    if (matches)
    {
        if (page.target == Target::Inspector) return {allocationRect()};
        const int kind = page.target == Target::Workers ? 6 : page.target == Target::Production ? 0 :
                         page.target == Target::Range ? 8 : 3;
        std::vector<ViewRect> result;
        if (usesDial())
        {
            for (const auto &region : dialRegions())
                if (region.action.kind == kind) result.push_back(region.box);
        }
        else
        {
            const auto rows = buildingActions();
            const auto content = panelContent();
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].kind == kind)
                {
                    const auto rect = buildingActionRect(i);
                    if (content.contains({rect.x + rect.w / 2, rect.y + rect.h / 2})) result.push_back(rect);
                    else return {content};
                }
        }
        return result;
    }
    if (page.target == Target::Inspector && inspectingReadOnly()) return {allocationRect()};
    // Opening an inspector means tapping its world object. Read only the
    // published Scene, never live entities or simulation caches.
    const auto &frame = gui.drawnScene();
    if (!*page.choice || !frame.buildingTypes) return {};
    for (const auto &b : frame.entities.buildings)
    {
        if (b.team != gui.localTeamNo || b.typeNum < 0 || size_t(b.typeNum) >= frame.buildingTypes->size()) continue;
        const auto &type = frame.buildingTypes->at(b.typeNum);
        if (type.type != page.choice) continue;
        auto wrap = [](double x, double extent) { return x - std::floor(x / extent) * extent; };
        const double x = wrap((b.posX - gui.viewportX + type.width / 2.0) * 32, frame.map.getW() * 32);
        const double y = wrap((b.posY - gui.viewportY + type.height / 2.0) * 32, frame.map.getH() * 32);
        const ViewPoint center{(x - gui.camera.fractionX()) * gui.camera.zoom + gui.camera.offsetX,
                               (y - gui.camera.fractionY()) * gui.camera.zoom + gui.camera.offsetY};
        if (world().contains(center) && interfaceRegion(center) == 0)
            return {{center.x - 24 * unit, center.y - 24 * unit, 48 * unit, 48 * unit}};
    }
    return {minimapRect()};
}
void GameGUITouch::drawTutorialHighlights()
{
    if (!usesHUD()) return;
    const auto targets = tutorialTargets();
    const auto safe = layout().safe;
    auto *gfx = globalContainer->gfx;
    gfx->setClipRect(int(safe.x), int(safe.y), int(safe.w), int(safe.h));
    const double unit = gfx->logicalUnitsPerPoint();
    const Color gold(235, 192, 90);
    const auto *building=inspectedBuilding();
    const auto &page=tutorialMessage ? tutorialMessage->pages[tutorialPage] : TouchTutorial::Page{};
    const auto target=page.target;
    const int kind=target==TouchTutorial::Target::Workers?6:target==TouchTutorial::Target::Production?0:
        target==TouchTutorial::Target::Range?8:target==TouchTutorial::Target::Upgrade?3:-1;
    const bool rings=kind>=0 && building && (!*page.choice || building->type->type==page.choice) &&
        usesDial() && !targets.empty();
    auto outline=[&](const ViewRect &rect) {
        for (int inset=0; inset<std::max(2,int(2*unit)); ++inset)
            if(rect.w>2*inset && rect.h>2*inset)
                gfx->drawRect(int(rect.x)+inset,int(rect.y)+inset,
                    int(rect.w)-2*inset,int(rect.h)-2*inset,gold);
    };
    // The card owns its pixels as well as its input. Do not draw highlights
    // through it when a constrained row panel sits underneath.
    const auto card=tutorialRect();
    std::vector<ViewRect> clips{safe};
    if(card.w>0 && card.h>0)
        clips={{safe.x,safe.y,safe.w,card.y-safe.y},
               {safe.x,card.y+card.h,safe.w,safe.y+safe.h-card.y-card.h},
               {safe.x,card.y,card.x-safe.x,card.h},
               {card.x+card.w,card.y,safe.x+safe.w-card.x-card.w,card.h}};
    for(const auto &clip:clips)
    {
        if(clip.w<=0 || clip.h<=0) continue;
        gfx->setClipRect(int(clip.x),int(clip.y),int(clip.w),int(clip.h));
        if(rings)
        {
            const auto dial=dialLayout(layout());
            for(const auto &region:dialRegions())
                if(region.action.kind==kind)
                {
                    if(region.ring<0) {outline(region.box);continue;}
                    const auto &ring=dial.geometry.rings[region.ring];
                    dialPainter.fill(dial.geometry,ring.inner,ring.inner+2,region.from,region.to,gold);
                    dialPainter.fill(dial.geometry,ring.outer-2,ring.outer,region.from,region.to,gold);
                }
        }
        else for(const auto &rect:targets) outline(rect);
    }
    gfx->setClipRect();
}
