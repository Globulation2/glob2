// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsScreen.h"
#include "ScriptLibrary.h"
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "PlatformClient.h"
#include "PlatformApi.h"
#include "SimVersion.h"
#include "Sha256.h"
#include "AiCatalog.h"
#include <nlohmann/json.hpp>
#include <set>

namespace
{
std::string customAIText(const std::string &text)
{
	return Glob2UI::tr("[" + text + "]");
}
} // namespace

namespace AH = GAGCore::ApplicationHost;
struct SettingsScreen::CustomAIState
{
	std::unique_ptr<Online::OnlineStorage> storage = Online::makeUserDirectoryStorage();
	Script::Library library{*storage};
	std::unique_ptr<AH::FileSelection> picker;
	std::unique_ptr<AH::Persistence> persistence;
	std::string replace, before, notice;
	std::map<std::string, std::string> errors;
	bool linked = false;
	std::unique_ptr<Online::PlatformScope> calls;
	nlohmann::json catalogue = nlohmann::json::array(), detail;
	std::map<std::string, nlohmann::json> installedDetails;
	std::set<std::string> socialPending;
	std::string origin, query, cursor, selectedId;
	std::vector<std::string> tags;
	int tab = 0, sort = 0, version = 0;
	bool loading = false, downloading = false, learn = true;
	unsigned generation = 0;
	Uint32 searchAt = 0;
};
void SettingsScreen::selectCustomAIFile(bool linked, const std::string &replace)
{
	if (!customAIs || customAIs->picker || customAIs->persistence)
		return;
	customAIs->replace = replace;
	customAIs->linked = linked;
	customAIs->picker = AH::selectFile("js");
	if (!customAIs->picker)
		customAIs->notice = customAIText("File selection is unavailable.");
	invalidate();
}
void SettingsScreen::pollCustomAIs()
{
	if (!customAIs)
		return;
	auto &s = *customAIs;
	if (s.searchAt && SDL_GetTicks() >= s.searchAt && !s.downloading)
	{
		s.searchAt = 0;
		fetchCustomAIs();
	}
	try
	{
		if (s.picker && s.picker->state() != AH::FileSelectionState::Pending)
		{
			auto picker = std::move(s.picker);
			if (picker->state() == AH::FileSelectionState::Selected)
			{
				auto file = picker->takeFile();
				if (s.linked && file.externalPath.empty())
					throw std::runtime_error("External links require a desktop file path");
				s.before = s.library.checkpoint();
				s.library.put(std::string(file.bytes.begin(), file.bytes.end()), file.name,
							  s.replace, s.linked ? file.externalPath : "");
				s.persistence = AH::persistStorage();
				s.notice = customAIText("Saving custom AI…");
			}
			else if (picker->state() == AH::FileSelectionState::Failed)
				s.notice = customAIText("Could not read the selected JavaScript file.");
			invalidate();
		}
		if (s.persistence && s.persistence->state() != AH::PersistenceState::Pending)
		{
			const bool ok = s.persistence->state() == AH::PersistenceState::Succeeded;
			s.persistence.reset();
			if (!ok)
				s.library.rollback(s.before);
			else
			{
				s.library.collectUnusedSources();
				s.errors.erase(s.replace);
			}
			s.notice =
				ok ? customAIText("Custom AI library saved.")
				   : customAIText("Storage could not be saved. The previous library is retained.");
			invalidate();
		}
	}
	catch (const std::exception &e)
	{
		s.notice = e.what();
		invalidate();
	}
}
void SettingsScreen::buildCustomAIs()
{
	try
	{
		if (!customAIs)
			customAIs = std::make_shared<CustomAIState>();
	}
	catch (const std::exception &e)
	{
		info(e.what());
		return;
	}
	auto &s = *customAIs;
	if (modal != Modal::AILibrary)
		button("ai.browse", customAIText("Open AI Library"), [this] { openCustomAILibrary(); });
	info(customAIText(
		"Import a single JavaScript AI file, then select it when setting up a local game."));
	info(customAIText(
		"Imports keep a copy in Glob2. Update replaces that copy for future games; saves keep "
		"their original AI."));
	if (!s.notice.empty())
		info(s.notice);
	if (s.picker || s.persistence)
	{
		info(customAIText("Waiting for file selection or storage…"));
		return;
	}
	button("ai.import", customAIText("Import JavaScript AI"),
		   [this] { selectCustomAIFile(false); });
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
	button("ai.link", customAIText("Link development file"), [this] { selectCustomAIFile(true); });
	info(customAIText(
		"Linked files are read at game start. Rebuild before starting a new game. Removing a "
		"link never deletes your file."));
#endif
	button(
		"ai.example", customAIText("Example AI and authoring guide"), []
		{ AH::openUrl("https://github.com/Globulation2/glob2-javascript-ai-starter-exampler"); });
	if (s.library.entries().empty())
		info(customAIText("No custom AIs yet. Import a bundled .js file to get started."));
	for (const auto &entry : s.library.entries())
	{
		const auto id = entry.id;
		add("", Kind::Section,
			entry.metadata.name + " · #" + entry.id +
				(entry.metadata.version.empty() ? "" : " · " + entry.metadata.version) +
				(entry.linked ? " · " + customAIText("Linked") : " · " + customAIText("Imported")));
		if (s.errors.contains(id))
			info(s.errors[id]);
		if (!entry.metadata.description.empty())
			info(entry.metadata.description);
		if (entry.linked)
			info(entry.path);
		if (entry.online && s.calls && entry.online->origin == s.origin)
		{
			const auto found = s.installedDetails.find(entry.online->aiId);
			if (found != s.installedDetails.end())
			{
				const auto &latest = found->second.at("ai").at("latestVersion");
				if (latest.at("id") != entry.online->versionId)
					info(customAIText("Update available") + " · " +
						 latest.at("label").get<std::string>());
			}
			button("ai.online." + id, customAIText("Browse versions"),
				   [this, aiId = entry.online->aiId]
				   {
					   customAIs->tab = 0;
					   selectOnlineAI(aiId);
				   });
		}
		const auto actionsStart = form.size();
		button("ai.update." + id, entry.linked ? customAIText("Relink") : customAIText("Update"),
			   [this, id, linked = entry.linked] { selectCustomAIFile(linked, id); });
		button("ai.validate." + id, customAIText("Validate"),
			   [this, id]
			   {
				   try
				   {
					   customAIs->library.configuration(id);
					   customAIs->errors.erase(id);
					   customAIs->notice =
						   customAIText("AI startup and persistent globals are valid.");
				   }
				   catch (const std::exception &e)
				   {
					   customAIs->notice = e.what();
					   customAIs->errors[id] = e.what();
				   }
				   invalidate();
			   });
		button("ai.remove." + id, customAIText("Remove from library"),
			   [this, id]
			   {
				   try
				   {
					   customAIs->before = customAIs->library.checkpoint();
					   customAIs->replace = id;
					   customAIs->library.remove(id);
					   customAIs->notice = customAIText("Saving custom AI…");
					   customAIs->persistence = AH::persistStorage();
				   }
				   catch (const std::exception &e)
				   {
					   customAIs->notice = e.what();
					   customAIs->errors[id] = e.what();
				   }
				   invalidate();
			   });
		// Keep related actions together; the shared settings builder wraps them on phones.
		for (size_t i = actionsStart; i < form.size(); ++i)
		{
			form[i].columns = 3;
			form[i].column = int(i - actionsStart);
		}
	}
}

