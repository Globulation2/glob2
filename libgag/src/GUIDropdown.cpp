// SPDX-License-Identifier: GPL-3.0-or-later
#include <GUIDropdown.h>
#include <algorithm>

using namespace GAGCore;
namespace GAGGUI
{
namespace {
bool contains(SDL_Rect r,int x,int y) { return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h; }
std::vector<std::string> wrap(Font* font,const std::string& text,int width)
{
    std::vector<std::string> lines;std::string line;
    for(size_t at=0;at<text.size();){
        size_t end=at+1;while(end<text.size() && (static_cast<unsigned char>(text[end])&0xc0)==0x80)++end;
        const auto glyph=text.substr(at,end-at);at=end;
        if(glyph=="\n"){lines.push_back(line);line.clear();continue;}
        if(!line.empty() && font->getStringWidth(line+glyph)>width){
            auto space=line.find_last_of(' ');
            if(space!=std::string::npos && space>0){lines.push_back(line.substr(0,space));line=line.substr(space+1);}
            else{lines.push_back(line);line.clear();}
        }
        if(!line.empty() || glyph!=" ")line+=glyph;
    }
    if(!line.empty() || lines.empty())lines.push_back(line);
    return lines;
}
}
void Dropdown::open(SDL_Rect anchor,SDL_Rect available,const std::vector<std::string>& options,int selected,Font* f)
{
    close();items.clear();offset=total=0;font=f;
    if(options.empty() || !font || available.w<32 || available.h<36)return;
    lineHeight=font->getStringHeight("Ag")+4;
    int width=anchor.w;
    for(const auto& option:options)width=std::max(width,std::min(400,font->getStringWidth(option)+36));
    width=std::min(width,available.w);
    for(const auto& option:options){
        Item item;item.lines=wrap(font,option,std::max(1,width-36));
        item.top=total;item.height=std::max(36,int(item.lines.size())*lineHeight+16);
        total+=item.height;items.push_back(std::move(item));
    }
    const int below=std::max(0,available.y+available.h-anchor.y-anchor.h-2);
    const int above=std::max(0,anchor.y-available.y-2);
    const bool down=total+2<=below || below>=above;
    int height=std::min(total+2,std::min(320,down?below:above));
    // Very tall wrapped fields can leave no useful space on either side.
    // Keep the list on-screen even then, allowing it to overlap the field.
    if(height<std::min(total+2,72))height=std::min(total+2,std::min(320,available.h));
    box={std::clamp(anchor.x,available.x,available.x+available.w-width),
         std::clamp(down?anchor.y+anchor.h+2:anchor.y-height-2,available.y,available.y+available.h-height),width,height};
    list={box.x+1,box.y+1,box.w-2,box.h-2};
    active=std::clamp(selected,0,int(items.size())-1);opened=true;reveal();
}
SDL_Rect Dropdown::itemBounds(int index) const
{
    if(index<0 || index>=int(items.size()))return {};
    const auto& i=items[index];return {list.x,list.y+i.top-offset,list.w-(total>list.h?12:0),i.height};
}
void Dropdown::scrollTo(int value){offset=std::clamp(value,0,std::max(0,total-list.h));}
void Dropdown::reveal()
{
    const auto& i=items[active];
    if(i.top<offset)scrollTo(i.top);
    else if(i.top+i.height>offset+list.h)scrollTo(i.top+i.height-list.h);
}
SDL_Rect Dropdown::thumb() const
{
    if(total<=list.h)return {};
    const int height=std::min(list.h,std::max(20,list.h*list.h/total));
    return {list.x+list.w-10,list.y+offset*(list.h-height)/std::max(1,total-list.h),8,height};
}
int Dropdown::hit(int x,int y) const
{
    if(!contains(list,x,y))return -1;
    for(int i=0;i<int(items.size());++i)if(contains(itemBounds(i),x,y))return i;
    return -1;
}
int Dropdown::handleEvent(const SDL_Event& e)
{
    if(!opened)return -1;
    if(e.type==SDL_WINDOWEVENT && (e.window.event==SDL_WINDOWEVENT_FOCUS_LOST || e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED)){close();return -1;}
    if(e.type==SDL_MOUSEBUTTONDOWN){
        if(e.button.button!=SDL_BUTTON_LEFT || !contains(box,e.button.x,e.button.y)){close();return -1;}
        if(total>list.h && e.button.x>=list.x+list.w-12){
            auto t=thumb();dragging=true;grab=contains(t,e.button.x,e.button.y)?e.button.y-t.y:t.h/2;
            scrollTo((e.button.y-list.y-grab)*(total-list.h)/std::max(1,list.h-t.h));return -1;
        }
        int selected=hit(e.button.x,e.button.y);if(selected>=0){close();return selected;}
    }
    if(e.type==SDL_MOUSEBUTTONUP)dragging=false;
    if(e.type==SDL_MOUSEMOTION){
        if(dragging){auto t=thumb();scrollTo((e.motion.y-list.y-grab)*(total-list.h)/std::max(1,list.h-t.h));}
        else{int hovered=hit(e.motion.x,e.motion.y);if(hovered>=0)active=hovered;}
    }
    if(e.type==SDL_MOUSEWHEEL)scrollTo(offset-e.wheel.y*(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1)*48);
    if(e.type==SDL_KEYDOWN){
        auto key=e.key.keysym.sym;
        if(key==SDLK_ESCAPE || key==SDLK_TAB){close();return -1;}
        if(key==SDLK_RETURN || key==SDLK_KP_ENTER || key==SDLK_SPACE){int selected=active;close();return selected;}
        if(key==SDLK_UP)active=std::max(0,active-1);
        else if(key==SDLK_DOWN)active=std::min(int(items.size())-1,active+1);
        else if(key==SDLK_HOME)active=0;
        else if(key==SDLK_END)active=int(items.size())-1;
        else if(key==SDLK_PAGEUP)active=std::max(0,active-std::max(1,list.h/36));
        else if(key==SDLK_PAGEDOWN)active=std::min(int(items.size())-1,active+std::max(1,list.h/36));
        else return -1;
        reveal();
    }
    return -1;
}
void Dropdown::paint(DrawableSurface* surface,Color text,Color background,Color selection,Color border)
{
    if(!opened)return;
    surface->drawFilledRect(box.x,box.y,box.w,box.h,background);
    SDL_Rect old;surface->getClipRect(&old.x,&old.y,&old.w,&old.h);
    SDL_Rect clipped;SDL_IntersectRect(&old,&list,&clipped);
    surface->setClipRect(clipped.x,clipped.y,clipped.w,clipped.h);
    font->pushStyle(Font::Style(Font::STYLE_NORMAL,text.r,text.g,text.b));
    for(int n=0;n<int(items.size());++n){
        const auto r=itemBounds(n);if(r.y+r.h<=list.y || r.y>=list.y+list.h)continue;
        if(n==active)surface->drawFilledRect(r.x,r.y,r.w,r.h,selection);
        int y=r.y+8;for(const auto& line:items[n].lines){surface->drawString(r.x+12,y,font,line);y+=lineHeight;}
    }
    font->popStyle();
    if(total>list.h){const auto t=thumb();surface->drawFilledRect(t.x,t.y,t.w,t.h,border);}
    surface->setClipRect(old.x,old.y,old.w,old.h);
    surface->drawRect(box.x,box.y,box.w,box.h,border);
}
}
