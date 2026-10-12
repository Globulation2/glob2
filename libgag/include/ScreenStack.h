// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <functional>
#include <memory>
#include <vector>

namespace GAGGUI
{
// Owns screens through completion. Mutations requested by callbacks become
// visible at the next frame boundary; a child never receives its opening input.
class ScreenStack
{
  public:
	using Completion = std::function<void(Screen &, int)>;
	explicit ScreenStack(GAGCore::DrawableSurface &surface) : surface(surface) {}
	~ScreenStack();
	void push(std::unique_ptr<Screen> screen, Completion completed = {});
	// Additional input is prepared after ordinary input and viewport changes,
	// only while the current screen can still receive it. Completion reports the
	// consumed prefix; opening a child intentionally discards the remaining tail.
	struct AdditionalInput
	{
		std::function<void(std::vector<SDL_Event> &)> prepare;
		// Releases may proceed through a buffered human frame without admitting
		// coordinate/key operations whose current target has not yet settled.
		std::function<void(std::vector<SDL_Event> &)> prepareCleanup;
		std::function<std::function<bool()>()> cancellation;
		std::function<void(std::size_t consumed, std::size_t total)> complete;
		// Snapshot batch-specific completion after prepare, before buffering.
		std::function<std::function<void(std::size_t, std::size_t)>()> completion;
		// Return true when an application-global action consumes the event.
		std::function<bool(const SDL_Event &)> intercept;
	};
	// Input, transitions and updates run even when painting is deferred.
	void frame(Uint32 tick, const std::vector<SDL_Event> &events, bool paint = true,
			   const AdditionalInput &additional = {});
	// Paint only: browser hosts call this independently on animation frames.
	void draw();
	bool running() const { return !stopped && (!screens.empty() || !pending.empty()); }
	Uint32 delay(Uint32 now, Uint32 fallback);
	int result() const { return lastResult; }
	// The screen receiving input, or null while the stack is empty.
	Screen *top() const { return screens.empty() ? nullptr : screens.back().screen.get(); }
	// Transitional SDL host. Browser scheduling will call frame directly.
	int execute(unsigned stepLength = 40);
	void stop();
	// True when the running top screen will receive SDL_EVENT_QUIT instead of the stack stopping.
	bool quitIntercepted() const;
	void suspendExecution();
	// Resolve consumer receipts without advancing screens or simulation (hidden hosts).
	void pollInputReceipts();
	void viewportResized(int oldWidth, int oldHeight, int width, int height);

  private:
	struct PendingInput
	{
		std::shared_ptr<GAGCore::InputDispatchReceipt> receipt;
		std::function<void(std::size_t, std::size_t)> complete;
		std::vector<std::size_t> bufferedIndices;
		std::size_t delivered = 0, total = 0;
	};
	std::vector<PendingInput> pendingInput;
	struct Entry
	{
		Completion completed;
		std::unique_ptr<Screen> screen;
		Screen *owner;
	};
	GAGCore::DrawableSurface &surface;
	std::vector<Entry> screens, pending;
	int lastResult = 0;
	bool stopped = false, dispatching = false;
	bool backgrounded=false, resetGraphics=false;
    void boundary();
    void configureViewport(Screen& screen);
};
} // namespace GAGGUI
