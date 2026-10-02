// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlatformProtocol.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace Online
{
namespace
{
bool isMethodName(const std::string &name)
{
	// ^[a-z]+(\.[A-Za-z]+)+$, at most 64 characters (realtime.ts).
	if (name.empty() || name.size() > 64)
		return false;
	std::size_t i = 0;
	while (i < name.size() && name[i] >= 'a' && name[i] <= 'z')
		++i;
	if (i == 0 || i == name.size())
		return false;
	while (i < name.size())
	{
		if (name[i++] != '.')
			return false;
		const auto start = i;
		while (i < name.size() && std::isalpha(static_cast<unsigned char>(name[i])))
			++i;
		if (i == start)
			return false;
	}
	return true;
}

bool isRequestId(const Json &id)
{
	return id.is_string() && !id.get_ref<const std::string &>().empty() &&
		   id.get_ref<const std::string &>().size() <= 64;
}

std::string stringField(const Json &object, const char *name)
{
	auto found = object.find(name);
	return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant).
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool digits(std::string_view text, std::size_t at, std::size_t count, int &value)
{
	if (at + count > text.size())
		return false;
	value = 0;
	for (std::size_t i = at; i < at + count; ++i)
	{
		if (text[i] < '0' || text[i] > '9')
			return false;
		value = value * 10 + (text[i] - '0');
	}
	return true;
}

std::optional<std::string> base64UrlDecode(std::string_view text)
{
	std::string out;
	unsigned buffer = 0;
	int bits = 0;
	for (char c : text)
	{
		int value;
		if (c >= 'A' && c <= 'Z')
			value = c - 'A';
		else if (c >= 'a' && c <= 'z')
			value = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			value = c - '0' + 52;
		else if (c == '-')
			value = 62;
		else if (c == '_')
			value = 63;
		else if (c == '=')
			break;
		else
			return std::nullopt;
		buffer = (buffer << 6) | unsigned(value);
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back(char((buffer >> bits) & 0xff));
		}
	}
	return out;
}
} // namespace

std::string encodeRequest(const std::string &id, const std::string &method, const Json &params)
{
	Json request = {{"type", "request"},
					{"id", id},
					{"method", method},
					{"params", params.is_null() ? Json::object() : params}};
	return request.dump(-1, ' ', false, Json::error_handler_t::replace);
}

ApiError errorFromJson(const Json &body, const std::string &fallbackCode)
{
	ApiError error;
	if (body.is_object())
	{
		error.code = stringField(body, "code");
		error.message = stringField(body, "message");
		if (auto details = body.find("details"); details != body.end())
			error.details = *details;
	}
	if (error.code.empty())
		error.code = fallbackCode;
	return error;
}

ServerMessage decodeServerMessage(std::string_view text)
{
	ServerMessage message;
	Json frame = Json::parse(text.begin(), text.end(), nullptr, false);
	if (frame.is_discarded() || !frame.is_object())
	{
		message.invalid = "frame is not a JSON object";
		return message;
	}
	const auto type = stringField(frame, "type");
	if (type == "response")
	{
		auto id = frame.find("id");
		auto ok = frame.find("ok");
		if (id == frame.end() || !isRequestId(*id) || ok == frame.end() || !ok->is_boolean())
		{
			message.invalid = "response without a valid id and ok flag";
			return message;
		}
		message.id = id->get<std::string>();
		message.ok = ok->get<bool>();
		if (message.ok)
		{
			auto result = frame.find("result");
			if (result == frame.end() || !result->is_object())
			{
				message.invalid = "successful response without a result object";
				return message;
			}
			message.result = std::move(*result);
		}
		else
		{
			auto error = frame.find("error");
			if (error == frame.end() || !error->is_object())
			{
				message.invalid = "failed response without an error object";
				return message;
			}
			message.error = errorFromJson(*error, "error");
		}
		message.kind = ServerMessage::Kind::Response;
		return message;
	}
	if (type == "event")
	{
		message.event = stringField(frame, "event");
		auto data = frame.find("data");
		if (!isMethodName(message.event) || data == frame.end() || !data->is_object())
		{
			message.invalid = "event without a valid name and data object";
			return message;
		}
		message.data = std::move(*data);
		message.kind = ServerMessage::Kind::Event;
		return message;
	}
	message.invalid = "unknown frame type";
	return message;
}

Json SimVersion::toJson() const
{
	return {{"versionMinor", versionMinor}, {"netProtocol", netProtocol}, {"dataHash", dataHash}};
}

SimVersion SimVersion::local()
{
	SimVersion version;
	version.versionMinor = VERSION_MINOR;
	version.netProtocol = NET_PROTOCOL_VERSION;
	version.dataHash = std::string(64, '0');
	return version;
}

const char *clientPlatform()
{
#if defined(__EMSCRIPTEN__)
	return "browser";
#elif defined(__ANDROID__)
	return "android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
	return "ios";
#else
	return "desktop";
#endif
}

