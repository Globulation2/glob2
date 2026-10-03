// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "HttpClient.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>

#include <stdexcept>

namespace Relay
{
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

Url Url::parse(const std::string& url)
{
	Url u;
	std::string rest;
	auto take = [&](const char* scheme, bool tls, const char* port) {
		const std::string prefix(scheme);
		if (url.compare(0, prefix.size(), prefix) != 0)
			return false;
		u.tls = tls;
		u.port = port;
		rest = url.substr(prefix.size());
		return true;
	};
	if (!take("https://", true, "443") && !take("http://", false, "80") && !take("wss://", true, "443") &&
	    !take("ws://", false, "80"))
		throw std::invalid_argument("URL must start with http://, https://, ws:// or wss://: " + url);
	const auto slash = rest.find('/');
	std::string authority = rest.substr(0, slash);
	u.target = slash == std::string::npos ? "/" : rest.substr(slash);
	if (authority.empty() || authority.find('@') != std::string::npos)
		throw std::invalid_argument("URL needs a host and no user info: " + url);
	if (authority[0] == '[')
	{
		const auto close = authority.find(']');
		if (close == std::string::npos)
			throw std::invalid_argument("Bad IPv6 literal in URL: " + url);
		u.host = authority.substr(1, close - 1);
		if (close + 1 < authority.size())
		{
			if (authority[close + 1] != ':')
				throw std::invalid_argument("Bad port in URL: " + url);
			u.port = authority.substr(close + 2);
		}
	}
	else
	{
		const auto colon = authority.rfind(':');
		u.host = authority.substr(0, colon);
		if (colon != std::string::npos)
			u.port = authority.substr(colon + 1);
	}
	if (u.host.empty() || u.port.empty() || u.port.find_first_not_of("0123456789") != std::string::npos)
		throw std::invalid_argument("Bad host or port in URL: " + url);
	return u;
}

std::string Url::origin() const
{
	const bool defaultPort = (tls && port == "443") || (!tls && port == "80");
	const bool v6 = host.find(':') != std::string::npos;
	return std::string(tls ? "https://" : "http://") + (v6 ? "[" + host + "]" : host) + (defaultPort ? "" : ":" + port);
}

HttpClient::HttpClient(const std::string& caFile) : tls(std::make_shared<ssl::context>(ssl::context::tls_client))
{
	SSL_CTX_set_min_proto_version(tls->native_handle(), TLS1_2_VERSION);
	if (caFile.empty())
		tls->set_default_verify_paths();
	else
		tls->load_verify_file(caFile);
	tls->set_verify_mode(ssl::verify_peer);
}

HttpClient::~HttpClient() = default;

namespace
{
	template <class Stream>
	asio::awaitable<HttpResponse> exchange(Stream& stream, const Url& url, const HttpRequest& request)
	{
		http::request<http::string_body> req;
		req.method(http::string_to_verb(request.method));
		req.target(url.target);
		req.version(11);
		const bool defaultPort = (url.tls && url.port == "443") || (!url.tls && url.port == "80");
		req.set(http::field::host, defaultPort ? url.host : url.host + ":" + url.port);
		req.set(http::field::user_agent, "glob2-relay");
		req.set(http::field::connection, "close");
		for (const auto& h : request.headers)
			req.set(h.first, h.second);
		if (!request.body.empty() || request.method == "POST" || request.method == "PUT")
		{
			req.body() = request.body;
			req.prepare_payload();
		}
		co_await http::async_write(stream, req, asio::use_awaitable);
		beast::flat_buffer buffer;
		http::response_parser<http::string_body> parser;
		parser.body_limit(request.maxResponseBytes);
		parser.header_limit(16 * 1024);
		co_await http::async_read(stream, buffer, parser, asio::use_awaitable);
		HttpResponse response;
		response.status = static_cast<int>(parser.get().result_int());
		response.body = std::move(parser.get().body());
		co_return response;
	}
}

asio::awaitable<HttpResponse> HttpClient::fetch(HttpRequest request)
{
	HttpResponse failure;
	try
	{
		const Url url = Url::parse(request.url);
		auto executor = co_await asio::this_coro::executor;
		tcp::resolver resolver(executor);
		beast::tcp_stream tcpStream(executor);
		tcpStream.expires_after(request.timeout);
		const auto endpoints = co_await resolver.async_resolve(url.host, url.port, asio::use_awaitable);
		co_await tcpStream.async_connect(endpoints, asio::use_awaitable);
		if (!url.tls)
		{
			auto response = co_await exchange(tcpStream, url, request);
			beast::error_code ignored;
			tcpStream.socket().shutdown(tcp::socket::shutdown_both, ignored);
			co_return response;
		}
		beast::ssl_stream<beast::tcp_stream> stream(std::move(tcpStream), *tls);
		beast::get_lowest_layer(stream).expires_after(request.timeout);
		if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str()))
			throw std::runtime_error("Could not set TLS server name");
		stream.set_verify_callback(ssl::host_name_verification(url.host));
		co_await stream.async_handshake(ssl::stream_base::client, asio::use_awaitable);
		auto response = co_await exchange(stream, url, request);
		beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(2));
		boost::system::error_code ignored;
		co_await stream.async_shutdown(asio::redirect_error(asio::use_awaitable, ignored));
		co_return response;
	}
	catch (const std::exception& e)
	{
		failure.error = e.what();
	}
	co_return failure;
}
}
