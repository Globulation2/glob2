// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include <ScreenStack.h>
#include <PerformanceTelemetry.h>
#include <optional>
#include <BrowserTextInput.h>
#include <ApplicationHost.h>
#include <EventQueue.h>
#include <GraphicContext.h>
#include <stdexcept>
#include <typeinfo>
#include <algorithm>

namespace GAGGUI
{
ScreenStack::~ScreenStack()
{
	stop();
	boundary();
}

void ScreenStack::push(std::unique_ptr<Screen> screen, Completion completed)
{
	if (!screen)
		throw std::invalid_argument("Cannot push a null screen");
	if (stopped)
		throw std::logic_error("Cannot push onto a stopped screen stack");
	pending.push_back({std::move(completed), std::move(screen),
					   screens.empty() ? nullptr : screens.back().screen.get()});
}

void ScreenStack::suspendExecution()
{
    if (auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface)) context->resetRenderPacing();
	for (auto &entry : screens)
		entry.screen->suspendExecution();
	for (auto &entry : pending)
		entry.screen->suspendExecution();
}

void ScreenStack::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    if (auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface)) context->resetRenderPacing();
	for (auto &entry : screens)
		entry.screen->viewportResized(oldWidth, oldHeight, width, height);
	for (auto &entry : pending)
		entry.screen->viewportResized(oldWidth, oldHeight, width, height);
}

void ScreenStack::configureViewport(Screen& screen)
{
    if (auto* context = dynamic_cast<GAGCore::GraphicContext*>(&surface)) {
        context->setCompactWindowAllowed(screen.supportsCompactViewport());
        const int oldWidth=context->getW(), oldHeight=context->getH();
        const auto [minimumWidth,minimumHeight]=screen.minimumViewportSize();
        context->setResponsiveViewport(screen.usesResponsiveViewport(),minimumWidth,minimumHeight);
        if (oldWidth!=context->getW() || oldHeight!=context->getH())
            viewportResized(oldWidth,oldHeight,context->getW(),context->getH());
    }
}

bool ScreenStack::quitIntercepted() const
{
	// Only a running top screen with nothing queued above it can answer a quit
	// request; otherwise the request stops the stack as before.
	return !stopped && pending.empty() && !screens.empty() &&
		   screens.back().screen->isExecutionRunning() && screens.back().screen->interceptsQuit();
}

void ScreenStack::stop()
{
	stopped = true;
	// Destruction is deferred until outside screen callbacks.
}

void ScreenStack::boundary()
{
	if (!screens.empty() && !screens.back().screen->isExecutionRunning())
	{
		Entry completed = std::move(screens.back());
		screens.pop_back();
		// A parent that completes before its queued child is admitted cancels
		// that child, including continuations capturing the parent.
		std::erase_if(pending,
					  [&](const Entry &entry) { return entry.owner == completed.screen.get(); });
		lastResult = completed.screen->finishExecution();
		if (lastResult == Screen::QUIT_APPLICATION)
			stopped = true;
		if (!stopped && completed.completed)
			completed.completed(*completed.screen, lastResult);
		if (!screens.empty())
		{
			Screen &resumed = *screens.back().screen;
            configureViewport(resumed);
			GAGCore::ApplicationHost::screenChanged(typeid(resumed).name());
		}
	}
	if (stopped)
	{
		pending.clear();
		while (!screens.empty())
		{
			screens.back().screen->endExecute(Screen::QUIT_APPLICATION);
			screens.back().screen->finishExecution();
			screens.pop_back();
		}
		lastResult = Screen::QUIT_APPLICATION;
		return;
	}
	// Creation callbacks can queue another child, but cannot recurse into it.
	auto additions = std::move(pending);
	pending.clear();
	for (auto &entry : additions)
	{
		if (!screens.empty()) screens.back().screen->cancelExecutionInput();
        screens.push_back(std::move(entry));
        configureViewport(*screens.back().screen);
		screens.back().screen->beginExecution(&surface);
	}
}

