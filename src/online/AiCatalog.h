// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Sha256.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Online
{
// Validate every field consumed by the native library before retaining server JSON.
// Unknown fields remain forward compatible; malformed responses never reach painting.
inline bool validAiText(const nlohmann::json &j, const char *key, std::size_t limit)
{
	auto i = j.find(key);
	return i != j.end() && i->is_string() && i->get_ref<const std::string &>().size() <= limit;
}
inline bool validAiCount(const nlohmann::json &j, const char *key)
{
	auto i = j.find(key);
	return i != j.end() && i->is_number_integer() && *i >= 0 && *i <= 2000000000;
}
inline bool validAiVersion(const nlohmann::json &v)
{
	if (!v.is_object() || !validAiText(v, "id", 64) || !validAiText(v, "hash", 64) ||
		!Sha256::isHexDigest(v["hash"].get<std::string>()) || !validAiText(v, "label", 256) ||
		!validAiText(v, "notes", 8000) || !validAiCount(v, "downloads"))
		return false;
	auto reports = v.find("validations");
	if (reports == v.end() || !reports->is_array() || reports->size() > 1000)
		return false;
	for (const auto &r : *reports)
	{
		if (!r.is_object() || !validAiText(r, "simVersion", 128) ||
			!validAiText(r, "sourceHash", 64) || !r.contains("valid") || !r["valid"].is_boolean() ||
			!validAiCount(r, "suite"))
			return false;
		auto checks = r.find("checks");
		if (checks == r.end() || !checks->is_array() || checks->size() != 7)
			return false;
		for (const auto &c : *checks)
			if (!c.is_object() || !validAiText(c, "id", 64) || !validAiText(c, "status", 64))
				return false;
	}
	return true;
}
inline bool validAiSummary(const nlohmann::json &a)
{
	if (!a.is_object() || !a.contains("owner") || !a["owner"].is_object() ||
		!validAiText(a["owner"], "displayName", 512) || !a.contains("tags") ||
		!a["tags"].is_array() || a["tags"].size() > 5)
		return false;
	for (const auto &tag : a["tags"])
		if (!tag.is_string() || tag.get_ref<const std::string &>().size() > 64)
			return false;
	return validAiText(a, "id", 64) && validAiText(a, "name", 512) &&
		   validAiText(a, "description", 16000) && validAiCount(a, "likes") &&
		   validAiCount(a, "downloads") && a.contains("liked") && a["liked"].is_boolean() &&
		   a.contains("favourited") && a["favourited"].is_boolean() &&
		   a.contains("latestVersion") && validAiVersion(a["latestVersion"]);
}
inline bool validAiDetail(const nlohmann::json &d)
{
	if (!d.is_object() || !d.contains("ai") || !validAiSummary(d["ai"]) ||
		!d.contains("versions") || !d["versions"].is_array() || d["versions"].empty() ||
		d["versions"].size() > 50)
		return false;
	return std::all_of(d["versions"].begin(), d["versions"].end(), validAiVersion);
}
inline bool compatibleAiVersion(const nlohmann::json &v, const std::string &sim)
{
	if (!validAiVersion(v))
		return false;
	for (const auto &r : v["validations"])
	{
		if (r["simVersion"] != sim || r["valid"] != true || r["suite"] != 1 ||
			r["sourceHash"] != v["hash"])
			continue;
		bool passed = true;
		for (const auto &id :
			 {"file", "syntax", "startup", "state", "gameplay", "determinism", "continuation"})
			passed &= std::count_if(r["checks"].begin(), r["checks"].end(), [&](const auto &c)
									{ return c["id"] == id && c["status"] == "passed"; }) == 1;
		if (passed)
			return true;
	}
	return false;
}
} // namespace Online
