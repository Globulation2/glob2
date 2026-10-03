// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "HttpFetch.h"
#include "PlatformProtocol.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class NetTransport;

// The client side of a platform instance: one realtime WebSocket
// (/realtime, JSON envelope) plus REST calls, with sign-in, token refresh and
// reconnection handled here so screens only send requests and read state.
//
// Everything runs on the caller's thread. Call update() regularly (screens
// from onTimer; Online::pump() does it every frame while the client exists);
// response and event callbacks run inside update(), never during a request()
// call and never on another thread. Nothing blocks.
//
// Lifecycle: start(origin) signs in (refresh token, else guest device
// credential, else a new guest when allowed) and connects. Lost connections
// are retried with exponential backoff and jitter; the access token is
// refreshed before it expires and re-attached to the socket; a browser sign-in
// in progress is resumed on the new socket. stop() closes everything.
namespace Online
{
class InstanceConfig;

// Side effects the client needs, injectable for tests.
struct ClientEnvironment
{
	std::function<std::unique_ptr<NetTransport>()> makeTransport;
	std::function<std::unique_ptr<HttpFetch::Fetch>(HttpFetch::Request)> startFetch;
	// Monotonic milliseconds, for timers.
	std::function<std::int64_t()> now;
	// Milliseconds since the Unix epoch, to read the platform's timestamps.
	std::function<std::int64_t()> wallClock;
	// Uniform in [0, 1), for backoff jitter.
	std::function<double()> random;
	// Opens a URL in the system browser; false if that is impossible.
	std::function<bool(const std::string &)> openUrl;
	// Native transport and HTTP, steady clock, ApplicationHost::openUrl.
	static ClientEnvironment native();
};

struct ClientOptions
{
	std::string clientVersion; // defaults to PACKAGE_VERSION
	std::string platform;	   // defaults to clientPlatform()
	SimVersion simVersion = SimVersion::local();
	// Create a guest account on first contact when nothing else signs in.
	bool createGuest = true;
	std::int64_t requestTimeoutMs = 15000;
	std::int64_t connectTimeoutMs = 20000;
	// Idle time after which the client checks the socket with session.ping,
	// and how long it waits for the answer before reconnecting. The server's
	// own WebSocket pings (every 30 s) are answered by the transport.
	std::int64_t pingIntervalMs = 25000;
	std::int64_t pingTimeoutMs = 10000;
	std::int64_t backoffInitialMs = 1000;
	std::int64_t backoffMaxMs = 60000;
};

class PlatformClient
{
  public:
	enum class Connection
	{
		Stopped,
		Connecting,	 // transport opening (or waiting for sign-in to finish)
		Handshaking, // session.hello sent
		Online,
		Waiting // backing off before the next attempt
	};
	enum class Auth
	{
		SignedOut,
		SigningIn,
		SignedIn
	};
	struct Response
	{
		bool ok = false;
		Json result;
		ApiError error;
		// restRaw only: the response bytes (also for errors).
		std::string body;
		std::string contentType;
	};
	using ResponseHandler = std::function<void(const Response &)>;
	using EventHandler = std::function<void(const std::string &event, const Json &data)>;
	using RequestId = std::uint64_t;
	using ListenerId = std::uint64_t;

	// Browser sign-in (auth.handoff.*).
	struct Handoff
	{
		enum class State
		{
			None,
			Starting,  // auth.handoff.begin sent
			Waiting,   // browser open, waiting for the player
			Completed, // signed in (see linked)
			Failed	   // see failure / conflict
		};
		State state = State::None;
		std::string attemptId, signInUrl, confirmationCode, expiresAt;
		bool browserOpened = false;
		bool linked = false;
		// expired | denied | cancelled | conflict | error, or a local error code.
		std::string failure;
		Json conflict;
	};

