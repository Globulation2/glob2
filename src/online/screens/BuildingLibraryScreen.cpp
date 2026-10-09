// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingLibraryScreen.h"
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "SimVersion.h"
#include "GlobalContainer.h"
#include "ui/OnlineUI.h"
#include <ApplicationHost.h>
#include <algorithm>
using namespace Glob2UI;
using Json = nlohmann::json;
std::string buildingFamilyIdFromLink(const std::string &input, const std::string &origin)
{
	if (input.size() > 1024)
		throw std::runtime_error("Family links must be at most 1024 characters.");
	const auto first = input.find_first_not_of(" \t\r\n"), last = input.find_last_not_of(" \t\r\n");
	std::string value =
		first == std::string::npos ? std::string{} : input.substr(first, last - first + 1);
	if (value.find("://") != std::string::npos)
	{
		if (origin.empty())
			throw std::runtime_error("Choose an online instance before opening a family link.");
		const auto prefix = origin + "/buildings/";
		if (!value.starts_with(prefix))
			throw std::runtime_error(
				"This link is from another instance. Switch to that instance in Online first.");
		value = value.substr(prefix.size());
		value = value.substr(0, value.find_first_of("?#"));
		if (value.ends_with('/'))
			value.pop_back();
	}
	if (value.size() != 36)
		throw std::runtime_error("Enter a building family ID or a link to its family page.");
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		if (i == 8 || i == 13 || i == 18 || i == 23)
		{
			if (value[i] != '-')
				throw std::runtime_error("Invalid building family ID.");
		}
		else
		{
			if (value[i] >= 'A' && value[i] <= 'F')
				value[i] += 'a' - 'A';
			if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')))
				throw std::runtime_error("Invalid building family ID.");
		}
	}
	return value;
}
BuildingLibraryScreen::BuildingLibraryScreen()
	: library(Online::services().storage), calls(Online::services().client)
{
}
void BuildingLibraryScreen::onTimer(Uint32)
{
	if (!started)
	{
		started = true;
		auto &service = Online::services();
		if (service.client.origin().empty())
			service.client.start(service.config.selectedOrigin());
		reload();
	}
}
void BuildingLibraryScreen::reload(bool more)
{
	busy = true;
	status = "Loading building families";
	invalidate();
	calls.rest(
		HttpFetch::Method::Get,
		"/api/v1/buildings?limit=24" + (more && !cursor.empty() ? "&cursor=" + cursor : ""),
		Online::Json(),
		[this, more](const auto &response)
		{
			busy = false;
			try
			{
				if (!response.ok)
					throw std::runtime_error(response.error.message);
				const auto &rows = response.result.at("items");
				if (!rows.is_array() || rows.size() > 24)
					throw std::runtime_error("Invalid building library response");
				if (!more)
					families = Json::array();
				for (const auto &row : rows)
					families.push_back(row);
				cursor = response.result.value("nextCursor", std::string{});
				if (!cursor.empty() &&
					(cursor.size() > 512 ||
					 cursor.find_first_not_of(
						 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
						 std::string::npos))
					throw std::runtime_error("Invalid library cursor");
				status = families.empty() ? "No public building families yet" : "";
			}
			catch (const std::exception &e)
			{
				status = e.what();
			}
			invalidate();
		});
}
void BuildingLibraryScreen::openFamily()
{
	if (busy)
		return;
	std::string id;
	try
	{
		id = buildingFamilyIdFromLink(familyInput, Online::services().client.origin());
	}
	catch (const std::exception &error)
	{
		status = error.what();
		invalidate();
		return;
	}
	busy = true;
	status = "Opening building family";
	openedFamily = Json();
	invalidate();
	calls.rest(HttpFetch::Method::Get, "/api/v1/buildings/" + id, Online::Json(),
			   [this, id](const auto &response)
			   {
				   busy = false;
				   try
				   {
					   if (!response.ok)
					   {
						   if (response.error.code == "not_found" ||
							   response.error.code == "forbidden" ||
							   response.error.code == "unauthenticated")
							   throw std::runtime_error(
								   "Family unavailable. Private families require signing in "
								   "through Online with an account that can access them.");
						   throw std::runtime_error(response.error.message);
					   }
					   if (!response.result.is_object() || response.result.at("id") != id)
						   throw std::runtime_error("The server returned a different family.");
					   openedFamily = response.result;
					   status = "Opened family. Install its compatible release below.";
				   }
				   catch (const std::exception &error)
				   {
					   status = error.what();
				   }
				   invalidate();
			   });
}
void BuildingLibraryScreen::install(const Json &family, const Json &release)
{
	busy = true;
	status = "Checking building release";
	invalidate();
	const auto validId = [](const Json &value)
	{
		const auto s = value.get<std::string>();
		if (s.size() != 36 || s.find_first_not_of("0123456789abcdef-") != std::string::npos)
			throw std::runtime_error("Invalid release identity");
		return s;
	};
	std::string path;
	try
	{
		path = "/api/v1/buildings/" + validId(family.at("id")) + "/releases/" +
			   validId(release.at("id"));
	}
	catch (const std::exception &e)
	{
		busy = false;
		status = e.what();
		invalidate();
		return;
	}
	calls.restRaw(
		HttpFetch::Method::Get, path + "/runtime", {}, {},
		[this, path, release](const auto &response)
		{
			try
			{
				if (!response.ok)
					throw std::runtime_error(response.error.message);
				const auto manifest = response.result;
				if (manifest.at("archiveHash") != release.at("archiveHash") ||
					manifest.at("baseHash") != release.at("baseHash"))
					throw std::runtime_error("Release identity changed during installation");
				if (!manifest.contains("artworkHash"))
				{
					library.install(manifest, {});
					busy = false;
					status = "Installed. Enable the family below for new maps.";
					invalidate();
					return;
				}
				calls.restRaw(
					HttpFetch::Method::Get, path + "/artwork", {}, {},
					[this, manifest](const auto &artwork)
					{
						try
						{
							if (!artwork.ok)
								throw std::runtime_error(artwork.error.message);
							library.install(manifest, artwork.body);
							status = "Installed. Enable the family below for new maps.";
						}
						catch (const std::exception &e)
						{
							status = e.what();
						}
						busy = false;
						invalidate();
					},
					BuildingArtwork::MaxBytes);
			}
			catch (const std::exception &e)
			{
				busy = false;
				status = e.what();
				invalidate();
			}
		},
		18 * 1024 * 1024);
}
Element BuildingLibraryScreen::build(const Presentation &p)
{
	std::vector<Element> body;
	body.push_back(paragraph("Install individual buildings or upgrade families alongside the stock "
							 "buildings. Selection applies only to newly generated maps."));
	body.push_back(paragraph(
		"Enable installed families below. Selection is saved in this local profile; "
		"existing maps and saves keep their own buildings. For online play, generate a map "
		"with these families, publish it in the map library, then choose that map in your room."));
	body.push_back(
		button("buildings/reload", "Refresh library", [this] { reload(); }, {.enabled = !busy}));
	TextFieldOptions linkOptions;
	linkOptions.maxLength = 1024;
	linkOptions.enabled = !busy;
	linkOptions.placeholder = "Family link or ID";
	linkOptions.submit = [this](const std::string &text)
	{
		familyInput = text;
		openFamily();
	};
	body.push_back(field("Family link or ID", textField(
												  "buildings/link", familyInput,
												  [this](const std::string &text)
												  {
													  familyInput = text;
													  invalidate();
												  },
												  linkOptions)));
	body.push_back(button("buildings/open", "Open family", [this] { openFamily(); },
						  {.enabled = !busy && !familyInput.empty()}));
	body.push_back(caption("Open an unlisted family by its link or ID. For private families, sign "
						   "in through Online first."));
	body.push_back(label("Installed families", {FontRole::Heading}));
	try
	{
		const auto installed = library.entries();
		if (installed.empty())
			body.push_back(caption("No building families installed."));
		for (const auto &row : installed)
		{
			const auto ns = row.at("namespace").get<std::string>(),
					   name = row.at("name").get<std::string>();
			body.push_back(toggle("buildings/select/" + ns, name, row.value("selected", false),
								  [this, ns](bool enabled)
								  {
									  try
									  {
										  library.select(ns, enabled);
										  status = "Selection saved for new maps.";
									  }
									  catch (const std::exception &e)
									  {
										  status = e.what();
									  }
									  invalidate();
								  }));
			body.push_back(caption("Installed release " +
								   row.at("archiveHash").get<std::string>().substr(0, 12)));
			body.push_back(button("buildings/remove/" + ns, "Remove " + name,
								  [this, ns]
								  {
									  try
									  {
										  library.remove(ns);
										  status = "Family removed.";
									  }
									  catch (const std::exception &e)
									  {
										  status = e.what();
									  }
									  invalidate();
								  },
								  {.enabled = !busy}));
		}
		body.push_back(label("Available families", {FontRole::Heading}));
		auto rows = families;
		if (openedFamily.is_object())
		{
			for (auto it = rows.begin(); it != rows.end();)
			{
				if (it->at("id") == openedFamily.at("id"))
					it = rows.erase(it);
				else
					++it;
			}
			rows.insert(rows.begin(), openedFamily);
		}
		for (const auto &family : rows)
		{
			body.push_back(label(family.at("name").get<std::string>(), {FontRole::Heading}));
			body.push_back(
				caption("By " + family.at("owner").at("displayName").get<std::string>()));
			body.push_back(paragraph(family.at("description").get<std::string>()));
			const auto &versions = family.at("releases");
			const auto compatible = std::find_if(
				versions.begin(), versions.end(),
				[](const auto &v)
				{
					return v.value("status", std::string{}) == "valid" &&
						   v.at("simVersion") == Online::SimVersion::local().key() &&
						   v.at("baseHash") == globalContainer->buildingsTypes.fingerprint();
				});
			if (compatible != versions.end())
			{
				const auto existing =
					std::find_if(installed.begin(), installed.end(), [&](const auto &row)
								 { return row.at("namespace") == family.at("namespace"); });
				const bool current = existing != installed.end() &&
									 existing->at("archiveHash") == compatible->at("archiveHash") &&
									 existing->value("simVersion", std::string{}) ==
										 compatible->at("simVersion").get<std::string>() &&
									 existing->value("baseHash", std::string{}) ==
										 compatible->at("baseHash").get<std::string>();
				body.push_back(caption(
					"Release " + compatible->at("archiveHash").get<std::string>().substr(0, 12) +
					(current                       ? " · Installed"
					 : existing != installed.end() ? " · Update available"
												   : "")));
				body.push_back(button("buildings/install/" + family.at("id").get<std::string>(),
									  current                       ? "Installed"
									  : existing != installed.end() ? "Update installed family"
																	: "Install compatible release",
									  [this, family, release = *compatible]
									  { install(family, release); },
									  {.enabled = !busy && !current}));
			}
			else
				body.push_back(caption("No validated release for this game version."));
			body.push_back(divider());
		}
		if (!cursor.empty())
			body.push_back(button("buildings/more", "Load more families", [this] { reload(true); },
								  {.enabled = !busy}));
	}
	catch (const std::exception &e)
	{
		body.push_back(caption(e.what()));
	}
	if (!status.empty())
		body.push_back(caption(status));
	OnlinePanel panel;
	panel.title = "Building library";
	panel.body = scroll("buildings/body", column(std::move(body), {p.pt(8)}));
	panel.actions = {{"buildings/studio", "Create in Building Studio",
					  []
					  {
						  auto &service = Online::services();
						  GAGCore::ApplicationHost::openUrl(service.client.origin() +
															"/building-studio");
					  }},
					 {"buildings/back", "Back", [this] { endExecute(1); }, false, SDLK_ESCAPE}};
	return onlinePanel(std::move(panel), p);
}
