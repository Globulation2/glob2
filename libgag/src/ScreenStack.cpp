// SPDX-License-Identifier: GPL-3.0-or-later
#include <ScreenStack.h>
#include <ApplicationHost.h>
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
	for (auto &entry : screens)
		entry.screen->suspendExecution();
	for (auto &entry : pending)
		entry.screen->suspendExecution();
}

void ScreenStack::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
	for (auto &entry : screens)
		entry.screen->viewportResized(oldWidth, oldHeight, width, height);
	for (auto &entry : pending)
		entry.screen->viewportResized(oldWidth, oldHeight, width, height);
}

void ScreenStack::configureViewport(Screen& screen)
{
    if (auto* context = dynamic_cast<GAGCore::GraphicContext*>(&surface)) {
        const int oldWidth=context->getW(), oldHeight=context->getH();
        const auto [minimumWidth,minimumHeight]=screen.minimumViewportSize();
        context->setResponsiveViewport(screen.usesResponsiveViewport(),minimumWidth,minimumHeight);
        if (oldWidth!=context->getW() || oldHeight!=context->getH())
            viewportResized(oldWidth,oldHeight,context->getW(),context->getH());
    }
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

void ScreenStack::frame(Uint32 tick, const std::vector<SDL_Event> &events)
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
        if (event.type==SDL_APP_WILLENTERBACKGROUND || event.type==SDL_APP_DIDENTERBACKGROUND) {
            backgrounded=true; suspendExecution();
            for (auto& entry:screens) entry.screen->cancelExecutionInput();
        } else if (event.type==SDL_APP_DIDENTERFOREGROUND) {
            backgrounded=false; suspendExecution();
        }
        if (event.type==SDL_RENDER_DEVICE_RESET || event.type==SDL_RENDER_TARGETS_RESET || event.type==SDL_APP_LOWMEMORY) resetGraphics=true;
        if (event.type==SDL_APP_TERMINATING || event.type==SDL_QUIT) stop();
    }
    if (backgrounded) { if(stopped) boundary(); return; }
    if (resetGraphics) { SDL_Event reset{};reset.type=SDL_RENDER_DEVICE_RESET;GAGCore::GraphicContext::translateMouseEvent(&reset);resetGraphics=false; }
	if (std::any_of(events.begin(), events.end(),
					[](const SDL_Event &e) { return e.type == SDL_QUIT; }))
		stop();
	boundary();
	if (screens.empty() || stopped)
		return;
	Screen &screen = *screens.back().screen;
    for (auto event:events) if(event.type==SDL_WINDOWEVENT && (event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event==SDL_WINDOWEVENT_RESIZED)) {
        const int oldWidth=surface.getW(),oldHeight=surface.getH();
        GAGCore::GraphicContext::translateMouseEvent(&event);
        if(oldWidth!=surface.getW() || oldHeight!=surface.getH()) viewportResized(oldWidth,oldHeight,surface.getW(),surface.getH());
    }
    configureViewport(screen);
    for (const auto& event : events)
        if ((event.type==SDL_WINDOWEVENT && (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST || event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED)) || event.type==SDL_APP_WILLENTERBACKGROUND)
            screen.cancelExecutionInput();
	// Pending child transitions suspend the parent immediately.
	for (const auto &event : events)
	{
		if (event.type == SDL_QUIT)
		{
			stop();
			break;
		}
		if (stopped || !pending.empty() || !screen.isExecutionRunning())
			break;
		screen.handleExecutionEvent(event);
	}
	// Admit queued cancellation before advancing a potentially expensive load.
	if (!stopped && pending.empty() && screen.isExecutionRunning())
		screen.updateExecution(tick);
	// Constructing a child can change presentation immediately. Keep the last
	// completed frame until the child is admitted at the next boundary.
	if (!stopped && pending.empty() && screen.isExecutionRunning())
		screen.drawExecution();
	if (stopped)
		boundary();
}

Uint32 ScreenStack::delay(Uint32 now, Uint32 fallback)
{
	if (backgrounded) return 100;
    return screens.empty() ? fallback : screens.back().screen->executionDelay(now, fallback);
}

int ScreenStack::execute(unsigned stepLength)
{
	while (running())
	{
		const Uint64 start = SDL_GetTicks64();
		std::vector<SDL_Event> events;
		SDL_Event event;
		while (SDL_PollEvent(&event))
			events.push_back(event);
		frame(static_cast<Uint32>(start), events);
		if (running())
		{
			const Uint64 elapsed = SDL_GetTicks64() - start;
			const Uint32 fallback = elapsed < stepLength ? stepLength - elapsed : 0;
			GAGCore::ApplicationHost::wait(delay(static_cast<Uint32>(SDL_GetTicks64()), fallback));
		}
	}
	return result();
}
} // namespace GAGGUI
