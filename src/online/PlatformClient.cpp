// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlatformClient.h"
#include "InstanceConfig.h"
#include "NetTransport.h"

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

#include <algorithm>

namespace Online
{
namespace
{
constexpr std::int64_t FALLBACK_TOKEN_LIFETIME_MS = 5 * 60 * 1000;
constexpr std::int64_t HANDOFF_GRACE_MS = 60 * 1000;

PlatformClient::Response localError(const std::string &code, const std::string &message)
{
	PlatformClient::Response response;
	response.error.code = code;
	response.error.message = message;
	return response;
}

bool isAuthFailure(const PlatformClient::Response &response, int status)
{
	return status == 400 || status == 401 || status == 403 ||
		   response.error.code == "unauthenticated" || response.error.code == "bad_request" ||
		   response.error.code == "forbidden";
}
} // namespace

const char *connectionName(PlatformClient::Connection connection)
{
	switch (connection)
	{
	case PlatformClient::Connection::Stopped:
		return "stopped";
	case PlatformClient::Connection::Connecting:
		return "connecting";
	case PlatformClient::Connection::Handshaking:
		return "handshaking";
	case PlatformClient::Connection::Online:
		return "online";
	case PlatformClient::Connection::Waiting:
		return "waiting";
	}
	return "unknown";
}

const char *authName(PlatformClient::Auth auth)
{
	switch (auth)
	{
	case PlatformClient::Auth::SignedOut:
		return "signed-out";
	case PlatformClient::Auth::SigningIn:
		return "signing-in";
	case PlatformClient::Auth::SignedIn:
		return "signed-in";
	}
	return "unknown";
}

struct PlatformClient::RestCall
{
	HttpFetch::Request request;
	std::unique_ptr<HttpFetch::Fetch> fetch;
	// Receives the converted response and the HTTP status (0 without one).
	std::function<void(const Response &, int)> done;
	bool bearer = false;
	bool retried = false;
	std::uint64_t epoch = 0;
};

PlatformClient::PlatformClient(InstanceConfig &config, ClientOptions options,
							   ClientEnvironment environment)
	: config(config), options(std::move(options)), env(std::move(environment)),
	  backoff(this->options.backoffInitialMs, this->options.backoffMaxMs)
{
	if (this->options.clientVersion.empty())
		this->options.clientVersion = PACKAGE_VERSION;
	if (this->options.platform.empty())
		this->options.platform = clientPlatform();
}

PlatformClient::~PlatformClient()
{
	// No callbacks from a destructor: drop everything silently.
	listeners.clear();
	stateListeners.clear();
	inFlight.clear();
	outbox.clear();
	restCalls.clear();
	awaitingToken.clear();
	if (transport)
		transport->close();
}

// ------------------------------------------------------------------ lifecycle

void PlatformClient::start(const std::string &origin)
{
	if (running)
		stop();
	instance = origin;
	running = true;
	++epoch;
	backoff.reset();
	problem.clear();
	link = Connection::Connecting;
	phaseStarted = env.now();
	beginSignIn();
	if (!signingIn)
		connectNow();
	changed();
}

void PlatformClient::stop()
{
	running = false;
	++epoch;
	if (transport)
		transport->close();
	transport.reset();
	link = Connection::Stopped;
	session.clear();
	pingInFlight.reset();
	failAll("cancelled", "The online client stopped.");
	for (auto &call : restCalls)
		if (call->fetch)
			call->fetch->cancel();
	auto calls = std::move(restCalls);
	auto waiting = std::move(awaitingToken);
	restCalls.clear();
	awaitingToken.clear();
	for (auto *list : {&calls, &waiting})
		for (auto &call : *list)
			if (call->done)
				call->done(localError("cancelled", "The online client stopped."), 0);
	tokens = {};
	currentAccount.reset();
	authState = Auth::SignedOut;
	refreshAt = 0;
	refreshing = false;
	signingIn = false;
	signIn = {};
	handoffResumeToken.clear();
	handoffRequest.reset();
	changed();
}

std::int64_t PlatformClient::retryInMs() const
{
	return link == Connection::Waiting ? std::max<std::int64_t>(0, retryAt - env.now()) : 0;
}

void PlatformClient::retryNow()
{
	if (link == Connection::Waiting)
		retryAt = env.now();
}

void PlatformClient::changed()
{
	auto copy = stateListeners;
	for (auto &[id, handler] : copy)
		if (stateListeners.count(id))
			handler();
}

void PlatformClient::setProblem(const std::string &text)
{
	problem = text;
}

void PlatformClient::saveConfig()
{
	config.save();
}

// ------------------------------------------------------------------ update

void PlatformClient::update()
{
	pumpRest();
	if (!running)
		return;
	const auto now = env.now();

	if (transport)
	{
		const auto state = transport->state();
		if (link == Connection::Connecting && state == NetTransport::State::Connected)
		{
			link = Connection::Handshaking;
			phaseStarted = now;
			lastReceived = now;
			sendHello();
			changed();
		}
		std::string text;
		while (transport && transport->receiveText(text))
		{
			lastReceived = env.now();
			handleText(text);
		}
		if (transport && transport->state() == NetTransport::State::Closed)
		{
			const auto reason = transport->error();
			disconnected(reason.empty() ? "The connection closed." : reason);
		}
		else if (transport && link == Connection::Connecting &&
				 now - phaseStarted > options.connectTimeoutMs)
			disconnected("Connecting timed out.");
	}
	else if (link == Connection::Connecting && !signingIn)
		connectNow();
	else if (link == Connection::Waiting && now >= retryAt)
	{
		link = Connection::Connecting;
		phaseStarted = now;
		if (tokens.accessToken.empty())
			beginSignIn();
		if (!signingIn)
			connectNow();
		changed();
	}

	expireRequests();

	if (link == Connection::Online && !pingInFlight &&
		env.now() - lastReceived >= options.pingIntervalMs)
	{
		pingInFlight = sendInternal(
			"session.ping", Json::object(),
			[this](const Response &response)
			{
				pingInFlight.reset();
				if (!response.ok && response.error.code == "timeout")
					disconnected("The server stopped answering.");
			},
			options.pingTimeoutMs);
	}

	if (refreshAt && env.now() >= refreshAt && !refreshing)
		refreshTokens();

	if (signIn.state == Handoff::State::Waiting && handoffDeadline &&
		env.now() > handoffDeadline)
	{
		signIn.state = Handoff::State::Failed;
		signIn.failure = "expired";
		handoffResumeToken.clear();
		changed();
	}
}

// ------------------------------------------------------------------ socket

void PlatformClient::connectNow()
{
	if (!running)
		return;
	transport = env.makeTransport();
	link = Connection::Connecting;
	phaseStarted = env.now();
	if (!transport)
	{
		disconnected("No network transport is available.");
		return;
	}
	transport->open(realtimeUrl(instance));
	if (transport->state() == NetTransport::State::Closed)
	{
		const auto reason = transport->error();
		disconnected(reason.empty() ? "The instance address is not usable." : reason);
	}
}

void PlatformClient::disconnected(const std::string &reason)
{
	if (transport)
		transport->close();
	transport.reset();
	link = running ? Connection::Waiting : Connection::Stopped;
	session.clear();
	pingInFlight.reset();
	setProblem(reason);
	// Requests on the socket are lost; queued ones wait for the next socket.
	auto lost = std::move(inFlight);
	inFlight.clear();
	for (auto &[wire, pending] : lost)
		if (pending.handler)
			pending.handler(localError("disconnected", reason));
	if (running)
		scheduleRetry();
	changed();
}

void PlatformClient::scheduleRetry()
{
	link = Connection::Waiting;
	retryAt = env.now() + backoff.next(env.random());
}

void PlatformClient::sendHello()
{
	Json params = {{"protocol", REALTIME_PROTOCOL_VERSION},
				   {"client",
					{{"platform", options.platform},
					 {"version", options.clientVersion.substr(0, 64)},
					 {"simVersion", options.simVersion.toJson()}}}};
	const bool withToken = !tokens.accessToken.empty();
	if (withToken)
		params["accessToken"] = tokens.accessToken;
	sendInternal("session.hello", std::move(params),
				 [this, withToken](const Response &response) { helloDone(response, withToken); },
				 options.connectTimeoutMs);
}

void PlatformClient::helloDone(const Response &response, bool withToken)
{
	if (link != Connection::Handshaking)
		return;
	if (!response.ok)
	{
		if (withToken && response.error.code == "unauthenticated")
		{
			// The token was refused (expired, revoked): continue anonymously
			// and sign in again; session.authenticate attaches the result.
			tokens.accessToken.clear();
			refreshAt = 0;
			authState = Auth::SignedOut;
			sendHello();
			beginSignIn();
			changed();
			return;
		}
		if (response.error.code == "update_required")
		{
			setProblem(response.error.message.empty() ? "This version is not supported."
													  : response.error.message);
			if (transport)
				transport->close();
			transport.reset();
			running = false;
			link = Connection::Stopped;
			failAll("update_required", problem);
			changed();
			return;
		}
		if (response.error.code != "disconnected")
			disconnected(response.error.message.empty() ? "session.hello failed: " + response.error.code
														: response.error.message);
		return;
	}
	link = Connection::Online;
	backoff.reset();
	problem.clear();
	session = response.result.value("sessionId", std::string());
	simIsSupported = response.result.value("simSupported", false);
	if (auto found = response.result.find("account"); found != response.result.end())
		if (auto parsed = Account::fromJson(*found))
		{
			currentAccount = parsed;
			if (!tokens.accessToken.empty())
				authState = Auth::SignedIn;
		}
	resumeHandoff();
	flushOutbox();
	changed();
}

void PlatformClient::handleText(const std::string &text)
{
	auto message = decodeServerMessage(text);
	switch (message.kind)
	{
	case ServerMessage::Kind::Response:
		handleResponse(message);
		break;
	case ServerMessage::Kind::Event:
		handleEvent(message);
		break;
	case ServerMessage::Kind::Invalid:
		// The server only sends valid frames; a broken one means the socket
		// cannot be trusted to stay in sync.
		disconnected("Invalid message from the server: " + message.invalid);
		break;
	}
}

void PlatformClient::handleResponse(ServerMessage &message)
{
	auto found = inFlight.find(message.id);
	if (found == inFlight.end())
		return; // timed out or cancelled
	auto pending = std::move(found->second);
	inFlight.erase(found);
	Response response;
	response.ok = message.ok;
	response.result = std::move(message.result);
	response.error = std::move(message.error);
	if (pending.handler)
		pending.handler(response);
}

void PlatformClient::handleEvent(const ServerMessage &message)
{
	const auto &data = message.data;
	if (message.event == "session.revoked")
	{
		forgetTokens();
		// Signing straight back in with the device credential would undo a
		// sign-out elsewhere or a ban; wait for the player instead.
		config.record(instance).autoSignIn = false;
		saveConfig();
		setProblem(data.value("reason", std::string("Signed out by the server.")));
		changed();
	}
	else if (message.event == "auth.handoff.completed")
	{
		const auto attempt = data.value("attemptId", std::string());
		if (signIn.attemptId.empty() || attempt == signIn.attemptId)
		{
			if (auto found = data.find("session"); found != data.end())
				adoptSession(*found, false);
			signIn.state = Handoff::State::Completed;
			signIn.linked = data.value("linked", false);
			signIn.attemptId = attempt;
			handoffResumeToken.clear();
			handoffDeadline = 0;
			changed();
		}
	}
	else if (message.event == "auth.handoff.failed")
	{
		const auto attempt = data.value("attemptId", std::string());
		if (attempt == signIn.attemptId)
		{
			signIn.state = Handoff::State::Failed;
			signIn.failure = data.value("reason", std::string("error"));
			if (auto conflict = data.find("conflict"); conflict != data.end())
				signIn.conflict = *conflict;
			handoffResumeToken.clear();
			handoffDeadline = 0;
			changed();
		}
	}
	std::vector<EventHandler> handlers;
	for (auto &[id, listener] : listeners)
		if (listener.first.empty() || listener.first == message.event)
			handlers.push_back(listener.second);
	for (auto &handler : handlers)
		handler(message.event, data);
}

// ------------------------------------------------------------------ requests

PlatformClient::RequestId PlatformClient::request(const std::string &method, Json params,
												  ResponseHandler handler, std::int64_t timeoutMs)
{
	Pending pending{nextRequest++,
					method,
					std::move(params),
					std::move(handler),
					env.now() + (timeoutMs > 0 ? timeoutMs : options.requestTimeoutMs),
					false};
	const auto id = pending.id;
	if (link == Connection::Online && transport)
		transmit(pending);
	else
		outbox.push_back(std::move(pending));
	return id;
}

PlatformClient::RequestId PlatformClient::sendInternal(const std::string &method, Json params,
													   ResponseHandler handler,
													   std::int64_t timeoutMs)
{
	Pending pending{nextRequest++,
					method,
					std::move(params),
					std::move(handler),
					env.now() + (timeoutMs > 0 ? timeoutMs : options.requestTimeoutMs),
					true};
	const auto id = pending.id;
	transmit(pending);
	return id;
}

void PlatformClient::transmit(Pending &pending)
{
	const auto wire = "c" + std::to_string(pending.id);
	const auto text = encodeRequest(wire, pending.method, pending.params);
	if (text.size() > REALTIME_FRAME_LIMIT)
	{
		if (pending.handler)
			pending.handler(localError("bad_request", "The request is too large."));
		return;
	}
	if (!transport || !transport->sendText(text))
	{
		if (pending.handler)
			pending.handler(localError("disconnected", "The request could not be sent."));
		return;
	}
	pending.params = Json(); // not needed once sent
	inFlight.emplace(wire, std::move(pending));
}

void PlatformClient::flushOutbox()
{
	auto queued = std::move(outbox);
	outbox.clear();
	for (auto &pending : queued)
	{
		if (link != Connection::Online)
		{
			outbox.push_back(std::move(pending));
			continue;
		}
		transmit(pending);
	}
}

void PlatformClient::cancelRequest(RequestId id)
{
	for (auto i = inFlight.begin(); i != inFlight.end(); ++i)
		if (i->second.id == id)
		{
			inFlight.erase(i);
			break;
		}
	outbox.erase(std::remove_if(outbox.begin(), outbox.end(),
								[id](const Pending &pending) { return pending.id == id; }),
				 outbox.end());
	if (handoffRequest == id)
		handoffRequest.reset();
}

void PlatformClient::failAll(const std::string &code, const std::string &message)
{
	auto lost = std::move(inFlight);
	auto queued = std::move(outbox);
	inFlight.clear();
	outbox.clear();
	for (auto &[wire, pending] : lost)
		if (pending.handler)
			pending.handler(localError(code, message));
	for (auto &pending : queued)
		if (pending.handler)
			pending.handler(localError(code, message));
}

void PlatformClient::expireRequests()
{
	const auto now = env.now();
	std::vector<Pending> expired;
	for (auto i = inFlight.begin(); i != inFlight.end();)
		if (i->second.deadline <= now)
		{
			expired.push_back(std::move(i->second));
			i = inFlight.erase(i);
		}
		else
			++i;
	for (auto i = outbox.begin(); i != outbox.end();)
		if (i->deadline <= now)
		{
			expired.push_back(std::move(*i));
			i = outbox.erase(i);
		}
		else
			++i;
	for (auto &pending : expired)
		if (pending.handler)
			pending.handler(localError("timeout", "No answer from the server in time."));
}

PlatformClient::ListenerId PlatformClient::addListener(const std::string &event,
													   EventHandler handler)
{
	const auto id = nextListener++;
	listeners.emplace(id, std::make_pair(event, std::move(handler)));
	return id;
}

PlatformClient::ListenerId PlatformClient::addStateListener(std::function<void()> handler)
{
	const auto id = nextListener++;
	stateListeners.emplace(id, std::move(handler));
	return id;
}

void PlatformClient::removeListener(ListenerId id)
{
	listeners.erase(id);
	stateListeners.erase(id);
}

// ------------------------------------------------------------------ REST

std::unique_ptr<PlatformClient::RestCall>
PlatformClient::makeRest(HttpFetch::Method method, const std::string &path, const Json &body)
{
	auto call = std::make_unique<RestCall>();
	call->request.method = method;
	call->request.url = apiUrl(instance, path);
	call->request.timeout = std::chrono::milliseconds(options.requestTimeoutMs);
	call->request.responseLimit = 1024 * 1024;
	call->request.headers.emplace_back("Accept", "application/json");
	if (method != HttpFetch::Method::Get)
	{
		call->request.headers.emplace_back("Content-Type", "application/json");
		call->request.body = (body.is_null() ? Json::object() : body).dump();
	}
	call->epoch = epoch;
	return call;
}

void PlatformClient::startRest(std::unique_ptr<RestCall> call)
{
	if (call->bearer)
	{
		auto &headers = call->request.headers;
		headers.erase(std::remove_if(headers.begin(), headers.end(),
									 [](const auto &header) { return header.first == "Authorization"; }),
					  headers.end());
		headers.emplace_back("Authorization", "Bearer " + tokens.accessToken);
	}
	call->fetch = env.startFetch(call->request);
	restCalls.push_back(std::move(call));
}

void PlatformClient::pumpRest()
{
	std::vector<std::unique_ptr<RestCall>> finished;
	for (auto i = restCalls.begin(); i != restCalls.end();)
	{
		if (!(*i)->fetch || (*i)->fetch->state() != HttpFetch::State::Pending)
		{
			finished.push_back(std::move(*i));
			i = restCalls.erase(i);
		}
		else
			++i;
	}
	for (auto &call : finished)
	{
		Response response;
		int status = 0;
		const auto state = call->fetch ? call->fetch->state() : HttpFetch::State::Failed;
		if (state == HttpFetch::State::Done)
		{
			const auto &http = call->fetch->response();
			status = http.status;
			Json body = http.body.empty() ? Json::object()
										  : Json::parse(http.body, nullptr, false);
			if (status >= 200 && status < 300)
			{
				response.ok = true;
				response.result = body.is_discarded() ? Json() : std::move(body);
			}
			else
				response.error = errorFromJson(body.is_discarded() ? Json() : body,
											   "http_" + std::to_string(status));
		}
		else if (state == HttpFetch::State::TimedOut)
			response = localError("timeout", "The request timed out.");
		else if (state == HttpFetch::State::Cancelled)
			response = localError("cancelled", "The request was cancelled.");
		else
			response = localError("network", call->fetch ? call->fetch->error()
														 : std::string("The request failed."));

		if (call->bearer && status == 401 && !call->retried && call->epoch == epoch &&
			!config.record(instance).refreshToken.empty())
		{
			// The access token was refused: refresh once, then retry.
			call->retried = true;
			call->fetch.reset();
			awaitingToken.push_back(std::move(call));
			refreshTokens();
			continue;
		}
		if (call->done)
			call->done(response, status);
	}
}

void PlatformClient::rest(HttpFetch::Method method, const std::string &path, Json body,
						  ResponseHandler handler)
{
	auto call = makeRest(method, path, body);
	call->bearer = true;
	call->done = [handler = std::move(handler)](const Response &response, int)
	{
		if (handler)
			handler(response);
	};
	if (!running)
	{
		call->done(localError("cancelled", "The online client is not running."), 0);
		return;
	}
	if (tokens.accessToken.empty())
	{
		if (signingIn || refreshing)
		{
			awaitingToken.push_back(std::move(call));
			return;
		}
		call->done(localError("unauthenticated", "Not signed in."), 0);
		return;
	}
	startRest(std::move(call));
}

void PlatformClient::refreshAccount(ResponseHandler handler)
{
	rest(HttpFetch::Method::Get, "/api/v1/accounts/me", Json(),
		 [this, handler = std::move(handler)](const Response &response)
		 {
			 if (response.ok)
				 if (auto parsed = Account::fromJson(response.result))
				 {
					 currentAccount = parsed;
					 config.record(instance).lastDisplayName = parsed->displayName;
					 saveConfig();
					 changed();
				 }
			 if (handler)
				 handler(response);
		 });
}

void PlatformClient::rename(const std::string &displayName, ResponseHandler handler)
{
	rest(HttpFetch::Method::Patch, "/api/v1/accounts/me", Json{{"displayName", displayName}},
		 [this, handler = std::move(handler)](const Response &response)
		 {
			 if (response.ok)
				 if (auto parsed = Account::fromJson(response.result))
				 {
					 currentAccount = parsed;
					 config.record(instance).lastDisplayName = parsed->displayName;
					 saveConfig();
					 changed();
				 }
			 if (handler)
				 handler(response);
		 });
}

// ------------------------------------------------------------------ sign-in

void PlatformClient::beginSignIn()
{
	if (!tokens.accessToken.empty() || signingIn || refreshing)
		return;
	auto &record = config.record(instance);
	if (!record.autoSignIn)
	{
		authState = Auth::SignedOut;
		return;
	}
	if (!record.refreshToken.empty())
		refreshTokens();
	else if (!record.deviceCredential.empty() || options.createGuest)
		guestSignIn(options.createGuest);
	else
		authState = Auth::SignedOut;
}

void PlatformClient::signInFinished(bool ok)
{
	if (!signingIn)
		return;
	signingIn = false;
	if (!ok && tokens.accessToken.empty())
		authState = Auth::SignedOut;
	tokenAvailable(ok && !tokens.accessToken.empty());
	if (running && link == Connection::Connecting && !transport)
		connectNow();
	changed();
}

void PlatformClient::tokenAvailable(bool ok)
{
	auto waiting = std::move(awaitingToken);
	awaitingToken.clear();
	for (auto &call : waiting)
	{
		if (ok && call->epoch == epoch)
			startRest(std::move(call));
		else if (call->done)
			call->done(localError("unauthenticated", "Not signed in."), 401);
	}
}

void PlatformClient::guestSignIn(bool allowCreate)
{
	auto &record = config.record(instance);
	if (record.deviceCredential.empty() && !allowCreate)
	{
		authState = Auth::SignedOut;
		return;
	}
	Json body = {{"platform", options.platform}};
	const bool withCredential = !record.deviceCredential.empty();
	if (withCredential)
		body["deviceCredential"] = record.deviceCredential;
	signingIn = true;
	authState = Auth::SigningIn;
	auto call = makeRest(HttpFetch::Method::Post, "/api/v1/auth/guest", body);
	const auto started = epoch;
	call->done = [this, withCredential, allowCreate, started](const Response &response, int status)
	{
		if (started != epoch)
			return;
		if (response.ok)
		{
			adoptSession(response.result, link == Connection::Online);
			signInFinished(true);
			return;
		}
		if (withCredential && (status == 401 || response.error.code == "unauthenticated"))
		{
			// The instance no longer knows this device (reset database):
			// start over as a new guest when allowed.
			config.record(instance).deviceCredential.clear();
			saveConfig();
			signingIn = false;
			if (allowCreate)
			{
				guestSignIn(true);
				return;
			}
		}
		setProblem(response.error.message.empty() ? "Guest sign-in failed: " + response.error.code
												   : response.error.message);
		signingIn = true; // let signInFinished run its bookkeeping
		signInFinished(false);
	};
	startRest(std::move(call));
	changed();
}

void PlatformClient::refreshTokens()
{
	if (refreshing)
		return;
	auto &record = config.record(instance);
	if (record.refreshToken.empty())
	{
		refreshAt = 0;
		return;
	}
	refreshing = true;
	if (tokens.accessToken.empty())
	{
		signingIn = true;
		authState = Auth::SigningIn;
	}
	auto call = makeRest(HttpFetch::Method::Post, "/api/v1/auth/refresh",
						 Json{{"refreshToken", record.refreshToken}});
	const auto started = epoch;
	call->done = [this, started](const Response &response, int status)
	{
		refreshing = false;
		if (started != epoch)
		{
			// Signed out or restarted meanwhile: the new family must not
			// linger, so revoke it with the token just issued.
			if (response.ok)
				if (auto issued = AuthTokens::fromJson(response.result))
				{
					auto revoke = makeRest(HttpFetch::Method::Post, "/api/v1/auth/sign-out",
										   Json{{"refreshToken", issued->refreshToken}});
					startRest(std::move(revoke));
				}
			return;
		}
		if (response.ok)
		{
			if (auto issued = AuthTokens::fromJson(response.result))
			{
				refreshFailures = 0;
				adoptTokens(*issued, link == Connection::Online);
				if (!currentAccount && link == Connection::Online)
					refreshAccount();
				signInFinished(true);
				tokenAvailable(true);
				changed();
				return;
			}
		}
		if (response.ok || isAuthFailure(response, status))
		{
			// Refused (expired, revoked or reused): this sign-in is over.
			auto &record = config.record(instance);
			record.refreshToken.clear();
			saveConfig();
			tokens = {};
			refreshAt = 0;
			currentAccount.reset();
			authState = Auth::SignedOut;
			if (!record.deviceCredential.empty() && record.autoSignIn)
			{
				signingIn = false;
				guestSignIn(false);
				return;
			}
			setProblem("Your sign-in expired. Sign in again.");
			signInFinished(false);
			tokenAvailable(false);
			changed();
			return;
		}
		// Network or server trouble: keep the current token, try again soon.
		++refreshFailures;
		refreshAt = env.now() + std::min<std::int64_t>(60000, 2000ll << std::min(refreshFailures, 5));
		setProblem("Could not refresh the sign-in: " + response.error.code);
		signInFinished(false);
		if (!tokens.accessToken.empty())
			authState = Auth::SignedIn;
		tokenAvailable(!tokens.accessToken.empty());
		changed();
	};
	startRest(std::move(call));
}

void PlatformClient::adoptSession(const Json &signInResponse, bool authenticateSocket)
{
	auto &record = config.record(instance);
	if (auto credential = signInResponse.find("deviceCredential");
		credential != signInResponse.end() && credential->is_string())
		record.deviceCredential = credential->get<std::string>();
	if (auto found = signInResponse.find("account"); found != signInResponse.end())
		if (auto parsed = Account::fromJson(*found))
		{
			currentAccount = parsed;
			record.lastDisplayName = parsed->displayName;
		}
	record.autoSignIn = true;
	if (auto found = signInResponse.find("tokens"); found != signInResponse.end())
		if (auto issued = AuthTokens::fromJson(*found))
		{
			adoptTokens(*issued, authenticateSocket);
			return;
		}
	saveConfig();
}

void PlatformClient::adoptTokens(const AuthTokens &issued, bool authenticateSocket)
{
	tokens = issued;
	auto &record = config.record(instance);
	record.refreshToken = issued.refreshToken;
	saveConfig();
	const auto lifetime = tokenLifetimeMs(issued.accessToken).value_or(FALLBACK_TOKEN_LIFETIME_MS);
	refreshAt = env.now() + refreshDelayMs(lifetime);
	authState = Auth::SignedIn;
	if (authenticateSocket && link == Connection::Online)
		sendInternal("session.authenticate", Json{{"accessToken", tokens.accessToken}},
					 [this](const Response &response)
					 {
						 if (response.ok)
						 {
							 if (auto found = response.result.find("account");
								 found != response.result.end())
								 if (auto parsed = Account::fromJson(*found))
									 currentAccount = parsed;
							 changed();
						 }
						 else if (response.error.code != "disconnected")
							 setProblem("session.authenticate failed: " + response.error.code);
					 },
					 0);
	changed();
}

void PlatformClient::forgetTokens()
{
	tokens = {};
	refreshAt = 0;
	currentAccount.reset();
	authState = Auth::SignedOut;
	if (!instance.empty())
		config.record(instance).refreshToken.clear();
}

void PlatformClient::signInAsGuest()
{
	if (!running)
		return;
	auto &record = config.record(instance);
	record.autoSignIn = true;
	if (!record.refreshToken.empty())
	{
		auto revoke = makeRest(HttpFetch::Method::Post, "/api/v1/auth/sign-out",
							   Json{{"refreshToken", record.refreshToken}});
		startRest(std::move(revoke));
	}
	++epoch;
	refreshing = false;
	signingIn = false;
	forgetTokens();
	saveConfig();
	guestSignIn(true);
}

void PlatformClient::signOut()
{
	auto &record = config.record(instance);
	if (!record.refreshToken.empty())
	{
		auto revoke = makeRest(HttpFetch::Method::Post, "/api/v1/auth/sign-out",
							   Json{{"refreshToken", record.refreshToken}});
		startRest(std::move(revoke));
	}
	++epoch;
	refreshing = false;
	signingIn = false;
	forgetTokens();
	record.autoSignIn = false;
	saveConfig();
	tokenAvailable(false);
	if (signIn.state == Handoff::State::Starting || signIn.state == Handoff::State::Waiting)
		cancelBrowserSignIn();
	// The socket stays authenticated server-side until it closes.
	if (running && transport)
	{
		disconnected("Signed out.");
		problem.clear();
		retryAt = env.now();
		backoff.reset();
	}
	changed();
}

// ------------------------------------------------------------------ handoff

void PlatformClient::beginBrowserSignIn(const std::string &mode, const std::string &provider)
{
	if (signIn.state == Handoff::State::Starting || signIn.state == Handoff::State::Waiting)
		cancelBrowserSignIn();
	signIn = {};
	signIn.state = Handoff::State::Starting;
	handoffResumeToken.clear();
	handoffDeadline = 0;
	Json params = Json::object();
	if (!mode.empty())
		params["mode"] = mode;
	if (!provider.empty())
		params["provider"] = provider;
	handoffRequest = request("auth.handoff.begin", std::move(params),
							 [this](const Response &response) { handoffResult(response); });
	changed();
}

void PlatformClient::handoffResult(const Response &response)
{
	handoffRequest.reset();
	if (signIn.state != Handoff::State::Starting)
		return;
	if (!response.ok)
	{
		signIn.state = Handoff::State::Failed;
		signIn.failure = response.error.code;
		setProblem(response.error.message);
		changed();
		return;
	}
	const auto &result = response.result;
	signIn.attemptId = result.value("attemptId", std::string());
	signIn.signInUrl = result.value("signInUrl", std::string());
	signIn.confirmationCode = result.value("confirmationCode", std::string());
	signIn.expiresAt = result.value("expiresAt", std::string());
	handoffResumeToken = result.value("resumeToken", std::string());
	signIn.state = Handoff::State::Waiting;
	if (auto expires = parseTimestamp(signIn.expiresAt))
		handoffDeadline =
			env.now() + std::max<std::int64_t>(0, *expires - env.wallClock()) + HANDOFF_GRACE_MS;
	signIn.browserOpened = openSignInPage();
	changed();
}

bool PlatformClient::openSignInPage()
{
	if (signIn.state != Handoff::State::Waiting || signIn.signInUrl.empty() || !env.openUrl)
		return false;
	// Only ever open the instance's own sign-in page.
	if (signIn.signInUrl.rfind(instance + "/", 0) != 0)
		return false;
	const bool opened = env.openUrl(signIn.signInUrl);
	if (opened)
		signIn.browserOpened = true;
	return opened;
}

void PlatformClient::cancelBrowserSignIn()
{
	if (signIn.state == Handoff::State::Starting && handoffRequest)
		cancelRequest(*handoffRequest);
	else if (signIn.state == Handoff::State::Waiting && link == Connection::Online)
		sendInternal("auth.handoff.cancel", Json{{"attemptId", signIn.attemptId}}, {}, 0);
	else if (signIn.state != Handoff::State::Waiting && signIn.state != Handoff::State::Starting)
		return;
	signIn.state = Handoff::State::Failed;
	signIn.failure = "cancelled";
	handoffResumeToken.clear();
	handoffDeadline = 0;
	changed();
}

void PlatformClient::resumeHandoff()
{
	if (signIn.state != Handoff::State::Waiting || handoffResumeToken.empty())
		return;
	sendInternal("auth.handoff.resume",
				 Json{{"attemptId", signIn.attemptId}, {"resumeToken", handoffResumeToken}},
				 [this](const Response &response)
				 {
					 if (response.ok || response.error.code == "disconnected" ||
						 signIn.state != Handoff::State::Waiting)
						 return;
					 signIn.state = Handoff::State::Failed;
					 signIn.failure = response.error.code == "not_found" ? "expired" : "error";
					 handoffResumeToken.clear();
					 changed();
				 },
				 0);
}
} // namespace Online
