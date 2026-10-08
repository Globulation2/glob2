// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineGeneratorsScreen.h"
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "OnlineHandoff.h"
#include "Sha256.h"
#include "PlatformApi.h"
#include "SimVersion.h"
#include "GeneratorControls.h"
#include "ui/OnlineUI.h"
#include <ScreenStack.h>
using namespace Glob2UI;
using Json = nlohmann::json;
namespace JS = MapGeneration::JavaScript;
namespace AH = GAGCore::ApplicationHost;
namespace
{
bool compatible(const Json &v)
{
	if (!v.is_object() || !v.contains("validations"))
		return false;
	for (const auto &r : v["validations"])
		if (r.value("valid", false) && r.value("suite", 0) == 1 &&
			r.value("simVersion", "") == Online::SimVersion::local().key())
			return true;
	return false;
}
} // namespace
OnlineGeneratorsScreen::OnlineGeneratorsScreen(GAGGUI::ScreenStack &s)
	: screens(s), calls(Online::services().client), storage(Online::makeUserDirectoryStorage()),
	  library(*storage)
{
	auto &client = calls.client();
	if (client.origin().empty())
		client.start(Online::services().config.selectedOrigin());
	origin = client.origin();
	fetch();
}
void OnlineGeneratorsScreen::onEscape()
{
	if (!downloading && !persistence)
		endExecute(1);
}
void OnlineGeneratorsScreen::fetch(bool more)
{
	if (downloading || persistence)
		return;
	calls.cancelAll();
	loading = true;
	if (!more)
	{
		catalogue = Json::array();
		detail = Json();
		cursor.clear();
		replacing.clear();
	}
	const char *sorts[] = {"newest", "updated", "likes", "downloads"};
	std::string path = "/api/v1/generators?limit=24&q=" + Online::urlEncode(query) +
					   "&tags=" + Online::urlEncode(tags) + "&sort=" + sorts[sort];
	if (mine)
		path += "&owner=me";
	if (compatibleOnly)
		path += "&simVersion=" + Online::urlEncode(Online::SimVersion::local().key());
	if (filter)
		path += "&editorOnly=" + std::string(filter == 1 ? "false" : "true");
	if (more)
		path += "&cursor=" + Online::urlEncode(cursor);
	calls.rest(HttpFetch::Method::Get, path, Json(),
			   [this](const auto &r)
			   {
				   loading = false;
				   try
				   {
					   if (!r.ok)
						   throw std::runtime_error(r.error.message);
					   for (const auto &v : r.result.at("items"))
						   catalogue.push_back(v);
					   cursor = r.result.value("nextCursor", "");
					   status.clear();
				   }
				   catch (const std::exception &e)
				   {
					   status = e.what();
				   }
				   invalidate();
			   });
	invalidate();
}
void OnlineGeneratorsScreen::select(const std::string &id)
{
	if (downloading || persistence)
		return;
	calls.cancelAll();
	loading = true;
	detail = Json();
	replacing.clear();
	calls.rest(HttpFetch::Method::Get, "/api/v1/generators/" + Online::urlEncode(id), Json(),
			   [this](const auto &r)
			   {
				   loading = false;
				   try
				   {
					   if (!r.ok)
						   throw std::runtime_error(r.error.message);
					   detail = r.result;
					   selectRelease(0);
				   }
				   catch (const std::exception &e)
				   {
					   status = e.what();
					   detail = Json();
				   }
				   invalidate();
			   });
	invalidate();
}
void OnlineGeneratorsScreen::selectRelease(int index)
{
	version = index;
	const auto &release = detail.at("versions").at(index);
	settings = release.at("example");
	auto &params = settings.at("params");
	for (const auto &control : sharedGeneratorControls())
		if (!params.contains(control.id))
			params[control.id] = control.defaultValue;
	for (const auto &control : release.at("metadata").at("controls"))
	{
		const std::string id = control.at("id");
		if (!params.contains(id))
			params[id] = control.at("default");
	}
	replacing.clear();
}
void OnlineGeneratorsScreen::install()
{
	if (downloading || persistence || !detail.is_object())
		return;
	const auto release = detail.at("versions").at(version);
	if (!compatible(release))
		return;
	const std::string packageId = release.at("metadata").at("id"),
					  id = detail.at("generator").at("id"), versionId = release.at("id"),
					  fileHash = release.at("hash"), packageHash = release.at("packageHash");
	if (library.entries().contains(packageId) && replacing != packageId)
	{
		replacing = packageId;
		status = "This package ID is installed. Confirm replacement to use the selected release.";
		invalidate();
		return;
	}
	downloading = true;
	status = "Downloading generator…";
	calls.restRaw(
		HttpFetch::Method::Get,
		"/api/v1/generators/" + Online::urlEncode(id) + "/versions/" +
			Online::urlEncode(versionId) + "/file",
		"", "",
		[this, packageId, id, versionId, fileHash, packageHash](const auto &r)
		{
			downloading = false;
			try
			{
				if (!r.ok)
					throw std::runtime_error(r.error.message);
				if (calls.client().origin() != origin)
					throw std::runtime_error("Online server changed during download");
				if (Online::Sha256::hex(r.body) != fileHash)
					throw std::runtime_error("Generator file hash mismatch");
				const auto package = JS::Package::parse(r.body);
				if (package->id != packageId || package->hash != packageHash)
					throw std::runtime_error("Generator package identity mismatch");
				before = library.checkpoint();
				JS::LibraryOrigin source{origin, id, versionId, fileHash, packageHash};
				library.put(r.body, library.entries().contains(packageId) ? packageId : "",
							&source);
				persistence = AH::persistStorage();
				if (!persistence)
					throw std::runtime_error("Storage persistence unavailable");
				status = "Saving generator library…";
			}
			catch (const std::exception &e)
			{
				failInstallation(e.what());
			}
			invalidate();
		},
		4 * 1024 * 1024);
	invalidate();
}
void OnlineGeneratorsScreen::failInstallation(const std::string &message)
{
	status = message;
	if (!before.empty())
	{
		try
		{
			library.rollback(before);
			status += " The previous library is retained.";
		}
		catch (const std::exception &error)
		{
			status += " Could not restore the previous library: ";
			status += error.what();
		}
	}
	persistence.reset();
	before.clear();
	replacing.clear();
}
void OnlineGeneratorsScreen::onTimer(Uint32)
{
	if (!persistence || persistence->state() == AH::PersistenceState::Pending)
		return;
	if (persistence->state() == AH::PersistenceState::Succeeded)
	{
		library.publish();
		status = "Generator installed. Future selections use this release.";
	}
	else
	{
		failInstallation("Storage failed.");
	}
	persistence.reset();
	before.clear();
	replacing.clear();
	invalidate();
}
void OnlineGeneratorsScreen::useInRoom()
{
	if (!detail.is_object() || downloading || persistence)
		return;
	const auto &release = detail.at("versions").at(version);
	if (!compatible(release) || release.at("metadata").value("editorOnly", false))
		return;
	auto descriptor = settings;
	descriptor["libraryId"] = detail.at("generator").at("id");
	descriptor["versionId"] = release.at("id");
	descriptor["fileHash"] = release.at("hash");
	descriptor["packageHash"] = release.at("packageHash");
	descriptor["generatorId"] = release.at("metadata").at("id");
	descriptor["revision"] = release.at("metadata").at("revision");
	Online::RoomMapChoice choice;
	choice.title = detail.at("generator").at("name");
	choice.scriptDescriptor = descriptor.dump();
	choice.teamCount = descriptor.at("params").at("teams").get<int>();
	Online::useMapInRoom(choice);
	endExecute(2);
}
Element OnlineGeneratorsScreen::build(const Presentation &p)
{
	std::vector<Element> rows;
	const bool busy = loading || downloading || bool(persistence);
	rows.push_back(row(
		{button("maps", "Maps", [this] { onEscape(); }), button("generators", "Generators", [] {})},
		{p.pt(8)}));
	TextFieldOptions search;
	search.submit = [this](const std::string &) { fetch(); };
	rows.push_back(field("Search", textField(
									   "generators/search", query,
									   [this](const std::string &s) { query = s; }, search)));
	rows.push_back(field(
		"Tags",
		textField("generators/tags", tags, [this](const std::string &s) { tags = s; }, search)));
	rows.push_back(choice("generators/sort",
						  {"Newest", "Recently updated", "Most liked", "Most downloaded"}, sort,
						  [this](int i)
						  {
							  sort = i;
							  fetch();
						  }));
	rows.push_back(choice("generators/ownership", {"Public library", "My generators"}, mine ? 1 : 0,
						  [this](int i)
						  {
							  mine = i == 1;
							  fetch();
						  }));
	rows.push_back(choice("generators/compatibility",
						  {"All engines", "Compatible with this engine"}, compatibleOnly ? 1 : 0,
						  [this](int i)
						  {
							  compatibleOnly = i == 1;
							  fetch();
						  }));
	rows.push_back(choice("generators/filter", {"All generators", "Playable", "Editor tools"},
						  filter,
						  [this](int i)
						  {
							  filter = i;
							  fetch();
						  }));
	rows.push_back(
		button("generators/search-button", "Search", [this] { fetch(); }, {.enabled = !busy}));
	if (!status.empty())
		rows.push_back(paragraph(status));
	if (loading)
		rows.push_back(paragraph("Loading…"));
	for (const auto &g : catalogue)
	{
		const std::string id = g.at("id");
		rows.push_back(button("generator/" + id, g.at("name").get<std::string>(),
							  [this, id] { select(id); }, {.enabled = !busy}));
	}
	if (!cursor.empty())
		rows.push_back(
			button("generators/more", "Load more", [this] { fetch(true); }, {.enabled = !busy}));
	if (detail.is_object() && !detail.at("versions").empty())
	{
		const auto &release = detail.at("versions").at(version);
		const auto &metadata = release.at("metadata");
		rows.push_back(paragraph(detail.at("generator").at("description")));
		const std::string previewUrl =
			origin + "/api/v1/generators/" + detail.at("generator").at("id").get<std::string>() +
			"/versions/" + release.at("id").get<std::string>() + "/preview.png";
		if (auto *surface = previews.get(&calls.client(), previewUrl, [this] { invalidate(); }))
			rows.push_back(previewPicture(surface, p.pt(256)));
		std::vector<std::string> labels;
		for (const auto &v : detail.at("versions"))
			labels.push_back(v.at("label").get<std::string>() + " · revision " +
							 std::to_string(v.at("metadata").at("revision").get<unsigned>()));
		rows.push_back(choice("generators/release", labels, version,
							  [this](int i)
							  {
								  if (downloading || persistence)
									  return;
								  selectRelease(i);
								  invalidate();
							  }));
		rows.push_back(paragraph(metadata.value("editorOnly", false)
									 ? "Editor tool: generates terrain for the map editor."
									 : "Playable generator"));
		rows.push_back(
			paragraph(compatible(release)
						  ? "Technical checks passed for this engine. Balance is not certified."
						  : "This release has no passing checks for this engine."));
		for (const auto &shared : sharedGeneratorControls())
		{
			const auto control =
				metadata.value("editorOnly", false) ? editorSizeControl(shared) : shared;
			const auto domain = control.values();
			std::vector<std::string> values;
			for (const int value : domain)
				values.push_back(std::to_string(control.displayValue(value)));
			const int chosen = control.indexOf(settings.at("params").at(control.id).get<int>());
			rows.push_back(field(control.label, choice("generator/" + control.id, values, chosen,
													   [this, id = control.id, domain](int i)
													   {
														   settings["params"][id] = domain.at(i);
														   invalidate();
													   })));
		}
		for (const auto &c : metadata.at("controls"))
		{
			const std::string id = c.at("id"), kind = c.at("kind");
			std::vector<int> domain;
			std::vector<std::string> labels;
			const int low = kind == "range" ? c.at("minimum").get<int>() : 0,
					  high = kind == "range"    ? c.at("maximum").get<int>()
							 : kind == "choice" ? int(c.at("choices").size()) - 1
												: 1,
					  step = kind == "range" ? c.at("step").get<int>() : 1;
			int chosen = 0;
			domain = c.value("values", std::vector<int>{});
			if (domain.empty())
				for (std::int64_t n = low; step > 0 && n <= high && domain.size() < 4096; n += step)
					domain.push_back(int(n));
			for (const int n : domain)
			{
				if (n == settings["params"].value(id, c.at("default").get<int>()))
					chosen = int(labels.size());
				const int shown =
					c.value("powerOfTwo", false) && n >= 0 && n < 31 ? int(1u << n) : n;
				labels.push_back(kind == "choice" ? c.at("choices").at(n).get<std::string>()
												  : std::to_string(shown));
			}
			rows.push_back(field(c.at("label"), choice("generator/control/" + id, labels, chosen,
													   [this, id, domain](int i)
													   {
														   settings["params"][id] = domain.at(i);
														   invalidate();
													   })));
		}
		rows.push_back(
			paragraph("Seed: " + std::to_string(settings.at("seed").get<std::uint32_t>())));
		const std::string manifestId = metadata.at("id");
		if (const auto found = library.origins().find(manifestId); found != library.origins().end())
			rows.push_back(paragraph(found->second.versionId == release.at("id").get<std::string>()
										 ? "This release is installed."
										 : "A different release is installed. Install the selected "
										   "release to update explicitly."));
		rows.push_back(button("generator/reroll", "Reroll seed",
							  [this]
							  {
								  settings["seed"] =
									  (settings.at("seed").get<std::uint32_t>() + 1u);
								  invalidate();
							  },
							  {.enabled = !busy}));
		rows.push_back(
			button("generator/install",
				   replacing.empty() ? "Install selected release" : "Confirm replacement",
				   [this] { install(); }, {.enabled = !busy && compatible(release)}));
		rows.push_back(button(
			"generator/room", "Use in a room", [this] { useInRoom(); },
			{.enabled = !busy && compatible(release) && !metadata.value("editorOnly", false)}));
	}
	OnlinePanel panel;
	panel.title = "Map generators";
	panel.body = scroll("generators/body", column(std::move(rows), {p.pt(8)}));
	panel.actions = {{"back", "Back", [this] { onEscape(); }, false, SDLK_ESCAPE}};
	return onlinePanel(std::move(panel), p);
}
