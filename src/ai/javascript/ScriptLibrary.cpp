// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptLibrary.h"
#include "scripting/javascript/ScriptCommand.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace Script
{
using Json = nlohmann::json;
namespace
{
constexpr const char *registry = "ais/registry.json";
}
Library::Library(Online::OnlineStorage &s) : storage(s)
{
	std::string bytes;
	if (storage.read(registry, bytes))
		decode(bytes);
}
std::string Library::encode() const
{
	Json root{{"version", 1}, {"next", next}, {"revision", revision}, {"entries", Json::array()}};
	for (const auto &e : entries_)
		root["entries"].push_back({{"id", e.id},
								   {"path", e.path},
								   {"linked", e.linked},
								   {"apiVersion", e.metadata.apiVersion},
								   {"name", e.metadata.name},
								   {"description", e.metadata.description},
								   {"version", e.metadata.version},
								   {"author", e.metadata.author}});
	return root.dump(2);
}
void Library::decode(const std::string &bytes)
{
	if (bytes.size() > 1024 * 1024)
		throw std::runtime_error("Custom AI registry exceeds limit");
	auto root = Json::parse(bytes);
	if (root.at("version") != 1 || !root.at("entries").is_array() || root["entries"].size() > 256)
		throw std::runtime_error("Unsupported custom AI registry");
	std::vector<LibraryEntry> entries;
	for (const auto &v : root["entries"])
	{
		LibraryEntry e{v.at("id"), v.at("path"), v.at("linked"), {}};
		e.metadata = {v.at("apiVersion"), v.at("name"), v.at("description"), v.at("version"),
					  v.at("author")};
		if (e.id.empty() || e.id.find_first_not_of("0123456789") != std::string::npos ||
			std::any_of(entries.begin(), entries.end(),
						[&](const auto &old) { return old.id == e.id; }))
			throw std::runtime_error("Invalid custom AI identity");
		if (!e.linked && (e.path.rfind("ais/", 0) != 0 || e.path.find("..") != std::string::npos ||
						  e.path.find('\\') != std::string::npos))
			throw std::runtime_error("Invalid managed AI path");
		entries.push_back(std::move(e));
	}
	next = root.at("next").get<unsigned>();
	revision = root.at("revision").get<unsigned>();
	if (!next || !revision)
		throw std::runtime_error("Invalid custom AI registry counters");
	entries_ = std::move(entries);
}
const LibraryEntry &Library::get(const std::string &id) const
{
	for (const auto &e : entries_)
		if (e.id == id)
			return e;
	throw std::runtime_error("Custom AI is no longer installed; choose another AI in game setup");
}
std::string Library::put(const std::string &text, const std::string &filename,
						 const std::string &replace, const std::string &externalPath)
{
	auto metadata = inspectAI(text);
	if (metadata.name.empty())
	{
		const auto label = std::filesystem::u8path(filename).stem().u8string();
		metadata.name.assign(label.begin(), label.end());
	}
	if (!replace.empty())
		get(replace);
	if (replace.empty() && entries_.size() >= 256)
		throw std::runtime_error("Custom AI library is full");
	if (!externalPath.empty() && !std::filesystem::u8path(externalPath).is_absolute())
		throw std::runtime_error("Linked AI requires an absolute file path");
	const auto before = encode();
	const auto id = replace.empty() ? std::to_string(next++) : replace;
	if (!next || revision == 0xffffffffu)
	{
		decode(before);
		throw std::runtime_error("Custom AI registry exhausted");
	}
	LibraryEntry entry{id,
					   externalPath.empty() ? "ais/" + id + "-" + std::to_string(revision++) + ".js"
											: externalPath,
					   !externalPath.empty(), std::move(metadata)};
	if (!entry.linked && !storage.write(entry.path, text))
	{
		decode(before);
		throw std::runtime_error("Cannot write imported AI");
	}
	try
	{
		auto it = std::find_if(entries_.begin(), entries_.end(),
							   [&](const auto &e) { return e.id == id; });
		if (it == entries_.end())
			entries_.push_back(entry);
		else
			*it = entry;
		if (!storage.write(registry, encode()))
			throw std::runtime_error("Cannot save custom AI registry");
	}
	catch (...)
	{
		decode(before);
		if (!entry.linked)
			storage.remove(entry.path);
		throw;
	}
	return id;
}
void Library::remove(const std::string &id)
{
	get(id);
	const auto before = encode();
	std::erase_if(entries_, [&](const auto &e) { return e.id == id; });
	if (!storage.write(registry, encode()))
	{
		decode(before);
		throw std::runtime_error("Cannot save custom AI registry");
	}
	// Retain previous source revisions until persistence is acknowledged. They
	// are never considered installed merely because their files exist.
}
void Library::collectUnusedSources()
{
	for (const auto &name : storage.list("ais"))
	{
		if (name.size() < 4 || name.substr(name.size() - 3) != ".js" ||
			name.find_first_not_of("0123456789-.js") != std::string::npos)
			continue;
		const auto path = "ais/" + name;
		if (std::none_of(entries_.begin(), entries_.end(),
						 [&](const auto &e) { return !e.linked && e.path == path; }))
			storage.remove(path);
	}
}
std::string Library::source(const std::string &id) const
{
	const auto &e = get(id);
	if (e.linked)
		return readSource(e.path);
	std::string text;
	if (!storage.read(e.path, text))
		throw std::runtime_error("Imported AI source is missing: " + e.metadata.name);
	return text;
}
std::string Library::configuration(const std::string &id) const
{
	const auto text = source(id);
	return config(text, inspectAI(text).apiVersion);
}
void Library::rollback(const std::string &before)
{
	if (!storage.write(registry, before))
		throw std::runtime_error("Cannot restore previous custom AI registry");
	decode(before);
}
} // namespace Script
