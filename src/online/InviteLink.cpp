// SPDX-License-Identifier: GPL-3.0-or-later
#include "InviteLink.h"
#include "InstanceConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Online
{
namespace
{
std::optional<InviteLink> pending;

int hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

std::optional<std::string> percentDecode(const std::string &text)
{
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '%')
		{
			if (i + 2 >= text.size())
				return std::nullopt;
			const int high = hexValue(text[i + 1]), low = hexValue(text[i + 2]);
			if (high < 0 || low < 0)
				return std::nullopt;
			out.push_back(char(high * 16 + low));
			i += 2;
		}
		else if (text[i] == '+')
			out.push_back(' ');
		else
			out.push_back(text[i]);
	}
	return out;
}

std::string percentEncode(const std::string &text)
{
	static const char digits[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text)
	{
		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
			out.push_back(char(c));
		else
		{
			out.push_back('%');
			out.push_back(digits[c >> 4]);
			out.push_back(digits[c & 15]);
		}
	}
	return out;
}

std::string lowerPrefix(const std::string &text, std::size_t count)
{
	std::string prefix = text.substr(0, count);
	std::transform(prefix.begin(), prefix.end(), prefix.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	return prefix;
}

// Finds a query parameter. Returns false when it is malformed; `present`
// tells whether it was there at all.
bool queryValue(const std::string &query, const std::string &name, std::string &value,
				bool &present)
{
	present = false;
	std::size_t at = 0;
	while (at <= query.size())
	{
		auto end = query.find('&', at);
		if (end == std::string::npos)
			end = query.size();
		const auto pair = query.substr(at, end - at);
		const auto equals = pair.find('=');
		if (pair.substr(0, equals) == name)
		{
			present = true;
			auto decoded = equals == std::string::npos ? std::optional<std::string>(std::string())
													   : percentDecode(pair.substr(equals + 1));
			if (!decoded)
				return false;
			value = *decoded;
			return true;
		}
		at = end + 1;
	}
	return true;
}
} // namespace

bool isInviteCode(const std::string &code)
{
	return code.size() >= 6 && code.size() <= 16 &&
		   std::all_of(code.begin(), code.end(),
					   [](unsigned char c) { return c < 128 && std::isalnum(c); });
}

std::optional<InviteLink> parseInviteLink(const std::string &input)
{
	if (input.size() > 2048)
		return std::nullopt;
	std::string link = input;
	// Some launchers pass a trailing newline or quote the argument.
	while (!link.empty() && std::isspace(static_cast<unsigned char>(link.back())))
		link.pop_back();
	if (link.empty() || std::any_of(link.begin(), link.end(), [](unsigned char c)
									{ return c <= 32 || c >= 127; }))
		return std::nullopt;
	if (const auto hash = link.find('#'); hash != std::string::npos)
		link.resize(hash);

	if (lowerPrefix(link, 6) == "glob2:")
	{
		// glob2://join?…, also glob2://join/?… and glob2:join?…
		std::string rest = link.substr(6);
		if (rest.rfind("//", 0) == 0)
			rest = rest.substr(2);
		const auto question = rest.find('?');
		std::string action = rest.substr(0, question);
		if (!action.empty() && action.back() == '/')
			action.pop_back();
		std::transform(action.begin(), action.end(), action.begin(),
					   [](unsigned char c) { return char(std::tolower(c)); });
		if (action != "join" || question == std::string::npos)
			return std::nullopt;
		const auto query = rest.substr(question + 1);
		std::string code, instance = OFFICIAL_INSTANCE_ORIGIN;
		bool hasCode = false, hasInstance = false;
		if (!queryValue(query, "code", code, hasCode) || !hasCode || !isInviteCode(code) ||
			!queryValue(query, "instance", instance, hasInstance))
			return std::nullopt;
		auto origin = normalizeOrigin(instance);
		if (!origin)
			return std::nullopt;
		return InviteLink{currentOrigin(*origin), code};
	}

	const auto scheme = lowerPrefix(link, 8);
	if (scheme == "https://" || scheme.rfind("http://", 0) == 0)
	{
		const auto authorityStart = link.find("://") + 3;
		const auto pathStart = link.find('/', authorityStart);
		if (pathStart == std::string::npos)
			return std::nullopt;
		auto origin = normalizeOrigin(link.substr(0, pathStart));
		if (!origin)
			return std::nullopt;
		std::string path = link.substr(pathStart);
		if (const auto question = path.find('?'); question != std::string::npos)
			path.resize(question);
		if (path.rfind("/j/", 0) != 0)
			return std::nullopt;
		std::string code = path.substr(3);
		if (!code.empty() && code.back() == '/')
			code.pop_back();
		if (!isInviteCode(code))
			return std::nullopt;
		// A former official origin's /j/ link redirects to the official one.
		return InviteLink{currentOrigin(*origin), code};
	}
	return std::nullopt;
}

std::optional<InviteLink> parseInvite(const std::string &linkOrCode, const std::string &defaultOrigin)
{
	if (isInviteCode(linkOrCode))
	{
		auto origin = normalizeOrigin(defaultOrigin);
		if (!origin)
			return std::nullopt;
		return InviteLink{currentOrigin(*origin), linkOrCode};
	}
	return parseInviteLink(linkOrCode);
}

std::string formatSchemeLink(const InviteLink &invite)
{
	return "glob2://join?instance=" + percentEncode(invite.origin) + "&code=" + invite.code;
}

std::string formatWebLink(const InviteLink &invite)
{
	return invite.origin + "/j/" + invite.code;
}

void setPendingJoin(const InviteLink &invite)
{
	pending = invite;
}

std::optional<InviteLink> pendingJoin()
{
	return pending;
}

std::optional<InviteLink> takePendingJoin()
{
	auto taken = std::move(pending);
	pending.reset();
	return taken;
}

void clearPendingJoin()
{
	pending.reset();
}

bool acceptInviteText(const std::string &text)
{
	auto invite = parseInviteLink(text);
	if (!invite)
		return false;
	setPendingJoin(*invite);
	return true;
}
int acceptLaunchArguments(int argc, char **argv, int index)
{
	const std::string argument = argv[index];
	if (argument == "--instance")
		return index + 1 < argc ? 2 : 1;
	if (argument == "--join")
	{
		if (index + 1 >= argc)
			return 1;
		std::string origin = OFFICIAL_INSTANCE_ORIGIN;
		for (int i = 1; i + 1 < argc; ++i)
			if (std::strcmp(argv[i], "--instance") == 0)
				origin = argv[i + 1];
		if (auto invite = parseInvite(argv[index + 1], origin))
			setPendingJoin(*invite);
		else
			std::fprintf(stderr, "Ignoring invalid invite: %s\n", argv[index + 1]);
		return 2;
	}
	return acceptInviteText(argument) ? 1 : 0;
}
} // namespace Online
