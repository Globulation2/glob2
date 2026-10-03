// SPDX-License-Identifier: GPL-3.0-or-later
// Native HttpFetch: NetHttp::Request (src/net/HttpExchange.h) pumped from the caller's thread.
#include "HttpFetch.h"
#include "HttpExchange.h"
#include "TlsSetup.h"
#include <boost/asio.hpp>
#include <stdexcept>

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

namespace HttpFetch
{
namespace
{
namespace asio = boost::asio;
namespace ssl = asio::ssl;

// The game's front end of NetHttp::Request: one io_context per request, pumped
// by state() on the caller's thread.
class NativeFetch final : public Fetch
{
	asio::io_context io;
	std::shared_ptr<NetHttp::Request> exchange;
	State status = State::Pending;
	Response result;
	std::string failure;

	void finished(NetHttp::Result outcome)
	{
		if (status != State::Pending)
			return;
		switch (outcome.outcome)
		{
		case NetHttp::Outcome::Done:
			status = State::Done;
			result.status = outcome.status;
			result.headers = std::move(outcome.headers);
			result.body = std::move(outcome.body);
			return;
		case NetHttp::Outcome::TimedOut:
			status = State::TimedOut;
			break;
		case NetHttp::Outcome::Cancelled:
			status = State::Cancelled;
			break;
		case NetHttp::Outcome::Failed:
			status = State::Failed;
			break;
		}
		failure = std::move(outcome.error);
	}

  public:
	explicit NativeFetch(Request spec)
	{
		try
		{
			const Url url = parseUrl(spec.url);
			NetHttp::Options options;
			options.method = methodName(spec.method);
			options.secure = url.secure;
			options.host = url.host;
			options.port = url.port;
			options.target = url.target;
			options.authority = url.authority();
			options.userAgent = std::string("Globulation2/") + PACKAGE_VERSION;
			options.headers = std::move(spec.headers);
			options.sendBody = spec.method != Method::Get || !spec.body.empty();
			options.body = std::move(spec.body);
			options.timeout = spec.timeout;
			options.responseLimit = spec.responseLimit;
			options.headerLimit = 64 * 1024;
			if (url.secure)
			{
				// The same trust as WssTransport: SSL_CERT_FILE, else the platform store.
				const auto trust = NetTls::withEnvironmentTrust({});
				options.tls = NetTls::contextFor(ssl::context::tls_client, trust);
				auto* context = options.tls.get();
				options.prepareTls = [context, trust](NetHttp::TlsStream& stream, const std::string& host) {
					stream.set_verify_mode(ssl::verify_peer);
					NetTls::verifyServer(stream, *context, host, trust);
					NetTls::serverName(stream, host);
				};
			}
			exchange = NetHttp::Request::start(io.get_executor(), std::move(options),
			                                   [this](NetHttp::Result outcome) { finished(std::move(outcome)); });
		}
		catch (const std::exception &error)
		{
			status = State::Failed;
			failure = error.what();
		}
	}
	~NativeFetch() override
	{
		cancel();
		// Let the aborted operations complete, so the request is released before io
		// (any handler still pending is destroyed with io without running).
		io.restart();
		while (io.poll_one())
		{
		}
		exchange.reset();
	}
	State state() override
	{
		if (status == State::Pending)
		{
			io.restart();
			// Bound the work done in one UI frame.
			for (unsigned i = 0; i < 64 && io.poll_one(); ++i)
			{
			}
		}
		return status;
	}
	const Response &response() const override
	{
		return result;
	}
	std::string error() const override
	{
		return failure;
	}
	void cancel() override
	{
		if (exchange)
			exchange->cancel();
		if (status == State::Pending)
			status = State::Cancelled;
	}
};
} // namespace

std::unique_ptr<Fetch> start(Request request)
{
	return std::make_unique<NativeFetch>(std::move(request));
}
} // namespace HttpFetch
