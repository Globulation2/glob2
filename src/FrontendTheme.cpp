// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrontendTheme.h"
#include "MenuColony.h"
#include "DynamicClouds.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace GAGCore;
using namespace GAGGUI;
using namespace FrontendPalette;
FrontendTheme* FrontendTheme::current = nullptr;
bool FrontendTheme::allowed = true;
namespace { const char* fontNames[] = {"menu", "standard", "little"}; }

FrontendTheme::FrontendTheme() : original(Style::style)
{
	current = this;
	colony = std::make_unique<MenuColony>();
	textColor = ink;
	highlightColor = Color(74,110,58);
	frameColor = ink;
	listSelectedElementColor = gold;
	backColor = backOverlayColor = membrane;
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
namespace
{
// Per-row [left inset, right inset] of a blob contour. Corners get slightly
// unequal radii and the vertical edges a gentle sine wobble, both derived from
// the rect so the same control always draws the same outline.
struct Contour
{
	std::vector<std::pair<int,int>> inset;
	Contour(int x,int y,int w,int h,int r,int wobble,int inflate)
	{
		const unsigned seed = unsigned(x)*73856093u ^ unsigned(y)*19349663u ^ unsigned(w)*83492791u ^ unsigned(h)*2654435761u;
		x-=inflate; y-=inflate; w+=2*inflate; h+=2*inflate;
		r = std::min({r,w/2,h/2});
		const int jitter = r>=6 ? 1 : 0;
		const int rTL=r+((seed>>0&1)?jitter:-jitter), rTR=r+((seed>>1&1)?jitter:-jitter);
		const int rBL=r+((seed>>2&1)?jitter:-jitter), rBR=r+((seed>>3&1)?jitter:-jitter);
		const float phaseL=(seed>>4&255)/40.0f, phaseR=(seed>>12&255)/40.0f;
		inset.resize(std::max(0,h));
		auto corner=[](int radius,int row,int rows)->int
		{
			if(row>=radius) return 0;
			const float dy=radius-row-0.5f;
			return radius-int(std::sqrt(std::max(0.0f,float(radius*radius)-dy*dy)));
		};
		for(int row=0;row<h;++row)
		{
			int l=std::max(corner(rTL,row,h),corner(rBL,h-1-row,h));
			int rr=std::max(corner(rTR,row,h),corner(rBR,h-1-row,h));
			if(wobble)
			{
				l+=int(std::lround(wobble*std::sin(row*0.23f+phaseL)));
				rr+=int(std::lround(wobble*std::sin(row*0.19f+phaseR)));
			}
			inset[row]={std::max(0,l),std::max(0,rr)};
		}
	}
};
Color lighter(Color c,int by)
{
	return Color(std::min(255,c.r+by),std::min(255,c.g+by),std::min(255,c.b+by),c.a);
}
}

void FrontendTheme::blob(DrawableSurface* s,int x,int y,int w,int h,int r,Color fill,Color ink,int wobble,int inflate)
{
	if(w<=0 || h<=0) return;
	const Contour c(x,y,w,h,r,wobble,inflate);
	x-=inflate; y-=inflate; w+=2*inflate; h+=2*inflate;
	// Ink and fill never overlap, so a translucent fill shows what is behind it.
	for(int row=0;row<h;++row)
	{
		const auto [l,rr]=c.inset[row];
		const int span=w-l-rr;
		if(span<=0) continue;
		if(row<2 || row>=h-2) { s->drawFilledRect(x+l,y+row,span,1,ink); continue; }
		s->drawFilledRect(x+l,y+row,std::min(2,span),1,ink);
		if(span>2) s->drawFilledRect(x+w-rr-std::min(2,span-2),y+row,std::min(2,span-2),1,ink);
		if(span>4) s->drawFilledRect(x+l+2,y+row,span-4,1,row==2 ? lighter(fill,10) : fill);
	}
}
void FrontendTheme::ring(DrawableSurface* s,int x,int y,int w,int h,int r,Color ink,int wobble,int inflate)
{
	if(w<=0 || h<=0) return;
	const Contour c(x,y,w,h,r,wobble,inflate);
	x-=inflate; y-=inflate; w+=2*inflate; h+=2*inflate;
	for(int row=0;row<h;++row)
	{
		const auto [l,rr]=c.inset[row];
		if(row<2 || row>=h-2) { s->drawFilledRect(x+l,y+row,std::max(0,w-l-rr),1,ink); continue; }
		s->drawFilledRect(x+l,y+row,2,1,ink);
		s->drawFilledRect(x+w-rr-2,y+row,2,1,ink);
	}
}
int FrontendTheme::panelAlpha(int normal)
{
	return (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) ? 255 : normal;
}
void FrontendTheme::onFrame()
{
	if (!painted) return; // Present the still before doing any loading work.
	if (!attempted) { attempted=true; colony->load(); }
	SDL_Window* window = SDL_GetKeyboardFocus();
	const bool visible = window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED);
	colony->update(SDL_GetTicks64(), visible);
}
// Cloud shadows cross the panel and its controls: the interface is a thing in
// the world, not a window over it. The engine's own guard keeps this GL-only.
void FrontendTheme::afterPaint(DrawableSurface* s)
{
	if (s != globalContainer->gfx || Style::style != this || !colony->ready()) return;
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) return;
	if (!clouds) clouds = std::make_unique<DynamicClouds>(&globalContainer->settings);
	colony->drawClouds(*clouds, s->getW(), s->getH());
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
	s->drawFilledRect(0,0,w,h,Color(scrim.r,scrim.g,scrim.b,44));
	if (panel)
	{
		const SDL_Rect area=content ? *content : SDL_Rect{(w-640)/2,(h-480)/2,640,480};
		const int x=std::max(0,area.x-12), y=std::max(0,area.y-12);
		const int pw=std::min(w,area.x+area.w+12)-x, ph=std::min(h,area.y+area.h+12)-y;
		rounded(s,x+3,y+5,pw,ph,12,Color(12,28,16,70));
		blob(s,x,y,pw,ph,12,Color(membrane.r,membrane.g,membrane.b,panelAlpha()),ink,2);
	}
	painted=true;
}
void FrontendTheme::drawTextButtonBackground(DrawableSurface* s,int x,int y,int w,int h,unsigned hi)
{
	// Swells up to two pixels under the cursor; the hit rect is unchanged.
	blob(s,x,y,w,h,5,gel,ink,1,int(hi)*2/255);
	if(hi) rounded(s,x+2,y+2,w-4,h-4,3,Color(gold.r,gold.g,gold.b,hi/2));
}
void FrontendTheme::drawFrame(DrawableSurface* s,int x,int y,int w,int h,unsigned hi)
{
	// Frames are also drawn AFTER list contents; never erase their interior.
	ring(s,x,y,w,h,5,hi ? highlightColor : frameColor,w>=20 && h>=20 ? 1 : 0);
}
void FrontendTheme::drawOnOffButton(DrawableSurface* s,int x,int y,int w,int h,unsigned hi,bool state)
{
	blob(s,x,y,w,h,4,state ? gold : gel,ink,w>=20 && h>=20 ? 1 : 0);
	if(hi) rounded(s,x+2,y+2,w-4,h-4,3,Color(gold.r,gold.g,gold.b,hi/2));
	if(state)
	{
		for(int t=0;t<2;++t)
		{
			s->drawLine(x+w/5,y+h/2+t,x+w*2/5,y+h*3/4+t,ink);
			s->drawLine(x+w*2/5,y+h*3/4+t,x+w*4/5,y+h/4+t,ink);
		}
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
	blob(s,x,y,width,h,4,gelDisabled,ink,0);
	rounded(s,x+3,y+end+pos,width-6,len,3,muted);
	for(int i=0;i<4;++i)
	{
		s->drawLine(x+width/2-i,y+end/2+i,x+width/2+i,y+end/2+i,textColor);
		s->drawLine(x+width/2-i,y+h-end/2-i,x+width/2+i,y+h-end/2-i,textColor);
	}
}
void FrontendTheme::drawProgressBar(DrawableSurface* s,int x,int y,int w,int value,int range)
{
	const int h=getStyleMetric(STYLE_METRIC_PROGRESS_BAR_HEIGHT);
	blob(s,x,y,w,h,4,gelDisabled,ink,0);
	if(range>0) rounded(s,x+2,y+2,std::max(0,int((w-4)*double(std::clamp(value,0,range))/range)),h-4,3,gold);
}
int FrontendTheme::getStyleMetric(StyleMetrics m)
{
	// Preserve legacy hit geometry, including widgets initialized before execute.
	return original->getStyleMetric(m);
}

void FrontendTheme::drawFieldBackground(DrawableSurface* s,int x,int y,int w,int h)
{
	blob(s,x,y,w,h,5,gel,ink,w>=20 && h>=20 ? 1 : 0);
}
void FrontendTheme::drawSelectionBackground(DrawableSurface* s,int x,int y,int w,int h)
{
	s->drawFilledRect(x,y,w,h,listSelectedElementColor);
}
bool FrontendTheme::drawSelector(DrawableSurface* s,int x,int y,int w,int h,unsigned value,unsigned maximum)
{
	rounded(s,x,y+h/2-1,w,6,3,ink);
	rounded(s,x+1,y+h/2,w-2,4,2,gelDisabled);
	const int position=maximum ? int((w-12)*double(value)/maximum) : 0;
	blob(s,x+position,y-2,12,h+8,5,gold,ink,0);
	return true;
}

void FrontendTheme::drawButtonSelection(DrawableSurface* s,int x,int y,int w,int h)
{
	rounded(s,x+2,y+2,w-4,h-4,3,gold);
}
