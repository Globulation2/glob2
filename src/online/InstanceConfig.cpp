// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstanceConfig.h"
#include "OnlineStorage.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace Online
{
namespace
{
std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	return text;
}

bool isLoopback(const std::string &host)
{
	return host == "localhost" || host == "::1" || host.rfind("127.", 0) == 0;
}
} // namespace

std::optional<std::string> normalizeOrigin(const std::string &input)
{
	if (input.size() > 2048)
		return std::nullopt;
	const auto separator = input.find("://");
	if (separator == std::string::npos)
		return std::nullopt;
	const auto scheme = lower(input.substr(0, separator));
	if (scheme != "https" && scheme != "http")
		return std::nullopt;
	auto authority = input.substr(separator + 3);
	if (!authority.empty() && authority.back() == '/')
		authority.pop_back();
	if (authority.empty() || authority.find_first_of("/?#@\\ \t\r\n") != std::string::npos)
		return std::nullopt;
	std::string host, port;
	if (authority.front() == '[')
	{
		const auto end = authority.find(']');
		if (end == std::string::npos)
			return std::nullopt;
		host = lower(authority.substr(1, end - 1));
		if (host.empty() || host.find_first_not_of("0123456789abcdef:.") != std::string::npos)
			return std::nullopt;
		if (end + 1 < authority.size())
		{
			if (authority[end + 1] != ':')
				return std::nullopt;
			port = authority.substr(end + 2);
			if (port.empty())
				return std::nullopt;
		}
	}
	else
	{
		const auto colon = authority.find(':');
		host = lower(authority.substr(0, colon));
		if (colon != std::string::npos)
		{
			port = authority.substr(colon + 1);
			if (port.empty())
				return std::nullopt;
		}
		if (host.empty() || host.size() > 253 || host.front() == '.' || host.front() == '-' ||
			host.find("..") != std::string::npos ||
			host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.") != std::string::npos)
			return std::nullopt;
	}
	if (!port.empty())
	{
		if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
			return std::nullopt;
		const int number = std::stoi(port);
		if (number < 1 || number > 65535)
			return std::nullopt;
		port = std::to_string(number);
		if ((scheme == "https" && number == 443) || (scheme == "http" && number == 80))
			port.clear();
	}
	if (scheme == "http" && !isLoopback(host))
		return std::nullopt;
	const bool ipv6 = host.find(':') != std::string::npos;
	return scheme + "://" + (ipv6 ? "[" + host + "]" : host) + (port.empty() ? "" : ":" + port);
}

std::string realtimeUrl(const std::string &origin)
{
	if (origin.rfind("https://", 0) == 0)
		return "wss://" + origin.substr(8) + "/realtime";
	if (origin.rfind("http://", 0) == 0)
		return "ws://" + origin.substr(7) + "/realtime";
	return origin + "/realtime";
}

std::string apiUrl(const std::string &origin, const std::string &path)
{
	return origin + (path.empty() || path.front() != '/' ? "/" + path : path);
}

InstanceConfig::InstanceConfig(OnlineStorage &storage)
	: storage(storage), selected(OFFICIAL_INSTANCE_ORIGIN)
{
}

bool InstanceConfig::load()
{
	std::string text;
	return storage.read(FILE_NAME, text) && fromJson(text);
}

bool InstanceConfig::save()
{
	if (!storage.write(FILE_NAME, toJson()))
		return false;
	storage.persist();
	return true;
}

bool InstanceConfig::selectInstance(const std::string &origin)
{
	auto normalized = normalizeOrigin(origin);
	if (!normalized)
		return false;
	selected = *normalized;
	return true;
}

InstanceRecord &InstanceConfig::record(const std::string &origin)
{
	return records[origin];
}

const InstanceRecord *InstanceConfig::find(const std::string &origin) const
{
	auto found = records.find(origin);
	return found == records.end() ? nullptr : &found->second;
}

void InstanceConfig::forget(const std::string &origin)
{
	records.erase(origin);
	sessionTrust.erase(origin);
}

bool InstanceConfig::isTrusted(const std::string &origin) const
{
	if (origin == OFFICIAL_INSTANCE_ORIGIN || origin == selected || sessionTrust.count(origin))
		return true;
	const auto *found = find(origin);
	return found && found->trusted;
}

void InstanceConfig::trust(const std::string &origin, bool remember)
{
	sessionTrust.insert(origin);
	if (remember)
		record(origin).trusted = true;
}

void InstanceConfig::untrust(const std::string &origin)
{
	sessionTrust.erase(origin);
	if (auto found = records.find(origin); found != records.end())
		found->second.trusted = false;
}

std::string InstanceConfig::toJson() const
{
	nlohmann::json instances = nlohmann::json::object();
	for (const auto &[origin, record] : records)
	{
		nlohmann::json entry = {{"trusted", record.trusted}, {"autoSignIn", record.autoSignIn}};
		if (!record.deviceCredential.empty())
			entry["deviceCredential"] = record.deviceCredential;
		if (!record.refreshToken.empty())
			entry["refreshToken"] = record.refreshToken;
		if (!record.lastDisplayName.empty())
			entry["lastDisplayName"] = record.lastDisplayName;
		instances[origin] = std::move(entry);
	}
	nlohmann::json document = {
		{"version", FORMAT_VERSION}, {"selected", selected}, {"instances", std::move(instances)}};
	return document.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

bool InstanceConfig::fromJson(const std::string &text)
{
	auto document = nlohmann::json::parse(text, nullptr, false);
	if (document.is_discarded() || !document.is_object())
		return false;
	auto string = [](const nlohmann::json &object, const char *name)
	{
		auto found = object.find(name);
		return found != object.end() && found->is_string() ? found->get<std::string>()
															: std::string();
	};
	auto boolean = [](const nlohmann::json &object, const char *name, bool fallback)
	{
		auto found = object.find(name);
		return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
	};
	std::string newSelected = OFFICIAL_INSTANCE_ORIGIN;
	if (auto origin = normalizeOrigin(string(document, "selected")))
		newSelected = *origin;
	std::map<std::string, InstanceRecord> newRecords;
	if (auto instances = document.find("instances");
		instances != document.end() && instances->is_object())
		for (const auto &[key, entry] : instances->items())
		{
			auto origin = normalizeOrigin(key);
			if (!origin || !entry.is_object())
				continue;
			InstanceRecord record;
			record.deviceCredential = string(entry, "deviceCredential");
			record.refreshToken = string(entry, "refreshToken");
			record.lastDisplayName = string(entry, "lastDisplayName");
			record.trusted = boolean(entry, "trusted", false);
			record.autoSignIn = boolean(entry, "autoSignIn", true);
			newRecords[*origin] = std::move(record);
		}
	selected = std::move(newSelected);
	records = std::move(newRecords);
	return true;
}
} // namespace Online