	PlatformClient(InstanceConfig &config, ClientOptions options = {},
				   ClientEnvironment environment = ClientEnvironment::native());
	~PlatformClient();
	PlatformClient(const PlatformClient &) = delete;
	PlatformClient &operator=(const PlatformClient &) = delete;

	// Connects to an instance (normalized origin), signing in with what the
	// configuration remembers for it. Restarts if already running.
	void start(const std::string &origin);
	// Closes the socket and fails outstanding requests with "cancelled".
	void stop();
	// Tests: stops the client and swaps its side effects (fake sockets, HTTP and
	// clock for the process-wide client of Online::services()).
	void replaceEnvironment(ClientEnvironment environment);
	void update();

	// -- state
	const std::string &origin() const
	{
		return instance;
	}
	Connection connection() const
	{
		return link;
	}
	Auth auth() const
	{
		return authState;
	}
	const std::optional<Account> &account() const
	{
		return currentAccount;
	}
	// session.hello's simSupported: false means rooms and queues answer
	// update_required.
	bool simSupported() const
	{
		return simIsSupported;
	}
	const std::string &sessionId() const
	{
		return session;
	}
	// The last connection or sign-in problem, for status lines.
	const std::string &lastError() const
	{
		return problem;
	}
	// Milliseconds until the next connection attempt while Waiting.
	std::int64_t retryInMs() const;
	// Retry now instead of waiting for the backoff.
	void retryNow();
	// Current access token (empty when signed out), for REST calls made
	// elsewhere, such as map downloads.
	const std::string &accessToken() const
	{
		return tokens.accessToken;
	}

	// -- realtime
	// Sends a request; queued until the socket is online, failing with
	// "timeout" when no response arrives within timeoutMs of this call
	// (0: options.requestTimeoutMs).
	RequestId request(const std::string &method, Json params, ResponseHandler handler,
					  std::int64_t timeoutMs = 0);
	// Forgets a request or REST call (rest(), restRaw(), instanceInfo());
	// its handler is not called. Unknown or finished ids are ignored.
	void cancelRequest(RequestId id);
	// Whether a request or REST call is still waiting for its answer.
	bool pending(RequestId id) const;
	// Requests and REST calls made by callers (not the client's own sign-in,
	// hello and keepalive traffic) still waiting for their answer, for tests.
	std::size_t pendingCalls() const;
	// Calls handler for every event with this name ("" for all events),
	// including session.revoked and auth.handoff.* after the client handled
	// them.
	ListenerId addListener(const std::string &event, EventHandler handler);
	void removeListener(ListenerId id);
	// Called whenever connection, auth, account or handoff state changes.
	ListenerId addStateListener(std::function<void()> handler);

	// -- REST, authenticated with the current access token. A 401 refreshes
	// the token once and retries. Paths come from PlatformApi.h. The returned
	// id cancels the call (cancelRequest); 0 when the handler already ran
	// (client not running or not signed in).
	RequestId rest(HttpFetch::Method method, const std::string &path, Json body,
				   ResponseHandler handler);
	// REST with raw bytes both ways (map files, previews, replays): body is sent
	// with contentType when not empty; the handler gets the response bytes in
	// Response::body, and result holds them parsed when they are JSON. path may
	// also be an absolute URL on this instance's origin. Authenticated like rest().
	RequestId restRaw(HttpFetch::Method method, const std::string &path, std::string body,
					  const std::string &contentType, ResponseHandler handler,
					  std::size_t responseLimit = 16 * 1024 * 1024);
	// GET /api/v1/instance, fetched once per start() and shared: callers
	// while it is in flight wait for the same answer, later callers get the
	// cached one (delivered from update(), never inside this call). A failed
	// fetch is not cached.
	RequestId instanceInfo(ResponseHandler handler);
	// The cached /api/v1/instance answer, or null before it arrives.
	const Json *cachedInstanceInfo() const
	{
		return instanceCache.is_object() ? &instanceCache : nullptr;
	}
	// GET /api/v1/accounts/me, updating account().
	void refreshAccount(ResponseHandler handler = {});
	// PATCH /api/v1/accounts/me {displayName}.
	void rename(const std::string &displayName, ResponseHandler handler = {});

