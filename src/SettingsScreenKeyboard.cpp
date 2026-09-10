// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>

using namespace GAGCore;
KeyboardManager& SettingsScreen::keyboard() { return shortcutMode==GameGUIShortcuts?gameKeys:editorKeys; }
std::string SettingsScreen::bindingLabel(const KeyboardShortcut& shortcut) const
{
    std::string text;
    for(size_t i=0;i<shortcut.getKeyPressCount();++i){
        if(i)text+=" → ";
        text+=shortcut.getKeyPress(i).getTranslated();
        if(!shortcut.getKeyPress(i).getPressed())text+=" ("+tr("Release")+")";
    }
    return text;
}
void SettingsScreen::buildKeyboard()
{
    auto& s=globalContainer->settings;
    toggle("controls.cursor","Use game cursor","Display the Globulation 2 cursor.",s.screenFlags & GraphicContext::CUSTOMCURSOR,
        [this](int v){changeDisplay([v](Settings& s){if(v)s.screenFlags|=GraphicContext::CUSTOMCURSOR;else s.screenFlags&=~GraphicContext::CUSTOMCURSOR;});});
    toggle("controls.wheel","Scroll wheel enabled",
        "Adjust assigned units with the wheel. When off, hold Ctrl. Shift adjusts flag radius. Also controls wheel input in other menus.",s.scrollWheelEnabled,[this](int v){
            globalContainer->settings.scrollWheelEnabled=v;GAGGUI::Screen::scrollWheelEnabled=v;commit();
        });
    section("Keyboard shortcuts");
    for(int i=0;i<2;++i){
        button("keys.mode."+std::to_string(i),tr(i?"Map editor":"Game"),[this,i]{shortcutMode=i?MapEditShortcuts:GameGUIShortcuts;},int(shortcutMode)==i);
        form.back().columns=2;form.back().column=i;
    }
    info(tr("Choose a binding to change it, or add another shortcut for an action."));
    button("keys.restore",tr("Restore default shortcuts"),[this]{modal=Modal::Restore;modalScroll=0;focus="restore.cancel";});
    auto& bindings=keyboard().getKeyboardShortcuts();
    const int count=shortcutMode==GameGUIShortcuts?int(GameGUIKeyActions::ActionSize):int(MapEditKeyActions::ActionSize);
    for(int action=0;action<count;++action){
        auto name=shortcutMode==GameGUIShortcuts?GameGUIKeyActions::getName(action):MapEditKeyActions::getName(action);
        auto label=Toolkit::getStringTable()->getString("["+name+"]");
        int index=0;bool found=false;
        for(const auto& binding:bindings){
            if(binding.getAction()==Uint32(action)){
                const int selected=index;
                auto& r=add("keys.binding."+std::to_string(index),Kind::Binding,label);
                r.value=bindingLabel(binding);r.action=[this,selected,action]{editBinding(selected,action);};
                // The extra button is a distinct focus target but shares the row.
                if(!found){r.extraId="keys.add."+std::to_string(action);r.change=[this,action](int){editBinding(-1,action);};}
                found=true;
            }++index;
        }
        if(!found){auto& r=add("keys.add."+std::to_string(action),Kind::Binding,label);
            r.value=tr("Unbound");r.action=[this,action]{editBinding(-1,action);};}
    }

}
void SettingsScreen::editBinding(int index,Uint32 action)
{
    bindingIndex=index;bindingAction=action;bindingKeys.clear();
    if(index>=0){auto it=keyboard().getKeyboardShortcuts().begin();std::advance(it,index);
        for(size_t i=0;i<it->getKeyPressCount();++i)bindingKeys.push_back(it->getKeyPress(i));
    }
    if(bindingKeys.empty())bindingKeys.push_back(KeyPress());
    modal=Modal::Binding;modalScroll=0;captureKey=0;bindingAdvanced=bindingKeys.size()>1;
    returnFocus=focus;focus="binding.key.0";
}
void SettingsScreen::saveBinding(bool replace)
{
    for(const auto& k:bindingKeys)if(k.getKey()=="no key")return;
    if(bindingKeys.empty())return;
    KeyboardShortcut proposed;proposed.setAction(bindingAction);
    for(const auto& k:bindingKeys)proposed.addKeyPress(k);
    auto& list=keyboard().getKeyboardShortcuts();
    if(!replace){
        conflicts.clear();int index=0;
        for(const auto& existing:list){
            if(index!=bindingIndex){
                size_t n=std::min(existing.getKeyPressCount(),proposed.getKeyPressCount());bool match=n>0;
                for(size_t i=0;i<n;++i)match=match && existing.getKeyPress(i)==proposed.getKeyPress(i);
                if(match)conflicts.push_back(index);
            }++index;
        }
        if(!conflicts.empty()){modal=Modal::Conflict;modalScroll=0;captureKey=-1;focus="conflict.cancel";return;}
    }
    int index=0;
    for(auto it=list.begin();it!=list.end();){
        if(index==bindingIndex){*it=proposed;++it;}
        else if(std::find(conflicts.begin(),conflicts.end(),index)!=conflicts.end())it=list.erase(it);
        else ++it;
        ++index;
    }
    if(bindingIndex<0)list.push_back(proposed);
    keyboardDirty[int(shortcutMode)]=true;persist();closeModal();
}
void SettingsScreen::deleteBinding()
{
    auto& list=keyboard().getKeyboardShortcuts();
    if(bindingIndex>=0 && bindingIndex<int(list.size())){auto it=list.begin();std::advance(it,bindingIndex);list.erase(it);
        keyboardDirty[int(shortcutMode)]=true;persist();}
    closeModal();
}
void SettingsScreen::closeModal()
{
    modal=Modal::None;modalScroll=0;captureKey=-1;focus=returnFocus;
}
void SettingsScreen::buildModal()
{
    if(modal==Modal::Display){
        info(tr("Keep this display mode?"));
        info(tr("Reverting in")+" "+std::to_string(std::max(0,int(Sint32(displayDeadline-SDL_GetTicks())+999)/1000))+" "+tr("seconds"));
        button("display.keep",tr("Keep"),[this]{confirmDisplay(true);});
        button("display.revert",tr("Revert"),[this]{confirmDisplay(false);});return;
    }
    if(modal==Modal::Restore){
        info(tr("Replace shortcuts in this context with the defaults? This saves immediately."));
        button("restore.confirm",tr("Restore default shortcuts"),[this]{keyboard().loadDefaultShortcuts();keyboardDirty[int(shortcutMode)]=true;persist();closeModal();});
        button("restore.cancel",tr("Cancel"),[this]{closeModal();});return;
    }
    if(modal==Modal::Conflict){
        info(tr("This shortcut conflicts with existing bindings:"));
        int index=0;for(const auto& b:keyboard().getKeyboardShortcuts()){
            if(std::find(conflicts.begin(),conflicts.end(),index)!=conflicts.end())info(b.formatTranslated(shortcutMode));++index;
        }
        button("conflict.replace",tr("Replace conflicting bindings"),[this]{saveBinding(true);});
        button("conflict.cancel",tr("Cancel"),[this]{modal=Modal::Binding;modalScroll=0;focus="binding.save";});return;
    }
    info(tr("Edit shortcut"));
    const auto name=shortcutMode==GameGUIShortcuts?GameGUIKeyActions::getName(bindingAction):MapEditKeyActions::getName(bindingAction);
    info(Toolkit::getStringTable()->getString("["+name+"]"));
    if(captureKey>=0)info(tr("Press a key. Escape cancels capture. Use Bind Escape to assign Escape."));
    for(size_t i=0;i<bindingKeys.size();++i){
        const std::string label=tr("Key")+" "+std::to_string(i+1)+": "+(bindingKeys[i].getKey()=="no key"?tr("Unbound"):bindingKeys[i].getTranslated());
        button("binding.key."+std::to_string(i),label,[this,i]{captureKey=int(i);},captureKey==int(i));
        if(bindingAdvanced){
            choice("binding.trigger."+std::to_string(i),"Trigger","",!bindingKeys[i].getPressed(),{tr("Press"),tr("Release")},[this,i](int v){bindingKeys[i]=KeyPress(bindingKeys[i],!v);});
            button("binding.remove."+std::to_string(i),tr("Remove key"),[this,i]{bindingKeys.erase(bindingKeys.begin()+i);captureKey=-1;});
        }
    }
    if(captureKey>=0)button("binding.escape",tr("Bind Escape"),[this]{SDL_Keysym k{};k.sym=SDLK_ESCAPE;bindingKeys[captureKey]=KeyPress(k,bindingKeys[captureKey].getPressed());captureKey=-1;});
    button("binding.advanced",tr("Advanced binding")+(bindingAdvanced?" −":" +"),[this]{bindingAdvanced=!bindingAdvanced;},bindingAdvanced);
    if(bindingAdvanced){
        info(tr("Keys form a sequence in order. Each key can trigger on press or release."));
        button("binding.addkey",tr("Add key to sequence"),[this]{bindingKeys.push_back(KeyPress());captureKey=int(bindingKeys.size())-1;focus="binding.key."+std::to_string(captureKey);});
    }
    button("binding.save",tr("Save shortcut"),[this]{saveBinding();});
    bool valid=!bindingKeys.empty();for(const auto& k:bindingKeys)valid &= k.getKey()!="no key";form.back().enabled=valid;
    if(bindingIndex>=0)button("binding.delete",tr("Remove shortcut"),[this]{deleteBinding();});
    button("binding.cancel",tr("Cancel"),[this]{closeModal();});
}
