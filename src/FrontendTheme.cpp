// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrontendTheme.h"
#include "MenuColony.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <algorithm>
#include <cmath>

using namespace GAGCore;
using namespace GAGGUI;
FrontendTheme* FrontendTheme::current = nullptr;
bool FrontendTheme::allowed = true;
namespace { const char* fontNames[] = {"menu", "standard", "little"}; }

FrontendTheme::FrontendTheme() : original(Style::style)
{
	current = this;
	colony = std::make_unique<MenuColony>();
	textColor = Color(26,48,30);
	highlightColor = Color(74,110,58);
	frameColor = Color(26,48,30);
	listSelectedElementColor = Color(233,176,53);
	backColor = backOverlayColor = Color(240,224,188);
	for (int i=0;i<3;++i) originalFonts[i] = Toolkit::getFont(fontNames[i])->getStyle();
	fallback = std::make_unique<DrawableSurface>(1,1);
	if (!fallback->loadImage("data/gfx/menu-colony.png")) fallback.reset();
}
FrontendTheme::~FrontendTheme() { current = nullptr; }

FrontendScope::FrontendScope(bool enabled) : previous(Style::style), previousAllowed(FrontendTheme::allowed)
{
	if (!FrontendTheme::current) return;
	auto& theme = *FrontendTheme::current;
	FrontendTheme::allowed = enabled;
	Style::style = enabled ? &theme : theme.original;
	for (int i=0;i<3;++i)
	{
		auto* font = Toolkit::getFont(fontNames[i]);
		fonts[i] = font->getStyle();
		font->setStyle(enabled ? Font::Style(Font::STYLE_NORMAL, theme.textColor) : theme.originalFonts[i]);
	}
	if (!enabled) theme.colony->pause();
}
FrontendScope::~FrontendScope()
{
	if (!FrontendTheme::current) return;
	Style::style = previous;
	FrontendTheme::allowed = previousAllowed;
	for (int i=0;i<3;++i) Toolkit::getFont(fontNames[i])->setStyle(fonts[i]);
	FrontendTheme::current->colony->pause();
}

