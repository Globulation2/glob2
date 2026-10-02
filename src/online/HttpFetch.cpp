// SPDX-License-Identifier: GPL-3.0-or-later
// Native HttpFetch: a Beast HTTP/1.1 client pumped from the caller's thread.
#include "HttpFetch.h"
#include "TlsSetup.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <optional>
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
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;

class NativeFetch final : public Fetch
{
	asio::io_context io;
	tcp::resolver resolver{io};
	asio::steady_timer deadline{io};
	std::shared_ptr<ssl::context> tls;
	std::optional<beast::tcp_stream> plain;
	std::optional<beast::ssl_stream<beast::tcp_stream>> secure;
	beast::flat_buffer buffer;
	http::request<http::string_body> request;
	std::optional<http::response_parser<http::string_body>> parser;
	Url url;
	State status = State::Pending;
	Response result;
	std::string failure;

	beast::tcp_stream &lowest()
	{
		return secure ? beast::get_lowest_layer(*secure) : *plain;
	}
	// Stops all I/O; the first terminal state wins.
	void finish(State state, const std::string &why = {})
	{
		if (status != State::Pending)
			return;
		status = state;
		failure = why;
		resolver.cancel();
		deadline.cancel();
		if (plain || secure)
		{
			Error ignored;
			lowest().socket().close(ignored);
		}
	}
	bool ok(Error error)
	{
		if (status != State::Pending)
			return false;
		if (error)
			finish(State::Failed, error.message());
		return status == State::Pending;
	}
	template <class Stream> void exchange(Stream &stream)
	{
		http::async_write(stream, request,
						  [this, &stream](Error error, size_t)
						  {
							  if (!ok(error))
								  return;
							  http::async_read(stream, buffer, *parser,
											   [this](Error error, size_t)
											   {
												   if (!ok(error))
													   return;
												   auto message = parser->release();
												   result.status = static_cast<int>(message.result_int());
												   for (const auto &field : message)
													   result.headers.emplace_back(std::string(field.name_string()),
																				   std::string(field.value()));
												   result.body = std::move(message.body());
												   finish(State::Done);
											   });
						  });
	}

  public:
	explicit NativeFetch(Request spec)
	{
		try
		{
			url = parseUrl(spec.url);
			request.method(http::string_to_verb(methodName(spec.method)));
			request.target(url.target);
			request.version(11);
			request.set(http::field::host, url.authority());
			request.set(http::field::user_agent, std::string("Globulation2/") + PACKAGE_VERSION);
			request.set(http::field::connection, "close");
			for (const auto &field : spec.headers)
			{
				if (field.first.empty() ||
					field.first.find_first_of(":\r\n") != std::string::npos ||
					field.second.find_first_of("\r\n") != std::string::npos)
					throw std::invalid_argument("Invalid request header");
				request.set(field.first, field.second);
			}
			if (spec.method != Method::Get || !spec.body.empty())
			{
				request.body() = std::move(spec.body);
				request.prepare_payload();
			}
			parser.emplace();
			parser->body_limit(spec.responseLimit);
			parser->header_limit(64 * 1024);
			if (url.secure)
			{
				const auto trust = NetTls::withEnvironmentTrust({});
				tls = NetTls::contextFor(ssl::context::tls_client, trust);
				secure.emplace(io, *tls);
				secure->set_verify_mode(ssl::verify_peer);
				NetTls::verifyServer(*secure, *tls, url.host, trust);
				NetTls::serverName(*secure, url.host);
			}
			else
				plain.emplace(io);
		}
		catch (const std::exception &error)
		{
			status = State::Failed;
			failure = error.what();
			return;
		}
		deadline.expires_after(spec.timeout);
		deadline.async_wait(
			[this](Error error)
			{
				if (!error)
					finish(State::TimedOut, "HTTP request timed out");
			});
		resolver.async_resolve(
			url.host, url.port,
			[this](Error error, tcp::resolver::results_type results)
			{
				if (!ok(error))
					return;
				lowest().async_connect(
					results,
					[this](Error error, const tcp::endpoint &)
					{
						if (!ok(error))
							return;
						if (!secure)
						{
							exchange(*plain);
							return;
						}
						secure->async_handshake(ssl::stream_base::client,
												[this](Error error)
												{
													if (ok(error))
														exchange(*secure);
												});
					});
			});
	}
	~NativeFetch() override
	{
		// Pending handlers are destroyed with io without running.
		finish(State::Cancelled);
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
		finish(State::Cancelled);
	}
};
} // namespace

std::unique_ptr<Fetch> start(Request request)
{
	return std::make_unique<NativeFetch>(std::move(request));
}
} // namespace HttpFetch
