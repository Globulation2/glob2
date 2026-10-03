// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "HttpClient.h"
#include "HttpExchange.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>

#include <stdexcept>

namespace Relay
{
namespace asio = boost::asio;
namespace ssl = asio::ssl;

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

asio::awaitable<HttpResponse> HttpClient::fetch(HttpRequest request)
{
	HttpResponse failure;
	NetHttp::Options options;
	try
	{
		const Url url = Url::parse(request.url);
		const bool defaultPort = (url.tls && url.port == "443") || (!url.tls && url.port == "80");
		options.method = request.method;
		options.secure = url.tls;
		options.host = url.host;
		options.port = url.port;
		options.target = url.target;
		options.authority = defaultPort ? url.host : url.host + ":" + url.port;
		options.userAgent = "glob2-relay";
		options.headers.assign(request.headers.begin(), request.headers.end());
		options.sendBody = !request.body.empty() || request.method == "POST" || request.method == "PUT";
		options.body = std::move(request.body);
		options.timeout = request.timeout;
		options.responseLimit = request.maxResponseBytes;
		options.headerLimit = 16 * 1024;
		if (url.tls)
		{
			options.tls = tls;
			options.prepareTls = [](NetHttp::TlsStream& stream, const std::string& host) {
				if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
					throw std::runtime_error("Could not set TLS server name");
				stream.set_verify_callback(ssl::host_name_verification(host));
			};
		}
	}
	catch (const std::exception& e)
	{
		failure.error = e.what();
		co_return failure;
	}
	auto executor = co_await asio::this_coro::executor;
	NetHttp::Result result;
	try
	{
		result = co_await asio::async_initiate<decltype(asio::use_awaitable), void(NetHttp::Result)>(
			[&executor, &options](auto handler) {
				auto shared = std::make_shared<decltype(handler)>(std::move(handler));
				NetHttp::Request::start(executor, std::move(options),
				                        [shared](NetHttp::Result r) { (*shared)(std::move(r)); });
			},
			asio::use_awaitable);
	}
	catch (const std::exception& e)
	{
		failure.error = e.what();
		co_return failure;
	}
	if (result.outcome != NetHttp::Outcome::Done)
	{
		failure.error = result.error.empty() ? "HTTP request failed" : result.error;
		co_return failure;
	}
	HttpResponse response;
	response.status = result.status;
	response.body = std::move(result.body);
	co_return response;
}
}