	// -- sign-in
	// Starts a browser sign-in: mode "link" attaches the identity to the
	// current account (a guest becomes registered), "signin" switches to the
	// identity's account; empty lets the server decide (link when signed in).
	// provider: an InstanceInfo.authProviders id, or empty for the page's
	// choice. Opens the system browser at the sign-in page.
	void beginBrowserSignIn(const std::string &mode = {}, const std::string &provider = {});
	void cancelBrowserSignIn();
	// Opens the sign-in page again (for instance when opening failed or, in
	// the browser, from a click so popup blockers allow it).
	bool openSignInPage();
	const Handoff &handoff() const
	{
		return signIn;
	}
	// Signs in as this device's guest (creating one if needed) and turns
	// automatic sign-in back on.
	void signInAsGuest();
	// Revokes the sign-in on the server, forgets the tokens and stays signed
	// out (also on the next start) until the player signs in again. The guest
	// device credential is kept: it is the only key to a guest account.
	void signOut();

  private:
	struct Pending
	{
		RequestId id;
		std::string method;
		Json params;
		ResponseHandler handler;
		std::int64_t deadline;
		bool internal;
	};
	struct RestCall;

	void changed();
	void setProblem(const std::string &text);
	void beginSignIn();
	void connectNow();
	void disconnected(const std::string &reason);
	void scheduleRetry();
	void handleText(const std::string &text);
	void handleResponse(ServerMessage &message);
	void handleEvent(const ServerMessage &message);
	void helloDone(const Response &response, bool withToken);
	void sendHello();
	RequestId sendInternal(const std::string &method, Json params, ResponseHandler handler,
						   std::int64_t timeoutMs);
	void transmit(Pending &pending);
	void flushOutbox();
	void failAll(const std::string &code, const std::string &message);
	void expireRequests();

	void adoptSession(const Json &signIn, bool authenticateSocket);
	void adoptTokens(const AuthTokens &tokens, bool authenticateSocket);
	void forgetTokens();
	void refreshTokens();
	void guestSignIn(bool allowCreate);
	void tokenAvailable(bool ok);
	void signInFinished(bool ok);
	void handoffResult(const Response &response);
	void resumeHandoff();
	void saveConfig();

	void startRest(std::unique_ptr<RestCall> call);
	std::unique_ptr<RestCall> makeRest(HttpFetch::Method method, const std::string &path,
									   const Json &body);
	void pumpRest();
	RequestId submitRest(std::unique_ptr<RestCall> call);
	void deliverInstanceInfo();
	void resetInstanceInfo();

	InstanceConfig &config;
	ClientOptions options;
	ClientEnvironment env;
	std::string instance;
	bool running = false;

	Connection link = Connection::Stopped;
	Auth authState = Auth::SignedOut;
	std::unique_ptr<NetTransport> transport;
	std::int64_t phaseStarted = 0;
	std::int64_t retryAt = 0;
	std::int64_t lastReceived = 0;
	std::optional<RequestId> pingInFlight;
	Backoff backoff;
	std::string session;
	bool simIsSupported = false;
	std::string problem;

	AuthTokens tokens;
	std::optional<Account> currentAccount;
	std::int64_t refreshAt = 0; // 0: no refresh scheduled
	bool refreshing = false;
	bool signingIn = false;	   // REST guest/refresh sign-in in flight
	int refreshFailures = 0;

	Handoff signIn;
	std::string handoffResumeToken;
	std::optional<RequestId> handoffRequest;
	std::int64_t handoffDeadline = 0;

