// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "PhoneForm.h"
#include "MobileSafeArea.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <TouchText.h>
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;
PhoneEditor::PhoneEditor(MapEdit& editor): editor(editor) {}
PhoneEditor::~PhoneEditor() = default;
bool PhoneEditor::hasOverlay() const {
    return editor.showingMenuScreen || editor.showingLoad || editor.showingSave ||
        editor.showingScriptEditor || editor.showingTeamsEditor || editor.isShowingAreaName;
}
void PhoneEditor::syncOverlay() {
    GAGGUI::OverlayScreen* current=nullptr;
    if(editor.showingMenuScreen) current=editor.menuScreen;
    if(editor.showingLoad || editor.showingSave) current=editor.loadSaveScreen;
    if(editor.showingScriptEditor) current=editor.scriptEditor->phoneDialog();
    if(editor.showingTeamsEditor) current=editor.teamsEditor;
    if(editor.isShowingAreaName) current=editor.areaName;
    if(current==overlay) return;
    form.reset();overlay=current;
    if(current) form=std::make_unique<PhoneForm>(*current,[this](auto* widget){return editor.showingTeamsEditor ? editor.teamsEditor->phoneLabel(widget) : std::string{};},[](auto*){return true;},
        [this](auto* widget){
            auto* button=dynamic_cast<GAGGUI::TextButton*>(widget);
            if(!button) return false;
            const auto caption=button->caption();auto* strings=Toolkit::getStringTable();
            return caption==strings->getString("[ok]") || caption==strings->getString("[Cancel]") ||
                caption==strings->getString("[return to editor]");
        });
}
void PhoneEditor::closeOverlay() { form.reset();overlay=nullptr; }
void PhoneEditor::showFailure() { syncOverlay();if(form) form->scrollToTop(); }
void PhoneEditor::cancel() {
    touch.cancel();held=-1;onMap=false;panX=panY=0;
    if(form) form->cancel();
}
void PhoneEditor::prepare() {
    auto* gfx=globalContainer->gfx;const double unit=gfx->logicalUnitsPerPoint();
    safe=mobileDialogSafe(gfx);content={safe.x+8*unit,safe.y+64*unit,safe.w-16*unit,std::max(0.,safe.h-72*unit)};
    rows.clear();
    if(!tools) return;
    const auto top=content.y;
    content.y+=64*unit;content.h=std::max(0.,content.h-64*unit);
    int tab=0;
    for(auto* widget: {static_cast<MapEditorWidget*>(editor.buildingView),static_cast<MapEditorWidget*>(editor.flagsView),
        static_cast<MapEditorWidget*>(editor.terrainView),static_cast<MapEditorWidget*>(editor.teamsView)}) {
        widget->area.updateWindowWidth(gfx->getW());
        rows.push_back({widget,{content.x+tab*content.w/4,top,content.w/4-4*unit,56*unit},1.5*unit,true});++tab;
    }
    double y=0;int column=0;double lineHeight=0;
    const int columns=std::max(1,int(content.w/(80*unit)));
    const double cell=(content.w-(columns-1)*8*unit)/columns;
    for(auto* widget:editor.mew) {
        if(!widget->enabled || widget==editor.menuIcon || widget==editor.mapCoordinatesLabel || dynamic_cast<PanelIcon*>(widget)) continue;
        widget->area.updateWindowWidth(gfx->getW());const auto& a=widget->area;
        const bool tile=a.width<=56 && a.height<=48;
        if(!tile && column) {y+=lineHeight+8*unit;column=0;lineHeight=0;}
        const double width=tile ? cell : content.w;
        double scale=std::min((tile?1.5:1.*globalContainer->settings.mobileDialogTextPercent/100.)*unit,(width-16*unit)/std::max(1,a.width));
        if(dynamic_cast<TeamColorSelector*>(widget)) scale=3*unit;
        if(dynamic_cast<BrushSelector*>(widget)) scale=std::max(1.5*unit,scale);
        const double h=std::max(48*unit,a.height*scale+16*unit);
        rows.push_back({widget,{content.x+column*(cell+8*unit),y,width,h},scale});
        if(tile) {lineHeight=std::max(lineHeight,h);if(++column==columns) {y+=lineHeight+8*unit;column=0;lineHeight=0;}}
        else y+=h+8*unit;
    }
    if(column) y+=lineHeight+8*unit;
    const double miniScale=std::min(160*unit,content.w-16*unit)/128.;
    rows.push_back({nullptr,{content.x,y,content.w,128*miniScale+16*unit},miniScale});y+=128*miniScale+24*unit;
    maximum=std::max(0.,y-content.h);offset=std::clamp(offset,0.,maximum);
    for(auto& row:rows) if(!row.fixed) row.rect.y+=content.y-offset;
}
int PhoneEditor::hit(ViewPoint p) const {
    if(!safe.contains(p)) return -1;
    if(p.y<safe.y+56*globalContainer->gfx->logicalUnitsPerPoint())
        return -2-std::clamp(int((p.x-safe.x)*3/safe.w),0,2);
    if(tools) for(size_t i=0;i<rows.size();++i) if((rows[i].fixed || content.contains(p)) && rows[i].rect.contains(p)) return int(i);
    return -1;
}
void PhoneEditor::act(const TouchAction& action) {
    auto* gfx=globalContainer->gfx;const double unit=gfx->logicalUnitsPerPoint();
    ViewPoint p{action.point.x*unit,action.point.y*unit};
    if(action.kind==TouchActionKind::Cancel) {editor.suspendInput();return;}
    if(action.kind==TouchActionKind::BeginStroke || action.kind==TouchActionKind::Stroke || action.kind==TouchActionKind::EndStroke) {
        if(!onMap) return;
        SDL_Event mouse{};
        if(action.kind==TouchActionKind::EndStroke || !content.contains(p)) {
            mouse.type=SDL_MOUSEBUTTONUP;mouse.button.button=SDL_BUTTON_LEFT;mouse.button.x=int(p.x);mouse.button.y=int(p.y);
            editor.processEvent(mouse);
            if(!content.contains(p)) onMap=false;
        } else if(action.kind==TouchActionKind::BeginStroke) {
            mouse.type=SDL_MOUSEBUTTONDOWN;mouse.button.button=SDL_BUTTON_LEFT;mouse.button.x=int(p.x);mouse.button.y=int(p.y);editor.processEvent(mouse);
        } else {
            mouse.type=SDL_MOUSEMOTION;mouse.motion.x=int(p.x);mouse.motion.y=int(p.y);editor.processEvent(mouse);
        }
        return;
    }
    if(action.kind==TouchActionKind::Pan) {
        if(tools) offset=std::clamp(offset-p.y,0.,maximum);
        else if(onMap) {
            panX-=p.x/32.;panY-=p.y/32.;
            int dx=int(std::copysign(std::floor(std::abs(panX)+1e-5),panX)),dy=int(std::copysign(std::floor(std::abs(panY)+1e-5),panY));panX-=dx;panY-=dy;
            editor.viewportX=(editor.viewportX+dx)&editor.game.map.wMask;
            editor.viewportY=(editor.viewportY+dy)&editor.game.map.hMask;
        }
        return;
    }
    if(action.kind==TouchActionKind::Select && hit(p)==held) {
        if(held==-2) {cancel();editor.suspendInput();editor.performAction("open menu screen");return;}
        if(held==-3) {cancel();tools=!tools;offset=0;return;}
        if(held==-4) {cancel();pan=!pan;tools=false;return;}
        if(tools && held>=0 && held<int(rows.size())) {
            auto row=rows[held];auto* widget=row.widget;
            if(!widget) {
                const int x=int((p.x-row.rect.x-(row.rect.w-128*row.scale)/2)/row.scale)+gfx->getW()-140;
                const int y=int((p.y-row.rect.y-8*unit)/row.scale)+5;
                if(editor.minimap.insideMinimap(x,y)) {
                    int mx,my;editor.minimap.convertToMap(x,y,mx,my);
                    editor.viewportX=(mx-gfx->getW()/64)&editor.game.map.wMask;
                    editor.viewportY=(my-gfx->getH()/64)&editor.game.map.hMask;
                }
                return;
            }
            auto a=widget->area;
            int x=int((p.x-row.rect.x-(row.rect.w-a.width*row.scale)/2)/row.scale);
            int y=int((p.y-row.rect.y-(row.rect.h-a.height*row.scale)/2)/row.scale);
            if(dynamic_cast<ValueScrollBox*>(widget)) {
                x=p.x<row.rect.x+48*unit ? 0 : p.x>=row.rect.x+row.rect.w-48*unit ? 111 :
                    10+int(92*(p.x-row.rect.x-48*unit)/(row.rect.w-96*unit));y=8;
            } else {x=std::clamp(x,0,a.width-1);y=std::clamp(y,0,a.height-1);}
            widget->handleClick(x,y);
            if(dynamic_cast<PanelIcon*>(widget)) offset=0;
            return;
        }
        if(!pan && onMap && content.contains(p)) {
            SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
            click.button.x=int(p.x);click.button.y=int(p.y);editor.processEvent(click);
            click.type=SDL_MOUSEBUTTONUP;editor.processEvent(click);
            if(editor.selectionMode==MapEdit::EditingBuilding || editor.selectionMode==MapEdit::EditingUnit) {tools=true;offset=0;}
        }
    }
}
bool PhoneEditor::event(SDL_Event event) {
    syncOverlay();
    if(form) {
        if(form->event(event)) {
            // Consume the dialog result without redispatching the touch as a mouse click.
            SDL_Event idle{};
            if(overlay->endValue>=0) {form.reset();overlay=nullptr;}
            editor.delegateMenu(idle);return true;
        }
        return false;
    }
    prepare();ViewPoint p;int phase=-1;Sint64 device=-1,id=0;
    switch(event.type) {
    case SDL_FINGERDOWN:case SDL_FINGERMOTION:case SDL_FINGERUP:
        p={event.tfinger.x*globalContainer->gfx->getW(),event.tfinger.y*globalContainer->gfx->getH()};
        device=event.tfinger.touchId;id=event.tfinger.fingerId;
        phase=event.type==SDL_FINGERDOWN?0:event.type==SDL_FINGERUP?2:1;break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:
        if(event.button.which==SDL_TOUCH_MOUSEID || event.button.button!=SDL_BUTTON_LEFT) return true;
        p={double(event.button.x),double(event.button.y)};phase=event.type==SDL_MOUSEBUTTONDOWN?0:2;break;
    case SDL_MOUSEMOTION:
        if(event.motion.which==SDL_TOUCH_MOUSEID) return true;
        p={double(event.motion.x),double(event.motion.y)};phase=1;break;
    case SDL_MOUSEWHEEL:if(tools) {cancel();offset-=event.wheel.y*48*globalContainer->gfx->logicalUnitsPerPoint();}return true;
    case SDL_KEYDOWN:if(event.key.keysym.sym==SDLK_ESCAPE && tools) {cancel();tools=false;return true;}return false;
    default:return false;
    }
    const double unit=globalContainer->gfx->logicalUnitsPerPoint();
    std::vector<TouchAction> actions;
    if(phase==0) {
        if(!touch.hasPointers()) {
            held=hit(p);onMap=!tools && held==-1 && content.contains(p);
            const bool paint=editor.selectionMode==MapEdit::PlaceTerrain || editor.selectionMode==MapEdit::PlaceZone ||
                editor.selectionMode==MapEdit::RemoveObject || editor.selectionMode==MapEdit::ChangeAreas || editor.selectionMode==MapEdit::ChangeNoResourceGrowthAreas;
            touch.setMode(onMap && !pan && paint ? TouchMode::Paint : TouchMode::Navigate);
        }
        actions=touch.down(device,id,{p.x/unit,p.y/unit});
    }
    else if(phase==1) actions=touch.move(device,id,{p.x/unit,p.y/unit});
    else actions=touch.up(device,id,{p.x/unit,p.y/unit});
    for(const auto& action:actions) act(action);
    return true;
}
void PhoneEditor::draw() {
    syncOverlay();if(form) {form->draw();return;}
    prepare();auto* gfx=globalContainer->gfx;double unit=gfx->logicalUnitsPerPoint();
    auto* font=globalContainer->standardFont;
    const char* labels[]={"[menu]",tools?"[Editor map]":"[Editor tools]",pan?"[Pan map]":"[Edit map]"};
    for(int i=0;i<3;++i) {
        ViewRect r{safe.x+i*safe.w/3,safe.y,safe.w/3-4*unit,56*unit};
        gfx->drawFilledRect(r.x,r.y,r.w,r.h,24,48,55,245);
        double scale=0.75*unit;auto lines=wrapTouchText(font,Toolkit::getStringTable()->getString(labels[i]),(r.w-8*unit)/scale);
        SDL_Rect clip{int(r.x),int(r.y),int(r.w),int(r.h)};
        gfx->setUITransform(scale,r.x+4*unit,r.y+4*unit,&clip);
        int y=0;for(const auto& line:lines) {gfx->drawString(0,y,font,line);y+=font->getStringHeight("Ag");}gfx->setUITransform();
    }
    if(!tools) return;
    gfx->drawFilledRect(content.x,content.y,content.w,content.h,16,32,40,245);
    for(const auto& row:rows) {
        auto r=row.rect;
        const int top=int(row.fixed ? r.y : std::max(r.y,content.y)),bottom=int(row.fixed ? r.y+r.h : std::min(r.y+r.h,content.y+content.h));
        if(bottom<=top) continue;
        SDL_Rect clip{int(r.x),top,int(r.w),bottom-top};
        gfx->setClipRect(clip.x,clip.y,clip.w,clip.h);
        gfx->drawFilledRect(r.x,r.y,r.w,r.h,30,56,64,255);
        if(!row.widget) {
            gfx->setUITransform(row.scale,r.x+(r.w-128*row.scale)/2-(gfx->getW()-140)*row.scale,r.y+8*unit-5*row.scale,&clip);
            editor.drawMiniMap();gfx->setUITransform();gfx->setClipRect();continue;
        }
        const auto a=row.widget->area;
        gfx->setUITransform(row.scale,r.x+(r.w-a.width*row.scale)/2-a.x*row.scale,
            r.y+(r.h-a.height*row.scale)/2-a.y*row.scale,&clip);
        row.widget->drawSelf();gfx->setUITransform();
        if(dynamic_cast<ValueScrollBox*>(row.widget)) {
            gfx->setUITransform(2*unit,r.x+8*unit,r.y+8*unit,&clip);gfx->drawString(0,0,font,"−");gfx->setUITransform();
            gfx->setUITransform(2*unit,r.x+r.w-36*unit,r.y+8*unit,&clip);gfx->drawString(0,0,font,"+");gfx->setUITransform();
        }
        gfx->setClipRect();
    }
    if(maximum>0) gfx->drawFilledRect(content.x+content.w-3*unit,content.y+offset/maximum*(content.h-24*unit),3*unit,24*unit,220,230,225);
}
