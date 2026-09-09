// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <algorithm>

using namespace GAGCore;
namespace {
size_t previousChar(const std::string& s,size_t at) { if(!at)return 0;--at;while(at && (static_cast<unsigned char>(s[at])&0xc0)==0x80)--at;return at; }
size_t nextChar(const std::string& s,size_t at) { if(at==s.size())return at;++at;while(at<s.size() && (static_cast<unsigned char>(s[at])&0xc0)==0x80)++at;return at; }
}
void SettingsScreen::invoke(Row row,int direction)
{
    if(!row.enabled)return;
    if(row.kind!=Kind::Text)commitText();
    if(row.action){row.action();buildRows();layout();ensureFocusVisible();return;}
    if(row.kind==Kind::Toggle && row.change)row.change(!row.number);
    else if(row.kind==Kind::Choice){
        finishInteraction();focus=row.id;ensureFocusVisible();layout();
        for(const auto& r:form)if(r.id==row.id)row.control=r.control;
        const auto c=row.control;
        dropdownChange=row.change;
        dropdown.open({c.x,c.y,c.w,c.h},{viewport.x,viewport.y,viewport.w,viewport.h},row.choices,row.number,Toolkit::getFont("standard"));
    }else if(row.kind==Kind::Number && row.change){row.change(std::clamp(row.number+(direction?direction:1),row.minimum,row.maximum));}
    else if(row.kind==Kind::Slider && row.change){row.change(std::clamp(row.number+(direction?direction:1)*3,row.minimum,row.maximum));}
    else if(row.kind==Kind::Text){
        if(!editingText){textDraft=globalContainer->settings.getUsername();editingText=true;textCursor=textDraft.size();selectAllText=false;SDL_StartTextInput();}
    }
}
void SettingsScreen::ensureFocusVisible()
{
    for(const auto& r:form)if(r.id==focus || (!r.extraId.empty() && r.extraId==focus)){
        if(r.control.y<viewport.y)scrollOffset()-=viewport.y-r.control.y;
        else if(r.control.y+r.control.h>viewport.y+viewport.h)scrollOffset()+=r.control.y+r.control.h-viewport.y-viewport.h;
        scrollOffset()=std::clamp(scrollOffset(),0,std::max(0,contentHeight-viewport.h));break;
    }
}
void SettingsScreen::openCategoryPicker()
{
    Row r;r.id="nav.current";r.control=categoryControl;r.kind=Kind::Choice;r.label=tr("Settings");r.number=int(current);
    for(auto name:{"Display & graphics","Audio","Gameplay","Building defaults","Controls","Language & player"})r.choices.push_back(tr(name));
    r.change=[this](int v){selectCategory(Category(v));};invoke(r);
}
void SettingsScreen::focusNext(bool backward)
{
    commitText();finishInteraction();
    std::vector<std::string> ids;
    if(modal==Modal::None){
        if(compactNavigation)ids.push_back("nav.current");
        else for(int i=0;i<6;++i)ids.push_back("nav."+std::to_string(i));
    }
    for(const auto& r:form)if(!r.id.empty() && r.enabled){ids.push_back(r.id);if(!r.extraId.empty())ids.push_back(r.extraId);}
    if(failed && modal==Modal::None)ids.push_back("retry");ids.push_back("done");
    auto it=std::find(ids.begin(),ids.end(),focus);int at=it==ids.end()?(backward?0:-1):int(it-ids.begin());
    at=(at+int(ids.size())+(backward?-1:1))%int(ids.size());focus=ids[at];ensureFocusVisible();
}
void SettingsScreen::adjustSlider(const std::string& id,int x)
{
    for(auto r:form)if(r.id==id && r.enabled && r.change){
        int v=(x-r.control.x)*r.maximum/std::max(1,r.control.w-8);
        r.change(std::clamp(v,r.minimum,r.maximum));return;
    }
}
void SettingsScreen::onSDLEvent(SDL_Event* event)
{
    layout();buildRows();layout();
    if(dropdown.isOpen()){
        int selected=dropdown.handleEvent(*event);
        if(selected>=0){auto change=dropdownChange;change(selected);buildRows();layout();ensureFocusVisible();}
        else if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_TAB)focusNext(event->key.keysym.mod & KMOD_SHIFT);
        return;
    }
    auto dismiss=[this]{
        if(modal==Modal::Display)confirmDisplay(false);
        else if(modal==Modal::Conflict){modal=Modal::Binding;modalScroll=0;}
        else if(modal!=Modal::None)closeModal();else done();
    };
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST){commitText();finishInteraction();captureKey=-1;return;}
    if(event->type==SDL_MOUSEWHEEL){
        // Deliberately bypass Screen::scrollWheelEnabled for this settings form.
        int x,y;SDL_GetMouseState(&x,&y);GraphicContext::translateMouseCoordinates(x,y);
        if(viewport.contains(x,y) || scrollbar.contains(x,y)){
            int delta=event->wheel.y*(event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1);
            scrollOffset()=std::clamp(scrollOffset()-delta*48,0,std::max(0,contentHeight-viewport.h));
        }return;
    }
    if(event->type==SDL_MOUSEMOTION){
        if(!dragging.empty())adjustSlider(dragging,event->motion.x);
        if(scrollingBar){
            const int size=std::max(24,viewport.h*viewport.h/std::max(1,contentHeight));
            scrollOffset()=std::clamp((event->motion.y-viewport.y-scrollbarGrab)*(contentHeight-viewport.h)/std::max(1,viewport.h-size),0,std::max(0,contentHeight-viewport.h));
        }
        return;
    }
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){
        if(!dragging.empty()){adjustSlider(dragging,event->button.x);finishInteraction();}
        scrollingBar=false;return;
    }
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT){
        int x=event->button.x,y=event->button.y;
        if(y>=footer.y && y<footer.y+footer.h){
            commitText();finishInteraction();
            if(x>=footer.x+footer.w-112){dismiss();return;}
            if(failed && modal==Modal::None && x>=footer.x+footer.w-208){persist();return;}
        }
        if(modal==Modal::None && compactNavigation && categoryControl.contains(x,y)){focus="nav.current";openCategoryPicker();return;}
        if(modal==Modal::None && !compactNavigation && x>=panel.x && x<panel.x+sidebar){
            const char* names[]={"Display & graphics","Audio","Gameplay","Building defaults","Controls","Language & player"};
            int ny=panel.y+76;
            for(int i=0;i<6;++i){int height=std::max(42,wrappedHeight(tr(names[i]),sidebar-32)+20);
                if(y>=ny && y<ny+height){selectCategory(Category(i));focus="nav."+std::to_string(i);return;}ny+=height+4;
            }
        }
        if(scrollbar.contains(x,y) && contentHeight>viewport.h){
            const int size=std::max(24,viewport.h*viewport.h/contentHeight);
            const int top=viewport.y+scrollOffset()*(viewport.h-size)/(contentHeight-viewport.h);
            scrollingBar=true;scrollbarGrab=y>=top && y<top+size?y-top:size/2;
            scrollOffset()=std::clamp((y-viewport.y-scrollbarGrab)*(contentHeight-viewport.h)/std::max(1,viewport.h-size),0,contentHeight-viewport.h);return;
        }
        if(!viewport.contains(x,y)){commitText();return;}
        for(auto r:form)if(r.enabled && !r.id.empty() && r.bounds.contains(x,y)){
            if(r.id!=focus)commitText();focus=r.id;
            if(r.kind==Kind::Toggle){invoke(r);return;}
            if(!r.control.contains(x,y))return;
            if(r.kind==Kind::Binding && !r.extraId.empty() && x>=r.control.x+r.control.w-36){focus=r.extraId;r.change(0);return;}
            if(r.kind==Kind::Slider){dragging=r.id;adjustSlider(r.id,x);return;}
            int direction=0;
            if(r.kind==Kind::Number){int segment=std::min(32,r.control.w/4);
                if(x<r.control.x+segment)direction=-1;else if(x>=r.control.x+r.control.w-segment)direction=1;else return;
            }
            invoke(r,direction);return;
        }
        commitText();return;
    }
    if(event->type==SDL_TEXTINPUT && editingText){
        std::string insert=event->text.text;
        if(selectAllText){textDraft.clear();textCursor=0;selectAllText=false;}
        if(textDraft.size()+insert.size()<=BasePlayer::MAX_NAME_LENGTH){textDraft.insert(textCursor,insert);textCursor+=insert.size();}return;
    }
    if(event->type!=SDL_KEYDOWN)return;
    SDL_Keycode key=event->key.keysym.sym;bool shift=event->key.keysym.mod & KMOD_SHIFT;
    if(modal==Modal::Binding && captureKey>=0){
        if(key==SDLK_ESCAPE){captureKey=-1;return;}
        if(key==SDLK_LSHIFT || key==SDLK_RSHIFT || key==SDLK_LCTRL || key==SDLK_RCTRL || key==SDLK_LALT || key==SDLK_RALT || key==SDLK_LGUI || key==SDLK_RGUI)return;
        bindingKeys[captureKey]=KeyPress(event->key.keysym,bindingKeys[captureKey].getPressed());captureKey=-1;return;
    }
    if(key==SDLK_ESCAPE){if(editingText){editingText=false;SDL_StopTextInput();}else dismiss();return;}
    if(key==SDLK_TAB){focusNext(shift);return;}
    if(editingText){
        const bool command=event->key.keysym.mod & (KMOD_CTRL|KMOD_GUI);
        if(command && key==SDLK_a){selectAllText=true;return;}
        if(command && (key==SDLK_c || key==SDLK_x) && selectAllText){SDL_SetClipboardText(textDraft.c_str());if(key==SDLK_x){textDraft.clear();textCursor=0;selectAllText=false;}return;}
        if(command && key==SDLK_v){char* clip=SDL_GetClipboardText();std::string pasted=clip?clip:"";SDL_free(clip);
            pasted.erase(std::remove_if(pasted.begin(),pasted.end(),[](char c){return c=='\n'||c=='\r';}),pasted.end());
            if(selectAllText){textDraft.clear();textCursor=0;selectAllText=false;}
            if(textDraft.size()+pasted.size()<=BasePlayer::MAX_NAME_LENGTH){textDraft.insert(textCursor,pasted);textCursor+=pasted.size();}return;
        }
        if(key==SDLK_BACKSPACE || key==SDLK_DELETE){
            if(selectAllText){textDraft.clear();textCursor=0;selectAllText=false;}
            else if(key==SDLK_BACKSPACE && textCursor){size_t prev=previousChar(textDraft,textCursor);textDraft.erase(prev,textCursor-prev);textCursor=prev;}
            else if(key==SDLK_DELETE && textCursor<textDraft.size())textDraft.erase(textCursor,nextChar(textDraft,textCursor)-textCursor);
        }else if(key==SDLK_LEFT){textCursor=previousChar(textDraft,textCursor);selectAllText=false;}
        else if(key==SDLK_RIGHT){textCursor=nextChar(textDraft,textCursor);selectAllText=false;}
        else if(key==SDLK_HOME){textCursor=0;selectAllText=false;}
        else if(key==SDLK_END){textCursor=textDraft.size();selectAllText=false;}
        else if(key==SDLK_RETURN || key==SDLK_KP_ENTER)commitText();
        return;
    }
    if(key==SDLK_PAGEUP || key==SDLK_PAGEDOWN){scrollOffset()=std::clamp(scrollOffset()+(key==SDLK_PAGEUP?-1:1)*viewport.h,0,std::max(0,contentHeight-viewport.h));return;}
    if(key==SDLK_UP || key==SDLK_DOWN){focusNext(key==SDLK_UP);return;}
    const bool activate=key==SDLK_RETURN || key==SDLK_KP_ENTER || key==SDLK_SPACE;
    if(activate){
        if(focus=="done"){dismiss();return;}
        if(focus=="retry"){persist();return;}
        if(focus=="nav.current"){openCategoryPicker();return;}
        if(focus.rfind("nav.",0)==0){int i=std::stoi(focus.substr(4));selectCategory(Category(i));focus="nav."+std::to_string(i);return;}
    }
    for(auto r:form)if(!r.extraId.empty() && r.extraId==focus){if(activate)r.change(0);return;}
    for(auto r:form)if(r.id==focus){
        if(activate)invoke(r);
        else if((key==SDLK_LEFT || key==SDLK_RIGHT) && (r.kind==Kind::Number || r.kind==Kind::Slider))invoke(r,key==SDLK_LEFT?-1:1);
        return;
    }
}
