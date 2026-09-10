// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <algorithm>

using namespace GAGCore;
namespace {
const Color ink(41,70,51),muted(83,101,76),paper(240,241,223),rail(225,231,209),line(190,203,176),field(249,250,240),gold(228,199,121);
}

std::vector<std::string> SettingsScreen::wrap(const std::string& text,int width,bool heading) const
{
    auto* font=Toolkit::getFont(heading?"menu":"standard");
    std::vector<std::string> result;std::string currentLine;
    size_t at=0;
    while(at<text.size()){
        size_t next=at+1;
        while(next<text.size() && (static_cast<unsigned char>(text[next])&0xc0)==0x80)++next;
        auto glyph=text.substr(at,next-at);at=next;
        if(glyph=="\n"){result.push_back(currentLine);currentLine.clear();continue;}
        if(!currentLine.empty() && font->getStringWidth(currentLine+glyph)>width){
            auto split=currentLine.find_last_of(' ');
            if(split!=std::string::npos && split>0){result.push_back(currentLine.substr(0,split));currentLine=currentLine.substr(split+1);}
            else{result.push_back(currentLine);currentLine.clear();}
        }
        if(!currentLine.empty() || glyph!=" ")currentLine+=glyph;
    }
    if(!currentLine.empty())result.push_back(currentLine);
    return result;
}
int SettingsScreen::wrappedHeight(const std::string& text,int width,bool heading) const
{
    return int(wrap(text,std::max(1,width),heading).size())*(Toolkit::getFont(heading?"menu":"standard")->getStringHeight("Ag")+4);
}
void SettingsScreen::drawText(int x,int y,const std::string& text,bool secondary,bool heading)
{
    auto* font=Toolkit::getFont(heading?"menu":"standard");const auto color=secondary?muted:ink;
    font->pushStyle(Font::Style(Font::STYLE_NORMAL,color.r,color.g,color.b));
    gfx->drawString(x,y,font,text);font->popStyle();
}
void SettingsScreen::drawWrapped(int x,int y,int width,const std::string& text,bool secondary,bool heading)
{
    const int height=Toolkit::getFont(heading?"menu":"standard")->getStringHeight("Ag")+4;
    for(const auto& l:wrap(text,width,heading)){drawText(x,y,l,secondary,heading);y+=height;}
}
int& SettingsScreen::scrollOffset() { return modal==Modal::None?scroll[int(current)]:modalScroll; }
void SettingsScreen::layout()
{
    const int w=globalContainer->gfx->getW(),h=globalContainer->gfx->getH();
    panel={std::max(0,(w-960)/2),std::max(0,(h-720)/2),std::min(w-32,960),std::min(h-32,720)};
    panel.x=(w-panel.w)/2;panel.y=(h-panel.h)/2;
    padding=panel.w<800?16:24;sidebar=panel.w<800?148:176;
    const int footH=std::max(64,wrappedHeight(tr("Changes saved automatically"),panel.w-240)+24);
    footer={panel.x,panel.y+panel.h-footH,panel.w,footH};
    const char* names[]={"Display & graphics","Audio","Gameplay","Building defaults","Controls","Language & player"};
    int navigationHeight=0;
    for(auto name:names)navigationHeight+=std::max(42,wrappedHeight(tr(name),sidebar-32)+20)+4;
    compactNavigation=navigationHeight-4>footer.y-panel.y-76;
    int headerHeight=64;
    categoryControl={panel.x+156,panel.y+12,panel.w-172,40};
    if(compactNavigation && modal==Modal::None){
        categoryControl.h=std::max(40,wrappedHeight(tr(names[int(current)]),categoryControl.w-36)+16);
        headerHeight=std::max(64,categoryControl.h+24);
    }
    const int railWidth=modal==Modal::None && !compactNavigation?sidebar:0;
    viewport={panel.x+railWidth+padding,panel.y+headerHeight+4,
        panel.w-railWidth-2*padding-16,footer.y-panel.y-headerHeight-16};
    int y=0;
    for(size_t i=0;i<form.size();){
        const int cols=form[i].columns;
        size_t end=i+1;
        if(cols>1 && form[i].column==0){while(end<form.size() && form[end].columns==cols && form[end].column!=0)++end;}
        const int shownCols=viewport.w>=cols*110?cols:(cols==4 && viewport.w>=220?2:1);
        const bool horizontal=shownCols>1;
        int groupH=0;
        for(size_t n=i;n<end;++n){
            Row& r=form[n];
            if(horizontal && n>i && (n-i)%shownCols==0){y+=groupH;groupH=0;}
            int cw=horizontal?(viewport.w-(shownCols-1)*8)/shownCols:viewport.w;
            int x=viewport.x+(horizontal?int((n-i)%shownCols)*(cw+8):0);
            const bool table=horizontal && r.kind!=Kind::Button;
            int controlW=std::min(180,cw),textW=cw-controlW-24;
            bool stacked=textW<220;
            if(table){controlW=std::min(180,cw);textW=cw;stacked=true;}
            int labelH=wrappedHeight(r.label,stacked?cw:textW);
            int helpH=r.help.empty()?0:8+wrappedHeight(r.help,stacked?cw:textW);
            int controlH=std::max(34,wrappedHeight(r.value,controlW-20-(r.extraId.empty()?0:40)-(r.kind==Kind::Choice?16:0))+16);
            if(r.kind==Kind::Slider)controlH=48;
            int height=0;
            if(r.kind==Kind::Section)height=24+wrappedHeight(r.label,cw)+8;
            else if(r.kind==Kind::Info)height=wrappedHeight(r.label,cw)+16;
            else if(r.kind==Kind::Button){height=std::max(40,wrappedHeight(r.label,cw-24)+20)+8;controlW=cw;controlH=height-8;}
            else height=(stacked?labelH+helpH+(labelH?12:0)+controlH:std::max(labelH+helpH,controlH))+24;
            r.bounds={x,y,cw,height};
            if(r.kind==Kind::Button)r.control={x,y,cw,controlH};
            else r.control={stacked?x:x+cw-controlW,y+12+(stacked?labelH+helpH+(labelH?12:0):0),controlW,controlH};
            if(horizontal)groupH=std::max(groupH,height);else y+=height;
        }
        if(horizontal){
            // Align table labels and absent-value dashes with numeric cells.
            for(size_t n=i;n<end;++n)if(form[n].kind==Kind::Info)form[n].bounds.h=groupH;
            y+=groupH;
        }
        i=end;
    }
    contentHeight=y;
    scrollOffset()=std::clamp(scrollOffset(),0,std::max(0,contentHeight-viewport.h));
    for(auto& r:form){r.bounds.y+=viewport.y-scrollOffset();r.control.y+=viewport.y-scrollOffset();}
    scrollbar={viewport.x+viewport.w+6,viewport.y,10,viewport.h};

}
void SettingsScreen::paintRow(const Row& r)
{
    if(r.bounds.y+r.bounds.h<=viewport.y || r.bounds.y>=viewport.y+viewport.h)return;
    const Rect& b=r.bounds;const Rect& c=r.control;
    if(r.kind==Kind::Section){drawWrapped(b.x,b.y+24,b.w,r.label);return;}
    if(r.kind==Kind::Info){drawWrapped(b.x,b.y+(r.columns>1?std::max(0,(b.h-wrappedHeight(r.label,b.w))/2):0),b.w,r.label,true);return;}
    bool focused=focus==r.id;
    if(r.kind==Kind::Button){
        gfx->drawFilledRect(c.x,c.y,c.w,c.h,r.selected?gold:field);
        gfx->drawRect(c.x,c.y,c.w,c.h,focused?ink:line);
        drawWrapped(c.x+12,c.y+10,c.w-24,r.label,!r.enabled);return;
    }
    int textW=c.x>b.x?c.x-b.x-24:b.w;
    drawWrapped(b.x,b.y+12,textW,r.label,!r.enabled);
    if(!r.help.empty())drawWrapped(b.x,b.y+12+wrappedHeight(r.label,textW)+8,textW,r.help,true);
    if(r.kind==Kind::Toggle){
        gfx->drawFilledRect(c.x,c.y,c.w,c.h,r.enabled?field:rail);
        gfx->drawRect(c.x,c.y,c.w,c.h,focused?ink:line);
        gfx->drawRect(c.x+10,c.y+(c.h-18)/2,18,18,ink);
        if(r.number){gfx->drawLine(c.x+13,c.y+c.h/2,c.x+17,c.y+c.h/2+5,ink);gfx->drawLine(c.x+17,c.y+c.h/2+5,c.x+25,c.y+c.h/2-6,ink);}
        drawText(c.x+38,c.y+8,tr(r.number?"On":"Off"));
    }else if(r.kind==Kind::Slider){
        drawText(c.x,c.y,r.value,!r.enabled);
        const int sy=c.y+c.h-10;
        gfx->drawFilledRect(c.x,sy,c.w,4,line);
        int dx=(c.w-8)*r.number/std::max(1,r.maximum);
        gfx->drawFilledRect(c.x+dx,sy-5,8,14,r.enabled?ink:line);
        if(focused)gfx->drawRect(c.x-3,c.y-3,c.w+6,c.h+6,ink);
    }else{
        gfx->drawFilledRect(c.x,c.y,c.w,c.h,r.enabled?field:rail);
        gfx->drawRect(c.x,c.y,c.w,c.h,focused?ink:line);
        if(r.kind==Kind::Number){
            const int segment=std::min(32,c.w/4);
            drawText(c.x+8,c.y+8,"−");drawText(c.x+c.w-20,c.y+8,"+");
            gfx->drawLine(c.x+segment,c.y,c.x+segment,c.y+c.h,line);
            gfx->drawLine(c.x+c.w-segment,c.y,c.x+c.w-segment,c.y+c.h,line);
            int tw=Toolkit::getFont("standard")->getStringWidth(r.value);
            drawText(c.x+(c.w-tw)/2,c.y+8,r.value);
        }else{
            gfx->setClipRect(c.x+6,std::max(c.y,viewport.y),c.w-12,std::max(0,std::min(c.y+c.h,viewport.y+viewport.h)-std::max(c.y,viewport.y)));
            if(r.kind==Kind::Text && editingText && selectAllText)gfx->drawFilledRect(c.x+6,c.y+4,c.w-12,c.h-8,gold);
            std::string shown=r.value;
            if(r.kind==Kind::Text && editingText)shown.insert(std::min(textCursor,shown.size()),"|");
            int available=c.w-20-(r.extraId.empty()?0:40)-(r.kind==Kind::Choice?16:0);
            drawWrapped(c.x+10,c.y+8,available,shown,!r.enabled);
            if(r.kind==Kind::Choice){
                gfx->drawLine(c.x+c.w-20,c.y+c.h/2-2,c.x+c.w-15,c.y+c.h/2+3,ink);
                gfx->drawLine(c.x+c.w-15,c.y+c.h/2+3,c.x+c.w-10,c.y+c.h/2-2,ink);
            }
            if(!r.extraId.empty()){
                gfx->drawRect(c.x+c.w-36,c.y,36,c.h,focus==r.extraId?ink:line);
                drawText(c.x+c.w-24,c.y+8,"+");
            }
            gfx->setClipRect(viewport.x,viewport.y,viewport.w,viewport.h);
        }
    }
    gfx->drawLine(b.x,b.y+b.h-1,b.x+b.w,b.y+b.h-1,line);
}
void SettingsScreen::paint()
{
    layout();buildRows();layout();
    // Opaque settings-local surfaces keep background texture out of the form.
    Glob2Screen::paint();
    gfx->drawFilledRect(0,0,getW(),getH(),Color(20,32,22,200));
    gfx->drawFilledRect(panel.x,panel.y,panel.w,panel.h,paper);
    gfx->drawRect(panel.x,panel.y,panel.w,panel.h,line);
    drawText(panel.x+padding,panel.y+20,tr("Settings"),false,true);
    const char* categories[]={"Display & graphics","Audio","Gameplay","Building defaults","Controls","Language & player"};
    if(modal==Modal::None && compactNavigation){
        const auto& c=categoryControl;
        gfx->drawFilledRect(c.x,c.y,c.w,c.h,field);
        gfx->drawRect(c.x,c.y,c.w,c.h,focus=="nav.current"?ink:line);
        drawWrapped(c.x+10,c.y+8,c.w-36,tr(categories[int(current)]));
        gfx->drawLine(c.x+c.w-20,c.y+c.h/2-2,c.x+c.w-15,c.y+c.h/2+3,ink);
        gfx->drawLine(c.x+c.w-15,c.y+c.h/2+3,c.x+c.w-10,c.y+c.h/2-2,ink);
    }
    if(modal==Modal::None && !compactNavigation){
        gfx->drawFilledRect(panel.x+1,panel.y+64,sidebar-1,footer.y-panel.y-64,rail);
        int ny=panel.y+76;
        for(int i=0;i<6;++i){
            int height=std::max(42,wrappedHeight(tr(categories[i]),sidebar-32)+20);
            if(i==int(current))gfx->drawFilledRect(panel.x+8,ny,sidebar-16,height,gold);
            if(focus=="nav."+std::to_string(i))gfx->drawRect(panel.x+8,ny,sidebar-16,height,ink);
            drawWrapped(panel.x+16,ny+10,sidebar-32,tr(categories[i]));ny+=height+4;
        }
    }
    gfx->setClipRect(viewport.x,viewport.y,viewport.w,viewport.h);
    for(const auto& r:form)paintRow(r);
    gfx->setClipRect();
    if(contentHeight>viewport.h){
        gfx->drawFilledRect(scrollbar.x,scrollbar.y,scrollbar.w,scrollbar.h,rail);
        int size=std::max(24,viewport.h*viewport.h/contentHeight);
        int top=scrollOffset()*(viewport.h-size)/std::max(1,contentHeight-viewport.h);
        gfx->drawFilledRect(scrollbar.x,scrollbar.y+top,scrollbar.w,size,muted);
    }
    gfx->drawLine(footer.x,footer.y,footer.x+footer.w,footer.y,line);
    std::string status=failed?tr("Could not save"):settingsDirty?tr("Saving…"):restartRequired()?tr("Saved — restart required"):tr("Changes saved automatically");
    drawWrapped(footer.x+padding,footer.y+16,footer.w-240,status,true);
    dropdown.paint(gfx,ink,field,gold,line);
    const Rect doneRect={footer.x+footer.w-112,footer.y+12,96,40};
    gfx->drawFilledRect(doneRect.x,doneRect.y,doneRect.w,doneRect.h,gold);
    gfx->drawRect(doneRect.x,doneRect.y,doneRect.w,doneRect.h,focus=="done"?ink:line);
    drawText(doneRect.x+12,doneRect.y+10,tr(modal==Modal::None?"Done":modal==Modal::Display?"Revert":"Cancel"));
    // Always available, and always closes in one click: every change is
    // already live and auto-saved, so there is nothing left to discard. Done
    // retries the durable flush on every click while failed; this is the
    // escape hatch for leaving without insisting that retry succeed first.
    if(modal==Modal::None){gfx->drawRect(doneRect.x-96,doneRect.y,88,40,focus=="cancel"?ink:line);drawText(doneRect.x-84,doneRect.y+10,tr(failed?"continue":"Cancel"));}
}
