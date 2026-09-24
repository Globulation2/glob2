// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Screen.h"
#include "FrontendTheme.h"
#include "gui/PhoneForm.h"
#include "gui/MobileSafeArea.h"
#include "GlobalContainer.h"

#include <Toolkit.h>
#include <InterfacePresentation.h>
#include <GUIText.h>

namespace
{
void drawFrontend(DrawableSurface *surface, const std::set<Widget *> &widgets)
{
	// Most menus use the centered 640x480 canvas. Expand for full-window
	// lists/charts, while keeping centered ALIGN_FILL titles inside the common canvas.
	SDL_Rect content{(surface->getW() - 640) / 2, (surface->getH() - 480) / 2, 640, 480};
	for (auto *widget : widgets)
	{
		if (!widget->visible)
			continue;
		auto *rectangle = dynamic_cast<RectangularWidget *>(widget);
		if (!rectangle)
			continue;
		SDL_Rect bounds = rectangle->getScreenRect();
		if (dynamic_cast<Text *>(widget) && bounds.w == surface->getW())
		{
			bounds.x = (surface->getW() - 640) / 2;
			bounds.w = 640;
		}
		SDL_UnionRect(&content, &bounds, &content);
	}
	FrontendTheme::current->background(surface, true, &content);
}
} // namespace

Glob2Screen::Glob2Screen() {}

Glob2Screen::~Glob2Screen() {}

int Glob2Screen::execute(DrawableSurface *surface, int stepLength)
{
	FrontendScope scope;
	return Screen::execute(surface, stepLength);
}

void Glob2Screen::paint(void)
{
	if (FrontendTheme::current)
		drawFrontend(gfx, widgets);
	else
		gfx->drawFilledRect(0, 0, getW(), getH(), Color(17, 35, 32));
}

Glob2TabScreen::Glob2TabScreen(bool fullScreen, bool longerButtons)
	: TabScreen(fullScreen, longerButtons)
{
}

Glob2TabScreen::~Glob2TabScreen() {}

int Glob2TabScreen::execute(DrawableSurface *surface, int stepLength)
{
	FrontendScope scope;
	return Screen::execute(surface, stepLength);
}

void Glob2TabScreen::paint(void)
{
	if (FrontendTheme::current)
		drawFrontend(gfx, widgets);
	else
		gfx->drawFilledRect(0, 0, getW(), getH(), Color(17, 35, 32));
}


bool Glob2Screen::phoneFormActive() const
{
    const bool requested=phonePresentationRequested();
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
    if(phoneFormActive()) {phoneForm=std::make_unique<PhoneForm>(*this,[this](auto* w){return phoneLabel(w);},[this](auto* w){return phoneVisible(w);},[this](auto* w){return phoneFooter(w);},[this](auto* w){return phoneColor(w);});return;}
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
    auto safe=mobileDialogSafe(globalContainer->gfx);
    double minimumHeight = 48;
if (phonePresentationRequested()) {
    // Keep phone menus compact instead of stretching across the landscape display.
    const double width=std::min(safe.w,640.0);
    safe.x+=(safe.w-width)/2;safe.w=width;
    minimumHeight=44;
}
    double minimumWidth = 224;
    for (auto* button : menuButtons) minimumWidth = std::max(minimumWidth, button->textWidth() + 32.0);
    menuLayout = ResponsiveMenu::calculate(safe, menuButtons.size(), minimumWidth, offset, 1, minimumHeight);
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
        if (rect.y + rect.h > clip.y && rect.y < clip.y + clip.h) if(menuButtons[i]->textWidth()<=menuLayout.buttons[i].w-24) menuButtons[i]->paint();
        else menuButtons[i]->paintResponsive();
    }
    context->setClipRect();
    if (menuLayout.maximumOffset > 0) {
        const int thumb = std::max(16, int(clip.h * clip.h / (clip.h + menuLayout.maximumOffset)));
        const int top = int(clip.y + menuLayout.offset / menuLayout.maximumOffset * (clip.h - thumb));
        gfx->drawFilledRect(getW() - 5, top, 3, thumb, Color(240, 220, 150));
    }
    context->nextFrame();
}


bool Glob2TabScreen::usesResponsiveViewport() const
{
    const bool requested=phonePresentationRequested();
    return phoneEnabled && requested && globalContainer->gfx && globalContainer->gfx->hasPortableRenderer();
}
void Glob2TabScreen::beginExecution(DrawableSurface* surface)
{
    TabScreen::beginExecution(surface);
    if(phoneEnabled && usesResponsiveViewport()) phoneForm=std::make_unique<PhoneForm>(*this,
        [this](auto* w){auto i=phoneLabels.find(w);return i==phoneLabels.end() ? std::string{} : i->second->getText();},
        [this](auto* w){if(phoneHidden.count(w)) return false;for(const auto& entry:phoneLabels) if(entry.second==w) return false;return true;},
        [this](auto* w){return phoneFooters.count(w)>0;},
        [](auto*){return std::optional<GAGCore::Color>{};},
        [this](auto* w){return phoneValueLabels.count(w)>0;});
}
void Glob2TabScreen::handleExecutionEvent(SDL_Event event)
{
    if(phoneForm && phoneForm->event(event)) return;
    TabScreen::handleExecutionEvent(event);
}
void Glob2TabScreen::drawExecution()
{
    if(!phoneForm) {TabScreen::drawExecution();return;}
    if(isExecutionRunning()) {paint();phoneForm->draw();globalContainer->gfx->nextFrame();}
}
void Glob2TabScreen::cancelExecutionInput()
{
    if(phoneForm) phoneForm->cancel();
}

void Glob2Screen::showPhoneStatus(GAGGUI::Text* title, const std::string& text) {
    if (title->getText() == text) return;
    title->setText(text);
    if (phoneForm) phoneForm->scrollToTop();
}
