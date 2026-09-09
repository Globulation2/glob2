// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneForm.h"
#include "PhoneGraphic.h"
#include "Glob2Screen.h"
#include "MobileSafeArea.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"
#include <TouchText.h>
#include <GUIButton.h>
#include <GUIList.h>
#include <GUICheckList.h>
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUITextInput.h>
#include <GUINumber.h>
#include <GUIRatio.h>
#include <GUISelector.h>
#include <GUIKeySelector.h>
#include <set>
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;
using namespace GAGGUI;
namespace {
SDL_Rect bounds(Widget* widget) {
    auto* rect=dynamic_cast<RectangularWidget*>(widget);
    return rect ? rect->screenRectangle() : SDL_Rect{};
}
}
PhoneForm::~PhoneForm() {if(editing) SDL_StopTextInput();}
void PhoneForm::prepare() {
    screen.updateLayout();rows.clear();
    auto widgets=screen.presentationWidgets();
    std::stable_sort(widgets.begin(),widgets.end(),[](auto* a,auto* b) {
        auto x=bounds(a),y=bounds(b);return x.y==y.y ? x.x<y.x : x.y<y.y;
    });
    std::set<Widget*> pairedLabels;
    for(auto* widget:widgets) {
        if(!widget->visible || !visible(widget) || pairedLabels.count(widget)) continue;
        Row row{widget,""};
        if(dynamic_cast<PhoneGraphic*>(widget)) {row.kind=13;}
        else if(auto* selector=dynamic_cast<Selector*>(widget)) {row.kind=11;row.text=std::to_string(selector->getValue());}
        else if(auto* key=dynamic_cast<KeySelector*>(widget)) {row.kind=12;row.text=key->caption();}
        else if(auto* number=dynamic_cast<Number*>(widget)) {row.kind=9;row.text=std::to_string(number->get());}
        else if(auto* ratio=dynamic_cast<Ratio*>(widget)) {row.kind=10;row.text=std::to_string(ratio->get());}
        else if(auto* button=dynamic_cast<MultiTextButton*>(widget)) {
            row.kind=6;row.text=button->getCount() ? button->getText() : button->caption();
        } else if(auto* button=dynamic_cast<OnOffButton*>(widget)) {
            row.kind=7;row.selected=button->getState();row.text=row.selected ? "[x]" : "[ ]";
        } else if(auto* button=dynamic_cast<ColorButton*>(widget)) {
            row.kind=8;row.text=std::to_string(button->getSelectedColor()+1);
        } else if(auto* button=dynamic_cast<TextButton*>(widget)) {
            row.text=button->caption();row.kind=1;row.footer=footer(widget);
        } else if(auto* list=dynamic_cast<List*>(widget)) {
            for(size_t i=0;i<list->getCount();++i) {
                std::string text=list->getText(i);
                if(auto* checks=dynamic_cast<CheckList*>(list)) text=(checks->isChecked(i) ? "[x] " : "[ ] ")+text;
                rows.push_back({widget,text,2,int(i),list->getSelectionIndex()==int(i)});
            }
            continue;
        } else if(auto* input=dynamic_cast<TextInput*>(widget)) {
            row.text=input->getText();row.kind=3;row.selected=input->isActivated();
            if(row.text.empty()) row.text="…";
        } else if(auto* text=dynamic_cast<Text*>(widget)) row.text=text->getText();
        else if(auto* text=dynamic_cast<TextArea*>(widget)) {
            if(text->isReadOnly()) row.text=text->getText();
            else {
                const auto value=text->getText();unsigned cursor=0;text->getCursorPos(cursor);
                const double unit=globalContainer->gfx->logicalUnitsPerPoint();
                const double scale=1.5*globalContainer->settings.mobileDialogTextPercent/100.*unit;
                const double width=std::min(mobileDialogSafe(globalContainer->gfx).w-16*unit,640*unit)/scale-8;
                size_t start=0;
                do {
                    size_t end=start;
                    while(end<value.size() && value[end]!='\n') {
                        size_t next=end+1;while(next<value.size() && (static_cast<unsigned char>(value[next])&0xc0)==0x80) ++next;
                        if(end>start && globalContainer->standardFont->getStringWidth(value.substr(start,next-start))>width) break;
                        end=next;
                    }
                    std::string line=value.substr(start,end-start);
                    bool selected=editing==widget && cursor>=start && (cursor<end || (cursor==end && (end==value.size() || value[end]=='\n')));
                    if(selected) line.insert(cursor-start,"|");
                    rows.push_back({widget,line.empty()?" ":line,14,int(start),selected});
                    if(end==value.size()) break;
                    start=end+(value[end]=='\n');
                } while(start<=value.size());
                continue;
            }
        }
        else if(auto* preview=dynamic_cast<MapPreview*>(widget)) {
            if(!preview->isThumbnailLoaded()) continue;
            row.kind=4;
        } else continue;
        auto caption=label(widget);
        if(caption.empty() && (row.kind==9 || row.kind==10 || row.kind==7 || row.kind==11)) {
            const auto r=bounds(widget);Text* nearest=nullptr;
            for(auto* other:widgets) if(other->visible) if(auto* text=dynamic_cast<Text*>(other)) {
                const auto t=bounds(text);
                if(t.y==r.y && t.x>=r.x+r.w && (!nearest || t.x<bounds(nearest).x)) nearest=text;
            }
            if(nearest) {caption=nearest->getText();pairedLabels.insert(nearest);}
        }
        if(!caption.empty()) row.text=labelIncludesValue(widget) ? caption : caption+": "+row.text;
        if(row.kind || !row.text.empty()) rows.push_back(row);
    }
    if(editing && (!editing->visible || std::find(widgets.begin(),widgets.end(),editing)==widgets.end())) {
        if(auto* text=dynamic_cast<TextArea*>(editing)) text->deactivate();
        if(auto* input=dynamic_cast<TextInput*>(editing)) input->deactivate();
        editing=nullptr;SDL_StopTextInput();
    }
    if(editing) rows.push_back({nullptr,Toolkit::getStringTable()->getString("[Hide keyboard]"),5,0,false,true});
    auto* gfx=globalContainer->gfx;const double unit=gfx->logicalUnitsPerPoint();
    const double scale=1.5*globalContainer->settings.mobileDialogTextPercent/100.0;
    const auto safe=mobileDialogSafe(gfx);
    std::vector<bool> fixed;for(const auto& row:rows) fixed.push_back(row.footer);
    auto height=[&](size_t i,double width) {
        if(rows[i].kind==13) return 240*unit;
        if(rows[i].kind==4) return std::min(160*unit,width);
        return std::max(48*unit,wrapTouchText(globalContainer->standardFont,rows[i].text,((rows[i].kind>=9 && rows[i].kind<=11) ? width-96*unit : rows[i].kind==8 ? width-48*unit : width)/(scale*unit)-8).size()*16*scale*unit+8*unit);
    };
    placement=ResponsiveDialog::calculate(safe,fixed,height,offset,unit);
    if(lastHeight && lastHeight!=placement.content.h) cancel();
    if(lastHeight!=placement.content.h && editing) for(size_t i=0;i<rows.size();++i) if(rows[i].widget==editing && (rows[i].kind!=14 || rows[i].selected)) {
        const auto r=placement.rows[i].rect;
        double scroll=placement.offset+std::max(0.0,r.y+r.h-placement.content.y-placement.content.h);
        scroll=std::min(scroll,placement.offset+r.y-placement.content.y);
        placement=ResponsiveDialog::calculate(safe,fixed,height,scroll,unit);break;
    }
    lastHeight=placement.content.h;offset=placement.offset;
    for(size_t i=0;i<rows.size();++i) {rows[i].rect=placement.rows[i].rect;rows[i].footer=placement.rows[i].footer;}
}
void PhoneForm::draw() {
    prepare();auto* gfx=globalContainer->gfx;const double unit=gfx->logicalUnitsPerPoint();
    const double scale=1.5*globalContainer->settings.mobileDialogTextPercent/100.0*unit;
    auto* font=globalContainer->standardFont;
    for(const auto& row:rows) {
        const auto r=row.rect;auto clip=row.footer ? r : placement.content;
        if(r.y+r.h<=clip.y || r.y>=clip.y+clip.h) continue;
        SDL_Rect scissor{int(clip.x),int(clip.y),int(clip.w),int(clip.h)};
        gfx->setClipRect(scissor.x,scissor.y,scissor.w,scissor.h);
        gfx->drawFilledRect(int(r.x),int(r.y),int(r.w),int(r.h),row.selected ? Color(55,100,75,245) : Color(24,40,48,245));
        if(row.kind==13) {
            gfx->setUITransform(scale,r.x+8*unit,r.y+8*unit,&scissor);
            dynamic_cast<PhoneGraphic*>(row.widget)->paintPhone(int((r.w-16*unit)/scale),int((r.h-16*unit)/scale));
            gfx->setUITransform();continue;
        }
        if(row.kind==4) {
            const auto original=bounds(row.widget);const double factor=r.h/128;
            gfx->setUITransform(factor,r.x+(r.w-r.h)/2-original.x*factor,r.y-original.y*factor,&scissor);
            row.widget->paint();gfx->setUITransform();continue;
        }
        if(auto tint=color(row.widget)) gfx->drawFilledRect(int(r.x),int(r.y),int(4*unit),int(r.h),*tint);
        double textLeft=r.x,textWidth=r.w;
        if(row.kind==8) {
            const auto original=bounds(row.widget);const double factor=40*unit/std::max(1,original.h);
            gfx->setUITransform(factor,r.x+4*unit-original.x*factor,r.y+(r.h-40*unit)/2-original.y*factor,&scissor);
            row.widget->paint();gfx->setUITransform();textLeft+=48*unit;textWidth-=48*unit;
        }
        if(row.kind>=9 && row.kind<=11) {
            for(int side=0;side<2;++side) {
                gfx->setUITransform(scale,r.x+(side ? r.w-48*unit : 0),r.y+(r.h-font->getStringHeight("Ag")*scale)/2,&scissor);
                gfx->drawString(6,0,font,side ? "+" : "−");gfx->setUITransform();
            }
            textLeft+=48*unit;textWidth-=96*unit;
        }
        const auto lines=row.kind==14 ? std::vector<std::string>{row.text} : wrapTouchText(font,row.text,textWidth/scale-8);
        const double top=r.y+std::max(0.0,(r.h-lines.size()*font->getStringHeight("Ag")*scale)/2);
        gfx->setUITransform(scale,textLeft,top,&scissor);
        for(size_t i=0;i<lines.size();++i)
            gfx->drawString(row.kind==14 ? 4 : std::max(4,int((textWidth/scale-font->getStringWidth(lines[i]))/2)),i*font->getStringHeight("Ag"),font,lines[i]);
        gfx->setUITransform();
    }
    gfx->setClipRect();
    if(placement.maximum>0) {
        const auto c=placement.content;const double total=c.h+placement.maximum;
        gfx->drawFilledRect(int(c.x+c.w-3*unit),int(c.y+placement.offset*c.h/total),std::max(1,int(2*unit)),int(c.h*c.h/total),Color(180,195,195));
    }
}
PhoneForm::Row* PhoneForm::hit(ViewPoint point) {
    for(auto& row:rows) if(row.kind!=4 && row.kind && row.rect.contains(point) && (row.footer || placement.content.contains(point))) return &row;
    return nullptr;
}
void PhoneForm::cancel() {touch.cancel();held=nullptr;heldKind=-1;}
void PhoneForm::act(const std::vector<TouchAction>& actions) {
    for(const auto& action:actions) {
        if(action.kind==TouchActionKind::Pan) {offset-=action.point.y*globalContainer->gfx->logicalUnitsPerPoint();prepare();}
        if(action.kind!=TouchActionKind::Select) continue;
        prepare();const double unit=globalContainer->gfx->logicalUnitsPerPoint();
        auto* row=hit({action.point.x*unit,action.point.y*unit});
        if(!row || row->widget!=held || row->kind!=heldKind || row->index!=heldIndex || row->text!=heldText) continue;
        if(row->kind==13) {
            const double scale=1.5*globalContainer->settings.mobileDialogTextPercent/100.0*unit;
            dynamic_cast<PhoneGraphic*>(row->widget)->inspectPhone(int((action.point.x*unit-row->rect.x-8*unit)/scale),int((action.point.y*unit-row->rect.y-8*unit)/scale));return;
        }
        if(row->kind==5) {
            if(auto* text=dynamic_cast<TextArea*>(editing)) text->deactivate();
            if(auto* input=dynamic_cast<TextInput*>(editing)) input->deactivate();
            editing=nullptr;SDL_StopTextInput();return;
        }
        if(row->kind==14) {
            for(auto* widget:screen.presentationWidgets()) {
                if(auto* input=dynamic_cast<TextInput*>(widget)) input->deactivate();
                if(auto* text=dynamic_cast<TextArea*>(widget)) text->deactivate();
            }
            auto* text=static_cast<TextArea*>(row->widget);const auto value=text->getText();
            size_t pos=row->index;const double scale=1.5*globalContainer->settings.mobileDialogTextPercent/100.*unit;
            const double x=(action.point.x*unit-row->rect.x)/scale-4;
            while(pos<value.size() && value[pos]!='\n') {
                size_t next=pos+1;while(next<value.size() && (static_cast<unsigned char>(value[next])&0xc0)==0x80) ++next;
                if(globalContainer->standardFont->getStringWidth(value.substr(row->index,next-row->index))>x) break;
                pos=next;
            }
            text->activate();text->setCursorPos(pos);editing=text;lastHeight=0;SDL_StartTextInput();return;
        }
        if(row->kind==2) {
            auto* list=static_cast<List*>(row->widget);list->setSelectionIndex(row->index);list->selectionChanged();return;
        }
        if(row->kind==3) {
            for(auto* w:screen.presentationWidgets()) {if(auto* input=dynamic_cast<TextInput*>(w)) input->deactivate();if(auto* text=dynamic_cast<TextArea*>(w)) text->deactivate();}
            auto* input=static_cast<TextInput*>(row->widget);input->activate();input->setCursorPos(input->getText().size());
            editing=input;lastHeight=0;SDL_StartTextInput();return;
        }
        const auto rect=bounds(row->widget);
        if(row->kind>=9 && row->kind<=11) {
            const double x=action.point.x*unit;
            const int delta=x<row->rect.x+48*unit ? -1 : x>=row->rect.x+row->rect.w-48*unit ? 1 : 0;
            if(!delta) return;
            if(row->kind==9) row->widget->activateAt(rect.x+(delta<0 ? 1 : rect.w-1),rect.y+rect.h/2);
            else if(row->kind==11) {
                auto* selector=static_cast<Selector*>(row->widget);
                const int step=std::max(1,int(selector->maximumValue()/16));
                int value=std::clamp(int(selector->getValue())+delta*step,0,int(selector->maximumValue()));
                const auto before=selector->getValue();
                selector->setValue(value);
                if(selector->getValue()!=before) screen.onAction(selector,VALUE_CHANGED,selector->getValue(),0);
            } else {
                auto* ratio=static_cast<Ratio*>(row->widget);int value=std::clamp(ratio->get()+delta,0,ratio->maximumValue());
                if(value!=ratio->get()) {ratio->set(value);ratio->onTimer(SDL_GetTicks());}
            }
            return;
        }
        row->widget->activateAt(rect.x+rect.w/2,rect.y+rect.h/2);return;
    }
}
bool PhoneForm::event(SDL_Event event) {
    auto* gfx=globalContainer->gfx;prepare();ViewPoint point;int phase=-1;Sint64 device=-1,pointer=0;
    switch(event.type) {
    case SDL_FINGERDOWN:case SDL_FINGERMOTION:case SDL_FINGERUP:
        point={event.tfinger.x*gfx->getW(),event.tfinger.y*gfx->getH()};device=event.tfinger.touchId;pointer=event.tfinger.fingerId;
        phase=event.type==SDL_FINGERDOWN ? 0 : event.type==SDL_FINGERUP ? 2 : 1;break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:
        if(event.button.which==SDL_TOUCH_MOUSEID) return true;
        if(event.button.button!=SDL_BUTTON_LEFT) return true;
        point={double(event.button.x),double(event.button.y)};phase=event.type==SDL_MOUSEBUTTONDOWN ? 0 : 2;break;
    case SDL_MOUSEMOTION:
        if(event.motion.which==SDL_TOUCH_MOUSEID) return true;
        point={double(event.motion.x),double(event.motion.y)};phase=1;break;
    case SDL_MOUSEWHEEL:cancel();offset-=(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y)*48*gfx->logicalUnitsPerPoint();prepare();return true;
    default:
        if((event.type==SDL_TEXTINPUT || event.type==SDL_KEYDOWN) && dynamic_cast<TextArea*>(editing)) lastHeight=0;
        return false;
    }
    if(phase==0) {
        auto* row=hit(point);held=row ? row->widget : nullptr;heldKind=row ? row->kind : -1;
        heldIndex=row ? row->index : 0;heldText=row ? row->text : "";
        const double unit=gfx->logicalUnitsPerPoint();
        act(touch.down(device,pointer,{point.x/unit,point.y/unit}));
    } else if(phase==1) act(touch.move(device,pointer,{point.x/gfx->logicalUnitsPerPoint(),point.y/gfx->logicalUnitsPerPoint()}));else act(touch.up(device,pointer,{point.x/gfx->logicalUnitsPerPoint(),point.y/gfx->logicalUnitsPerPoint()}));
    return true;
}
