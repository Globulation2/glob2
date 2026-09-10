// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>

using namespace GAGCore;

SettingsScreen::SettingsScreen() : gameKeys(GameGUIShortcuts), editorKeys(MapEditShortcuts) {}
SettingsScreen::~SettingsScreen()
{
    if (modal==Modal::Display) confirmDisplay(false);
    commitText();
    if (settingsDirty || keyboardDirty[0] || keyboardDirty[1]) persist();
    SDL_StopTextInput();
}

std::string SettingsScreen::tr(const std::string& text)
{
    return Toolkit::getStringTable()->getString("[settings "+text+"]");
}

SettingsScreen::Row& SettingsScreen::add(const std::string& id,Kind kind,const std::string& label,const std::string& help)
{
    Row r; r.id=id; r.kind=kind; r.label=label; r.help=help;
    form.push_back(std::move(r)); return form.back();
}
void SettingsScreen::section(const std::string& label) { add("",Kind::Section,tr(label)); }
void SettingsScreen::info(const std::string& label) { add("",Kind::Info,label); }
void SettingsScreen::button(const std::string& id,const std::string& label,std::function<void()> action,bool selected)
{
    auto& r=add(id,Kind::Button,label); r.action=std::move(action); r.selected=selected;
}
void SettingsScreen::choice(const std::string& id,const std::string& label,const std::string& help,int value,
                           std::vector<std::string> labels,std::function<void(int)> change)
{
    auto& r=add(id,Kind::Choice,tr(label),help.empty()?"":tr(help)); r.number=value;
    r.choices=std::move(labels); r.change=std::move(change);
    if(value>=0 && value<int(r.choices.size())) r.value=r.choices[value];
}
void SettingsScreen::toggle(const std::string& id,const std::string& label,const std::string& help,bool value,std::function<void(int)> change)
{
    auto& r=add(id,Kind::Toggle,tr(label),help.empty()?"":tr(help));r.number=value;r.change=std::move(change);
}
void SettingsScreen::number(const std::string& id,const std::string& label,int value,int minimum,int maximum,std::function<void(int)> change)
{
    auto& r=add(id,Kind::Number,label);r.number=value;r.minimum=minimum;r.maximum=maximum;r.change=std::move(change);r.value=std::to_string(value);
}

