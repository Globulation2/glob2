// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Asynchronous HTTP(S) requests for platform clients.
//
// Requests follow the host polling contract used by ApplicationHost file
// selection and NetTransport: start a request, then poll state() from the UI
// thread (for example in a screen's onTimer) until it leaves Pending. No
// callback runs on another thread and no call blocks. Destroying the handle or
// calling cancel() aborts the request.
//
// Natively, state() pumps a Beast client on the caller's thread, using the
// same TLS trust and host verification as WssTransport (SSL_CERT_FILE, the
// platform trust store on Apple, mobile and Windows hosts). In the browser the
// request is an emscripten_fetch; the browser enforces CORS and decides which
// request headers may be set. Plain http:// is accepted only for loopback
// hosts, so credentials never leave the machine unencrypted.
namespace HttpFetch
{
enum class Method
{
	Get,
	Post,
	Put,
	Patch,
	Delete
};

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Request
{
	Method method = Method::Get;
	std::string url;
	Headers headers;
	// Sent as the request body (POST, PUT, PATCH); set Content-Type in headers.
	std::string body;
	// Whole-request deadline: name resolution, connection, TLS and transfer.
	std::chrono::milliseconds timeout{30000};
	// Larger responses fail rather than exhaust memory.
	std::size_t responseLimit = 16 * 1024 * 1024;
};

enum class State
{
	Pending,
	// An HTTP response arrived. Check response().status: 4xx/5xx are Done too.
	Done,
	// No response: invalid URL, DNS, connection, TLS or protocol failure.
	Failed,
	TimedOut,
	Cancelled
};

struct Response
{
	int status = 0;
	// Header names keep the server's spelling; compare case-insensitively.
	Headers headers;
	std::string body;
	// Returns the first header with this name (case-insensitive), or "".
	std::string header(const std::string &name) const;
};

class Fetch
{
  public:
	virtual ~Fetch() = default;
	// Advances the request and returns its state. Call from the UI thread.
	virtual State state() = 0;
	// Valid once state() returned Done.
	virtual const Response &response() const = 0;
	// Why the request Failed or TimedOut.
	virtual std::string error() const = 0;
	virtual void cancel() = 0;
};

// Starts a request. Never returns null: invalid requests fail on first poll.
std::unique_ptr<Fetch> start(Request request);

// Parsed form of an http:// or https:// URL. Exposed for tests.
struct Url
{
	bool secure = true;
	std::string host;	// without IPv6 brackets
	std::string port;	// numeric, defaulted from the scheme
	std::string target; // path and query, at least "/"
	std::string authority() const;
};
// Throws std::invalid_argument for unsupported, credential-bearing or
// malformed URLs, and for plain http:// to a non-loopback host.
Url parseUrl(const std::string &url);
const char *methodName(Method method);
} // namespace HttpFetch
