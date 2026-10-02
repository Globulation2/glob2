// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapCatalog.h"

#include <cctype>
#include <cstdio>

namespace Online
{
std::string urlEncode(const std::string &text)
{
	std::string out;
	for (unsigned char c : text)
	{
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			out += static_cast<char>(c);
		else
		{
			char buffer[4];
			std::snprintf(buffer, sizeof buffer, "%%%02X", c);
			out += buffer;
		}
	}
	return out;
}

std::string MapQuery::path() const
{
	std::string query = "sort=" + urlEncode(sort) + "&limit=" + std::to_string(limit);
	if (mine)
		query += "&owner=me";
	if (!search.empty())
		query += "&q=" + urlEncode(search.substr(0, 128));
	if (teams > 0)
		query += "&teams=" + std::to_string(teams);
	if (minSide > 0)
		query += "&minSide=" + std::to_string(minSide);
	if (maxSide > 0)
		query += "&maxSide=" + std::to_string(maxSide);
	if (!cursor.empty())
		query += "&cursor=" + urlEncode(cursor);
	return "/api/v1/maps?" + query;
}

MapShare::MapShare(PlatformClient &client, std::string mapId, Details details, std::string bytes,
				   std::string simVersionKey, std::function<std::int64_t()> now, std::int64_t pollIntervalMs)
	: client(client), id(std::move(mapId)), details(std::move(details)), bytes(std::move(bytes)),
	  simKey(std::move(simVersionKey)), now(std::move(now)), interval(pollIntervalMs)
{
	if (id.empty())
		create();
	else
		upload();
}

void MapShare::create()
{
	current = Stage::Creating;
	waiting = true;
	Json body = {{"title", details.title.substr(0, 128)},
				 {"description", details.description.substr(0, 4000)},
				 {"visibility", details.visibility},
				 {"madeWith", details.madeWith}};
	auto alive = this->alive;
	client.rest(HttpFetch::Method::Post, "/api/v1/maps", std::move(body),
				[this, alive](const PlatformClient::Response &response)
				{
					if (!*alive)
						return;
					waiting = false;
					if (!response.ok)
					{
						problem = response.error;
						current = Stage::Failed;
						return;
					}
					id = response.result.value("id", std::string());
					if (id.empty())
					{
						problem = {"bad_response", "The server did not return the new map.", Json()};
						current = Stage::Failed;
						return;
					}
					upload();
				});
}

void MapShare::upload()
{
	current = Stage::Uploading;
	waiting = true;
	std::string path = "/api/v1/maps/" + urlEncode(id) + "/versions";
	if (!simKey.empty())
		path += "?simVersion=" + urlEncode(simKey);
	auto alive = this->alive;
	client.restRaw(HttpFetch::Method::Post, path, bytes, "application/octet-stream",
				   [this, alive](const PlatformClient::Response &response)
				   {
					   if (!*alive)
						   return;
					   waiting = false;
					   if (!response.ok)
					   {
						   problem = response.error;
						   current = Stage::Failed;
						   return;
					   }
					   uploaded = MapVersionInfo::fromJson(response.result);
					   if (!uploaded)
					   {
						   problem = {"bad_response", "The server did not describe the upload.", Json()};
						   current = Stage::Failed;
						   return;
					   }
					   bytes.clear();
					   current = Stage::Checking;
					   nextPoll = now();
					   update();
				   });
}

void MapShare::poll()
{
	waiting = true;
	auto alive = this->alive;
	const std::string hash = uploaded->hash;
	client.rest(HttpFetch::Method::Get, "/api/v1/maps/" + urlEncode(id) + "/versions/" + hash, Json(),
				[this, alive](const PlatformClient::Response &response)
				{
					if (!*alive)
						return;
					waiting = false;
					nextPoll = now() + interval;
					if (!response.ok)
						return; // try again on the next poll
					if (auto version = MapVersionInfo::fromJson(response.result))
						uploaded = version;
				});
}

void MapShare::update()
{
	if (current != Stage::Checking || waiting || !uploaded)
		return;
	if (uploaded->validation == "invalid")
	{
		current = Stage::Rejected;
		return;
	}
	if (uploaded->validation == "valid" && uploaded->preview != "pending")
	{
		current = Stage::Ready;
		return;
	}
	if (now() >= nextPoll)
		poll();
}
} // namespace Online
