// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "Order.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GUIList.h>
#include <GUISelector.h>
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUITextInput.h>
#include <ResponsiveDialog.h>
#include "MobileSafeArea.h"
#include <Toolkit.h>
#include <StringTable.h>
#if defined(__IPHONEOS__)
#include "mobile/ios/SafeArea.h"
#endif
using namespace GAGCore;
using namespace GAGGUI;

OverlayScreen* GameGUITouch::activeDialog() const
{
    if (gui.gameMenuScreen) return gui.gameMenuScreen.get();
    if (gui.typingInputScreen) return gui.typingInputScreen;
    return gui.scrollableText;
}
void GameGUITouch::prepareDialog()
{
    auto* screen=activeDialog();
    if (dialogOwner!=screen) { dialogOwner=screen; dialogScroll=0; editingDialogWidget=nullptr; SDL_StopTextInput(); }
    dialogRows.clear();
    if (!screen) return;
    screen->updateLayout();
    auto widgets=screen->presentationWidgets();
    auto bounds=[](Widget* w) { auto* r=dynamic_cast<RectangularWidget*>(w); return r ? r->screenRectangle() : SDL_Rect{}; };
    std::stable_sort(widgets.begin(),widgets.end(),[&](Widget* a,Widget* b) {
        const auto x=bounds(a),y=bounds(b); return x.y==y.y ? x.x<y.x : x.y<y.y;
    });
    auto* alliance=dynamic_cast<InGameAllianceScreen*>(screen);
    for (auto* widget:widgets) {
        if (!widget->visible) continue;
        const auto rect=bounds(widget);
        DialogRow row{widget,""};
        if (auto* button=dynamic_cast<TextButton*>(widget)) {
            row.text=button->caption(); row.kind=1; row.footer=rect.y>=screen->getH()-60;
        } else if (auto* button=dynamic_cast<OnOffButton*>(widget)) {
            row.kind=2; row.selected=button->getState(); row.text=Toolkit::getStringTable()->getString("[Mute]");
            if (alliance) {
                const char* keys[]={"[abreaviation explanation A]","[abreaviation explanation V]","[abreaviation explanation fV]","[abreaviation explanation mV]","[abreaviation explanation C]"};
                for (int i=0;i<16;++i) {
                    OnOffButton* choices[]={alliance->alliance[i],alliance->normalVision[i],alliance->foodVision[i],alliance->marketVision[i],alliance->chat[i]};
                    for (int j=0;j<5;++j) if (choices[j]==widget && alliance->texts[i])
                        row.text=alliance->texts[i]->getText()+": "+Toolkit::getStringTable()->getString(keys[j]);
                }
            }
        } else if (auto* selector=dynamic_cast<Selector*>(widget)) {
            row.kind=3; row.text=std::to_string(selector->getValue());
        } else if (auto* input=dynamic_cast<TextInput*>(widget)) {
            row.kind=4; row.text=input->getText(); row.selected=input->isActivated();
            if (row.text.empty()) row.text="…";
        } else if (auto* list=dynamic_cast<List*>(widget)) {
            for (size_t i=0;i<list->getCount();++i) dialogRows.push_back({widget,list->getText(i),5,int(i),list->getSelectionIndex()==int(i)});
            continue;
        } else if (auto* text=dynamic_cast<Text*>(widget)) {
            if (alliance) continue; // Each alliance toggle carries its full player/action label.
            row.text=text->getText();
            for (auto* other:widgets) if (other->visible && bounds(other).y==rect.y)
                if (auto* state=dynamic_cast<TriButton*>(other)) row.text=(state->getState()==1 ? "[x] " : state->getState()==2 ? "[!] " : "[ ] ")+row.text;
        } else if (auto* text=dynamic_cast<TextArea*>(widget)) row.text=text->getText();
        else continue;
        dialogRows.push_back(row);
    }
    if (gui.inGameMenu==GameGUI::IGM_MAIN) {
        const char* labels[]={"[pause game]","[open chat box]","[Minimap]","[Statistics]","[view history]","[mark map]","[toggle draw information]"};
        for (int i=0;i<7;++i) if (!globalContainer->replaying || (i!=1 && i!=5))
            dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString(labels[i]),100,i});
        for(int i=0;i<3;++i) dialogRows.push_back({nullptr,
            Toolkit::getStringTable()->getString("[Dialog text size]")+std::string(": ")+std::to_string(100+i*25)+"%",
            100,90+i,globalContainer->settings.mobileDialogTextPercent==100+i*25});
        const char* overlays[]={"[starving]","[damage]","[Defense Map]","[Fertility Map]"};
        const bool states[]={gui.showStarvingMap,gui.showDamagedMap,gui.showDefenseMap,gui.showFertilityMap};
        for (int i=0;i<4;++i) dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString(overlays[i]),100,20+i,states[i]});
        if (globalContainer->replaying) {
            const char* replay[]={"[Fast forward]","[fog of war]","[combined vision]","[show areas]","[show flags]"};
            const bool values[]={globalContainer->replayFastForward,globalContainer->replayShowFog,globalContainer->replayVisibleTeams==0xffffffff,
                globalContainer->replayShowAreas,globalContainer->replayShowFlags};
            for (int i=0;i<5;++i) dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString(replay[i]),100,30+i,values[i]});
            for (int i=0;i<gui.game.teamsCount();++i) dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString("[watching:]")+std::string(" ")+std::to_string(i+1),100,40+i,gui.localTeamNo==i});
        }
    }
    if (gui.typingInputScreen) {
        dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString("[ok]"),101,0,false,true});
        dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString("[Cancel]"),101,1,false,true});
    } else if (gui.scrollableText) dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString("[ok]"),102,0,false,true});
    if (editingDialogWidget) dialogRows.push_back({nullptr,Toolkit::getStringTable()->getString("[Hide keyboard]"),103,0,false,true});
    const double unit=globalContainer->gfx->logicalUnitsPerPoint();
    const auto safe=mobileDialogSafe(globalContainer->gfx);
    const double textScale=1.2*globalContainer->settings.mobileDialogTextPercent/100.0;
    std::vector<bool> footer;for(const auto& row:dialogRows) footer.push_back(row.footer);
    auto height=[&](size_t index,double width) {
        const auto& row=dialogRows[index];
        const double textWidth=std::max(unit,row.kind==3 ? width-96*unit : width);
        return std::max(48*unit,pointLines((row.kind==2 ? (row.selected ? "[x] " : "[ ] ") : "")+row.text,textWidth,textScale).size()*16*textScale*unit+8*unit);
    };
    auto placement=ResponsiveDialog::calculate(safe,footer,height,dialogScroll*unit,unit);
    if(editingDialogWidget && lastDialogHeight!=placement.content.h)
        for(size_t i=0;i<dialogRows.size();++i) if(dialogRows[i].widget==editingDialogWidget) {
            const auto& rect=placement.rows[i].rect;
            double offset=placement.offset+std::max(0.0,rect.y+rect.h-placement.content.y-placement.content.h);
            offset=std::min(offset,placement.offset+rect.y-placement.content.y);
            placement=ResponsiveDialog::calculate(safe,footer,height,offset,unit);break;
        }
    dialogContent=placement.content;lastDialogHeight=dialogContent.h;
    dialogScroll=placement.offset/unit;dialogMaximum=placement.maximum/unit;
    for(size_t i=0;i<dialogRows.size();++i) {
        dialogRows[i].rect=placement.rows[i].rect;dialogRows[i].footer=placement.rows[i].footer;
    }

}
bool GameGUITouch::drawDialog()
{
    dialogHUDDrawn=usesHUD() && activeDialog();
    if (!dialogHUDDrawn) return false;
    prepareDialog();
    auto* gfx=globalContainer->gfx;
    const double unit=gfx->logicalUnitsPerPoint();
    const double textScale=1.2*globalContainer->settings.mobileDialogTextPercent/100.0;
    gfx->setClipRect(); gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),Color(10,16,24,245));
    for (const auto& row:dialogRows) {
        labelClip=row.footer ? std::optional<ViewRect>{} : dialogContent;
        const auto r=row.rect;
        if (!row.footer && (r.y+r.h<=dialogContent.y || r.y>=dialogContent.y+dialogContent.h)) continue;
        SDL_Rect clip{int(dialogContent.x),int(dialogContent.y),int(dialogContent.w),int(dialogContent.h)};
        if (!row.footer) gfx->setClipRect(clip.x,clip.y,clip.w,clip.h); else gfx->setClipRect();
        if (row.kind) gfx->drawFilledRect(int(r.x),int(r.y),int(r.w),int(r.h),row.selected ? Color(55,100,75) : Color(30,50,60));
        if (row.kind==3) {
            drawPointLabel({r.x,r.y,48*unit,r.h},"−",textScale);
            drawPointLabel({r.x+48*unit,r.y,r.w-96*unit,r.h},row.text,textScale);
            drawPointLabel({r.x+r.w-48*unit,r.y,48*unit,r.h},"+",textScale);
        } else drawPointLabel(r,(row.kind==2 ? (row.selected ? "[x] " : "[ ] ") : "")+row.text,textScale);
    }
    labelClip.reset(); gfx->setClipRect();
    if (dialogMaximum>0) {
        const double extent=dialogContent.h+dialogMaximum*unit;
        gfx->drawFilledRect(int(dialogContent.x+dialogContent.w-3*unit),int(dialogContent.y+dialogScroll*unit*dialogContent.h/extent),
            std::max(1,int(2*unit)),int(dialogContent.h*dialogContent.h/extent),Color(170,185,190));
    }
    return true;
}
void GameGUITouch::tapDialog(ViewPoint point)
{
    prepareDialog();
    auto* screen=activeDialog(); if (!screen) return;
    const double unit=globalContainer->gfx->logicalUnitsPerPoint();
    for (const auto row:dialogRows) {
        if (row.widget!=heldDialogWidget || row.index!=heldDialogIndex || row.kind!=heldDialogKind || !row.rect.contains(point) || (!row.footer && !dialogContent.contains(point)) || !row.kind) continue;
        if (row.kind==100) { menuAction(row.index); return; }
        if (row.kind==103) { editingDialogWidget=nullptr; SDL_StopTextInput(); return; }
        if (row.kind==101) {
            SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=row.index ? SDLK_ESCAPE : SDLK_RETURN;
            gui.processTypingInput(&event);
            delete gui.typingInputScreen; gui.typingInputScreen=nullptr; dialogOwner=nullptr; SDL_StopTextInput(); return;
        }
        if (row.kind==102) { delete gui.scrollableText; gui.scrollableText=nullptr; dialogOwner=nullptr; return; }
        if (row.kind==5) {
            auto* list=static_cast<List*>(row.widget);list->setSelectionIndex(row.index);list->selectionChanged();
        } else if (row.kind==3) {
            auto* selector=static_cast<Selector*>(row.widget);
            const int delta=point.x<row.rect.x+48*unit ? -1 : point.x>=row.rect.x+row.rect.w-48*unit ? 1 : 0;
            const int step=std::max(1,int(selector->maximumValue()/16));
            selector->setValue(std::clamp(int(selector->getValue())+delta*step,0,int(selector->maximumValue())));
            screen->onAction(selector,VALUE_CHANGED,selector->getValue(),0);
        } else if (row.kind==4) {
            for (auto* widget:screen->presentationWidgets()) if (auto* input=dynamic_cast<TextInput*>(widget)) input->deactivate();
            auto* input=static_cast<TextInput*>(row.widget);input->activate();input->setCursorPos(input->getText().size());
            editingDialogWidget=input; lastDialogHeight=-1; SDL_StartTextInput();
        } else {
            auto r=static_cast<RectangularWidget*>(row.widget)->screenRectangle();
            row.widget->activateAt(r.x+r.w/2,r.y+r.h/2);
        }
        SDL_Event event{};event.type=SDL_USEREVENT;
        if (gui.gameMenuScreen) gui.processGameMenu(&event);
        if (activeDialog()!=screen) { SDL_StopTextInput(); dialogOwner=nullptr; }
        return;
    }
}

