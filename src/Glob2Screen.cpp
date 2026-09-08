// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Screen.h"
#include "gui/PhoneForm.h"
#include "gui/MobileSafeArea.h"
#include "GlobalContainer.h"
#include "DynamicClouds.h"
#include <Toolkit.h>


Glob2Screen::Glob2Screen()
{
}

Glob2Screen::~Glob2Screen()
{

}

void Glob2Screen::paint(void)
{
	static int time = 0;
	time++;
	randomSeed = 1;

	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
	dynamic_cast<GraphicContext*>(gfx)->finishDrawingSprite(globalContainer->terrain, 255);

	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		static DynamicClouds ds(&globalContainer->settings);
		ds.compute(0, 0, getW(), getH(), time);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::SHADOW);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::CLOUD);
	}
}

unsigned Glob2Screen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}




Glob2TabScreen::Glob2TabScreen(bool fullScreen, bool longerButtons)
	: TabScreen(fullScreen, longerButtons)
{
}

Glob2TabScreen::~Glob2TabScreen()
{

}

void Glob2TabScreen::paint(void)
{
	static int time = 0;
	time++;
	randomSeed = 1;

	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
	dynamic_cast<GraphicContext*>(gfx)->finishDrawingSprite(globalContainer->terrain, 255);

	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		static DynamicClouds ds(&globalContainer->settings);
		ds.compute(0, 0, getW(), getH(), time);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::SHADOW);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::CLOUD);
	}
}

unsigned Glob2TabScreen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}


bool Glob2Screen::phoneFormActive() const
{
#if defined(GLOB2_MOBILE)
    const bool requested=true;
#else
    const bool requested=SDL_getenv("GLOB2_PHONE_FORMS") && std::string(SDL_getenv("GLOB2_PHONE_FORMS"))=="1";
#endif
    return phoneFormEnabled && requested && globalContainer->gfx && globalContainer->gfx->hasPortableRenderer();
}
bool Glob2Screen::usesResponsiveViewport() const { return responsiveMenu || phoneFormActive(); }

bool Glob2Screen::responsiveActive() const
{
    auto* context = dynamic_cast<GraphicContext*>(gfx);
    return responsiveMenu && context && context->isResponsiveViewport();
}

void Glob2Screen::beginExecution(DrawableSurface* surface)
{
    Screen::beginExecution(surface);
    if(phoneFormActive()) {phoneForm=std::make_unique<PhoneForm>(*this);return;}
    if (!responsiveMenu) return;
    if (menuButtons.empty()) {
        for (auto* widget : widgets)
            if (auto* button = dynamic_cast<TextButton*>(widget)) menuButtons.push_back(button);
        std::sort(menuButtons.begin(), menuButtons.end(), [](auto* a, auto* b) {
            return a->getTop() == b->getTop() ? a->getLeft() < b->getLeft() : a->getTop() < b->getTop();
        });
    }
    if (responsiveActive()) layoutMenu(0);
}

void Glob2Screen::layoutMenu(double offset)
{
    const auto safe=mobileDialogSafe(globalContainer->gfx);
    double minimumWidth = 224;
    for (auto* button : menuButtons) minimumWidth = std::max(minimumWidth, button->textWidth() + 32.0);
    menuLayout = ResponsiveMenu::calculate(safe, menuButtons.size(), minimumWidth, offset);
    double minimumHeight = 48;
    for (size_t i = 0; i < menuButtons.size(); ++i)
        minimumHeight = std::max(minimumHeight, double(menuButtons[i]->wrappedHeight(int(menuLayout.buttons[i].w))));
    menuLayout = ResponsiveMenu::calculate(safe, menuButtons.size(), minimumWidth, offset, 1, minimumHeight);
    for (size_t i = 0; i < menuButtons.size(); ++i) {
        auto& rect = menuLayout.buttons[i];
        rect = {std::floor(rect.x), std::floor(rect.y), std::floor(rect.w), std::floor(rect.h)};
        menuButtons[i]->setScreenRectangle(int(rect.x), int(rect.y), int(rect.w), int(rect.h));
    }
    layoutW = getW(); layoutH = getH();
}

void Glob2Screen::updateExecution(Uint32 tick)
{
    if (responsiveActive() && (layoutW != getW() || layoutH != getH())) {
        cancelExecutionInput();
        layoutMenu(menuLayout.offset);
    }
    Screen::updateExecution(tick);
}