void SettingsScreen::buildRows()
{
    form.clear();
    if(modal!=Modal::None) buildModal();
    else if(current==Category::Buildings) buildBuildings();
    else if(current==Category::Controls) buildKeyboard();
    else buildGeneral();
}
const std::vector<SettingsScreen::Row>& SettingsScreen::rows() { layout(); buildRows(); layout(); return form; }
bool SettingsScreen::changeSetting(const std::string& id,int value)
{
    buildRows();
    for(auto r:form) if(r.id==id && r.enabled && r.change) {
        if(r.kind==Kind::Choice && (value<0 || value>=int(r.choices.size())))return false;
        if(r.kind==Kind::Number || r.kind==Kind::Slider)value=std::clamp(value,r.minimum,r.maximum);
        if(r.kind==Kind::Toggle)value=!!value;
        r.change(value); return true;
    }
    return false;
}
void SettingsScreen::activateSetting(const std::string& id)
{
    buildRows(); layout(); for(auto r:form) if(r.enabled) {
        if(r.id==id){focus=id;invoke(r);return;}
        if(!r.extraId.empty() && r.extraId==id){focus=id;r.change(0);return;}
    }
}
void SettingsScreen::selectCategory(Category category)
{
    dropdown.close();
    if(modal==Modal::Display)confirmDisplay(false);
    commitText();finishInteraction();current=category;modal=Modal::None;focus.clear();
}
void SettingsScreen::commit(bool defer)
{
    settingsDirty=true;
    if(defer) saveAt=SDL_GetTicks()+300;
    else persist();
}
bool SettingsScreen::persist()
{
    saveAt=0;
    try {
        // Write unconditionally, not only when locally dirty: a close with
        // nothing edited must still flush to durable storage (a prior browser
        // storage-restore failure can leave already-committed settings
        // unflushed), and every other caller only reaches persist() with at
        // least one dirty flag already set, so this adds no redundant I/O there.
        if(globalContainer->settings.save()) settingsDirty=false;
        if(gameKeys.saveKeyboardLayout()) keyboardDirty[0]=false;
        if(editorKeys.saveKeyboardLayout()) keyboardDirty[1]=false;
        failed=settingsDirty || keyboardDirty[0] || keyboardDirty[1];
        // The write above is already durable on native builds. In the browser it
        // lands in Emscripten's virtual filesystem first and needs this separate
        // flush to survive a reload; poll it from onTimer rather than block here.
        if(!failed && !persistence) {
            if(GAGCore::ApplicationHost::storageRestoreFailed()) failed=true;
            else {
                persistence=GAGCore::ApplicationHost::persistStorage();
                if(!persistence) failed=true;
            }
        }
    } catch(const std::exception&) { failed=true; persistence.reset(); }
    return !failed;
}
void SettingsScreen::finishInteraction() { dragging.clear(); if(settingsDirty || keyboardDirty[0] || keyboardDirty[1]) persist(); }
void SettingsScreen::commitText()
{
    if(!editingText)return;
    editingText=false;SDL_StopTextInput();
    if(textDraft!=globalContainer->settings.getUsername()) {
        globalContainer->settings.setUsername(textDraft);commit();
    }
}
void SettingsScreen::done()
{
    dropdown.close();
    if(modal==Modal::Display) confirmDisplay(false);
    commitText();
    dragging.clear();
    // Always confirm durability on close, not just when something in this
    // session is dirty: a prior browser storage-restore failure can leave
    // already-committed settings unflushed.
    persist();
    // Keep Retry available instead of silently losing local shortcut edits.
    if(!failed)
    {
        if(persistence) closing=true;
        else endExecute(1);
    }
}
void SettingsScreen::onAction(Widget*,Action action,int,int)
{
    if(action==SCREEN_DESTROYED) {
        if(modal==Modal::Display) confirmDisplay(false);
        commitText();finishInteraction();
    }
}
void SettingsScreen::onTimer(Uint32 tick)
{
    if(modal==Modal::Display && Sint32(tick-displayDeadline)>=0) confirmDisplay(false);
    if(saveAt && dragging.empty() && Sint32(tick-saveAt)>=0)persist();
    if(persistence) {
        const auto state=persistence->state();
        if(state!=GAGCore::ApplicationHost::PersistenceState::Pending) {
            if(state==GAGCore::ApplicationHost::PersistenceState::Failed) {
                failed=true;
                closing=false;
                // Retry the whole write, not just the flush: the file itself
                // may also need rewriting if this state was reached because
                // the browser evicted storage mid-session.
                settingsDirty=keyboardDirty[0]=keyboardDirty[1]=true;
                saveAt=tick+300;
            }
            persistence.reset();
            if(closing && !failed) { closing=false; endExecute(1); }
        }
    }
}
bool SettingsScreen::displayConfirmationPending() const { return modal==Modal::Display; }
bool SettingsScreen::restartRequired() const
{
    const auto& s=globalContainer->settings;auto* g=globalContainer->gfx;
    const Uint32 mask=GraphicContext::USEGPU|GraphicContext::FULLSCREEN|GraphicContext::CUSTOMCURSOR;
    return (s.screenFlags & mask)!=(g->getOptionFlags() & mask) ||
           s.screenWidth!=g->getW() || s.screenHeight!=g->getH();
}
bool SettingsScreen::applyDisplayMode(int width,int height,Uint32 flags)
{
    return globalContainer->gfx->setRes(width,height,flags);
}
void SettingsScreen::changeDisplay(std::function<void(Settings&)> change)
{
    auto& s=globalContainer->settings;
    Settings candidate=s;change(candidate);displayError=false;
    if(candidate.screenWidth==s.screenWidth && candidate.screenHeight==s.screenHeight && candidate.screenFlags==s.screenFlags)return;
    // A GPU context cannot switch to a software surface in place.
    if((candidate.screenFlags|globalContainer->gfx->getOptionFlags()) & GraphicContext::USEGPU) {
        s=candidate;commit();return;
    }
    previousDisplay=s;
    if(!applyDisplayMode(candidate.screenWidth,candidate.screenHeight,candidate.screenFlags)) {
        applyDisplayMode(s.screenWidth,s.screenHeight,s.screenFlags);
        displayError=true;return;
    }
    if(candidate.screenWidth==s.screenWidth && candidate.screenHeight==s.screenHeight &&
       ((candidate.screenFlags^s.screenFlags)&GraphicContext::FULLSCREEN)==0) {
        s=candidate;commit();return;
    }
    // Keep the persisted preferences unchanged until the player accepts the mode.
    picker.number=candidate.screenWidth;picker.maximum=candidate.screenHeight;
    picker.minimum=int(candidate.screenFlags);
    modal=Modal::Display;modalScroll=0;focus="display.keep";displayDeadline=SDL_GetTicks()+15000;
}
void SettingsScreen::confirmDisplay(bool keep)
{
    if(modal!=Modal::Display)return;
    auto& s=globalContainer->settings;
    if(keep) {
        s.screenWidth=picker.number;s.screenHeight=picker.maximum;s.screenFlags=Uint32(picker.minimum);commit();
    } else if(!applyDisplayMode(previousDisplay.screenWidth,previousDisplay.screenHeight,previousDisplay.screenFlags)) {
        displayError=true;
    }
    modal=Modal::None;focus.clear();
}
int SettingsScreen::menu() { return SettingsScreen().execute(globalContainer->gfx,30); }
