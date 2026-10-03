// SPDX-License-Identifier: GPL-3.0-or-later
#include "RulesetCatalog.h"
#include "CustomGameSetup.h"
#include <FileManager.h>
#include <Stream.h>
#include <Toolkit.h>
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>

using CustomGameRules::Kind;
using CustomGameRules::Rule;
using Json = nlohmann::json;

int Ruleset::value(const Rule &rule, const CustomGameSetup &setup) const
{
	for (const auto &[candidate, value] : values)
		if (candidate == &rule)
			return value;
	return CustomGameRules::standardValue(rule, setup);
}

const Ruleset *RulesetCatalog::find(std::string_view id) const
{
	for (const auto &ruleset : rulesets)
		if (ruleset.id == id)
			return &ruleset;
	return nullptr;
}

RulesetCatalog RulesetCatalog::fallback()
{
	RulesetCatalog catalog;
	catalog.rulesets.push_back({"standard", "[Standard]", "[Classic colony building]", {}});
	return catalog;
}

namespace
{
// Starting workers are bounded per generator; this is the envelope every generator fits in
// (the preferences file uses the same bound). setRule() clamps to the chosen generator.
constexpr int kMinimumWorkers = 1, kMaximumWorkers = 8;

bool isTextKey(const Json &value)
{
	if (!value.is_string())
		return false;
	const auto &text = value.get_ref<const std::string &>();
	return text.size() > 2 && text.front() == '[' && text.back() == ']';
}

// A rule value as a ruleset writes it: true/false for a toggle, a number for the stepper,
// or one of the rule's option ids.
bool ruleValue(const Rule &rule, const Json &value, int &result)
{
	if (rule.kind == Kind::Stepper)
	{
		if (!value.is_number_integer())
			return false;
		result = value.get<int>();
		return result >= kMinimumWorkers && result <= kMaximumWorkers;
	}
	if (rule.kind == Kind::Toggle && value.is_boolean())
	{
		result = value.get<bool>();
		return true;
	}
	if (!value.is_string())
		return false;
	const auto &id = value.get_ref<const std::string &>();
	for (std::size_t i = 0; i < rule.optionIds.size(); ++i)
		if (id == rule.optionIds[i])
		{
			result = int(i);
			return true;
		}
	return false;
}

std::string optionList(const Rule &rule)
{
	if (rule.kind == Kind::Stepper)
		return std::to_string(kMinimumWorkers) + " to " + std::to_string(kMaximumWorkers);
	std::string list = rule.kind == Kind::Toggle ? "true, false" : "";
	for (const char *id : rule.optionIds)
		if (rule.kind != Kind::Toggle)
			list += (list.empty() ? "\"" : ", \"") + std::string(id) + "\"";
	return list;
}
} // namespace

RulesetCatalog RulesetCatalog::parse(std::string_view text, std::vector<std::string> &errors)
{
	Json root = Json::parse(text.begin(), text.end(), nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		errors.push_back("not a JSON object");
		return fallback();
	}
	if (root.value("version", 0) != 1)
		errors.push_back("unsupported version; expected \"version\": 1");
	const auto list = root.find("rulesets");
	if (list == root.end() || !list->is_array())
	{
		errors.push_back("missing \"rulesets\" array");
		return fallback();
	}
	RulesetCatalog catalog;
	std::set<std::string> ids;
	for (std::size_t index = 0; index < list->size(); ++index)
	{
		const Json &entry = (*list)[index];
		const std::string where = "ruleset " + std::to_string(index + 1);
		if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() ||
			entry["id"].get_ref<const std::string &>().empty())
		{
			errors.push_back(where + ": needs a non-empty string \"id\"");
			continue;
		}
		Ruleset ruleset;
		ruleset.id = entry["id"].get<std::string>();
		const std::string named = where + " (\"" + ruleset.id + "\")";
		if (!ids.insert(ruleset.id).second)
		{
			errors.push_back(named + ": duplicate id");
			continue;
		}
		if (!isTextKey(entry.value("name", Json())) || !isTextKey(entry.value("description", Json())))
		{
			errors.push_back(named + ": \"name\" and \"description\" must be text keys such as \"[Blitz]\"");
			continue;
		}
		ruleset.name = entry["name"].get<std::string>();
		ruleset.description = entry["description"].get<std::string>();
		bool valid = true;
		const Json rules = entry.value("rules", Json::object());
		if (!rules.is_object())
		{
			errors.push_back(named + ": \"rules\" must be an object");
			continue;
		}
		for (const auto &[key, value] : rules.items())
		{
			const Rule *rule = CustomGameRules::find(key);
			int parsed = 0;
			if (!rule)
			{
				errors.push_back(named + ": unknown rule \"" + key + "\"");
				valid = false;
			}
			else if (!ruleValue(*rule, value, parsed))
			{
				errors.push_back(named + ": \"" + key + "\" takes " + optionList(*rule) + ", not " + value.dump());
				valid = false;
			}
			else
				ruleset.values.emplace_back(rule, parsed);
		}
		if (valid)
			catalog.rulesets.push_back(std::move(ruleset));
	}
	const auto standard = std::find_if(catalog.rulesets.begin(), catalog.rulesets.end(),
									   [](const Ruleset &r) { return r.id == "standard"; });
	if (standard == catalog.rulesets.end())
	{
		errors.push_back("no valid \"standard\" ruleset");
		auto rest = std::move(catalog.rulesets);
		catalog = fallback();
		catalog.rulesets.insert(catalog.rulesets.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
		return catalog;
	}
	if (!standard->values.empty())
		errors.push_back("\"standard\" must not change any rule");
	standard->values.clear();
	// Standard leads the list: it is the baseline every other ruleset is described against.
	std::rotate(catalog.rulesets.begin(), standard, standard + 1);
	return catalog;
}

const RulesetCatalog &RulesetCatalog::shipped()
{
	static const RulesetCatalog catalog = []
	{
		std::vector<std::string> errors;
		RulesetCatalog loaded = fallback();
		GAGCore::InputLineStream input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(filename));
		if (input.isEndOfStream())
			errors.push_back("file not found");
		else
		{
			std::string text;
			while (!input.isEndOfStream())
				text += input.readLine() + "\n";
			loaded = parse(text, errors);
		}
		for (const auto &error : errors)
			std::cerr << filename << ": " << error << std::endl;
		return loaded;
	}();
	return catalog;
}