void ScreenStack::frame(Uint32 tick, const std::vector<SDL_Event> &events, bool paint)
{
	if (dispatching)
		throw std::logic_error("Screen stack frames cannot recurse");
	struct Guard
	{
		bool &flag;
		Guard(bool &f) : flag(f) { flag = true; }
		~Guard() { flag = false; }
	} guard(dispatching);
    for (const auto& event:events) {
        if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND || event.type==SDL_EVENT_DID_ENTER_BACKGROUND) {
            backgrounded=true; suspendExecution();
            for (auto& entry:screens) entry.screen->cancelExecutionInput();
        } else if (event.type==SDL_EVENT_DID_ENTER_FOREGROUND) {
            backgrounded=false; suspendExecution();
        }
        if (event.type==SDL_EVENT_RENDER_DEVICE_RESET || event.type==SDL_EVENT_RENDER_TARGETS_RESET || event.type==SDL_EVENT_LOW_MEMORY) resetGraphics=true;
        if (event.type==SDL_EVENT_TERMINATING || (event.type==SDL_EVENT_QUIT && !quitIntercepted())) stop();
    }
    if (backgrounded) { if(stopped) boundary(); return; }
    if (resetGraphics) {
        if (auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface)) context->resetRenderPacing();
        SDL_Event reset{};reset.type=SDL_EVENT_RENDER_DEVICE_RESET;GAGCore::GraphicContext::translateMouseEvent(&reset);resetGraphics=false; }
	if (!quitIntercepted() && std::any_of(events.begin(), events.end(),
					[](const SDL_Event &e) { return e.type == SDL_EVENT_QUIT; }))
		stop();
    const int frameWidth=surface.getW(), frameHeight=surface.getH();
    GAGCore::beginBrowserTextFrame();
    bool presentationChanged=false;
    if (auto* context=dynamic_cast<GAGCore::GraphicContext*>(&surface))
        presentationChanged=context->refreshPresentation();
	boundary();
	if (screens.empty() || stopped) { GAGCore::endBrowserTextFrame(); return; }
	Screen &screen = *screens.back().screen;
    for (auto event:events) if((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && (event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type==SDL_EVENT_WINDOW_RESIZED)) {
        const int oldWidth=surface.getW(),oldHeight=surface.getH();
        GAGCore::GraphicContext::translateMouseEvent(&event);
        if(oldWidth!=surface.getW() || oldHeight!=surface.getH()) viewportResized(oldWidth,oldHeight,surface.getW(),surface.getH());
    }
    configureViewport(screen);
    if (presentationChanged) {
        for (auto& entry:screens) entry.screen->cancelExecutionInput();
        if (frameWidth==surface.getW() && frameHeight==surface.getH())
            viewportResized(surface.getW(),surface.getH(),surface.getW(),surface.getH());
    }
    for (const auto& event : events)
        if (((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && (event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)) || event.type==SDL_EVENT_WILL_ENTER_BACKGROUND)
            screen.cancelExecutionInput();
	// Pending child transitions suspend the parent immediately.
	for (const auto &event : events)
	{
		if (event.type == SDL_EVENT_QUIT && !screen.interceptsQuit())
		{
			stop();
			break;
		}
		if (stopped || !pending.empty() || !screen.isExecutionRunning())
			break;
        if ((event.type==SDL_EVENT_MOUSE_MOTION && event.motion.which==SDL_TOUCH_MOUSEID) ||
            ((event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) && event.button.which==SDL_TOUCH_MOUSEID)) continue;
		screen.handleExecutionEvent(event);
	}
	// Admit queued cancellation before advancing a potentially expensive load.
	if (!stopped && pending.empty() && screen.isExecutionRunning())
		screen.updateExecution(tick);
	// Constructing a child can change presentation immediately. Keep the last
	// completed frame until the child is admitted at the next boundary.
    if (paint) draw();
	if (stopped)
		boundary();
}

void ScreenStack::draw()
{
    if (backgrounded || stopped || !pending.empty() || screens.empty()) return;
    auto &screen = *screens.back().screen;
    if (!screen.isExecutionRunning()) return;
    auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface);
    if (context && !context->beginRenderFrame()) return;
    GAGCore::Recording::recorder().screen(screen.recordingId());
    screen.drawExecution();
}

Uint32 ScreenStack::delay(Uint32 now, Uint32 fallback)
{
	if (backgrounded) return 100;
    const auto delay = screens.empty() ? fallback : screens.back().screen->executionDelay(now, fallback);
    // Native hosts draw on update turns, so wake for whichever deadline comes
    // first. Browser painting has its own animation-frame chain and does not
    // change timer-driven update deadlines. Zero wait must not spin idle screens.
#ifndef __EMSCRIPTEN__
    if (delay != GAGCore::ApplicationHost::AnimationFrameDelay)
        if (auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface))
            if (const auto renderWait = context->renderFrameWait()) return std::min(delay, renderWait);
#endif
    return delay;
}

int ScreenStack::execute(unsigned stepLength)
{
	while (running())
	{
		const Uint64 start = SDL_GetTicks();
		GAGCore::EventQueue events;
		SDL_Event event;
		while (SDL_PollEvent(&event))
			events.push_back(event);
		frame(static_cast<Uint32>(start), events.events());
		if (running())
		{
			const Uint64 elapsed = SDL_GetTicks() - start;
			const Uint32 fallback = elapsed < stepLength ? stepLength - elapsed : 0;
			const Uint32 wait = delay(static_cast<Uint32>(SDL_GetTicks()), fallback);
			// A running game times its pacing waits, separating waits for the network.
			const auto kind = backgrounded || !top() ? Screen::ExecutionWait::Untimed : top()->executionWait();
			std::optional<PerformanceTelemetry::Scope> waitTime;
			if (kind != Screen::ExecutionWait::Untimed)
				waitTime.emplace(kind == Screen::ExecutionWait::Network ? PerformanceTelemetry::Id::NetworkSleep
				                                                        : PerformanceTelemetry::Id::Sleep);
			GAGCore::ApplicationHost::wait(wait);
		}
	}
	return result();
}
} // namespace GAGGUI