std::optional<AuthTokens> AuthTokens::fromJson(const Json &json)
{
	if (!json.is_object())
		return std::nullopt;
	AuthTokens tokens;
	tokens.accessToken = stringField(json, "accessToken");
	tokens.accessTokenExpiresAt = stringField(json, "accessTokenExpiresAt");
	tokens.refreshToken = stringField(json, "refreshToken");
	tokens.refreshTokenExpiresAt = stringField(json, "refreshTokenExpiresAt");
	if (tokens.accessToken.empty() || tokens.refreshToken.empty())
		return std::nullopt;
	return tokens;
}

std::optional<Account> Account::fromJson(const Json &json)
{
	if (!json.is_object())
		return std::nullopt;
	Account account;
	account.id = stringField(json, "id");
	account.displayName = stringField(json, "displayName");
	account.kind = stringField(json, "kind");
	account.role = stringField(json, "role");
	if (account.id.empty())
		return std::nullopt;
	account.raw = json;
	return account;
}

std::optional<std::int64_t> parseTimestamp(std::string_view text)
{
	// YYYY-MM-DDTHH:MM:SS[.fraction](Z|+HH:MM|-HH:MM)
	int year, month, day, hour, minute, second;
	if (!digits(text, 0, 4, year) || text.size() < 20 || text[4] != '-' ||
		!digits(text, 5, 2, month) || text[7] != '-' || !digits(text, 8, 2, day) ||
		(text[10] != 'T' && text[10] != 't') || !digits(text, 11, 2, hour) || text[13] != ':' ||
		!digits(text, 14, 2, minute) || text[16] != ':' || !digits(text, 17, 2, second))
		return std::nullopt;
	if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
		return std::nullopt;
	std::size_t at = 19;
	std::int64_t millis = 0;
	if (at < text.size() && text[at] == '.')
	{
		++at;
		const auto start = at;
		std::int64_t scale = 100;
		while (at < text.size() && text[at] >= '0' && text[at] <= '9')
		{
			millis += (text[at] - '0') * scale;
			scale /= 10;
			++at;
		}
		if (at == start)
			return std::nullopt;
	}
	std::int64_t offsetMinutes = 0;
	if (at < text.size() && (text[at] == 'Z' || text[at] == 'z'))
		++at;
	else if (at < text.size() && (text[at] == '+' || text[at] == '-'))
	{
		int oh, om;
		if (!digits(text, at + 1, 2, oh) || at + 3 >= text.size() || text[at + 3] != ':' ||
			!digits(text, at + 4, 2, om))
			return std::nullopt;
		offsetMinutes = (oh * 60 + om) * (text[at] == '-' ? -1 : 1);
		at += 6;
	}
	else
		return std::nullopt;
	if (at != text.size())
		return std::nullopt;
	const auto days = daysFromCivil(year, unsigned(month), unsigned(day));
	const std::int64_t seconds =
		days * 86400 + hour * 3600 + minute * 60 + std::min(second, 59) - offsetMinutes * 60;
	return seconds * 1000 + millis;
}

std::optional<Json> jwtClaims(std::string_view token)
{
	const auto first = token.find('.');
	if (first == std::string_view::npos)
		return std::nullopt;
	const auto second = token.find('.', first + 1);
	if (second == std::string_view::npos)
		return std::nullopt;
	auto payload = base64UrlDecode(token.substr(first + 1, second - first - 1));
	if (!payload)
		return std::nullopt;
	Json claims = Json::parse(*payload, nullptr, false);
	if (claims.is_discarded() || !claims.is_object())
		return std::nullopt;
	return claims;
}

std::optional<std::int64_t> tokenLifetimeMs(std::string_view token)
{
	auto claims = jwtClaims(token);
	if (!claims)
		return std::nullopt;
	auto iat = claims->find("iat");
	auto exp = claims->find("exp");
	if (iat == claims->end() || exp == claims->end() || !iat->is_number() || !exp->is_number())
		return std::nullopt;
	const double lifetime = exp->get<double>() - iat->get<double>();
	if (!(lifetime > 0) || lifetime > 366.0 * 86400)
		return std::nullopt;
	return static_cast<std::int64_t>(lifetime * 1000);
}

std::int64_t refreshDelayMs(std::int64_t lifetimeMs)
{
	const std::int64_t margin = std::max<std::int64_t>(60000, lifetimeMs / 5);
	std::int64_t delay = lifetimeMs - margin;
	// Very short lifetimes (tests, misconfigured instances): halfway.
	if (delay < lifetimeMs / 2)
		delay = lifetimeMs / 2;
	return std::max<std::int64_t>(1000, delay);
}

Backoff::Backoff(std::int64_t initialMs, std::int64_t maxMs, double factor, double jitter)
	: initialMs(std::max<std::int64_t>(1, initialMs)), maxMs(std::max(initialMs, maxMs)),
	  factor(std::max(1.0, factor)), jitter(std::clamp(jitter, 0.0, 1.0))
{
}

std::int64_t Backoff::next(double random)
{
	const double raw = double(initialMs) * std::pow(factor, std::min(attempt, 62));
	const double capped = std::min(raw, double(maxMs));
	++attempt;
	const double reduced = capped * (1.0 - jitter * std::clamp(random, 0.0, 1.0));
	return std::max<std::int64_t>(1, static_cast<std::int64_t>(reduced));
}
} // namespace Online