bool SettingsScreen::customAIBusy() const
{
	return customAIs && (customAIs->picker || customAIs->persistence || customAIs->downloading);
}

namespace
{
using Json = nlohmann::json;
const std::vector<std::string> aiTags = {"Balanced",  "Rush",         "Defensive",        "Economy",
										 "Expansion", "Experimental", "Beginner-friendly"};
const std::vector<std::string> aiSorts = {"likes", "newest", "updated", "downloads"};
bool compatibleAI(const Json &version)
{
	return Online::compatibleAiVersion(version, Online::SimVersion::local().key());
}
} // namespace
void SettingsScreen::closeCustomAILibrary()
{
	if (customAIs)
	{
		customAIs->calls.reset();
		++customAIs->generation;
		customAIs->searchAt = 0;
		customAIs->socialPending.clear();
	}
}
void SettingsScreen::openCustomAILibrary()
{
	auto &s = *customAIs;
	auto &services = Online::services();
	if (services.client.origin().empty())
		services.client.start(services.config.selectedOrigin());
	s.origin = services.client.origin();
	s.calls = std::make_unique<Online::PlatformScope>(services.client);
	std::string dismissed;
	s.learn = !s.storage->read("ais/onboarding-dismissed", dismissed);
	modal = Modal::AILibrary;
	s.tab = 0;
	fetchCustomAIs();
	invalidate();
}
void SettingsScreen::fetchCustomAIs(bool more)
{
	if (!customAIs || !customAIs->calls)
		return;
	auto &s = *customAIs;
	if (s.downloading)
		return;
	s.calls->cancelAll();
	s.socialPending.clear();
	const auto generation = ++s.generation;
	if (s.tab == 2)
	{
		s.loading = false;
		for (const auto &entry : s.library.entries())
			if (entry.online && entry.online->origin == s.origin)
			{
				const auto id = entry.online->aiId;
				s.calls->rest(HttpFetch::Method::Get, "/api/v1/ais/" + Online::urlEncode(id),
							  Json(),
							  [this, id, generation](const auto &r)
							  {
								  auto &s = *customAIs;
								  if (generation != s.generation)
									  return;
								  if (r.ok && Online::validAiDetail(r.result))
									  s.installedDetails[id] = r.result;
								  invalidate();
							  });
			}
		invalidate();
		return;
	}
	s.loading = true;
	s.notice.clear();
	if (!more)
	{
		s.catalogue = Json::array();
		s.cursor.clear();
		s.detail = Json();
		s.selectedId.clear();
	}
	std::string path =
		"/api/v1/ais?limit=24&sort=" + aiSorts[s.sort] + "&q=" + Online::urlEncode(s.query);
	if (s.tab == 1)
		path += "&favourites=true";
	if (more)
		path += "&cursor=" + Online::urlEncode(s.cursor);
	std::string tags;
	for (const auto &tag : s.tags)
		tags += (tags.empty() ? "" : ",") + tag;
	path += "&tags=" + Online::urlEncode(tags);
	s.calls->rest(HttpFetch::Method::Get, path, Json(),
				  [this, generation](const auto &r)
				  {
					  auto &s = *customAIs;
					  if (generation != s.generation)
						  return;
					  s.loading = false;
					  try
					  {
						  if (!r.ok)
							  s.notice = r.error.message;
						  else
						  {
							  const auto &items = r.result.at("items");
							  if (!items.is_array() || items.size() > 100)
								  throw std::runtime_error("Invalid AI catalogue response");
							  for (const auto &item : items)
								  if (!Online::validAiSummary(item))
									  throw std::runtime_error("Invalid AI catalogue entry");
							  for (const auto &item : items)
								  s.catalogue.push_back(item);
							  s.cursor = r.result.value("nextCursor", "");
						  }
					  }
					  catch (const std::exception &e)
					  {
						  s.notice = e.what();
					  }
					  invalidate();
				  });
	invalidate();
}
void SettingsScreen::selectOnlineAI(const std::string &id)
{
	auto &s = *customAIs;
	if (s.downloading)
		return;
	s.selectedId = id;
	s.detail = Json();
	s.version = 0;
	s.calls->rest(
		HttpFetch::Method::Get, "/api/v1/ais/" + Online::urlEncode(id), Json(),
		[this, id](const auto &r)
		{
			auto &s = *customAIs;
			if (s.selectedId != id)
				return;
			if (r.ok && Online::validAiDetail(r.result))
			{
				s.detail = r.result;
				const auto &versions = s.detail.at("versions");
				std::string installedVersion;
				for (const auto &entry : s.library.entries())
					if (entry.online && entry.online->origin == s.origin &&
						entry.online->aiId == id)
						installedVersion = entry.online->versionId;
				auto selected = std::find_if(versions.begin(), versions.end(), [&](const auto &v)
											 { return v.at("id") == installedVersion; });
				if (selected == versions.end())
					selected = std::find_if(versions.begin(), versions.end(), compatibleAI);
				s.version = selected == versions.end() ? 0 : int(selected - versions.begin());
			}
			else
				s.notice = r.ok ? customAIText("Invalid AI catalogue response") : r.error.message;
			invalidate();
		});
	invalidate();
}
void SettingsScreen::socialOnlineAI(bool favourite)
{
	auto &s = *customAIs;
	if (!s.detail.is_object())
		return;
	const auto &ai = s.detail.at("ai");
	const std::string id = ai.at("id");
	const std::string field = favourite ? "favourited" : "liked";
	const std::string pendingKey = id;
	if (!s.socialPending.insert(pendingKey).second)
		return;
	const bool active = ai.at(field);
	s.calls->rest(
		active ? HttpFetch::Method::Delete : HttpFetch::Method::Put,
		"/api/v1/ais/" + Online::urlEncode(id) + (favourite ? "/favourite" : "/like"), Json(),
		[this, id, field, pendingKey](const auto &r)
		{
			auto &s = *customAIs;
			s.socialPending.erase(pendingKey);
			if (!r.ok)
				s.notice = r.error.message;
			else if (!r.result.is_object() || !r.result.contains("active") ||
					 !r.result["active"].is_boolean() || !Online::validAiCount(r.result, "likes"))
				s.notice = customAIText("Invalid AI catalogue response");
			else
			{
				// Social actions belong to the AI, not its release. Update cached
				// summaries without navigating or disturbing version selection.
				auto update = [&](Json &summary)
				{
					if (summary.at("id") == id)
					{
						summary[field] = r.result.at("active");
						summary["likes"] = r.result.at("likes");
					}
				};
				for (auto &summary : s.catalogue)
					update(summary);
				for (auto &[_, detail] : s.installedDetails)
					update(detail.at("ai"));
				if (s.detail.is_object())
					update(s.detail.at("ai"));
				if (s.tab == 1 && field == "favourited" && r.result.at("active") == false)
					std::erase_if(s.catalogue.get_ref<Json::array_t &>(),
								  [&](const auto &summary) { return summary.at("id") == id; });
			}
			invalidate();
		});
	invalidate();
}
void SettingsScreen::installOnlineAI()
{
	auto &s = *customAIs;
	if (customAIBusy() || !s.detail.is_object())
		return;
	const auto versions = s.detail.at("versions");
	if (s.version < 0 || s.version >= int(versions.size()))
		return;
	const auto version = versions[s.version];
	if (!compatibleAI(version))
		return;
	const auto ai = s.detail.at("ai");
	const std::string id = ai.at("id"), versionId = version.at("id"), hash = version.at("hash"),
					  name = ai.at("name");
	const auto origin = s.origin;
	s.downloading = true;
	s.notice = customAIText("Downloading AI…");
	s.calls->restRaw(
		HttpFetch::Method::Get,
		"/api/v1/ais/" + Online::urlEncode(id) + "/versions/" + Online::urlEncode(versionId) +
			"/file",
		"", "",
		[this, id, versionId, hash, name, origin](const auto &r)
		{
			auto &s = *customAIs;
			s.downloading = false;
			try
			{
				if (!r.ok)
					throw std::runtime_error(r.error.message);
				if (origin != s.calls->client().origin())
					throw std::runtime_error("The online server changed during download");
				std::string replace;
				for (const auto &entry : s.library.entries())
					if (entry.online && entry.online->origin == origin && entry.online->aiId == id)
					{
						replace = entry.id;
						break;
					}
				s.before = s.library.checkpoint();
				s.replace = replace;
				s.library.put(r.body, name + ".js", replace, "",
							  Script::LibraryOrigin{origin, id, versionId, hash});
				s.persistence = AH::persistStorage();
				s.notice = customAIText("Saving custom AI…");
			}
			catch (const std::exception &e)
			{
				s.notice = e.what();
			}
			invalidate();
		},
		Script::SourceLimit);
	invalidate();
}
Glob2UI::Element SettingsScreen::buildCustomAILibrary(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	auto &s = *customAIs;
	const bool busy = customAIBusy();
	std::vector<Element> top;
	Element desktopPanels;
	std::vector<Element> tabs;
	const std::vector<std::string> tabNames = {"Discover", "Favourites", "Installed"};
	for (int i = 0; i < 3; ++i)
		tabs.push_back(Glob2UI::button("ais/tab/" + std::to_string(i), customAIText(tabNames[i]),
									   [this, i]
									   {
										   customAIs->tab = i;
										   customAIs->searchAt = 0;
										   fetchCustomAIs();
										   invalidate();
									   },
									   {.selected = s.tab == i, .enabled = !busy}));
	top.push_back(wrap(std::move(tabs)));
	if (!s.notice.empty())
		top.push_back(paragraph(s.notice));
	if (s.learn)
	{
		top.push_back(
			paragraph(customAIText("Download an AI, then choose it for a computer seat in a local "
								   "game. Installed versions work offline. Updates affect future "
								   "games; saves keep their original code.")));
		top.push_back(
			paragraph(customAIText("Compatibility checks do not certify safety or playing "
								   "strength. Only run code from authors you trust."),
					  {FontRole::Support, true}));
		top.push_back(wrap(
			{Glob2UI::button("ais/guide", customAIText("Authoring guide"),
							 []
							 {
								 AH::openUrl("https://github.com/Globulation2/glob2/blob/master/"
											 "docs/scripting/javascript.md");
							 }),
			 Glob2UI::button("ais/api", customAIText("JavaScript API reference"),
							 []
							 {
								 AH::openUrl("https://github.com/Globulation2/glob2/blob/master/"
											 "docs/scripting/javascript-api.md");
							 }),
			 Glob2UI::button(
				 "ais/starter", customAIText("Starter project"),
				 []
				 {
					 AH::openUrl(
						 "https://github.com/Globulation2/glob2-javascript-ai-starter-exampler");
				 }),
			 Glob2UI::button("ais/gotit", customAIText("Got it"),
							 [this]
							 {
								 customAIs->learn = false;
								 customAIs->storage->write("ais/onboarding-dismissed", "1");
								 customAIs->storage->persist();
								 invalidate();
							 })}));
	}
	if (s.tab == 2)
	{
		form.clear();
		buildCustomAIs();
		for (const auto &r : form)
			top.push_back(rowElement(r, p));
	}
	else
	{
		const bool sheet = (p.touch || p.compact()) && s.detail.is_object();
		if (!sheet)
		{
			top.push_back(row(
				{expanded(textField("ais/search", s.query,
									[this](const std::string &value)
									{
										customAIs->query = value;
										customAIs->searchAt = SDL_GetTicks() + 250;
									},
									{.maxLength = 128,
									 .placeholder = customAIText("Search"),
									 .submit = [this](const std::string &) { fetchCustomAIs(); },
									 .enabled = !busy})),
				 Glob2UI::button("ais/search-go", customAIText("Search"),
								 [this] { fetchCustomAIs(); }, {.enabled = !busy})}));
			std::vector<std::string> sorts;
			for (const auto &label :
				 {"Most liked", "Newest", "Recently updated", "Most downloaded"})
				sorts.push_back(customAIText(label));
			top.push_back(Glob2UI::choice("ais/sort", sorts, s.sort,
										  [this](int i)
										  {
											  customAIs->sort = i;
											  fetchCustomAIs();
										  }));
			std::vector<Element> tags;
			for (const auto &tag : aiTags)
			{
				const bool selected = std::find(s.tags.begin(), s.tags.end(), tag) != s.tags.end();
				tags.push_back(Glob2UI::button("ais/tag/" + tag, customAIText(tag),
											   [this, tag, selected]
											   {
												   auto &tags = customAIs->tags;
												   if (selected)
													   std::erase(tags, tag);
												   else if (tags.size() < 5)
													   tags.push_back(tag);
												   fetchCustomAIs();
											   },
											   {.selected = selected, .enabled = !busy}));
			}
			top.push_back(wrap(std::move(tags)));
			if (s.loading)
				top.push_back(paragraph(customAIText("Loading AI library…")));
		}
		std::vector<Element> cards;
		for (const auto &ai : s.catalogue)
		{
			const std::string id = ai.value("id", ""), name = ai.value("name", "");
			std::string tagText;
			for (const auto &tag : ai.at("tags"))
				tagText += (tagText.empty() ? "" : " · ") + customAIText(tag.get<std::string>());
			cards.push_back(card(column(
				{Glob2UI::button("ais/item/" + id, name, [this, id] { selectOnlineAI(id); },
								 {.selected = s.selectedId == id, .enabled = !busy}),
				 caption(ai.at("owner").at("displayName").get<std::string>()), caption(tagText),
				 paragraph(ai.value("description", ""), {FontRole::Support, true}),
				 caption("\xE2\x99\xA5 " + std::to_string(ai.value("likes", 0)) + " · " +
						 std::to_string(ai.value("downloads", 0)) + " " +
						 customAIText("downloads"))})));
		}
		if (cards.empty() && !s.loading)
			cards.push_back(paragraph(customAIText("No AIs match these filters.")));
		if (!s.cursor.empty())
			cards.push_back(Glob2UI::button("ais/more", customAIText("Show more"),
											[this] { fetchCustomAIs(true); },
											{.enabled = !busy && !s.loading}));
		std::vector<Element> details;
		if (s.detail.is_object())
		{
			const auto &ai = s.detail.at("ai");
			const auto &versions = s.detail.at("versions");
			details.push_back(heading(ai.value("name", "")));
			details.push_back(caption(ai.at("owner").at("displayName").get<std::string>()));
			std::string tagText;
			for (const auto &tag : ai.at("tags"))
				tagText += (tagText.empty() ? "" : " · ") + customAIText(tag.get<std::string>());
			details.push_back(caption(tagText));
			details.push_back(paragraph(ai.value("description", "")));
			std::vector<std::string> labels;
			for (const auto &v : versions)
			{
				std::string label =
					v.at("label").get<std::string>() + (compatibleAI(v) ? " · ✓" : " · ×");
				for (const auto &entry : s.library.entries())
					if (entry.online && entry.online->origin == s.origin &&
						entry.online->aiId == s.selectedId && v.at("id") == entry.online->versionId)
						label += " · " + customAIText("Installed");
				labels.push_back(std::move(label));
			}
			if (!labels.empty())
			{
				s.version = std::clamp(s.version, 0, int(labels.size()) - 1);
				details.push_back(Glob2UI::choice("ais/version", labels, s.version,
												  [this](int i)
												  {
													  if (!customAIBusy())
													  {
														  customAIs->version = i;
														  invalidate();
													  }
												  }));
				const auto &v = versions[s.version];
				const bool compatible = compatibleAI(v);
				std::string installLabel = "Download & install";
				for (const auto &e : s.library.entries())
					if (e.online && e.online->origin == s.origin && e.online->aiId == s.selectedId)
						installLabel = e.online->versionId == v.value("id", "")
										   ? "Installed"
										   : "Install selected version";
				details.push_back(paragraph(v.value("notes", "")));
				details.push_back(caption(std::to_string(v.value("downloads", 0)) + " " +
										  customAIText("downloads of this version")));
				details.push_back(
					paragraph(customAIText(compatible ? "Passed compatibility checks"
													  : "Not validated for this game version"),
							  {FontRole::Support, true}));
				details.push_back(Glob2UI::button(
					"ais/install", customAIText(installLabel), [this] { installOnlineAI(); },
					{.primary = true,
					 .enabled = compatible && !busy && installLabel != "Installed"}));
			}
			const auto &account = s.calls->client().account();
			const bool signedIn = account && account->kind == "registered";
			details.push_back(wrap(
				{Glob2UI::button("ais/like",
								 customAIText(ai.value("liked", false) ? "Liked" : "Like"),
								 [this] { socialOnlineAI(false); },
								 {.selected = ai.value("liked", false),
								  .enabled = signedIn && !busy &&
											 !s.socialPending.contains(s.selectedId)}),
				 Glob2UI::button(
					 "ais/favourite",
					 customAIText(ai.value("favourited", false) ? "Favourited" : "Favourite"),
					 [this] { socialOnlineAI(true); },
					 {.selected = ai.value("favourited", false),
					  .enabled = signedIn && !busy &&
								 !s.socialPending.contains(s.selectedId)})}));
			if (!signedIn)
				details.push_back(paragraph(
					customAIText("Sign in through Online settings to like and favourite AIs."),
					{FontRole::Support, true}));
			details.push_back(Glob2UI::button("ais/website", customAIText("Open AI page"),
											  [this]
											  {
												  AH::openUrl(
													  customAIs->origin + "/ais/" +
													  Online::urlEncode(customAIs->selectedId));
											  }));
		}
		if (p.touch || p.compact())
		{
			if (sheet)
			{
				top.push_back(Glob2UI::button("ais/results", customAIText("Back to results"),
											  [this]
											  {
												  host().focus("ais/item/" + customAIs->selectedId, false);
												  customAIs->detail = Json();
												  customAIs->selectedId.clear();
												  invalidate();
											  }));
				for (auto &e : details)
					top.push_back(e);
			}
			else
				for (auto &e : cards)
					top.push_back(e);
		}
		else
			desktopPanels =
				row({expanded(scroll("ais/results-list", column(std::move(cards), {p.pt(8)}))),
					 expanded(scroll("ais/details/" + s.selectedId,
									 column(std::move(details), {p.pt(8)})))},
					{p.pt(16)});
	}
	auto footer =
		actions({{"ais/learn", customAIText("How AIs work"),
				  [this]
				  {
					  customAIs->learn = !customAIs->learn;
					  invalidate();
				  },
				  false},
				 {"ais/share", customAIText("Share your AI"),
				  [this] { AH::openUrl(customAIs->origin + "/ais/new"); }, false},
				 {"ais/close", customAIText("Done"), [this] { dismiss(); }, true, SDLK_ESCAPE}},
				p);
	Element body;
	if (desktopPanels)
		body =
			column({constrained({0, 0, Constraints::Unbounded, std::min(p.pt(300), p.safe.h / 3)},
								scroll("ais/filters", column(std::move(top), {p.pt(10)}))),
					expanded(std::move(desktopPanels))},
				   {p.pt(10)});
	else
		body = scroll(s.tab != 2 && s.detail.is_object() ? "ais/sheet/" + s.selectedId
														 : "ais/body/" + std::to_string(s.tab),
					  column(std::move(top), {p.pt(10)}));
	if (p.touch)
		return page(customAIText("AI Library"), std::move(body), footer, p, 960);
	const int width = std::min(p.safe.w - p.pt(32), p.pt(1024)),
			  height = std::min(p.safe.h - p.pt(32), p.pt(760));
	return center(sized({width, height}, card(column({pageTitle(customAIText("AI Library")),
													  expanded(std::move(body)), footer},
													 {p.pt(16)}))));
}
