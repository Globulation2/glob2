// SPDX-License-Identifier: GPL-3.0-or-later
#include "ApiErrorText.h"
#include "generated/OnlineErrorCatalog.h"
#include <cmath>

namespace Online
{
namespace
{
constexpr const char *Generic = "Something went wrong. Please try again.";
const Json &catalog(std::string_view locale)
{
	if (locale == "th" || locale == "th-TH")
	{
		static const Json value = []() {
			std::string text;
			for (auto chunk : ThaiErrorCatalogData::Chunks) text.append(chunk.data(), chunk.size());
			return Json::parse(text);
		}();
		return value;
	}
	static const Json value = []() {
		std::string text;
		for (auto chunk : ErrorCatalogData::Chunks) text.append(chunk.data(), chunk.size());
		return Json::parse(text);
	}();
	return value;
}
bool nameCharacter(char c, bool first)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (!first && ((c >= '0' && c <= '9') || c == '_'));
}
bool validParams(const Json &params)
{
	if (!params.is_object()) return false;
	for (const auto &value : params)
		if (!(value.is_string() || value.is_number() || value.is_boolean() || value.is_null()) ||
			(value.is_number_float() && !std::isfinite(value.get<double>()))) return false;
	return true;
}
// Read only the original template once. Inserted values containing braces are
// literal bytes, never templates, regardless of parameter ordering/repetition.
std::optional<std::string> interpolate(const std::string &text, const Json &params)
{
	std::string out;
	for (std::size_t i = 0; i < text.size();)
	{
		if (text[i] == '{' && i + 1 < text.size() && nameCharacter(text[i + 1], true))
		{
			auto end = i + 2;
			while (end < text.size() && nameCharacter(text[end], false)) ++end;
			if (end < text.size() && text[end] == '}')
			{
				const auto found = params.find(text.substr(i + 1, end - i - 1));
				if (found == params.end()) return std::nullopt;
				out += found->is_string() ? found->get<std::string>() : found->dump();
				i = end + 1;
				continue;
			}
		}
		out += text[i++];
	}
	return out;
}
} // namespace

std::string presentApiError(const ApiError &error, std::string_view locale)
{
	if (locale != "bg" && locale != "bg-BG" && locale != "th" && locale != "th-TH") return error.message;
	const auto &messages = catalog(locale).at("messages");
	const auto generic = [&]() { return messages.at(Generic).get<std::string>(); };
	if (error.invalidLocalization || !validParams(error.messageParams)) return generic();
	auto translated = messages.end();
	if (error.messageKey)
	{
		if (error.messageKey->empty()) return generic();
		translated = messages.find(*error.messageKey);
	}
	if (translated == messages.end())
	{
		if (catalog(locale).at("ambiguousLiterals").contains(error.message)) return generic();
		// Legacy servers/transport errors may supply just the exact English
		// source literal. Never reverse parameter substitution or fuzzy-match.
		translated = messages.find(error.message);
		if (catalog(locale).at("nonliteralKeys").contains(error.message)) translated = messages.end();
		if (translated == messages.end())
		{
			const auto &aliases = catalog(locale).at("literals");
			const auto alias = aliases.find(error.message);
			if (alias != aliases.end()) translated = messages.find(alias->get<std::string>());
		}
	}
	if (translated == messages.end()) return generic();
	const auto result = interpolate(translated->get<std::string>(), error.messageParams);
	return result ? *result : generic();
}

std::string presentOnlineProblem(const std::string &diagnostic, std::string_view locale)
{
	return presentApiError(ApiError{"", diagnostic, Json()}, locale);
}
} // namespace Online