void Glob2Screen::cancelExecutionInput()
{
    menuTouch.cancel();
    if(phoneForm) phoneForm->cancel();
}

void Glob2Screen::menuActions(const std::vector<TouchAction>& actions)
{
    for (const auto& action : actions) {
        if (action.kind == TouchActionKind::Pan) layoutMenu(menuLayout.offset - action.point.y);
        if (action.kind == TouchActionKind::Select && menuLayout.hit(action.point) >= 0) {
            SDL_Event click{}; click.type = SDL_MOUSEBUTTONUP;
            click.button.button = SDL_BUTTON_LEFT;
            click.button.x = static_cast<int>(action.point.x); click.button.y = static_cast<int>(action.point.y);
            Screen::handleExecutionEvent(click);
        }
    }
}

void Glob2Screen::handleExecutionEvent(SDL_Event event)
{
    if(phoneFormActive() && phoneForm) {if(!phoneForm->event(event)) Screen::handleExecutionEvent(event);return;}
    if (!responsiveActive()) { Screen::handleExecutionEvent(event); return; }
    switch (event.type) {
    case SDL_FINGERDOWN:
        menuActions(menuTouch.down(event.tfinger.touchId, event.tfinger.fingerId, {event.tfinger.x * getW(), event.tfinger.y * getH()})); return;
    case SDL_FINGERMOTION:
        menuActions(menuTouch.move(event.tfinger.touchId, event.tfinger.fingerId, {event.tfinger.x * getW(), event.tfinger.y * getH()})); return;
    case SDL_FINGERUP:
        menuActions(menuTouch.up(event.tfinger.touchId, event.tfinger.fingerId, {event.tfinger.x * getW(), event.tfinger.y * getH()})); return;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (event.button.which == SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT) return;
        if (event.type == SDL_MOUSEBUTTONDOWN) menuActions(menuTouch.down(-1, 0, {double(event.button.x), double(event.button.y)}));
        else menuActions(menuTouch.up(-1, 0, {double(event.button.x), double(event.button.y)}));
        return;
    case SDL_MOUSEMOTION:
        if (event.motion.which != SDL_TOUCH_MOUSEID)
            menuActions(menuTouch.move(-1, 0, {double(event.motion.x), double(event.motion.y)}));
        return;
    case SDL_MOUSEWHEEL:
        cancelExecutionInput();
        layoutMenu(menuLayout.offset - event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -48 : 48));
        return;
    default: Screen::handleExecutionEvent(event);
    }
}

void Glob2Screen::drawExecution()
{
    if(phoneFormActive() && phoneForm) {
        if(isExecutionRunning()) {paint();phoneForm->draw();globalContainer->gfx->nextFrame();}return;
    }
    if (!responsiveActive()) { Screen::drawExecution(); return; }
    if (!isExecutionRunning()) return;
    auto* context = dynamic_cast<GraphicContext*>(gfx);
    context->setClipRect();
    paint();
    const auto safe=mobileDialogSafe(context);
    if (menuTitle.empty()) {
        auto* title = globalContainer->title.get();
        const double scale = std::min((safe.w - 16.0) / title->getW(), 40.0 / title->getH());
        const int width = int(title->getW() * scale), height = int(title->getH() * scale);
        gfx->drawSurface(int(safe.x+(safe.w-width)/2),int(safe.y+(56-height)/2),width,height,title);
    } else {
        auto* font = Toolkit::getFont("menu");
        gfx->drawString(int(safe.x+(safe.w-font->getStringWidth(menuTitle))/2),int(safe.y+16),font,menuTitle);
    }
    const auto& clip = menuLayout.content;
    context->setClipRect(int(clip.x), int(clip.y), int(clip.w), int(clip.h));
    for (size_t i = 0; i < menuButtons.size(); ++i) {
        const auto& rect = menuLayout.buttons[i];
        if (rect.y + rect.h > clip.y && rect.y < clip.y + clip.h) menuButtons[i]->paintResponsive();
    }
    context->setClipRect();
    if (menuLayout.maximumOffset > 0) {
        const int thumb = std::max(16, int(clip.h * clip.h / (clip.h + menuLayout.maximumOffset)));
        const int top = int(clip.y + menuLayout.offset / menuLayout.maximumOffset * (clip.h - thumb));
        gfx->drawFilledRect(getW() - 5, top, 3, thumb, Color(240, 220, 150));
    }
    context->nextFrame();
}
