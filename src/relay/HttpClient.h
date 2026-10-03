// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The relay's HTTP/1.1 client for its calls to the platform (JWKS, registration,
// heartbeat, setup, record upload): the relay's front end of NetHttp::Request
// (src/net/HttpExchange.h), the HTTP implementation the game's HttpFetch uses too.
// Plain HTTP and HTTPS, one request per connection, bounded response size and a
// whole-request deadline. An awaitable on the relay's io_context; nothing blocks
// the event loop.

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace boost::asio::ssl
{
	class context;
}

namespace Relay
{
	struct Url
	{
		bool tls = false;
		std::string host;
		std::string port;
		std::string target = "/";
		/// Parses http(s):// and ws(s):// URLs. Throws std::invalid_argument.
		static Url parse(const std::string& url);
		/// Resolves a path against this URL's origin: "/a" replaces the path.
		std::string origin() const;
	};

	struct HttpRequest
	{
		std::string method = "GET";
		std::string url;
		std::map<std::string, std::string> headers;
		std::string body;
		std::chrono::milliseconds timeout{10000};
		std::size_t maxResponseBytes = 1u << 20;
	};

	struct HttpResponse
	{
		int status = 0;        ///< 0 when the request failed before a response arrived
		std::string body;
		std::string error;     ///< transport error, if any
		bool ok() const { return error.empty() && status >= 200 && status < 300; }
	};

	class HttpClient
	{
	public:
		/// caFile: extra trust anchors for HTTPS; empty uses the system store.
		explicit HttpClient(const std::string& caFile = {});
		~HttpClient();
		/// Never throws: failures come back in HttpResponse::error.
		boost::asio::awaitable<HttpResponse> fetch(HttpRequest request);

	private:
		std::shared_ptr<boost::asio::ssl::context> tls;
	};
}
