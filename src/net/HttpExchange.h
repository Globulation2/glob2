// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// One HTTP/1.1 request over its own connection (plain or TLS), on any asio
// executor: the single native HTTP client implementation. Two front ends adapt
// it to their runtimes:
//
//   HttpFetch (src/online)   the game: an io_context per request, pumped from
//                            the UI thread through Fetch::state();
//   Relay::HttpClient        the relay: an awaitable on its event loop.
//
// The front ends keep their own URL policy, trust configuration and API; this
// does the resolution, connection, TLS handshake, exchange, limits and deadline.
// (The browser build uses emscripten_fetch instead, browser/HttpFetch.cpp.)
// Header-only, so the relay and the game link it without extra sources.

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace NetHttp
{
	using Headers = std::vector<std::pair<std::string, std::string>>;
	using TlsStream = boost::asio::ssl::stream<boost::beast::tcp_stream>;

	struct Options
	{
		std::string method = "GET";
		bool secure = true;
		std::string host; ///< without IPv6 brackets
		std::string port;
		std::string target = "/";
		/// The Host header value.
		std::string authority;
		std::string userAgent;
		Headers headers;
		std::string body;
		/// Send `body` with a Content-Length, even when empty.
		bool sendBody = false;
		/// Whole-request deadline: resolution, connection, TLS and transfer.
		std::chrono::milliseconds timeout{30000};
		std::size_t responseLimit = 1u << 20;
		std::size_t headerLimit = 64 * 1024;
		/// TLS only: the client context (trust anchors) and the per-connection
		/// verification and server name setup. `host` is the request's own copy,
		/// which lives as long as the connection (verifiers may keep a pointer).
		std::shared_ptr<boost::asio::ssl::context> tls;
		std::function<void(TlsStream& stream, const std::string& host)> prepareTls;
	};

	enum class Outcome
	{
		Done,     ///< a response arrived (any status)
		Failed,   ///< no response: resolution, connection, TLS or protocol failure
		TimedOut,
		Cancelled
	};

	struct Result
	{
		Outcome outcome = Outcome::Failed;
		int status = 0;
		/// Header names keep the server's spelling.
		Headers headers;
		std::string body;
		std::string error;
	};

	class Request
	{
	public:
		using Done = std::function<void(Result)>;
		/// Starts the request; `done` runs exactly once, on the executor, unless the
		/// executor is destroyed first. Throws std::invalid_argument for a header with
		/// a line break or colon in its name, or a CR/LF in its value.
		static std::shared_ptr<Request> start(boost::asio::any_io_executor executor, Options options, Done done);
		/// Aborts the request; `done` then runs with Outcome::Cancelled.
		virtual void cancel() = 0;
		virtual ~Request() = default;
	};
}

namespace NetHttp
{
namespace detail
{
	namespace asio = boost::asio;
	namespace ssl = asio::ssl;
	namespace beast = boost::beast;
	namespace http = beast::http;
	using tcp = asio::ip::tcp;
	using Error = boost::system::error_code;

	class Exchange final : public Request, public std::enable_shared_from_this<Exchange>
	{
	public:
		Exchange(asio::any_io_executor executor, Options options, Done done)
			: options(std::move(options)), done(std::move(done)), resolver(executor), deadline(executor)
		{
			request.method(http::string_to_verb(this->options.method));
			request.target(this->options.target);
			request.version(11);
			request.set(http::field::host, this->options.authority);
			request.set(http::field::user_agent, this->options.userAgent);
			request.set(http::field::connection, "close");
			for (const auto& field : this->options.headers)
			{
				if (field.first.empty() || field.first.find_first_of(":\r\n") != std::string::npos ||
				    field.second.find_first_of("\r\n") != std::string::npos)
					throw std::invalid_argument("Invalid request header");
				request.set(field.first, field.second);
			}
			if (this->options.sendBody)
			{
				request.body() = std::move(this->options.body);
				request.prepare_payload();
			}
			parser.body_limit(this->options.responseLimit);
			parser.header_limit(static_cast<std::uint32_t>(this->options.headerLimit));
			if (this->options.secure)
			{
				if (!this->options.tls)
					throw std::invalid_argument("HTTPS needs a TLS context");
				secure.emplace(executor, *this->options.tls);
				if (this->options.prepareTls)
					this->options.prepareTls(*secure, this->options.host);
			}
			else
				plain.emplace(executor);
		}

		void run()
		{
			auto self = shared_from_this();
			deadline.expires_after(options.timeout);
			deadline.async_wait([self](Error error) {
				if (!error)
					self->finish(Outcome::TimedOut, "HTTP request timed out");
			});
			resolver.async_resolve(options.host, options.port,
			                       [self](Error error, tcp::resolver::results_type results) {
				                       if (!self->ok(error))
					                       return;
				                       self->lowest().async_connect(results, [self](Error error, const tcp::endpoint&) {
					                       if (!self->ok(error))
						                       return;
					                       if (!self->secure)
					                       {
						                       self->exchange(*self->plain);
						                       return;
					                       }
					                       self->secure->async_handshake(ssl::stream_base::client, [self](Error error) {
						                       if (self->ok(error))
							                       self->exchange(*self->secure);
					                       });
				                       });
			                       });
		}

		void cancel() override { finish(Outcome::Cancelled, "HTTP request cancelled"); }

	private:
		beast::tcp_stream& lowest() { return secure ? beast::get_lowest_layer(*secure) : *plain; }

		// Stops all I/O and reports; the first outcome wins.
		void finish(Outcome outcome, const std::string& why = {}, Result result = {})
		{
			if (finished)
				return;
			finished = true;
			resolver.cancel();
			deadline.cancel();
			if (plain || secure)
			{
				Error ignored;
				lowest().socket().close(ignored);
			}
			result.outcome = outcome;
			result.error = why;
			if (auto report = std::exchange(done, nullptr))
				report(std::move(result));
		}

		bool ok(Error error)
		{
			if (finished)
				return false;
			if (error)
				finish(Outcome::Failed, error.message());
			return !finished;
		}

		template <class Stream>
		void exchange(Stream& stream)
		{
			auto self = shared_from_this();
			http::async_write(stream, request, [self, &stream](Error error, std::size_t) {
				if (!self->ok(error))
					return;
				http::async_read(stream, self->buffer, self->parser, [self](Error error, std::size_t) {
					if (!self->ok(error))
						return;
					auto message = self->parser.release();
					Result result;
					result.status = static_cast<int>(message.result_int());
					for (const auto& field : message)
						result.headers.emplace_back(std::string(field.name_string()), std::string(field.value()));
					result.body = std::move(message.body());
					self->finish(Outcome::Done, {}, std::move(result));
				});
			});
		}

		Options options;
		Done done;
		bool finished = false;
		tcp::resolver resolver;
		asio::steady_timer deadline;
		std::optional<beast::tcp_stream> plain;
		std::optional<TlsStream> secure;
		beast::flat_buffer buffer;
		http::request<http::string_body> request;
		http::response_parser<http::string_body> parser;
	};
}

inline std::shared_ptr<Request> Request::start(boost::asio::any_io_executor executor, Options options, Done done)
{
	auto exchange = std::make_shared<detail::Exchange>(std::move(executor), std::move(options), std::move(done));
	exchange->run();
	return exchange;
}
}
