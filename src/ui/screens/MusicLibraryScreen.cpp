// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicUI.h"
#include "MusicLibraryScreen.h"
#include "MusicSetScreen.h"
#include "MusicImportScreen.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include "Settings.h"
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "PlatformClient.h"
#include "PlatformApi.h"
#include "Sha256.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <ScreenStack.h>
#include <chrono>
using namespace Glob2UI;
using Json = nlohmann::json;
namespace AH = GAGCore::ApplicationHost;
namespace
{
HttpFetch::Request requestFor(const std::string &origin, const std::string &path)
{
	HttpFetch::Request request;
	request.url = origin + path;
	auto &client = Online::services().client;
	if (client.origin() == origin && !client.accessToken().empty())
		request.headers.push_back({"Authorization", "Bearer " + client.accessToken()});
	return request;
}
bool onlineEnabled()
{
#if defined(GLOB2_CHINA_RELEASE) || defined(GLOB2_AMAZON_RELEASE)
	return false;
#else
	return true;
#endif
}
} // namespace
MusicLibraryScreen::MusicLibraryScreen(GAGGUI::ScreenStack &stack)
	: screens(stack),
	  library(std::filesystem::u8path(GAGCore::Toolkit::getFileManager()->getDir(0)))
{
	if (onlineEnabled())
		origin = Online::services().config.selectedOrigin();
	else
		tab = 1;
	previewRoot =
		std::filesystem::u8path(GAGCore::Toolkit::getFileManager()->getDir(0)) / "music-preview";
	refreshInstalled();
}
MusicLibraryScreen::~MusicLibraryScreen()
{
	std::error_code error;
	std::filesystem::remove_all(previewRoot, error);
}
void MusicLibraryScreen::refreshInstalled()
{
	try
	{
		installed = library.list();
	}
	catch (const std::exception &e)
	{
		notice = e.what();
	}
	invalidate();
}
void MusicLibraryScreen::reload(bool more)
{
	if (!onlineEnabled())
		return;
	// A newer query replaces a pending catalogue request; downloads and saving
	// must finish before applying filter changes.
	if (fetch && kind == FetchKind::Catalog && !more)
	{
		fetch->cancel();
		fetch.reset();
	}
	if (fetch || job || persistence || persistenceFailed)
	{
		pendingReload = pendingReload || !more;
		return;
	}
	pendingReload = false;
	requested = true;
	append = more;
	kind = FetchKind::Catalog;
	const char *sorts[] = {"likes", "recent", "downloads"};
	const char *licences[] = {"", "CC0-1.0", "CC-BY-4.0", "CC-BY-SA-4.0"};
	std::string path = "/api/v1/music?q=" + Online::urlEncode(search) + "&sort=" + sorts[sort] +
					   "&license=" + licences[license] + "&tag=" + Online::urlEncode(tag);
	if (ai)
		path += "&ai=" + std::string(ai == 1 ? "false" : "true");
	if (duration == 1)
		path += "&max=120";
	if (duration == 2)
		path += "&min=120&max=300";
	if (duration == 3)
		path += "&min=300";
	if (more)
		path = catalogPath + "&cursor=" + Online::urlEncode(next);
	else
	{
		catalogPath = path;
		next.clear();
		items = Json::array();
	}
	fetch = HttpFetch::start(requestFor(origin, path));
	notice = musicText("Loading music…");
	invalidate();
}
void MusicLibraryScreen::action(const std::string &id, const std::string &suffix,
								HttpFetch::Method method, Json body)
{
	if (fetch)
		return;
	auto request = requestFor(origin, "/api/v1/music/" + Online::urlEncode(id) + suffix);
	request.method = method;
	if (!body.is_null())
	{
		request.body = body.dump();
		request.headers.push_back({"Content-Type", "application/json"});
	}
	kind = FetchKind::Action;
	fetch = HttpFetch::start(std::move(request));
}
void MusicLibraryScreen::startDownload(Json release, bool preview)
{
	if (fetch || job || persistence || persistenceFailed)
		return;
	active = std::move(release);
	previewOnly = preview;
	track = 0;
	for (auto &bytes : tracks)
		bytes.clear();
	downloadTrack();
}
void MusicLibraryScreen::downloadTrack()
{
	auto request = requestFor(
		origin, "/api/v1/music/" + Online::urlEncode(active.at("id").get<std::string>()) +
					"/tracks/" + std::array<std::string, 3>{"calm", "building", "combat"}[track]);
	request.timeout = std::chrono::minutes(3);
	request.responseLimit = 16 * 1024 * 1024;
	kind = FetchKind::Track;
	fetch = HttpFetch::start(std::move(request));
	notice = musicText("Downloading music…");
	invalidate();
}
void MusicLibraryScreen::selectInstalled(const Music::Installed &entry)
{
	if (globalContainer->mix && globalContainer->mix->selectMusicSet(entry.directory))
	{
		globalContainer->settings.musicSet = entry.directory;
		globalContainer->settings.save();
		persistence = AH::persistStorage();
	}
}
void MusicLibraryScreen::onTimer(Uint32)
{
	if (job)
	{
		job->advance();
		if (job->finished())
		{
			if (!job->error().empty())
			{
				notice = job->error();
				queue.clear();
			}
			else if (previewOnly)
			{
				auto entry = job->installed().front();
				screens.push(std::make_unique<MusicSetScreen>(entry.info,
															  temporary->paths(entry.directory),
															  musicText("Install")),
							 [this, release = active](GAGGUI::Screen &, int result)
							 {
								 temporary.reset();
								 std::error_code error;
								 std::filesystem::remove_all(previewRoot, error);
								 if (result == 1)
									 startDownload(release, false);
							 });
				notice = "";
			}
			else
			{
				persistence = AH::persistStorage();
				notice = musicText("Saving music…");
			}
			job.reset();
			invalidate();
		}
	}

	if ((!requested && tab == 0) || pendingReload)
		reload();
	if (persistence && persistence->state() != AH::PersistenceState::Pending)
	{
		persistenceFailed = persistence->state() == AH::PersistenceState::Failed;
		notice = musicText(persistenceFailed
							   ? "Music is not yet saved permanently. Retry saving before leaving."
							   : "Music installed.");
		persistence.reset();
		refreshInstalled();
		if (!persistenceFailed && !queue.empty())
		{
			auto release = queue.front();
			queue.pop_front();
			startDownload(release, false);
		}
	}
	if (!fetch)
		return;
	auto state = fetch->state();
	if (state == HttpFetch::State::Pending)
		return;
	HttpFetch::Response response;
	if (state == HttpFetch::State::Done)
		response = fetch->response();
	auto error = fetch->error();
	fetch.reset();
	try
	{
		if (state != HttpFetch::State::Done)
			throw std::runtime_error(error.empty() ? musicText("Music download failed.") : error);
		if (response.status < 200 || response.status >= 300)
		{
			auto json = Json::parse(response.body, nullptr, false);
			throw std::runtime_error(json.is_object()
										 ? json.value("message", musicText("Music request failed."))
										 : musicText("Music request failed."));
		}
		if (kind == FetchKind::Catalog)
		{
			auto json = Json::parse(response.body);
			if (!json.at("items").is_array())
				throw std::runtime_error("Invalid music catalogue.");
			if (!append)
				items = Json::array();
			for (const auto &item : json.at("items"))
				items.push_back(item);
			next = json.at("next").is_string() ? json.at("next").get<std::string>() : "";
			notice = items.empty() ? musicText("No music found.") : "";
		}
		else if (kind == FetchKind::Action)
		{
			notice = musicText("Saved.");
			reload();
		}
		else
		{
			auto wanted = active.at("tracks").at(track).at("sha256").get<std::string>();
			if (Online::Sha256::hex(response.body) != wanted)
				throw std::runtime_error("Music download checksum failed.");
			tracks[track].assign(response.body.begin(), response.body.end());
			if (++track < 3)
			{
				downloadTrack();
				return;
			}
			if (previewOnly)
			{
				temporary = std::make_unique<Music::Library>(previewRoot);
				job = std::make_unique<Music::ImportJob>(*temporary, std::move(tracks));
			}
			else
				job = std::make_unique<Music::ImportJob>(library, tracks);
			notice = musicText("Validating music…");
		}
	}
	catch (const std::exception &e)
	{
		notice = e.what();
		queue.clear();
	}
	invalidate();
}
Element MusicLibraryScreen::build(const Presentation &p)
{
	std::vector<Element> body;
	const bool idle = !fetch && !job && !persistence && !persistenceFailed;
	auto tabs = segments("music.tabs", {musicText("Browse"), musicText("Installed")}, tab,
						 [this](int v)
						 {
							 tab = v;
							 refreshInstalled();
							 invalidate();
						 });
	auto importButton =
		button("music.import", musicText("Import ZIP or three tracks"),
			   [this]
			   {
				   screens.push(std::make_unique<MusicImportScreen>(),
								[this](GAGGUI::Screen &, int) { refreshInstalled(); });
			   },
			   {.enabled = idle});
	if (tab == 0 && onlineEnabled())
	{
		auto searchField = textField(
			"music.search", search, [this](const std::string &value) { search = value; },
			{.maxLength = 200, .placeholder = musicText("Search title, artist, description…")});
		auto tagField =
			textField("music.tag", tag, [this](const std::string &value) { tag = value; },
					  {.maxLength = 32, .placeholder = musicText("Tag")});
		auto sortChoice = choice(
			"music.sort",
			{musicText("Most liked"), musicText("Newest"), musicText("Most downloaded")}, sort,
			[this](int v)
			{
				sort = v;
				reload();
			});
		auto licenseChoice = choice(
			"music.license",
			{musicText("All open licences"), "CC0-1.0", "CC-BY-4.0", "CC-BY-SA-4.0"}, license,
			[this](int v)
			{
				license = v;
				reload();
			});
		auto aiChoice = choice(
			"music.ai",
			{musicText("All music"), musicText("Not AI-generated"), musicText("AI-generated")}, ai,
			[this](int v)
			{
				ai = v;
				reload();
			});
		auto durationChoice = choice("music.duration",
									 {musicText("Any duration"), musicText("Up to 2 minutes"),
									  musicText("2–5 minutes"), musicText("5–15 minutes")},
									 duration,
									 [this](int v)
									 {
										 duration = v;
										 reload();
									 });
		auto searchButton = button("music.find", musicText("Search"), [this] { reload(); });

		// Keep catalogue results visible without scrolling past a tall filter form.
		const bool wide = p.safe.w > p.pt(700);
		if (wide)
		{
			body.push_back(row({expanded(tabs), importButton}, {p.pt(8)}));
			body.push_back(
				row({expanded(searchField), expanded(sortChoice), searchButton}, {p.pt(8)}));
			body.push_back(row({expanded(tagField), expanded(licenseChoice), expanded(aiChoice),
								expanded(durationChoice)},
							   {p.pt(8)}));
		}
		else
		{
			body.push_back(tabs);
			body.push_back(importButton);
			body.push_back(row({expanded(searchField), searchButton}, {p.pt(8)}));
			body.push_back(row({expanded(sortChoice), button("music.filters", musicText("Filters"),
															 [this]
															 {
																 filters = !filters;
																 invalidate();
															 })},
							   {p.pt(8)}));
			if (filters)
				body.insert(body.end(), {tagField, licenseChoice, aiChoice, durationChoice});
		}
		for (const auto &release : items)
		{
			try
			{
				auto id = release.at("id").get<std::string>();
				const auto &meta = release.at("metadata");
				bool exists =
					std::any_of(installed.begin(), installed.end(), [&](const auto &entry)
								{ return entry.info.id == id && entry.info.origin == origin; });
				std::vector<Element> cardBody{
					musicPlaceholder(p.pt(96)), heading(meta.at("title").get<std::string>()),
					caption(meta.at("artist").get<std::string>() + " · " +
								std::to_string(release.value("frames", 0) / 48000) + " s",
							false),
					paragraph(meta.at("description").get<std::string>())};
				if (release.contains("coverUrl") && release["coverUrl"].is_string())
				{
					auto &client = Online::services().client;
					// Construct a same-instance URL; never follow a catalogue-supplied host.
					if (client.origin() == origin)
						if (auto *cover = covers.get(&client, "/api/v1/music/" + id + "/cover",
													 [this] { invalidate(); }))
							cardBody.front() = previewPicture(cover, p.pt(96));
				}
				cardBody.push_back(
					row({button("music.preview." + id, musicText("Preview"), [this, release]
								{ startDownload(release, true); }, {.enabled = idle}),
						 button("music.install." + id, musicText(exists ? "Installed" : "Install"),
								[this, release] { startDownload(release, false); },
								{.enabled = !exists && idle})},
						{p.pt(8)}));
				cardBody.push_back(toggle("music.select." + id,
										  musicText("Select for installation"),
										  selected.contains(id),
										  [this, id, release](bool value)
										  {
											  if (value)
												  selected[id] = release;
											  else
												  selected.erase(id);
											  invalidate();
										  }));
				cardBody.push_back(
					button("music.like." + id, "♥ " + std::to_string(release.value("likes", 0)),
						   [this, id, release]
						   {
							   action(id, "/like",
									  release.value("liked", false) ? HttpFetch::Method::Delete
																	: HttpFetch::Method::Put);
						   },
						   {.enabled = !fetch}));
				cardBody.push_back(button("music.web." + id,
										  musicText("Credits, downloads and reporting"),
										  [this, id] { AH::openUrl(origin + "/music/" + id); }));
				auto art = cardBody.front();
				cardBody.erase(cardBody.begin());
				body.push_back(card(row({art, expanded(column(std::move(cardBody), {p.pt(8)}))},
										{p.pt(12), CrossAlign::Start})));
			}
			catch (const std::exception &)
			{
				notice = musicText("Some catalogue entries could not be read.");
			}
		}
		if (!next.empty())
			body.push_back(button("music.more", musicText("Load more"), [this] { reload(true); },
								  {.enabled = !fetch}));
		if (!selected.empty())
			body.push_back(
				button("music.bulk",
					   musicText("Install selected") + " (" + std::to_string(selected.size()) + ")",
					   [this]
					   {
						   for (const auto &[id, item] : selected)
							   queue.push_back(item);
						   selected.clear();
						   if (!queue.empty())
						   {
							   auto item = queue.front();
							   queue.pop_front();
							   startDownload(item, false);
						   }
					   },
					   {.enabled = idle}));
		body.push_back(button("music.create", musicText("Share music on the web"),
							  [this] { AH::openUrl(origin + "/music/new"); }));
	}
	else
	{
		body.insert(body.end(), {tabs, importButton});
		for (const auto &entry : installed)
			body.push_back(card(column(
				{heading(entry.info.title), paragraph(entry.info.artist),
				 row({button("music.use." + entry.directory, musicText("Use this music"),
							 [this, entry] { selectInstalled(entry); }, {.enabled = idle}),
					  button("music.play." + entry.directory, musicText("Preview"),
							 [this, entry]
							 {
								 screens.push(std::make_unique<MusicSetScreen>(
												  entry.info, library.paths(entry.directory),
												  musicText("Use this music")),
											  [this, entry](GAGGUI::Screen &, int result)
											  {
												  if (result == 1)
													  selectInstalled(entry);
											  });
							 },
							 {.enabled = idle}),
					  button("music.remove." + entry.directory, musicText("Remove"),
							 [this, entry]
							 {
								 try
								 {
									 if (globalContainer->mix &&
										 globalContainer->mix->getMusicSet() == entry.directory)
									 {
										 globalContainer->mix->selectMusicSet("original");
										 globalContainer->settings.musicSet = "original";
										 globalContainer->settings.save();
									 }
									 library.remove(entry.directory);
									 persistence = AH::persistStorage();
									 refreshInstalled();
								 }
								 catch (const std::exception &e)
								 {
									 notice = e.what();
								 }
							 },
							 {.enabled = idle})},
					 {p.pt(8)})},
				{p.pt(8)})));
		if (installed.empty())
			body.push_back(paragraph(musicText(
				"No community music installed yet. Import files or browse the online library.")));
	}
	if (persistenceFailed)
	{
		body.push_back(button("music.leave", musicText("Leave without saving"),
							  [this]
							  {
								  persistenceFailed = false;
								  onEscape();
							  }));
		for (int i = 0; i < 3; ++i)
			if (!tracks[i].empty())
				body.push_back(button(
					"music.export." + std::to_string(i),
					musicText("Export recovery files") + " · a" + std::to_string(i + 1) + ".opus",
					[this, i]
					{ AH::exportFile("a" + std::to_string(i + 1) + ".opus", tracks[i]); }));
		body.push_back(button("music.retry", musicText("Retry saving"),
							  [this] { persistence = AH::persistStorage(); }));
	}
	if (!notice.empty())
		body.push_back(paragraph(notice));
	OnlinePanel panel;
	panel.title = musicText("Music library");
	panel.body = scroll("music.library", column(std::move(body), {p.pt(12)}));
	panel.actions = {{"back", musicText("Back"),
					  [this]
					  {
						  if (!persistence && !persistenceFailed)
							  endExecute(0);
					  },
					  false, SDLK_ESCAPE}};
	return onlinePanel(panel, p);
}
