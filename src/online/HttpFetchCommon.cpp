// SPDX-License-Identifier: GPL-3.0-or-later
// Platform-independent parts of HttpFetch, shared by the native and browser builds.
#include "HttpFetch.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace HttpFetch
{
namespace
{
std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

bool loopback(const std::string &host)
{
	if (lower(host) == "localhost" || host == "::1")
		return true;
	// 127.0.0.0/8 in dotted-quad form.
	if (host.compare(0, 4, "127.") != 0)
		return false;
	int parts = 0;
	size_t start = 0;
	while (start <= host.size())
	{
		size_t end = host.find('.', start);
		if (end == std::string::npos)
			end = host.size();
		const auto part = host.substr(start, end - start);
		if (part.empty() || part.size() > 3 ||
			!std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); }) ||
			std::stoi(part) > 255)
			return false;
		++parts;
		start = end + 1;
	}
	return parts == 4;
}
} // namespace

const char *methodName(Method method)
{
	switch (method)
	{
	case Method::Get:
		return "GET";
	case Method::Post:
		return "POST";
	case Method::Put:
		return "PUT";
	case Method::Patch:
		return "PATCH";
	case Method::Delete:
		return "DELETE";
	}
	return "GET";
}

std::string Response::header(const std::string &name) const
{
	const auto wanted = lower(name);
	for (const auto &field : headers)
		if (lower(field.first) == wanted)
			return field.second;
	return {};
}

std::string Url::authority() const
{
	const bool ipv6 = host.find(':') != std::string::npos;
	const auto name = ipv6 ? "[" + host + "]" : host;
	return port == (secure ? "443" : "80") ? name : name + ":" + port;
}

Url parseUrl(const std::string &text)
{
	Url url;
	size_t rest = 0;
	if (text.compare(0, 8, "https://") == 0)
		rest = 8;
	else if (text.compare(0, 7, "http://") == 0)
	{
		url.secure = false;
		rest = 7;
	}
	else
		throw std::invalid_argument("Only http:// and https:// URLs are supported");
	for (unsigned char c : text)
		if (c <= ' ' || c == 0x7f)
			throw std::invalid_argument("URL contains whitespace or control characters");
	const auto pathStart = text.find_first_of("/?#", rest);
	const auto authority =
		text.substr(rest, pathStart == std::string::npos ? std::string::npos : pathStart - rest);
	if (authority.find('@') != std::string::npos)
		throw std::invalid_argument("URLs must not carry credentials");
	if (authority.empty())
		throw std::invalid_argument("URL has no host");
	std::string port;
	if (authority[0] == '[')
	{
		const auto close = authority.find(']');
		if (close == std::string::npos)
			throw std::invalid_argument("Unterminated IPv6 address");
		url.host = authority.substr(1, close - 1);
		if (close + 1 < authority.size())
		{
			if (authority[close + 1] != ':')
				throw std::invalid_argument("Invalid URL authority");
			port = authority.substr(close + 2);
		}
	}
	else
	{
		const auto colon = authority.find(':');
		url.host = authority.substr(0, colon);
		if (colon != std::string::npos)
			port = authority.substr(colon + 1);
	}
	if (url.host.empty())
		throw std::invalid_argument("URL has no host");
	if (!port.empty())
	{
		if (port.size() > 5 ||
			!std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); }) ||
			std::stoi(port) < 1 || std::stoi(port) > 65535)
			throw std::invalid_argument("Invalid URL port");
		url.port = std::to_string(std::stoi(port));
	}
	else
		url.port = url.secure ? "443" : "80";
	if (pathStart == std::string::npos)
		url.target = "/";
	else
	{
		// The fragment is never sent to the server.
		url.target = text.substr(pathStart, text.find('#', pathStart) - pathStart);
		if (url.target.empty() || url.target[0] != '/')
			url.target = "/" + url.target;
	}
	if (!url.secure && !loopback(url.host))
		throw std::invalid_argument("Plain http:// is only allowed for loopback hosts");
	return url;
}
} // namespace HttpFetch