void GameGUITouch::menuAction(int action)
{
    if(action>=90 && action<=92) {
        globalContainer->settings.mobileDialogTextPercent=100+(action-90)*25;
        lastDialogHeight=0;return;
    }
    gui.inGameMenu=GameGUI::IGM_NONE; gui.gameMenuScreen.reset(); dialogOwner=nullptr;
    if (action>=20 && action<24) {
        gui.displayMode=GameGUI::STAT_GRAPH_VIEW; gui.replayDisplayMode=GameGUI::RDM_STAT_GRAPH_VIEW; gui.clearSelection();
        gui.handleMenuClick(16,YPOS_BASE_STAT+140+(globalContainer->replaying ? 15 : 0)+72+(action-20)*24,SDL_BUTTON_LEFT);
        return;
    }
    if (globalContainer->replaying && action>=31) {
        gui.replayDisplayMode=GameGUI::RDM_REPLAY_VIEW; gui.clearSelection();
        const int y=action<40 ? REPLAY_PANEL_YOFFSET+(action-30)*REPLAY_PANEL_SPACE_BETWEEN_OPTIONS+10 :
            REPLAY_PANEL_YOFFSET+REPLAY_PANEL_PLAYERLIST_YOFFSET+(action-39)*REPLAY_PANEL_SPACE_BETWEEN_OPTIONS+10;
        gui.handleMenuClick(REPLAY_PANEL_XOFFSET+10,y,SDL_BUTTON_LEFT); return;
    }
    switch (action) {
        case 0:
            if (globalContainer->replaying) gui.gamePaused=!gui.gamePaused;
            else gui.orderQueue.push_back(std::make_shared<PauseGameOrder>(!gui.gamePaused)); break;
        case 1:
            gui.typingInputScreen=new InGameTextInput(globalContainer->gfx);
            gui.typingInputScreenInc=0; gui.typingInputScreenPos=TYPING_INPUT_MAX_POS;
            prepareDialog(); SDL_StartTextInput(); break;
        case 2: panelOpen=true; gui.clearSelection(); panelScroll=0; break;
        case 3: panelOpen=true; gui.clearSelection(); gui.replayDisplayMode=GameGUI::RDM_STAT_GRAPH_VIEW; gui.displayMode=GameGUI::STAT_GRAPH_VIEW; panelScroll=144; break;
        case 4: gui.scrollableText=gui.messageManager.createScrollableHistoryScreen(); break;
        case 5: gui.putMark=true; break;
        case 6: gui.drawHealthFoodBar=!gui.drawHealthFoodBar; break;
        case 30: globalContainer->replayFastForward=!globalContainer->replayFastForward; gui.gamePaused=false; break;
    }
}
