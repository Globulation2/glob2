// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Availability.h"
#if GLOB2_DESKTOP_MCP
#include "Protocol.h"
#include <GraphicContext.h>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <ui/Host.h>
namespace MCP
{
class HttpServer;
enum class ListenerState
{
	Disabled,
	Starting,
	Running,
	Error
};

// The application thread owns every field except the mutex-protected admission
// queue. Transport workers only submit requests and signal atomic cancellation.
class UIService : public ExecutionContext
{
	GAGCore::GraphicContext &gfx;
	ToolRegistry registry;
	Protocol protocol;
	std::unique_ptr<HttpServer> server;
	std::vector<std::unique_ptr<HttpServer>> retiringServers;
	std::mutex mutex;
	std::deque<std::shared_ptr<Request>> queue;
	std::shared_ptr<Request> active;
	std::deque<std::shared_ptr<Request>> observers;
	std::shared_ptr<GAGGUI::ui::SemanticDispatch> semantic;
	struct CapturedViewport
	{
		std::uint64_t id, viewport;
		int width, height, nativeWidth, nativeHeight;
	};
	std::deque<CapturedViewport> captures;
	std::uint64_t nextCapture = 0;
	bool coordinatesConverted = false;
	Json inspection(const Json &arguments);
	bool addInspection(Result &result, const Json &arguments = Json::object());
	void observe();
	bool accepting = false, enabled = false, dispatched = false, capturing = false,
		 uiAvailable = false, awaitingDispatch = false;
	// Generation and actual consumption differ for staged cleanup/buffered input.
	bool dispatchAcknowledged = false;
	int port = 27202, width = 0, height = 0, logicalWidth = 0, logicalHeight = 0;
	Uint64 windowGeometry = 0;
	std::uint64_t viewport = 0, capture = 0;
	std::string failure, token, controller, cancellationReason;
	Clock::time_point renewed, dragStart;
	std::size_t dragIndex = 0;
	std::map<SDL_Scancode, Clock::time_point> keys;
	std::map<Uint8, Clock::time_point> buttons;
	std::set<SDL_Scancode> humanKeys;
	std::set<Uint8> humanButtons;
	std::vector<SDL_Scancode> temporaryKeys;
	Uint8 temporaryButton = 0;
	float pointerX = 0, pointerY = 0;
	std::deque<std::string> textStorage;
	std::vector<SDL_Event> cleanup, dispatchBatch;
	SDL_Keymod modifiers() const;
	SDL_MouseButtonFlags buttonMask() const;
	void key(SDL_Scancode, bool down, std::vector<SDL_Event> &);
	void button(Uint8, bool down, std::vector<SDL_Event> &, int clicks = 1);
	void move(const Json &, std::vector<SDL_Event> &);
	void release(std::vector<SDL_Event> &);
	void releaseTemporary(std::vector<SDL_Event> &);
	void finish(std::string error = {});
	void updateViewport();
	Json metadata() const;
	void startCapture();
	void maintainListener();
	void observeHumanInput(std::vector<SDL_Event> &);
	void expireInputs(Clock::time_point);
    void cancelControllerWork(const std::string &reason);
	void advanceRequest(std::vector<SDL_Event> &, bool available);
	void advanceDrag(const Json &, std::vector<SDL_Event> &, Clock::time_point);
	void dispatchKey(const Json &, std::vector<SDL_Event> &);

  public:
	// Called only at the application's UI boundary; supplied by the owning screen stack.
	std::function<std::vector<GAGGUI::ui::Host *>()> inspectionHosts;
	std::function<std::string()> inspectionScreen;
	std::function<Result(const std::string &, const Json &)> domainHandler;
	explicit UIService(GAGCore::GraphicContext &,
					   std::function<void(ToolRegistry &)> extensions = {});
	~UIService() override;
	std::shared_ptr<Request> submit(const std::string &, Json, std::string) override;
	std::shared_ptr<Request> enqueue(std::function<Result()>, std::string) override;
	void configure(bool enable, int newPort, bool retry = false);
	void revoke();
	// Main-thread calls only. Observation-only pumping merges human ownership;
	// the stack's later admission hook generates remote input and queued cleanup.
	// Unavailable/hidden frames still drain inspection and release requests.
	void pump(std::vector<SDL_Event> &, bool available, bool dispatchRequests = true);
	void drainCleanup(std::vector<SDL_Event> &);
	std::function<bool()> dispatchCancellation();
	bool currentAvailability() const;
	bool awaitingInputDispatch() const { return awaitingDispatch; }
	std::function<void(std::size_t, std::size_t)> dispatchCompletion();
	void afterDispatch(bool complete = true, std::size_t consumed = 0,
					   std::optional<bool> available = {});
	ListenerState listenerState() const;
	const std::string &failureReason() const { return failure; }
	bool hasController() const { return !token.empty(); }
	std::string status() const;
	std::string endpoint() const;
	std::string currentController() const { return controller.empty() ? "None" : controller; }
};
inline constexpr Uint32 RemoteInputDevice = 0x474c4f42;
inline bool remoteInput(const SDL_Event &e)
{
	switch (e.type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return e.key.which == RemoteInputDevice;
	case SDL_EVENT_MOUSE_MOTION:
		return e.motion.which == RemoteInputDevice;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return e.button.which == RemoteInputDevice;
	case SDL_EVENT_MOUSE_WHEEL:
		return e.wheel.which == RemoteInputDevice;
	default:
		return false;
	}
}
UIService *service();
} // namespace MCP
#endif
