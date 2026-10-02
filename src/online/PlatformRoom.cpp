// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "PlatformRoom.h"

#include "AINames.h"
#include "CustomGameSetup.h"
#include "Game.h"
#include "MapCache.h"
#include "OnlineMatch.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "RoomSetup.h"
#include "Team.h"
#include "Utilities.h"

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include <algorithm>
#include <random>

namespace Online
{
namespace
{
std::string text(const char *key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}
std::string formatted(const char *key, const std::string &a)
{
	return GAGCore::FormattableString(text(key)).arg(a);
}
std::string occupantKind(const Json &seat)
{
	if (!seat.contains("occupant") || !seat["occupant"].is_object())
		return "open";
	return seat["occupant"].value("kind", "open");
}
} // namespace

struct PlatformRoom::MapFetch
{
	std::unique_ptr<MapCache::Download> download;
	std::string hash;
};

PlatformRoom::PlatformRoom(PlatformClient *client) : client(client)
{
	if (client && client->account())
		accountId = client->account()->id;
}

PlatformRoom::~PlatformRoom()
{
	*alive = false;
	if (client)
	{
		for (auto id : listeners)
			client->removeListener(id);
		for (auto id : requests)
			client->cancelRequest(id);
	}
}

std::shared_ptr<PlatformRoom> PlatformRoom::create(PlatformClient &client, const std::string &name, bool listed,
												   const CustomGameSetup &setup)
{
	std::shared_ptr<PlatformRoom> room(new PlatformRoom(&client));
	room->listen();
	Json params{{"name", name.substr(0, 64)}, {"visibility", listed ? "public" : "link"}};
	try
	{
		params["map"] = Json{{"kind", "generated"}, {"generator", generatorDescriptor(setup, std::random_device{}())}};
	}
	catch (const std::exception &)
	{
		// A landscape the platform cannot generate: the room starts without a map and
		// the host picks one.
	}
	params["rules"] = matchRules(setup);
	room->call("room.create", params, [room = room.get(), teams = setupTeams(setup)](const Json &result) {
		if (!result.contains("room"))
			return;
		room->adopt(result["room"]);
		// The draft's alliances, once the room has its seats.
		const Json &state = room->state;
		if (state.contains("teams") && state["teams"].size() == teams.size() && state["teams"] != teams)
			room->call("room.update",
					   Json{{"roomId", state["id"]}, {"revision", state["revision"]}, {"changes", {{"teams", teams}}}});
	});
	return room;
}

std::shared_ptr<PlatformRoom> PlatformRoom::join(PlatformClient &client, const std::string &code)
{
	std::shared_ptr<PlatformRoom> room(new PlatformRoom(&client));
	room->listen();
	room->call("room.join", Json{{"code", code}}, [room = room.get()](const Json &result) {
		if (result.contains("room"))
			room->adopt(result["room"]);
	});
	return room;
}

std::shared_ptr<PlatformRoom> PlatformRoom::rematch(PlatformClient &client, const std::string &matchId)
{
	std::shared_ptr<PlatformRoom> room(new PlatformRoom(&client));
	room->listen();
	room->call("match.rematch", Json{{"matchId", matchId}}, [room = room.get()](const Json &result) {
		if (result.contains("room"))
			room->adopt(result["room"]);
	});
	return room;
}

std::shared_ptr<PlatformRoom> PlatformRoom::preview(Json roomState, std::string account,
													std::vector<std::pair<std::string, std::string>> chat)
{
	std::shared_ptr<PlatformRoom> room(new PlatformRoom(nullptr));
	room->accountId = std::move(account);
	room->adopt(roomState);
	for (auto &line : chat)
	{
		Event event;
		event.kind = Event::Chat;
		event.author = line.first;
		event.text = line.second;
		event.system = line.first.empty();
		room->push(std::move(event));
	}
	return room;
}

void PlatformRoom::listen()
{
	auto keep = alive;
	listeners.push_back(client->addListener("room.state", [this, keep](const std::string &, const Json &data) {
		if (*keep && data.contains("room"))
			adopt(data["room"]);
	}));
	listeners.push_back(client->addListener("room.chat", [this, keep](const std::string &, const Json &data) {
		if (!*keep || !data.contains("message") || state.is_null())
			return;
		const Json &message = data["message"];
		if (message.value("roomId", "") != state.value("id", ""))
			return;
		Event event;
		event.kind = Event::Chat;
		event.author = message.value("displayName", "");
		event.text = message.value("text", "");
		push(std::move(event));
	}));
	listeners.push_back(client->addListener("room.closed", [this, keep](const std::string &, const Json &data) {
		if (!*keep || state.is_null() || data.value("roomId", "") != state.value("id", "") || finished)
			return;
		finished = true;
		const std::string reason = data.value("reason", "");
		Event event;
		event.kind = Event::Finished;
		if (reason == "kicked")
		{
			event.code = Kicked;
			event.text = text("[room you were removed]");
		}
		else if (reason == "host_closed")
		{
			event.code = GameCancelled;
			event.text = isHost() ? std::string() : text("[room host closed]");
		}
		else
		{
			event.code = GameCancelled;
			event.text = text("[room expired]");
		}
		push(std::move(event));
	}));
	listeners.push_back(client->addListener("match.start", [this, keep](const std::string &, const Json &data) {
		if (!*keep || state.is_null())
			return;
		// The room's match (or one the room is about to start).
		const std::string matchId = data.value("matchId", "");
		if (match && match->matchId() == matchId)
			return;
		if (state.contains("matchId") && state["matchId"].is_string() && state["matchId"] != matchId &&
			pendingMatchId != matchId)
			return;
		OnlineMatch::Context context;
		context.label = text("[room match label]") + " · " + roomName();
		context.fromRoom = true;
		match = std::make_shared<OnlineMatch>(*client, data, context);
		Event event;
		event.kind = Event::Launch;
		push(std::move(event));
	}));
}

void PlatformRoom::call(const std::string &method, Json params, std::function<void(const Json &)> done)
{
	if (!client)
		return;
	auto keep = alive;
	auto id = std::make_shared<std::uint64_t>(0);
	*id = client->request(method, std::move(params),
						  [this, keep, id, method, done = std::move(done)](const PlatformClient::Response &response) {
							  if (!*keep)
								  return;
							  requests.erase(*id);
							  if (!response.ok)
							  {
								  problem = response.error.message.empty() ? response.error.code : response.error.message;
								  if (method == "room.create" || method == "room.join")
								  {
									  finished = true;
									  Event event;
									  event.kind = Event::Finished;
									  event.code = response.error.code == "update_required" ? GameRefused : ServerDisconnected;
									  event.text = response.error.code == "not_found" ? text("[room invite not found]")
												   : response.error.code == "update_required"
													   ? text("[room update required]")
													   : problem;
									  push(std::move(event));
								  }
								  else
								  {
									  Event event;
									  event.kind = Event::Changed;
									  push(std::move(event));
								  }
								  return;
							  }
							  problem.clear();
							  if (done)
								  done(response.result);
						  });
	requests.insert(*id);
}

void PlatformRoom::push(Event event)
{
	events.push_back(std::move(event));
}

void PlatformRoom::notice(const std::string &line)
{
	Event event;
	event.kind = Event::Chat;
	event.text = line;
	event.system = true;
	push(std::move(event));
}

void PlatformRoom::adopt(const Json &room)
{
	if (!room.is_object())
		return;
	if (!state.is_null())
	{
		if (room.value("id", "") != state.value("id", ""))
			return;
		if (room.value("revision", 0) < state.value("revision", 0))
			return;
	}
	const Json previous = state;
	state = room;
	if (previous.is_null())
		notice(isHost() ? text("[room you created]") : formatted("[room you joined %0]", roomName()));
	else
		systemLinesFor(previous, room);
	if (state.value("status", "") == "starting" && state.contains("matchId") && state["matchId"].is_string())
		pendingMatchId = state["matchId"].get<std::string>();
	fetchMap();
	Event event;
	event.kind = Event::Changed;
	push(std::move(event));
}

void PlatformRoom::systemLinesFor(const Json &previous, const Json &next)
{
	auto names = [](const Json &room) {
		std::map<std::string, std::string> result;
		if (room.contains("members"))
			for (const auto &m : room["members"])
				result[m.value("accountId", "")] = m.value("displayName", "");
		return result;
	};
	const auto before = names(previous), after = names(next);
	for (const auto &[id, name] : after)
		if (!before.count(id))
			notice(formatted("[room member joined %0]", name));
	for (const auto &[id, name] : before)
		if (!after.count(id))
			notice(formatted("[room member left %0]", name));
	if (previous.value("map", Json()) != next.value("map", Json()))
		notice(formatted("[room map changed %0]", mapName()));
	else if (previous.value("rules", Json()) != next.value("rules", Json()))
		notice(text("[room rules changed]"));
	if (previous.contains("seats") && next.contains("seats"))
		for (const auto &seat : next["seats"])
		{
			if (occupantKind(seat) != "human" || !seat["occupant"].value("ready", false))
				continue;
			const int index = seat.value("seat", -1);
			const Json *old = nullptr;
			for (const auto &s : previous["seats"])
				if (s.value("seat", -2) == index)
					old = &s;
			const bool wasReady = old && occupantKind(*old) == "human" &&
								  (*old)["occupant"].value("accountId", "") == seat["occupant"].value("accountId", "") &&
								  (*old)["occupant"].value("ready", false);
			if (!wasReady)
				notice(formatted("[room member ready %0]", seat["occupant"].value("displayName", "")));
		}
}

void PlatformRoom::fetchMap()
{
	if (!state.contains("map") || !state["map"].is_object())
		return;
	const std::string hash = state["map"].value("hash", "");
	if (hash.empty() || hash == mapHash)
		return;
	mapHash = hash;
	mapPath.clear();
	if (!client)
	{
		if (auto path = services().maps.path(hash))
			mapPath = *path;
		return;
	}
	mapFetch = std::make_shared<MapFetch>();
	mapFetch->hash = hash;
	HttpFetch::Headers headers;
	if (!client->accessToken().empty())
		headers.push_back({"Authorization", "Bearer " + client->accessToken()});
	mapFetch->download = services().maps.fetch(client->origin(), hash, headers);
}

void PlatformRoom::update()
{
	if (mapFetch && mapFetch->download)
	{
		const auto status = mapFetch->download->state();
		if (status == MapCache::Download::State::Done)
		{
			if (mapFetch->hash == mapHash)
				mapPath = mapFetch->download->path();
			mapFetch.reset();
			Event event;
			event.kind = Event::Changed;
			push(std::move(event));
		}
		else if (status == MapCache::Download::State::Failed)
			mapFetch.reset();
	}
}

std::optional<RoomBackend::Event> PlatformRoom::takeEvent()
{
	if (events.empty())
		return std::nullopt;
	Event event = std::move(events.front());
	events.pop_front();
	return event;
}

std::string PlatformRoom::myAccount() const
{
	if (client && client->account())
		return client->account()->id;
	return accountId;
}

bool PlatformRoom::isHost() const
{
	return !state.is_null() && state.value("hostAccountId", "") == myAccount() && !myAccount().empty();
}

const Json *PlatformRoom::seatJson(int seat) const
{
	if (!state.contains("seats"))
		return nullptr;
	for (const auto &s : state["seats"])
		if (s.value("seat", -1) == seat)
			return &s;
	return nullptr;
}

const Json *PlatformRoom::member(const std::string &id) const
{
	if (!state.contains("members"))
		return nullptr;
	for (const auto &m : state["members"])
		if (m.value("accountId", "") == id)
			return &m;
	return nullptr;
}

std::string PlatformRoom::mapName() const
{
	if (state.is_null() || !state.contains("map") || !state["map"].is_object())
		return text("[room no map]");
	const Json &map = state["map"];
	const std::string kind = map.value("kind", "");
	if (kind == "generated" && map.contains("generator"))
	{
		const Json &g = map["generator"];
		const int method = GeneratorRegistry::builtins().idOf(g.value("generatorId", ""));
		std::string name = GeneratorRegistry::builtins().find(method)
							   ? text(("[" + std::string(GenerationRequest::methodName(method)) + "]").c_str())
							   : g.value("generatorId", "");
		// Untranslated landscape names come back as "[Name]".
		if (name.size() > 2 && name.front() == '[' && name.back() == ']')
			name = name.substr(1, name.size() - 2);
		if (g.contains("params") && g["params"].contains("width") && g["params"].contains("height"))
			name += " " + std::to_string(1 << g["params"]["width"].get<int>()) + "×" +
					std::to_string(1 << g["params"]["height"].get<int>());
		return name;
	}
	if (map.contains("title") && map["title"].is_string())
		return map["title"].get<std::string>();
	return kind == "upload" ? text("[room uploaded map]") : text("[room catalog map]");
}

int PlatformRoom::teamCount() const
{
	return state.contains("seats") ? int(state["seats"].size()) : 0;
}

std::optional<std::array<std::uint8_t, 3>> PlatformRoom::teamColor(int team) const
{
	const int teams = teamCount();
	if (team < 0 || team >= teams)
		return std::nullopt;
	// The colours the generators give their teams (Game::setBase / setTeamColors).
	float r, g, b;
	Utilities::HSVtoRGB(&r, &g, &b, (float(team) * TEAM_COLOR_HUE_DEGREES) / float(teams),
						Team::TEAM_COLOR_SATURATION, Team::TEAM_COLOR_VALUE);
	return std::array<std::uint8_t, 3>{std::uint8_t(255 * r), std::uint8_t(255 * g), std::uint8_t(255 * b)};
}

std::optional<std::array<std::uint8_t, 3>> PlatformRoom::seatColor(const Slot &slot) const
{
	return teamColor(slot.index);
}

std::vector<RoomBackend::Slot> PlatformRoom::slots() const
{
	std::vector<Slot> result;
	if (!state.contains("seats"))
		return result;
	const std::string me = myAccount();
	const std::string host = state.value("hostAccountId", "");
	for (const auto &seat : state["seats"])
	{
		Slot slot;
		slot.index = seat.value("seat", 0);
		slot.team = slot.index;
		if (state.contains("teams"))
			for (const auto &team : state["teams"])
				if (team.value("team", -1) == slot.index)
					slot.team = team.value("alliance", slot.index);
		slot.locked = seat.value("locked", false);
		const std::string kind = occupantKind(seat);
		const Json occupant = seat.value("occupant", Json::object());
		if (kind == "human")
		{
			const std::string account = occupant.value("accountId", "");
			slot.name = occupant.value("displayName", "");
			slot.local = account == me;
			slot.host = account == host;
			slot.ready = slot.host || occupant.value("ready", false);
			if (const Json *m = member(account))
			{
				slot.guest = m->value("kind", "") == "guest";
				if (!m->value("connected", true))
					slot.detail = text("[room disconnected]");
			}
		}
		else if (kind == "ai")
		{
			slot.ai = true;
			slot.aiId = occupant.value("ai", "");
			slot.name = occupant.value("name", slot.aiId);
			const int id = AINames::parseAIName(slot.aiId);
			if (id != AINames::AI_UNKNOWN_NAME)
			{
				slot.name = AINames::getAIText(id);
				slot.detail = AINames::getAISummary(id);
			}
		}
		else
		{
			slot.open = !slot.locked;
			slot.name = slot.locked ? text("[room closed seat]") : text("[room open seat]");
			slot.detail = slot.locked ? text("[room closed seat detail]") : text("[room open seat detail]");
		}
		result.push_back(slot);
	}
	return result;
}

bool PlatformRoom::canChangeTeam(const Slot &) const
{
	return isHost() && !starting();
}

bool PlatformRoom::canKick(const Slot &slot) const
{
	return isHost() && !slot.local && !slot.ai && !slot.open && !slot.locked && !starting();
}

void PlatformRoom::changeTeam(int slotIndex, int team)
{
	if (!canChangeTeam(Slot()) || !state.contains("teams"))
		return;
	Json teams = state["teams"];
	for (auto &entry : teams)
		if (entry.value("team", -1) == slotIndex)
			entry["alliance"] = team;
	// Alliances must be dense from 0 in order of first appearance.
	std::map<int, int> dense;
	for (auto &entry : teams)
	{
		const int alliance = entry.value("alliance", 0);
		auto found = dense.find(alliance);
		if (found == dense.end())
			found = dense.emplace(alliance, int(dense.size())).first;
		entry["alliance"] = found->second;
	}
	call("room.update", Json{{"roomId", state["id"]}, {"revision", state["revision"]}, {"changes", {{"teams", teams}}}});
}

void PlatformRoom::kick(int slotIndex)
{
	const Json *seat = seatJson(slotIndex);
	if (!seat || occupantKind(*seat) != "human")
		return;
	call("room.kick", Json{{"roomId", state["id"]}, {"accountId", (*seat)["occupant"].value("accountId", "")}});
}

bool PlatformRoom::canAddAI() const
{
	if (!isHost() || starting())
		return false;
	for (const auto &slot : slots())
		if (slot.open)
			return true;
	return false;
}

void PlatformRoom::addAI(AI::ImplementationID ai)
{
	for (const auto &slot : slots())
		if (slot.open)
		{
			setOccupant(slot.index, Occupant::AI, AINames::getCLIName(int(ai)));
			return;
		}
}

void PlatformRoom::setReady(bool ready)
{
	if (state.is_null())
		return;
	call("room.setReady", Json{{"roomId", state["id"]}, {"ready", ready}});
}

bool PlatformRoom::localReady() const
{
	for (const auto &slot : slots())
		if (slot.local)
			return slot.ready;
	return false;
}

bool PlatformRoom::everyoneReady() const
{
	for (const auto &slot : slots())
		if (!slot.ai && !slot.open && !slot.locked && !slot.ready)
			return false;
	return true;
}

std::string PlatformRoom::waitingFor() const
{
	if (state.is_null())
		return text("[room connecting]");
	if (starting())
		return text("[room starting match]");
	const std::string map = state.value("mapStatus", "ready");
	if (map == "pending")
		return text("[room preparing map]");
	if (map == "failed")
		return text("[room map failed]");
	std::vector<std::string> waiting;
	int occupied = 0;
	for (const auto &slot : slots())
	{
		if (!slot.open && !slot.locked)
			++occupied;
		if (!slot.ai && !slot.open && !slot.locked && !slot.ready)
			waiting.push_back(slot.name);
	}
	if (occupied < 2)
		return text("[room needs two seats]");
	if (waiting.size() == 1)
		return formatted("[room waiting for %0]", waiting.front());
	if (!waiting.empty())
		return formatted("[room waiting for players %0]", std::to_string(waiting.size()));
	if (!isHost())
		return text("[room waiting for host]");
	return {};
}

bool PlatformRoom::canStart() const
{
	return isHost() && !starting() && waitingFor().empty();
}

void PlatformRoom::start()
{
	if (!canStart())
		return;
	call("room.start", Json{{"roomId", state["id"]}}, [this](const Json &result) {
		if (result.contains("matchId") && result["matchId"].is_string())
			pendingMatchId = result["matchId"].get<std::string>();
	});
}

bool PlatformRoom::starting() const
{
	const std::string status = state.is_null() ? "" : state.value("status", "");
	return status == "starting" || status == "in_match";
}

void PlatformRoom::sendChat(const std::string &line)
{
	if (state.is_null() || line.empty())
		return;
	call("room.chat", Json{{"roomId", state["id"]}, {"text", line.substr(0, 500)}});
}

std::string PlatformRoom::experimentsLabel() const
{
	if (!state.contains("experiments") || !state["experiments"].is_array())
		return {};
	std::string label;
	for (const auto &key : state["experiments"])
		if (key.is_string())
			label += (label.empty() ? "" : ", ") + key.get<std::string>();
	return label;
}

void PlatformRoom::leave()
{
	if (left || state.is_null())
		return;
	left = true;
	call("room.leave", Json{{"roomId", state["id"]}});
}

GAGCore::CooperativeTask PlatformRoom::initGame(Engine &)
{
	// Online rooms start through OnlineMatch (takeMatch), never here.
	co_return false;
}

void PlatformRoom::gameEnded(bool)
{
	match.reset();
	pendingMatchId.clear();
}

std::string PlatformRoom::roomName() const
{
	return state.is_null() ? std::string() : state.value("name", "");
}

std::string PlatformRoom::hostName() const
{
	if (const Json *m = member(state.is_null() ? "" : state.value("hostAccountId", "")))
		return m->value("displayName", "");
	return {};
}

bool PlatformRoom::listed() const
{
	return !state.is_null() && state.value("visibility", "link") == "public";
}

void PlatformRoom::setListed(bool value)
{
	if (!canChangeVisibility() || value == listed())
		return;
	call("room.update", Json{{"roomId", state["id"]},
							 {"revision", state["revision"]},
							 {"changes", {{"visibility", value ? "public" : "link"}}}});
}

std::string PlatformRoom::inviteLink() const
{
	return state.is_null() ? std::string() : state.value("inviteUrl", "");
}

std::string PlatformRoom::inviteCode() const
{
	return state.is_null() ? std::string() : state.value("code", "");
}

std::string PlatformRoom::setupSummary() const
{
	if (state.is_null())
		return {};
	CustomGameSetup setup;
	applyRulesToSetup(state.value("rules", Json::object()), setup);
	return text(("[" + formatName(state.value("teams", Json::array())) + "]").c_str()) + " · " + mapName() + " · " +
		   rulesetName(setup);
}

std::string PlatformRoom::mapStatus() const
{
	if (state.is_null())
		return {};
	const std::string status = state.value("mapStatus", "ready");
	if (status == "pending")
		return text("[room preparing map]");
	if (status == "failed")
		return text("[room map failed]") + (state.contains("mapProblem") ? ": " + state.value("mapProblem", "") : "");
	if (!mapHash.empty() && mapPath.empty())
		return text("[room downloading map]");
	return {};
}

std::optional<std::string> PlatformRoom::mapFile() const
{
	if (mapPath.empty())
		return std::nullopt;
	return mapPath;
}

bool PlatformRoom::canTakeSeat(const Slot &slot) const
{
	return slot.open && !slot.locked && !starting() && !state.is_null();
}

void PlatformRoom::takeSeat(int slotIndex)
{
	call("room.setSeat", Json{{"roomId", state["id"]}, {"seat", slotIndex}, {"occupant", {{"kind", "self"}}}});
}

bool PlatformRoom::canSetOccupant(const Slot &slot) const
{
	return isHost() && !starting() && !(!slot.ai && !slot.open && !slot.locked && slot.local);
}

void PlatformRoom::setOccupant(int slotIndex, Occupant occupant, const std::string &aiId)
{
	Json value;
	switch (occupant)
	{
	case Occupant::Open:
		value = {{"kind", "open"}};
		break;
	case Occupant::Closed:
		value = {{"kind", "locked"}};
		break;
	case Occupant::AI:
		value = {{"kind", "ai"}, {"ai", aiId.empty() ? std::string("nicowar") : aiId}};
		break;
	}
	call("room.setSeat", Json{{"roomId", state["id"]}, {"seat", slotIndex}, {"occupant", value}});
}

bool PlatformRoom::setupDraft(CustomGameSetup &draft) const
{
	if (state.is_null())
		return false;
	applyRoomToSetup(state, draft);
	return true;
}

void PlatformRoom::applySetup(const CustomGameSetup &setup)
{
	if (!canEditSetup())
		return;
	Json changes{{"rules", matchRules(setup)}};
	try
	{
		Json descriptor = generatorDescriptor(setup, std::random_device{}());
		const Json current = state.value("map", Json());
		const bool sameMap = current.is_object() && current.value("kind", "") == "generated" &&
							 current.contains("generator") && current["generator"] == descriptor;
		if (!sameMap)
			changes["map"] = Json{{"kind", "generated"}, {"generator", descriptor}};
	}
	catch (const std::exception &error)
	{
		problem = error.what();
	}
	call("room.update", Json{{"roomId", state["id"]}, {"revision", state["revision"]}, {"changes", changes}},
		 [this, teams = setupTeams(setup)](const Json &result) {
			 if (result.contains("room"))
				 adopt(result["room"]);
			 // Alliances need the seats the new map gave the room.
			 if (!state.is_null() && state.contains("teams") && state["teams"].size() == teams.size() &&
				 state["teams"] != teams)
				 call("room.update",
					  Json{{"roomId", state["id"]}, {"revision", state["revision"]}, {"changes", {{"teams", teams}}}});
		 });
}

void PlatformRoom::useCatalogMap(const std::string &hash, const std::string &mapId)
{
	if (!canEditSetup() || hash.empty())
		return;
	Json map{{"kind", "catalog"}, {"hash", hash}};
	if (!mapId.empty())
		map["mapId"] = mapId;
	call("room.update", Json{{"roomId", state["id"]}, {"revision", state["revision"]}, {"changes", {{"map", map}}}},
		 [this](const Json &result) {
			 if (result.contains("room"))
				 adopt(result["room"]);
		 });
}

std::shared_ptr<OnlineMatch> PlatformRoom::takeMatch()
{
	return match;
}
} // namespace Online