void FrontendTheme::rounded(DrawableSurface* s,int x,int y,int w,int h,int r,Color c)
{
	if (w<=0 || h<=0) return;
	r = std::min({r,w/2,h/2});
	s->drawFilledRect(x,y+r,w,h-2*r,c);
	for(int row=0;row<r;++row)
	{
		const float dy=r-row-0.5f;
		const int inset=r-int(std::sqrt(r*r-dy*dy));
		s->drawFilledRect(x+inset,y+row,w-2*inset,1,c);
		s->drawFilledRect(x+inset,y+h-row-1,w-2*inset,1,c);
	}
}
void FrontendTheme::onFrame()
{
	if (!painted) return; // Present the still before doing any loading work.
	if (!attempted) { attempted=true; colony->load(); }
	SDL_Window* window = SDL_GetKeyboardFocus();
	const bool visible = window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED);
	colony->update(SDL_GetTicks64(), visible);
}
void FrontendTheme::background(DrawableSurface* s, bool panel, const SDL_Rect* content)
{
	const int w=s->getW(),h=s->getH();
	s->drawFilledRect(0,0,w,h,Color(17,35,32));
	if (colony->ready()) colony->draw(w,h);
	else if (fallback)
	{
		// Software drawing has no stretched-surface overload. Fit only when
		// the logical resolution changes, then use an ordinary blit.
		if(!fittedFallback || fittedFallback->getW()!=w || fittedFallback->getH()!=h)
		{
			const double scale=std::max(double(w)/fallback->getW(),double(h)/fallback->getH());
			SDL_Rect dest{0,0,int(std::ceil(fallback->getW()*scale)),int(std::ceil(fallback->getH()*scale))};
			dest.x=(w-dest.w)/2; dest.y=(h-dest.h)/2;
			auto* fitted=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
			if(fitted && SDL_BlitScaled(fallback->getSDLSurface(),nullptr,fitted,&dest)==0)
				fittedFallback=std::make_unique<DrawableSurface>(fitted);
			SDL_FreeSurface(fitted);
		}
		if(fittedFallback) s->drawSurface(0,0,fittedFallback.get());
	}
	s->drawFilledRect(0,0,w,h,Color(18,34,22,44));
	if (panel)
	{
		const SDL_Rect area=content ? *content : SDL_Rect{(w-640)/2,(h-480)/2,640,480};
		const int x=std::max(0,area.x-12), y=std::max(0,area.y-12);
		const int pw=std::min(w,area.x+area.w+12)-x, ph=std::min(h,area.y+area.h+12)-y;
		rounded(s,x+3,y+5,pw,ph,12,Color(12,28,16,70));
		rounded(s,x,y,pw,ph,12,Color(26,48,30));
		rounded(s,x+3,y+3,pw-6,ph-6,9,Color(240,224,188,252));
	}
	painted=true;
}
void FrontendTheme::drawTextButtonBackground(DrawableSurface* s,int x,int y,int w,int h,unsigned hi)
{
	rounded(s,x,y,w,h,5,Color(26,48,30));
	rounded(s,x+2,y+2,w-4,h-4,3,Color(250,241,214));
	if(hi) rounded(s,x+2,y+2,w-4,h-4,3,Color(233,176,53,hi/2));
}
void FrontendTheme::drawFrame(DrawableSurface* s,int x,int y,int w,int h,unsigned hi)
{
	// Frames are also drawn AFTER list contents; never erase their interior.
	const Color edge = hi ? highlightColor : frameColor;
	s->drawRect(x,y,w,h,edge);
	s->drawRect(x+1,y+1,w-2,h-2,edge);
}
void FrontendTheme::drawOnOffButton(DrawableSurface* s,int x,int y,int w,int h,unsigned hi,bool state)
{
	drawTextButtonBackground(s,x,y,w,h,hi);
	drawFrame(s,x,y,w,h,hi);
	if(state)
	{
		s->drawLine(x+w/5,y+h/2,x+w*2/5,y+h*3/4,textColor);
		s->drawLine(x+w*2/5,y+h*3/4,x+w*4/5,y+h/4,textColor);
	}
}
void FrontendTheme::drawTriButton(DrawableSurface* s,int x,int y,int w,int h,unsigned hi,Uint8 state)
{
	drawOnOffButton(s,x,y,w,h,hi,state==1);
	if(state==2) s->drawLine(x+w/4,y+h/2,x+w*3/4,y+h/2,textColor);
}
void FrontendTheme::drawScrollBar(DrawableSurface* s,int x,int y,int,int h,int pos,int len)
{
	const int width=getStyleMetric(STYLE_METRIC_LIST_SCROLLBAR_WIDTH);
	const int end=getStyleMetric(STYLE_METRIC_LIST_SCROLLBAR_TOP_WIDTH);
	rounded(s,x,y,width,h,4,Color(26,48,30));
	rounded(s,x+1,y+1,width-2,h-2,3,Color(226,208,170));
	rounded(s,x+3,y+end+pos,width-6,len,3,Color(120,142,86));
	for(int i=0;i<4;++i)
	{
		s->drawLine(x+width/2-i,y+end/2+i,x+width/2+i,y+end/2+i,textColor);
		s->drawLine(x+width/2-i,y+h-end/2-i,x+width/2+i,y+h-end/2-i,textColor);
	}
}
void FrontendTheme::drawProgressBar(DrawableSurface* s,int x,int y,int w,int value,int range)
{
	const int h=getStyleMetric(STYLE_METRIC_PROGRESS_BAR_HEIGHT);
	rounded(s,x,y,w,h,4,Color(26,48,30));
	rounded(s,x+1,y+1,w-2,h-2,3,Color(226,208,170));
	if(range>0) rounded(s,x+1,y+1,std::max(0,int((w-2)*double(std::clamp(value,0,range))/range)),h-2,3,listSelectedElementColor);
}
int FrontendTheme::getStyleMetric(StyleMetrics m)
{
	// Preserve legacy hit geometry, including widgets initialized before execute.
	return original->getStyleMetric(m);
}

void FrontendTheme::drawFieldBackground(DrawableSurface* s,int x,int y,int w,int h)
{
	rounded(s,x,y,w,h,4,Color(26,48,30));
	rounded(s,x+2,y+2,w-4,h-4,3,Color(250,241,214));
}
void FrontendTheme::drawSelectionBackground(DrawableSurface* s,int x,int y,int w,int h)
{
	s->drawFilledRect(x,y,w,h,listSelectedElementColor);
}
bool FrontendTheme::drawSelector(DrawableSurface* s,int x,int y,int w,int h,unsigned value,unsigned maximum)
{
	rounded(s,x,y+h/2-1,w,6,3,Color(26,48,30));
	rounded(s,x+1,y+h/2,w-2,4,2,Color(226,208,170));
	const int position=maximum ? int((w-10)*double(value)/maximum) : 0;
	rounded(s,x+position,y-1,10,h+6,5,Color(26,48,30));
	rounded(s,x+position+2,y+1,6,h+2,3,Color(233,176,53));
	return true;
}

void FrontendTheme::drawButtonSelection(DrawableSurface* s,int x,int y,int w,int h)
{
	rounded(s,x,y,w,h,4,Color(92,74,198));
}