	RequestId nextRequest = 1;
	std::map<std::string, Pending> inFlight; // by wire id
	std::deque<Pending> outbox;
	std::map<ListenerId, std::pair<std::string, EventHandler>> listeners;
	std::map<ListenerId, std::function<void()>> stateListeners;
	ListenerId nextListener = 1;
	std::vector<std::unique_ptr<RestCall>> restCalls;
	// Authenticated calls waiting for a token (sign-in or refresh in flight).
	std::vector<std::unique_ptr<RestCall>> awaitingToken;
	// Bumped by start, stop and sign-out: results of older sign-ins are stale.
	std::uint64_t epoch = 0;

	// instanceInfo(): the cached answer, the shared fetch and its waiters
	// (id, handler), answered in update(). start/stop bump the generation.
	Json instanceCache;
	std::optional<Response> instanceAnswer;
	RequestId instanceFetch = 0;
	std::vector<std::pair<RequestId, ResponseHandler>> instanceWaiters;
	std::uint64_t instanceGeneration = 0;
};

// Ties the requests, REST calls and listeners one owner (a screen, a
// presenter, a room) makes on a PlatformClient to the owner's lifetime, so no
// callback can reach the owner after it is gone. Destroying the scope (or
// cancelAll) cancels its outstanding calls and removes its listeners, and
// every handler passed through it first checks a liveness token, which also
// covers a handler already taken off the client's queue during the same
// update(). Every object that captures itself in a PlatformClient callback
// owns one of these as a member instead of calling the client directly.
//
// The client must outlive the scope (Online::services() lives for the whole
// process; tests declare the client first).
class PlatformScope
{
  public:
	using RequestId = PlatformClient::RequestId;
	using ListenerId = PlatformClient::ListenerId;
	using ResponseHandler = PlatformClient::ResponseHandler;
	using EventHandler = PlatformClient::EventHandler;

	explicit PlatformScope(PlatformClient &client);
	~PlatformScope();
	PlatformScope(const PlatformScope &) = delete;
	PlatformScope &operator=(const PlatformScope &) = delete;

	PlatformClient &client() const
	{
		return *owner;
	}
	RequestId request(const std::string &method, Json params, ResponseHandler handler,
					  std::int64_t timeoutMs = 0);
	RequestId rest(HttpFetch::Method method, const std::string &path, Json body,
				   ResponseHandler handler);
	RequestId restRaw(HttpFetch::Method method, const std::string &path, std::string body,
					  const std::string &contentType, ResponseHandler handler,
					  std::size_t responseLimit = 16 * 1024 * 1024);
	RequestId instanceInfo(ResponseHandler handler);
	// The client's own account calls: not cancelled (the client still updates
	// account()), only the handler is guarded.
	void refreshAccount(ResponseHandler handler);
	void rename(const std::string &displayName, ResponseHandler handler);
	ListenerId listen(const std::string &event, EventHandler handler);
	ListenerId onStateChange(std::function<void()> handler);
	void cancel(RequestId id);
	void removeListener(ListenerId id);
	// Cancels every call and removes every listener; the scope stays usable.
	void cancelAll();
	// Wraps any other callback so it does nothing once the scope is gone.
	template <typename F> auto guard(F callback) const
	{
		return [alive = std::weak_ptr<bool>(alive), callback = std::move(callback)](auto &&...args)
		{
			if (!alive.lock())
				return;
			// An empty std::function handler means "no callback".
			if constexpr (requires { static_cast<bool>(callback); })
				if (!static_cast<bool>(callback))
					return;
			callback(std::forward<decltype(args)>(args)...);
		};
	}
	// Ids still tracked (finished ones are pruned lazily), for tests.
	std::size_t tracked() const
	{
		return calls.size();
	}

  private:
	RequestId track(RequestId id);

	PlatformClient *owner;
	std::shared_ptr<bool> alive;
	std::vector<RequestId> calls;
	std::vector<ListenerId> listeners;
};

const char *connectionName(PlatformClient::Connection connection);
const char *authName(PlatformClient::Auth auth);
} // namespace Online
