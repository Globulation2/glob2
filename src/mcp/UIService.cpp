// SPDX-License-Identifier: GPL-3.0-or-later
#include "UIService.h"
#if GLOB2_DESKTOP_MCP
#include "HttpServer.h"
#include <ApplicationHost.h>
#include <algorithm>
#include <cmath>
namespace MCP
{
namespace
{
UIService *current = nullptr;
bool inputTool(const std::string &tool)
{
	return tool != "ui_status" && tool != "ui_screenshot" && tool != "ui_acquire_control";
}
Uint8 mouseButton(const Json &args)
{
	const auto name = args.value("button", "left");
	return name == "right"    ? SDL_BUTTON_RIGHT
		   : name == "middle" ? SDL_BUTTON_MIDDLE
							  : SDL_BUTTON_LEFT;
}
SDL_Keymod modifier(SDL_Scancode s)
{
	switch (s)
	{
	case SDL_SCANCODE_LCTRL:
		return SDL_KMOD_LCTRL;
	case SDL_SCANCODE_RCTRL:
		return SDL_KMOD_RCTRL;
	case SDL_SCANCODE_LSHIFT:
		return SDL_KMOD_LSHIFT;
	case SDL_SCANCODE_RSHIFT:
		return SDL_KMOD_RSHIFT;
	case SDL_SCANCODE_LALT:
		return SDL_KMOD_LALT;
	case SDL_SCANCODE_RALT:
		return SDL_KMOD_RALT;
	case SDL_SCANCODE_LGUI:
		return SDL_KMOD_LGUI;
	case SDL_SCANCODE_RGUI:
		return SDL_KMOD_RGUI;
	default:
		return SDL_KMOD_NONE;
	}
}
} // namespace
UIService *service()
{
	return current;
}
UIService::UIService(GAGCore::GraphicContext &g, std::function<void(ToolRegistry &)> extensions)
	: gfx(g), protocol(registry, *this)
{
	registerUITools(registry);
	if (extensions)
		extensions(registry);
	current = this;
}
UIService::~UIService()
{
	configure(false, port);
	// Final teardown may join workers: normal reconfiguration never blocks a UI
	// frame, and these workers must finish before their protocol context dies.
	retiringServers.clear();
	current = nullptr;
	if (capture)
		gfx.cancelMemoryCapture(capture);
}
std::shared_ptr<Request> UIService::submit(const std::string &tool, Json args, std::string client)
{
	auto r = std::make_shared<Request>();
	r->tool = tool;
	r->arguments = std::move(args);
	r->client = std::move(client);
	std::lock_guard lock(mutex);
	if (!accepting || queue.size() >= 64)
	{
		Result result;
		result.error = accepting ? "UI request queue full" : "MCP server disabled";
		r->completion.set_value(std::move(result));
	}
	else
		queue.push_back(r);
	return r;
}
std::shared_ptr<Request> UIService::enqueue(std::function<Result()> execute, std::string client)
{
	auto r = std::make_shared<Request>();
	r->tool = "extension";
	r->execute = std::move(execute);
	r->client = std::move(client);
	std::lock_guard lock(mutex);
	if (!accepting || queue.size() >= 64)
	{
		Result result;
		result.error = "MCP unavailable or queue full";
		r->completion.set_value(std::move(result));
	}
	else
		queue.push_back(r);
	return r;
}
void UIService::configure(bool enable, int newPort, bool retry)
{
	if (enable == enabled && newPort == port && !retry)
		return;
	{
		std::lock_guard lock(mutex);
		accepting = false;
	}
	if (server)
	{
		server->stop();
		retiringServers.push_back(std::move(server));
	}
	revoke();
	// Complete inspection requests too: no request survives listener replacement.
	{
		std::lock_guard lock(mutex);
		for (auto &r : queue)
		{
			Result result;
			result.error = "Listener stopped";
			r->completion.set_value(std::move(result));
		}
		queue.clear();
	}
	if (active)
		finish("Listener stopped");
	enabled = enable;
	port = std::clamp(newPort, 1, 65535);
	failure.clear();
	maintainListener();
}
void UIService::maintainListener()
{
	// Join only workers that have already exited. The old port is not reused
	// until its confined shutdown finishes, including any in-flight PNG encode.
	std::erase_if(retiringServers, [](const auto &retiring) { return retiring->stopped(); });
	if (!enabled || server || !retiringServers.empty() || !failure.empty())
		return;
	try
	{
		server = std::make_unique<HttpServer>(protocol, port);
		std::lock_guard lock(mutex);
		accepting = true;
	}
	catch (const std::exception &error)
	{
		failure = error.what();
	}
}

std::string UIService::endpoint() const
{
	return "http://127.0.0.1:" + std::to_string(port) + "/mcp";
}
ListenerState UIService::listenerState() const
{
	if (!enabled)
		return ListenerState::Disabled;
	if (!failure.empty())
		return ListenerState::Error;
	return server ? ListenerState::Running : ListenerState::Starting;
}
std::string UIService::status() const
{
	switch (listenerState())
	{
	case ListenerState::Disabled:
		return "Disabled";
	case ListenerState::Starting:
		return "Starting";
	case ListenerState::Running:
		return "Running";
	case ListenerState::Error:
		return "Error: " + failure;
	}
	return "Disabled";
}
void UIService::revoke()
{
	token.clear();
	controller.clear();
	release(cleanup);
	{
		std::lock_guard lock(mutex);
		auto i = queue.begin();
		while (i != queue.end())
		{
			if ((*i)->tool == "ui_status" || (*i)->tool == "ui_screenshot")
			{
				++i;
				continue;
			}
			Result result;
			result.error = "Control revoked";
			(*i)->completion.set_value(std::move(result));
			i = queue.erase(i);
		}
	}
	if (active && active->tool != "ui_status" && active->tool != "ui_screenshot")
		finish("Control revoked");
}
SDL_Keymod UIService::modifiers() const
{
	SDL_Keymod result = SDL_KMOD_NONE;
	for (const auto &[s, time] : keys)
		result |= modifier(s);
	for (auto s : humanKeys)
		result |= modifier(s);
	return result;
}
SDL_MouseButtonFlags UIService::buttonMask() const
{
	SDL_MouseButtonFlags mask = 0;
	for (const auto &[b, time] : buttons)
		mask |= SDL_BUTTON_MASK(b);
	for (auto b : humanButtons)
		mask |= SDL_BUTTON_MASK(b);
	return mask;
}
void UIService::key(SDL_Scancode s, bool down, std::vector<SDL_Event> &events)
{
	// Ownership is independent of delivered state: a remote hold that overlaps a
	// human hold must be remembered, but must never release the human's input.
	const bool held = keys.contains(s);
	if (down)
		keys[s] = Clock::now();
	else
		keys.erase(s);
	if (humanKeys.contains(s) || (down && held) || (!down && !held))
		return;
	SDL_Event event{};
	event.common.timestamp = SDL_GetTicksNS();
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = SDL_GetWindowID(gfx.getWindow());
	event.key.which = RemoteInputDevice;
	event.key.scancode = s;
	event.key.mod = modifiers();
	event.key.down = down;
	// Match native KEY events, including SDL_HINT_KEYCODE_OPTIONS, rather than
	// translating a character from modifiers (text uses SDL_EVENT_TEXT_INPUT).
	event.key.key = SDL_GetKeyFromScancode(s, event.key.mod, true);
	events.push_back(event);
}
void UIService::button(Uint8 b, bool down, std::vector<SDL_Event> &events, int clicks)
{
	const bool held = buttons.contains(b);
	if (down)
		buttons[b] = Clock::now();
	else
		buttons.erase(b);
	if (humanButtons.contains(b) || (down && held) || (!down && !held))
		return;
	SDL_Event event{};
	event.common.timestamp = SDL_GetTicksNS();
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.windowID = SDL_GetWindowID(gfx.getWindow());
	event.button.which = RemoteInputDevice;
	event.button.button = b;
	event.button.down = down;
	event.button.clicks = clicks;
	event.button.x = pointerX;
	event.button.y = pointerY;
	events.push_back(event);
}
void UIService::move(const Json &p, std::vector<SDL_Event> &events)
{
	float x = p["x"].get<float>(), y = p["y"].get<float>();
	gfx.outputToWindow(x, y);
	SDL_Event event{};
	event.common.timestamp = SDL_GetTicksNS();
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.windowID = SDL_GetWindowID(gfx.getWindow());
	event.motion.which = RemoteInputDevice;
	event.motion.x = x;
	event.motion.y = y;
	event.motion.xrel = x - pointerX;
	event.motion.yrel = y - pointerY;
	pointerX = x;
	pointerY = y;
	event.motion.state = buttonMask();
	events.push_back(event);
}
void UIService::releaseTemporary(std::vector<SDL_Event> &events)
{
	if (temporaryButton)
		button(temporaryButton, false, events);
	for (auto i = temporaryKeys.rbegin(); i != temporaryKeys.rend(); ++i)
		key(*i, false, events);
	temporaryButton = 0;
	temporaryKeys.clear();
}
void UIService::release(std::vector<SDL_Event> &events)
{
	while (!keys.empty())
		key(keys.begin()->first, false, events);
	while (!buttons.empty())
		button(buttons.begin()->first, false, events);
	temporaryKeys.clear();
	temporaryButton = 0;
}
void UIService::updateViewport()
{
	int w = gfx.getDrawableW(), h = gfx.getDrawableH();
	if (w <= 0 || h <= 0)
	{
		w = gfx.getW();
		h = gfx.getH();
	}

	// Identity includes output/window/logical geometry, not just PNG dimensions.
	int ww = 0, wh = 0;
	if (gfx.getWindow())
		SDL_GetWindowSize(gfx.getWindow(), &ww, &wh);
	const auto transform = (Uint64(Uint32(ww)) << 32) | Uint32(wh);
	if (w != width || h != height || logicalWidth != gfx.getW() || logicalHeight != gfx.getH() ||
		transform != windowGeometry)
	{
		width = w;
		height = h;
		logicalWidth = gfx.getW();
		logicalHeight = gfx.getH();
		windowGeometry = transform;
		++viewport;
	}
}
Json UIService::metadata() const
{
	return {{"width", width},
			{"height", height},
			{"viewport_id", viewport},
			{"frame_id", gfx.frameIdentity()},
			{"available", uiAvailable && bool(gfx.getWindow() && gfx.getSDLSurface())},
			{"controller", currentController()},
			{"controlled", !token.empty()}};
}
void UIService::finish(std::string error)
{
	if (!active)
		return;
	if (!error.empty() && !cancellationReason.empty())
		error = cancellationReason;
	auto r = std::move(active);
	// Generating the final batch does not acknowledge a buffered consumer.
	const bool dispatchComplete = dispatched && dispatchAcknowledged;
	auto id = capture;
	capture = 0;
	capturing = false;
	dispatched = false;
	dispatchAcknowledged = false;
	Result result;
	result.data = metadata();
	result.data["dispatched"] = dispatchComplete;
	result.data["input_started"] = r->inputStarted.load();
	result.error = std::move(error);
	if (!result.error.empty())
		r->cancelled = true;
	r->completion.set_value(std::move(result));
	if (id)
		gfx.cancelMemoryCapture(id);
}
void UIService::startCapture()
{
	capturing = true;
	auto request = active;
	auto capturedViewport = viewport;
	capture = gfx.requestMemoryCapture(
		[this, request, capturedViewport](std::shared_ptr<SDL_Surface> pixels, std::uint64_t frame,
										  std::string error)
		{
			if (active != request)
				return;
			updateViewport();
			Result result;
			result.data = metadata();
			result.data["viewport_id"] = capturedViewport;
			result.data["frame_id"] = frame;
			result.data["dispatched"] = request->tool != "ui_screenshot";
			if (pixels)
			{
				result.data["width"] = pixels->w;
				result.data["height"] = pixels->h;
			}
			result.pixels = std::move(pixels);
			result.error = std::move(error);
			if (!result.pixels && result.error.empty())
				result.error = "No capturable frame";
			if (request->cancelled || viewport != capturedViewport)
			{
				result.pixels.reset();
				result.error =
					request->cancelled ? "Capture cancelled" : "Viewport changed during capture";
			}
			active.reset();
			capture = 0;
			capturing = false;
			dispatched = false;
			dispatchAcknowledged = false;
			request->completion.set_value(std::move(result));
		});
	if (!active)
		capture = 0; // Synchronous admission failure already completed it.
}
void UIService::observeHumanInput(std::vector<SDL_Event> &events)
{
	std::vector<SDL_Event> human;
	human.reserve(events.size());
	for (auto event : events)
	{
		if (remoteInput(event))
		{
			human.push_back(event);
			continue;
		}
		bool deliver = true;
		if (event.type == SDL_EVENT_KEY_DOWN)
			humanKeys.insert(event.key.scancode);
		if (event.type == SDL_EVENT_KEY_UP)
		{
			humanKeys.erase(event.key.scancode);
			deliver = !keys.contains(event.key.scancode);
		}
		if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
		{
			for (const auto &[s, time] : keys)
				event.key.mod |= modifier(s);
		}
		if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			humanButtons.insert(event.button.button);
		if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
		{
			humanButtons.erase(event.button.button);
			deliver = !buttons.contains(event.button.button);
		}
		if (event.type == SDL_EVENT_MOUSE_MOTION)
		{
			pointerX = event.motion.x;
			pointerY = event.motion.y;
			for (const auto &[b, time] : buttons)
				event.motion.state |= SDL_BUTTON_MASK(b);
		}
		if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
		{
			pointerX = event.button.x;
			pointerY = event.button.y;
		}
		if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
		{
			humanKeys.clear();
			humanButtons.clear();
			revoke();
		}
		if (deliver)
			human.push_back(event);
	}
	events = std::move(human);
}
void UIService::expireInputs(Clock::time_point now)
{
	if (!token.empty() && now - renewed >= std::chrono::seconds(60))
		revoke();
	for (auto i = keys.begin(); i != keys.end();)
	{
		auto s = i->first;
		auto t = i++->second;
		// An accepted DOWN may wait in an engine queue. Its five-second hold
		// begins at actual consumption, while the request deadline still bounds
		// how long that pending event may wait before cancellation.
		const bool pendingDown =
			awaitingDispatch && std::any_of(dispatchBatch.begin(), dispatchBatch.end(),
											[s](const SDL_Event &event) {
												return remoteInput(event) &&
													   event.type == SDL_EVENT_KEY_DOWN &&
													   event.key.scancode == s;
											});
		if (!pendingDown && now - t >= std::chrono::seconds(5))
			key(s, false, cleanup);
	}
	for (auto i = buttons.begin(); i != buttons.end();)
	{
		auto b = i->first;
		auto t = i++->second;
		const bool pendingDown =
			awaitingDispatch && std::any_of(dispatchBatch.begin(), dispatchBatch.end(),
											[b](const SDL_Event &event)
											{
												return remoteInput(event) &&
													   event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
													   event.button.button == b;
											});
		if (!pendingDown && now - t >= std::chrono::seconds(5))
		{
			const bool expiredDrag = active && active->tool == "ui_drag" && temporaryButton == b;
			button(b, false, cleanup);
			if (expiredDrag)
			{
				// A later motion can still be awaiting a buffered consumer. Stop
				// that range before it can resurrect or continue an expired hold.
				releaseTemporary(cleanup);
				finish("Drag cancelled because its held mouse button expired");
			}
		}
	}
}
void UIService::pump(std::vector<SDL_Event> &events, bool available, bool dispatchRequests)
{
	maintainListener();
	textStorage.clear();
	updateViewport();
	uiAvailable = available;
	observeHumanInput(events);
	const auto now = Clock::now();
	expireInputs(now);
	const bool cleanupOperation =
		active && (active->tool == "ui_release_control" || active->tool == "ui_release_inputs");
	if (active && (active->cancelled || now >= active->deadline ||
				   (!available && (!cleanupOperation || capturing))))
	{
		releaseTemporary(cleanup);
		finish(!available ? "Game UI unavailable (hidden, native dialog or graphics transition)"
						  : "UI operation cancelled or expired");
	}
	if (!dispatchRequests)
	{
		bool inspection = false;
		if (!active)
		{
			std::lock_guard lock(mutex);
			if (!queue.empty())
			{
				const auto &name = queue.front()->tool;
				inspection =
					name == "ui_status" || name == "ui_screenshot" || name == "ui_acquire_control";
			}
		}
		if ((!available || inspection) && !awaitingDispatch)
		{
			// Non-event inspection may run before human dispatch without bypassing
			// any earlier queued input. Captures reject subsequent viewport changes.
			// Cleanup still waits for tracked delivery, including hidden hosts.
			std::vector<SDL_Event> generated;
			advanceRequest(generated, available);
			cleanup.insert(cleanup.end(), generated.begin(), generated.end());
		}
		return;
	}
	if (!awaitingDispatch)
	{
		events.insert(events.end(), cleanup.begin(), cleanup.end());
		cleanup.clear();
		advanceRequest(events, available);
		dispatchBatch = events;
		awaitingDispatch = !events.empty();
	}
}

void UIService::drainCleanup(std::vector<SDL_Event> &events)
{
	if (awaitingDispatch)
		return;
	events.insert(events.end(), cleanup.begin(), cleanup.end());
	cleanup.clear();
	dispatchBatch = events;
	awaitingDispatch = !events.empty();
}
std::function<void(std::size_t, std::size_t)> UIService::dispatchCompletion()
{
	// A buffered consumer may resolve after revocation/listener restart. Snapshot
	// the exact batch and request so its receipt can never complete newer work.
	const auto request = active;
	const auto batch = dispatchBatch;
	return [this, request, batch](std::size_t consumed, std::size_t total)
	{
		if (active && active != request)
		{
			for (std::size_t i = consumed; i < batch.size(); ++i)
			{
				const auto &event = batch[i];
				if (remoteInput(event) &&
					((event.type == SDL_EVENT_KEY_UP && !humanKeys.contains(event.key.scancode) &&
					  !keys.contains(event.key.scancode)) ||
					 (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
					  !humanButtons.contains(event.button.button) &&
					  !buttons.contains(event.button.button))))
					cleanup.push_back(event);
			}
			return;
		}
		dispatchBatch = batch;
		afterDispatch(consumed == total, consumed, currentAvailability());
	};
}
bool UIService::currentAvailability() const
{
	auto *window = gfx.getWindow();
	return uiAvailable && window && !GAGCore::ApplicationHost::nativeDialogOpen() &&
		   !(SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
}
std::function<bool()> UIService::dispatchCancellation()
{
	if (!active)
		return {}; // Cleanup remains deliverable after request completion.
	std::weak_ptr<Request> request = active;
	const auto acceptedViewport = viewport;
	const bool coordinates =
		active->arguments.contains("x") || active->arguments.contains("points");
	const bool cleanupOperation =
		active->tool == "ui_release_control" || active->tool == "ui_release_inputs";
	// Buffered consumers evaluate this on the application thread, while the
	// service still outlives all screens and engine queues. Recheck geometry at
	// consumption: a human resize can occur after event generation and admission.
	return [this, request, acceptedViewport, coordinates, cleanupOperation]
	{
		const auto held = request.lock();
		if (!held || held->cancelled || Clock::now() >= held->deadline)
			return true;
		// A native dialog can appear in the same screen update after admission.
		// Only releases may proceed while the UI is unavailable.
		if (!cleanupOperation && !currentAvailability())
		{
			held->cancelled = true;
			if (active == held)
				cancellationReason = "Game UI unavailable before input dispatch";
			return true;
		}
		if (coordinates)
		{
			updateViewport();
			if (viewport != acceptedViewport)
			{
				held->cancelled = true;
				if (active == held)
					cancellationReason = "Stale viewport identity while input awaited dispatch";
				return true;
			}
		}
		return false;
	};
}

void UIService::advanceRequest(std::vector<SDL_Event> &events, bool available)
{
	const auto now = Clock::now();
	if (awaitingDispatch)
		return;
	if (!active)
	{
		std::lock_guard lock(mutex);
		if (!queue.empty())
		{
			active = queue.front();
			queue.pop_front();
			dragIndex = 0;
			cancellationReason.clear();
			dispatched = false;
			dispatchAcknowledged = false;
		}
	}
	if (!active || capturing || dispatched)
		return;
	auto &r = *active;
	auto &a = r.arguments;
	auto &tool = r.tool;
	if (r.cancelled || now >= r.deadline)
	{
		finish("UI operation cancelled or expired");
		return;
	}
	if (r.execute)
	{
		// Extension callbacks can revoke control or reconfigure the listener.
		// Keep their request alive and avoid completing it twice after reentrancy.
		const auto request = active;
		Result result;
		try
		{
			result = r.execute();
		}
		catch (const std::exception &e)
		{
			result.error = e.what();
		}
		if (active == request)
		{
			request->completion.set_value(std::move(result));
			active.reset();
		}
		return;
	}
	if (tool == "ui_status")
	{
		finish();
		return;
	}
	if (tool == "ui_acquire_control")
	{
		if (!token.empty())
		{
			finish("Another client controls the game");
			return;
		}
		try
		{
			token = randomToken();
		}
		catch (const std::exception &e)
		{
			finish(e.what());
			return;
		}
		controller = r.client;
		renewed = now;
		Result result;
		result.data = metadata();
		result.data["token"] = token;
		result.data["lease_seconds"] = 60;
		active->completion.set_value(std::move(result));
		active.reset();
		return;
	}
	if (inputTool(tool))
	{
		if (token.empty() || a.value("token", "") != token)
		{
			finish("Controller lease lost or token invalid");
			return;
		}
		if (dragIndex == 0)
			renewed = now;
	}
	if (tool == "ui_release_control")
	{
		r.inputStarted = true;
		release(events);
		token.clear();
		controller.clear();
		dispatched = true;
		return;
	}
	// Releasing ownership must work while minimized or behind a native dialog.
	if (tool == "ui_release_inputs")
	{
		r.inputStarted = true;
		release(events);
		dispatched = true;
		return;
	}
	if (!available || !gfx.getWindow() || !gfx.getSDLSurface())
	{
		finish("Game UI unavailable (hidden, native dialog or graphics transition)");
		return;
	}
	if (tool == "ui_screenshot")
	{
		startCapture();
		return;
	}
	if (a.contains("viewport_id") && a["viewport_id"] != viewport)
	{
		releaseTemporary(events);
		finish("Stale viewport identity; inspect the current viewport before sending coordinates");
		return;
	}
	auto validPoint = [&](const Json &p)
	{ return p["x"].get<double>() < width && p["y"].get<double>() < height; };
	if ((a.contains("x") && !validPoint(a)) ||
		(a.contains("points") && std::any_of(a["points"].begin(), a["points"].end(),
											 [&](const Json &p) { return !validPoint(p); })))
	{
		releaseTemporary(events);
		finish("Coordinates outside viewport");
		return;
	}
	r.inputStarted = true;
	if (tool == "ui_drag")
	{
		advanceDrag(a, events, now);
		return;
	}
	if (a.contains("x"))
		move(a, events);
	if (tool == "ui_click")
	{
		const auto b = mouseButton(a);
		if (buttons.contains(b) || humanButtons.contains(b))
		{
			finish("Click button already held");
			return;
		}
		for (int c = 1; c <= a.value("count", 1); ++c)
		{
			button(b, true, events, c);
			button(b, false, events, c);
		}
	}
	else if (tool == "ui_mouse_button")
		button(mouseButton(a), a["action"] == "down", events);
	else if (tool == "ui_scroll")
	{
		SDL_Event e{};
		e.common.timestamp = SDL_GetTicksNS();
		e.type = SDL_EVENT_MOUSE_WHEEL;
		e.wheel.windowID = SDL_GetWindowID(gfx.getWindow());
		e.wheel.which = RemoteInputDevice;
		e.wheel.x = a["dx"];
		e.wheel.y = a["dy"];
		e.wheel.mouse_x = pointerX;
		e.wheel.mouse_y = pointerY;
		e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
		events.push_back(e);
	}
	else if (tool == "ui_key")
	{
		dispatchKey(a, events);
	}
	else if (tool == "ui_type_text")
	{
		const auto &text = a["text"].get_ref<const std::string &>();
		if (text.find('\0') != std::string::npos || !SDL_TextInputActive(gfx.getWindow()))
		{
			finish("Text input unavailable or payload contains NUL");
			return;
		}
		// nlohmann validates UTF-8 when serializing; validate before passing to SDL consumers.
		try
		{
			(void)Json(text).dump();
		}
		catch (...)
		{
			finish("Invalid UTF-8 text");
			return;
		}
		textStorage.push_back(text);
		SDL_Event e{};
		e.common.timestamp = SDL_GetTicksNS();
		e.type = SDL_EVENT_TEXT_INPUT;
		e.text.windowID = SDL_GetWindowID(gfx.getWindow());
		e.text.text = textStorage.back().c_str();
		events.push_back(e);
	}
	else if (tool != "ui_move")
	{
		finish("Unknown UI operation");
		return;
	}
	if (!active)
		return;
	dispatched = true;
}
void UIService::advanceDrag(const Json &args, std::vector<SDL_Event> &events, Clock::time_point now)
{
	const auto &points = args["points"];
	const auto b = mouseButton(args);
	if (!dragIndex)
	{
		if (buttons.contains(b) || humanButtons.contains(b))
		{
			finish("Drag button already held");
			return;
		}
		move(points[0], events);
		button(b, true, events);
		temporaryButton = b;
		dragIndex = 1;
		dragStart = now;
	}
	const auto elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(now - dragStart).count();
	const auto duration = args.value("duration_ms", 250);
	const auto index =
		std::min(points.size() - 1, std::size_t(elapsed * (points.size() - 1) / duration));
	const auto nextIndex = std::min(index + 1, points.size() - 1);
	const double along = std::min(1.0, double(elapsed) / duration) * (points.size() - 1);
	const double fraction = along - index;
	Json position = {{"x", points[index]["x"].get<double>() * (1 - fraction) +
							   points[nextIndex]["x"].get<double>() * fraction},
					 {"y", points[index]["y"].get<double>() * (1 - fraction) +
							   points[nextIndex]["y"].get<double>() * fraction}};
	if (elapsed > 0)
		move(position, events);
	buttons[b] = now;
	if (elapsed >= duration)
	{
		releaseTemporary(events);
		dispatched = true;
	}
}
void UIService::dispatchKey(const Json &args, std::vector<SDL_Event> &events)
{
	auto scan = SDL_GetScancodeFromName(args["key"].get_ref<const std::string &>().c_str());
	std::vector<SDL_Scancode> chord;
	for (const auto &m : args.value("modifiers", Json::array()))
	{
		const auto s = SDL_GetScancodeFromName(m.get_ref<const std::string &>().c_str());
		if (!modifier(s))
		{
			finish("Unknown modifier scancode");
			return;
		}
		chord.push_back(s);
	}
	if (scan == SDL_SCANCODE_UNKNOWN)
	{
		finish("Unknown SDL scancode name");
		return;
	}
	const auto action = args.value("action", "press");
	if (action != "up")
	{
		for (auto s : chord)
		{
			if (!keys.contains(s))
				temporaryKeys.push_back(s);
			key(s, true, events);
		}
		if (action == "press" && !keys.contains(scan))
			temporaryKeys.push_back(scan);
		key(scan, true, events);
		if (action == "press")
			releaseTemporary(events);
		else
			temporaryKeys.clear();
	}
	else
	{
		key(scan, false, events);
		for (auto s : chord)
			key(s, false, events);
	}
}
void UIService::afterDispatch(bool complete, std::size_t consumed, std::optional<bool> available)
{
	awaitingDispatch = false;
	if (available)
		uiAvailable = *available;
	updateViewport(); // Input can resize or rescale the window before its completion.
	if (!complete)
	{
		// The screen opened a child or ended execution before receiving the whole
		// batch. Do not acknowledge a dropped key/button-up as a successful call.
		release(cleanup);
		// Temporary press/chord ownership is removed while generating the batch.
		// Preserve its undelivered releases as well as still-held remote inputs.
		for (std::size_t i = consumed; i < dispatchBatch.size(); ++i)
		{
			const auto &event = dispatchBatch[i];
			if (remoteInput(event) &&
				((event.type == SDL_EVENT_KEY_UP && !humanKeys.contains(event.key.scancode)) ||
				 (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
				  !humanButtons.contains(event.button.button))))
				cleanup.push_back(event);
		}
		dispatchBatch.clear();
		// A later cleanup batch can fail while capture_after is pending. Its
		// failure cannot undo a final input batch already consumed by the UI.
		if (!dispatchAcknowledged)
			dispatched = false;
		finish("Input dispatch interrupted or UI consumer unavailable");
		return;
	}
	const auto now = Clock::now();
	for (const auto &event : dispatchBatch)
	{
		if (!remoteInput(event))
			continue;
		if (event.type == SDL_EVENT_KEY_DOWN && keys.contains(event.key.scancode))
			keys[event.key.scancode] = now;
		if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && buttons.contains(event.button.button))
		{
			buttons[event.button.button] = now;
			// First DOWN establishes the gesture's real start. Time spent waiting
			// for the consumer must not compress a drag into its final frame.
			if (active && active->tool == "ui_drag" && temporaryButton == event.button.button)
				dragStart = now;
		}
	}
	dispatchBatch.clear();
	if (active && dispatched)
		dispatchAcknowledged = true;
	if (!active || !dispatched || capturing)
		return;
	if (active->cancelled)
	{
		releaseTemporary(cleanup);
		finish("UI operation cancelled after dispatch");
		return;
	}
	if (active->arguments.value("capture_after", false))
	{
		if (!uiAvailable)
			finish("Capture unavailable after dispatch (hidden or native dialog)");
		else
			startCapture();
	}
	else
		finish();
}
} // namespace MCP
#endif
