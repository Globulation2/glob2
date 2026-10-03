// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiveClient.h"
#include "HiveObservation.h"
#include "GameGUI.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "PlatformClient.h"
#include "Order.h"
#include "OnlineServices.h"
#include "OnlineStorage.h"
#include "team/Team.h"
#include "script/ScriptOrders.h"
#include <ApplicationHost.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
namespace Hive
{
namespace
{
std::string executable;
std::string uuid()
{
	std::random_device rd;
	static const char *hex = "0123456789abcdef";
	std::string out;
	for (int i = 0; i < 32; i++)
	{
		if (i == 8 || i == 12 || i == 16 || i == 20)
			out += '-';
		out += hex[i == 12 ? 4 : i == 16 ? 8 + (rd() % 4) : rd() % 16];
	}
	return out;
}
#ifndef __EMSCRIPTEN__
Json runWorker(const Json &input)
{
	const char *args[] = {executable.c_str(), "--hive-worker", nullptr};
	SDL_PropertiesID props = SDL_CreateProperties();
	auto *environment = SDL_CreateEnvironment(false);
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
	auto *process = SDL_CreateProcessWithProperties(props);
	SDL_DestroyProperties(props);
	SDL_DestroyEnvironment(environment);
	if (!process)
		return {{"ok", false}};
	std::string wire = input.dump() + "\n", output;
	auto *stdinStream = SDL_GetProcessInput(process);
	auto *stdoutStream = SDL_GetProcessOutput(process);
	// SDL process input is nonblocking; pump both pipes so large observations cannot deadlock.
	std::size_t sent = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	bool complete = false;
	int exitCode = 0;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (sent < wire.size())
			sent += SDL_WriteIO(stdinStream, wire.data() + sent, wire.size() - sent);
		char bytes[8192];
		auto n = SDL_ReadIO(stdoutStream, bytes, sizeof(bytes));
		if (n)
			output.append(bytes, n);
		if (output.size() > 2 * 1024 * 1024)
			break;
		if (output.find('\n') != std::string::npos)
		{
			complete = true;
			break;
		}
		if (SDL_WaitProcess(process, false, &exitCode))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	SDL_KillProcess(process, true);
	SDL_WaitProcess(process, true, &exitCode);
	SDL_DestroyProcess(process);
	if (!complete)
		return {{"ok", false}};
	try
	{
		return Json::parse(output);
	}
	catch (...)
	{
		return {{"ok", false}};
	}
}
#else
// clang-format off
void hiveBegin(const char *text)
{
	MAIN_THREAD_EM_ASM(
		{
			if (Module.hiveWorker)
				Module.hiveWorker.terminate();
			Module.hiveResult = null;
			Module.hiveWorker = new Worker(new URL('hive-worker.js', document.baseURI));
			Module.hiveWorker.onmessage = e =>
			{
				Module.hiveResult = JSON.stringify(e.data);
				Module.hiveWorker.terminate();
			};
			Module.hiveWorker.onerror = () =>
			{
				Module.hiveResult = '{"ok":false}';
				Module.hiveWorker.terminate();
			};
			Module.hiveWorker.postMessage(UTF8ToString($0));
			clearTimeout(Module.hiveTimeout);
			Module.hiveTimeout = setTimeout(() =>
												 {
													 if (Module.hiveResult === null)
													 {
														 Module.hiveWorker.terminate();
														 Module.hiveResult = '{"ok":false}';
													 }
												 },
											5000);
		},
		text);
}
char *hiveResult()
{
	return reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
		if (Module.hiveResult === null || Module.hiveResult === undefined)
			return 0;
		const r = Module.hiveResult;
		Module.hiveResult = null;
		return stringToNewUTF8(r);
	}));
}
// clang-format on
#endif
} // namespace
void setExecutable(const char *path)
{
	executable = std::filesystem::absolute(path).string();
}
std::uint64_t Client::now() const
{
	return environment.now ? environment.now() : SDL_GetTicks();
}
Online::OnlineStorage &Client::storage()
{
	return environment.storage ? *environment.storage : Online::services().storage;
}
void Client::rest(HttpFetch::Method method, const std::string &path, const Json &body,
				  Online::PlatformClient::ResponseHandler callback)
{
	if (environment.request)
		environment.request(method, path, body, std::move(callback));
	else
		platform.rest(method, path, body, std::move(callback));
}
Client::Client(GameGUI &g, Online::PlatformClient &p, std::string m, int seat, ClientEnvironment e)
	: gui(g), platform(p), observations(g.game, g.localTeamNo), environment(std::move(e)),
	  match(std::move(m)), clientId(uuid())
{
	base = "/api/v1/hive/matches/" + match + "/" + std::to_string(seat);
	auto live = alive;
	rest(HttpFetch::Method::Get, "/api/v1/hive/account", nullptr,
		 [this, live](const auto &r)
		 {
			 if (!*live)
				 return;
			 if (r.ok)
			 {
				 account = r.result;
				 enabled = account.value("enabled", false);
			 }
		 });
	restore();
}
Client::~Client()
{
	*alive = false;
	save();
}
Json Client::snapshot()
{
	return capture(observations, gui.game, gui.localTeamNo);
}
void Client::start(const Json &request)
{
	working = true;
	invocationLease = lease;
#ifndef __EMSCRIPTEN__
	auto worker = environment.worker;
	pending = std::async(std::launch::async, [request, worker]
						 { return worker ? worker(request) : runWorker(request); });
#else
	hiveBegin(request.dump().c_str());
#endif
}
void Client::execute(const Json &operation)
{
	current = operation;
	currentProgram.clear();
	const auto &tool = operation.at("request");
	const std::string kind = tool.at("kind");
	try
	{
		if (kind == "list")
		{
			Json list = Json::array();
			for (auto &[id, p] : programs)
				list.push_back({{"definition", p.definition},
								{"status", p.paused ? "paused" : "active"},
								{"recentResult", p.recent},
								{"nextTick", p.nextTick}});
			finish({{"ok", true}, {"output", list}, {"orders", Json::array()}});
			return;
		}
		if (kind == "pause" || kind == "resume" || kind == "remove")
		{
			auto it = programs.find(tool.at("programId").get<std::string>());
			if (it == programs.end() ||
				it->second.definition.at("revision") != tool.at("expectedRevision"))
				throw std::runtime_error("Stale standing order");
			if (kind == "resume" && it->second.missingCheckpoint)
				throw std::runtime_error("Missing checkpoint; replace the standing order first");
			finish({{"ok", true}, {"output", standingOrders()}, {"orders", Json::array()}});
			return;
		}
		std::string source;
		if (kind == "execute")
			source = tool.at("source");
		else
		{
			const auto &p = tool.at("program");
			std::string id = p.at("id");
			int revision = p.at("revision"), interval = p.at("intervalTicks");
			if (interval < 25 || p.at("source").get<std::string>().size() > Script::SourceLimit)
				throw std::runtime_error("Invalid standing order");
			auto it = programs.find(id);
			if (kind == "install" &&
				(it != programs.end() || programs.size() >= 8 || revision != 1))
				throw std::runtime_error("Cannot install standing order");
			if (kind == "replace" &&
				(it == programs.end() ||
				 it->second.definition.at("revision") != tool.at("expectedRevision") ||
				 revision != tool.at("expectedRevision").get<int>() + 1))
				throw std::runtime_error("Stale standing order");
			source = p.at("source");
		}
		// Preflight new definitions against a real observation. Discard all effects before installation.
		Json request = {{"source", source}, {"snapshot", snapshot()}, {"initialized", false}};
		if (tool.contains("migration"))
		{
			if (programs.at(tool.at("program").at("id").get<std::string>()).missingCheckpoint)
				throw std::runtime_error("Cannot migrate a missing checkpoint");
			request["migration"] = tool.at("migration");
			request["previousState"] =
				programs.at(tool.at("program").at("id").get<std::string>()).state;
		}
		start(request);
	}
	catch (...)
	{
		finish({{"ok", false}});
	}
}
void Client::finish(const Json &r)
{
	const bool wasWorking = working;
	working = false;
	if (!currentProgram.empty() && programs.contains(currentProgram))
	{
		auto &p = programs.at(currentProgram);
		if (p.nextTick <= gui.game.stepCounter)
			p.nextTick = gui.game.stepCounter + p.definition.at("intervalTicks").get<unsigned>();
	}
	const auto before = programs;
	const bool ok = (cancelledOperation.empty() || !current.is_object() ||
					 current.value("id", "") != cancelledOperation) &&
					r.value("ok", false) && (!wasWorking || invocationLease == lease) && ready &&
					!lease.empty() && now() - lastLeaseAck < 10000;
	bool accepted = ok, submitted = false;
	auto result = r;
	try
	{
		if (ok)
		{
			if (!currentProgram.empty() && programs.at(currentProgram).paused)
				throw std::runtime_error("Standing order paused");
			const std::string kind = current.is_object() && current.contains("request")
										 ? current["request"].value("kind", "")
										 : "scheduled";
			if (kind == "pause" || kind == "resume" || kind == "remove")
			{
				const auto id = current["request"].at("programId").get<std::string>();
				controlStatus.erase(id);
				++controlGeneration[id];
				if (kind == "remove")
					programs.erase(id);
				else
				{
					programs.at(id).paused = kind == "pause";
					programs.at(id).nextTick = gui.game.stepCounter + 25;
				}
				if (!save())
					throw std::runtime_error("Checkpoint unavailable");
			}
			else if (kind == "install" || kind == "replace")
			{
				// Preflight also validates order ownership; it never submits those orders.
				for (auto &o : r.value("orders", Json::array()))
					Script::order(gui.game, gui.localTeamNo, value(o));
				const auto &definition = current["request"]["program"];
				Program p;
				p.definition = definition;
				p.nextTick = gui.game.stepCounter;
				p.state = r.value("installedState", Json(nullptr));
				p.initialized = r.value("installedInitialized", false);
				programs[definition.at("id").get<std::string>()] = std::move(p);
				if (!save())
					throw std::runtime_error("Checkpoint unavailable");
			}
			else
			{
				std::vector<std::shared_ptr<Order>> orders;
				for (auto &o : r.value("orders", Json::array()))
					orders.push_back(Script::order(gui.game, gui.localTeamNo, value(o)));
				if (orders.size() > 32)
					throw std::runtime_error("Too many colony orders");
				// Validate and allocate the complete batch first, then commit state and the
				// uncertainty marker before the nonthrowing queue splice. Never replay it.
				const auto previous = programs;
				if (!gui.enqueueCommanderOrders(
						orders,
						[&]
						{
							if (!currentProgram.empty())
							{
								auto &p = programs.at(currentProgram);
								p.state = r.at("state");
								p.initialized = true;
								p.random = r.at("random");
								p.recent = {{"tick", gui.game.stepCounter},
											{"output", r.value("output", Json(nullptr))}};
							}
							uncertainDispatch = uncertainDispatch || !orders.empty();
							if (save())
								return true;
							programs = previous;
							return false;
						}))
					throw std::runtime_error(
						"Colony order queue is full or its journal is unavailable");
				submitted = !orders.empty();
				for (auto &w : r.value("wakes", Json::array()))
					if (!currentProgram.empty())
					{
						const auto &p = programs.at(currentProgram);
						if (pendingWakes.size() < 32)
							pendingWakes.push_back(
								{{"eventId", uuid()},
								 {"programId", currentProgram},
								 {"revision", p.definition.at("revision")},
								 {"tick", gui.game.stepCounter},
								 {"key", w.at("key")},
								 {"reason", w.at("reason")},
								 {"data", w.value("data", Json(nullptr)).dump()}});
					}
			}
		}
	}
	catch (...)
	{
		accepted = false;
		if (!submitted)
			programs = before;
	}
	if (!accepted && !currentProgram.empty())
	{
		programs[currentProgram].paused = true;
		reports.push_back("A standing order needs attention and has been paused.");
	}
	if (current.contains("id"))
	{
		if (!accepted && current["request"].contains("programId"))
			controlStatus[current["request"]["programId"].get<std::string>()] =
				"Update failed; try again";
		Json payload = {
			{"operationId", current.at("id")},
			{"lease", lease},
			{"tick", gui.game.stepCounter},
			{"status", accepted    ? "completed"
					   : submitted ? "uncertain"
								   : "failed"},
			{"output",
			 accepted
				 ? result.value("output", Json(nullptr)).dump()
				 : result.value(
					   "diagnostic",
					   "The order could not be completed. Inspect the colony before retrying.")}};
		pendingResult = payload;
	}
	if (!save())
	{
		for (auto &[id, p] : programs)
			p.paused = true;
	}
	current = Json::object();
	currentProgram.clear();
}
void Client::update(bool caughtUp)
{
	ready = caughtUp && !globalContainer->replaying && !globalContainer->liveSpectating &&
			gui.localTeamNo >= 0 && gui.localTeamNo < gui.game.mapHeader.getNumberOfTeams() &&
			gui.game.teams[gui.localTeamNo] && gui.game.teams[gui.localTeamNo]->isAlive;
	if (!enabled)
		return;
	observations.observe();
	auto timestamp = now();
	if (working)
	{
#ifndef __EMSCRIPTEN__
		if (pending.valid() &&
			pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
		{
			try
			{
				finish(pending.get());
			}
			catch (...)
			{
				finish({{"ok", false}});
			}
		}
#else
		if (char *text = hiveResult())
		{
			try
			{
				finish(Json::parse(text));
			}
			catch (...)
			{
				finish({{"ok", false}});
			}
			std::free(text);
		}
#endif
	}
	if (!working && ready && !lease.empty() && timestamp - lastLeaseAck < 10000 &&
		current.contains("request"))
	{
		auto next = current;
		execute(next);
	}
	auto live = alive;
	if (!polling && timestamp - lastPoll >= 1000)
	{
		polling = true;
		lastPoll = timestamp;
		Json body = {{"clientId", clientId},
					 {"tick", gui.game.stepCounter},
					 {"caughtUp", ready && !working && current.empty() && pendingResult.empty()}};
		if (!lease.empty())
			body["lease"] = lease;
		rest(HttpFetch::Method::Post, base + "/poll", body,
			 [this, live, sentAt = timestamp, polledTick = gui.game.stepCounter](const auto &r)
			 {
				 if (!*live)
					 return;
				 polling = false;
				 if (!r.ok)
					 return;
				 if (now() - sentAt >= 10000)
					 return; // Never extend a lease by response transit time.
				 auto nextLease = r.result.value("lease", "");
				 if ((!lease.empty() && lease != nextLease) ||
					 (!pendingResult.empty() && pendingResult.value("lease", "") != nextLease))
				 {
					 for (auto &[id, p] : programs)
						 p.paused = true;
					 pendingResult = Json::object();
					 current = Json::object();
					 reports.push_back("The connection changed. Review your paused standing orders "
									   "before resuming.");
					 save();
				 }
				 lease = nextLease;
				 lastLeaseAck = sentAt;
				 if (r.result.value("team", -1) != gui.localTeamNo)
				 {
					 lease.clear();
					 enabled = false;
					 return;
				 }
				 if (!working && pendingResult.empty() && current.empty())
				 {
					 for (auto &record : r.result.value("programs", Json::array()))
					 {
						 auto it = programs.find(
							 record.at("definition").at("id").template get<std::string>());
						 if (it == programs.end() ||
							 it->second.definition != record.at("definition"))
						 {
							 Program missing;
							 missing.definition = record.at("definition");
							 missing.paused = true;
							 missing.missingCheckpoint = true;
							 programs[missing.definition.at("id").get<std::string>()] =
								 std::move(missing);
							 reports.push_back(
								 "A standing order needs a fresh instruction after reconnecting. "
								 "You can cancel it or ask the commander to replace it.");
							 save();
						 }
						 else if (record.at("status") != "active" ||
								  it->second.definition != record.at("definition"))
							 it->second.paused = true;
					 }
				 }
				 for (auto it = pendingWakes.begin(); it != pendingWakes.end();)
				 {
					 if (it->at("tick").template get<unsigned>() <= polledTick)
					 {
						 rest(HttpFetch::Method::Post, base + "/wake",
							  {{"lease", lease}, {"wake", *it}}, [](const auto &) {});
						 it = pendingWakes.erase(it);
					 }
					 else
						 ++it;
				 }
				 if (!working && current.empty())
					 for (auto &op : r.result.value("operations", Json::array()))
					 {
						 current = op;
						 break;
					 }
			 });
	}
	if (timestamp - lastEvents >= 1500)
	{
		lastEvents = timestamp;
		rest(HttpFetch::Method::Get, base + "/events?after=" + cursor, nullptr,
			 [this, live](const auto &r)
			 {
				 if (!*live || !r.ok)
					 return;
				 for (auto &e : r.result.value("events", Json::array()))
				 {
					 cursor = e.at("id").template get<std::string>();
					 const auto &b = e.at("body");
					 if (b.contains("text"))
					 {
						 if (e.at("kind") == "progress")
							 progress = b.at("text");
						 else
						 {
							 progress.clear();
							 reports.push_back(b.at("text"));
						 }
					 }
				 }
				 while (reports.size() > 100)
					 reports.erase(reports.begin());
			 });
	}
	if (!pendingResult.empty() && !resultSending && timestamp - lastResult >= 1000)
		deliverResult();
	if (timestamp - lastAccount >= 5000)
	{
		lastAccount = timestamp;
		rest(HttpFetch::Method::Get, "/api/v1/hive/account", nullptr,
			 [this, live](const auto &r)
			 {
				 if (*live && r.ok)
					 account = r.result;
			 });
	}
	if (!working && current.empty() && pendingResult.empty() && ready && !lease.empty() &&
		timestamp - lastLeaseAck < 10000)
	{
		for (auto &[id, p] : programs)
			if (!p.paused && gui.game.stepCounter >= p.nextTick)
			{
				p.nextTick =
					gui.game.stepCounter + p.definition.at("intervalTicks").get<unsigned>();
				currentProgram = id;
				current = Json::object();
				try
				{
					start({{"source", p.definition.at("source")},
						   {"state", p.state},
						   {"initialized", p.initialized},
						   {"random", p.random},
						   {"snapshot", snapshot()}});
				}
				catch (...)
				{
					finish({{"ok", false}});
				}
				break;
			}
	}
}
void Client::deliverResult()
{
	if (pendingResult.empty() || lease.empty() || pendingResult.value("lease", "") != lease)
		return;
	resultSending = true;
	lastResult = now();
	auto live = alive;
	auto submitted = pendingResult;
	rest(HttpFetch::Method::Post, base + "/result", submitted,
		 [this, live, submitted](const auto &r)
		 {
			 if (!*live)
				 return;
			 resultSending = false;
			 if (r.ok && pendingResult == submitted)
			 {
				 if (!r.result.value("accepted", false))
				 {
					 for (auto &[id, p] : programs)
						 p.paused = true;
					 reports.push_back("An order could not be confirmed. Review your "
									   "standing orders before resuming.");
				 }
				 pendingResult = Json::object();
				 save();
			 }
		 });
}
void Client::command(const std::string &text, bool ongoing)
{
	if (text.empty() || commandSending)
		return;
	commandDraft = text;
	if (retryCommandText != text || retryCommandOngoing != ongoing || retryCommandId.empty())
	{
		retryCommandText = text;
		retryCommandOngoing = ongoing;
		retryCommandId = uuid();
	}
	commandSending = true;
	auto live = alive;
	rest(HttpFetch::Method::Post, base + "/command",
		 {{"id", retryCommandId}, {"text", text}, {"ongoing", ongoing}},
		 [this, live, text](const auto &r)
		 {
			 if (!*live)
				 return;
			 commandSending = false;
			 if (r.ok)
			 {
				 if (commandDraft == text)
					 commandDraft.clear();
				 retryCommandId.clear();
				 reports.push_back("Order received: " + text);
			 }
			 else
				 reports.push_back("Order not confirmed. Open the commander to retry; your message "
								   "has been kept.");
		 });
}
void Client::stop()
{
	if (current.is_object() && current.contains("id"))
		cancelledOperation = current.at("id");
	auto live = alive;
	controlStatus["commander"] = "Stopping…";
	rest(HttpFetch::Method::Post, base + "/stop", Json::object(),
		 [this, live](const auto &r)
		 {
			 if (!*live)
				 return;
			 controlStatus["commander"] = r.ok ? "Stopped" : "Stop failed; retry the stop shortcut";
			 reports.push_back(controlStatus["commander"]);
		 });
}
void Client::change(const std::string &id, const std::string &action)
{
	auto it = programs.find(id);
	if (it == programs.end())
		return;
	// Apply pause locally immediately. The durable server operation completes the edit.
	if (action == "pause" || action == "remove")
	{
		it->second.paused = true;
		if (currentProgram == id)
			ready = false;
	}
	save();
	controlStatus[id] = "Updating…";
	const auto generation = ++controlGeneration[id];
	auto live = alive;
	rest(HttpFetch::Method::Post, base + "/standing-orders",
		 {{"programId", id},
		  {"expectedRevision", it->second.definition.at("revision")},
		  {"action", action}},
		 [this, live, id, generation](const auto &r)
		 {
			 if (!*live || controlGeneration[id] != generation)
				 return;
			 controlStatus[id] = r.ok ? "Requested" : "Update failed; try again";
			 if (!r.ok)
				 reports.push_back("Standing order update failed. Your order is paused locally; "
								   "retry the control.");
		 });
}
void Client::buyCredits()
{
	GAGCore::ApplicationHost::openUrl(platform.origin() + "/commander");
}
Json Client::standingOrders() const
{
	Json out = Json::array();
	for (auto &[id, p] : programs)
		out.push_back({{"id", id},
					   {"revision", p.definition.at("revision")},
					   {"name", p.definition.at("name")},
					   {"description", p.definition.at("description")},
					   {"paused", p.paused},
					   {"missingCheckpoint", p.missingCheckpoint},
					   {"controlStatus", controlStatus.contains(id) ? controlStatus.at(id) : ""}});
	return out;
}
bool Client::save()
{
	try
	{
		Json saved = {{"version", 1},
					  {"origin", platform.origin()},
					  {"team", gui.localTeamNo},
					  {"programs", Json::array()},
					  {"uncertain", working || uncertainDispatch},
					  {"pendingResult", pendingResult}};
		for (auto &[id, p] : programs)
			saved["programs"].push_back({{"definition", p.definition},
										 {"state", p.state},
										 {"initialized", p.initialized},
										 {"paused", p.paused},
										 {"missingCheckpoint", p.missingCheckpoint},
										 {"nextTick", p.nextTick},
										 {"random", p.random},
										 {"recent", p.recent}});
		auto &storage = this->storage();
		if (!storage.write("online/hive/" + match + "-" + std::to_string(gui.localPlayer) + ".json",
						   saved.dump()))
			throw std::runtime_error("Checkpoint failed");
		storage.persist();
		return true;
	}
	catch (...)
	{
		for (auto &[id, p] : programs)
			p.paused = true;
		return false;
	}
}
void Client::restore()
{
	try
	{
		std::string bytes;
		if (!storage().read(
				"online/hive/" + match + "-" + std::to_string(gui.localPlayer) + ".json", bytes))
			return;
		if (bytes.size() > 12 * 1024 * 1024)
			return;
		auto saved = Json::parse(bytes);
		if (saved.value("version", 0) != 1 || saved.value("origin", "") != platform.origin() ||
			saved.at("team") != gui.localTeamNo || saved.at("programs").size() > 8)
			return;
		pendingResult = saved.value("pendingResult", Json::object());
		uncertainDispatch = saved.value("uncertain", true);
		for (auto &entry : saved.at("programs"))
		{
			Program p;
			p.definition = entry.at("definition");
			p.state = entry.at("state");
			p.initialized = entry.at("initialized");
			p.missingCheckpoint = entry.value("missingCheckpoint", false);
			p.paused = p.missingCheckpoint || entry.value("paused", true) ||
					   saved.value("uncertain", true);
			p.nextTick = entry.at("nextTick");
			p.random = entry.at("random");
			p.recent = entry.value("recent", Json(nullptr));
			programs[p.definition.at("id").get<std::string>()] = std::move(p);
		}
	}
	catch (...)
	{
		programs.clear();
	}
}
} // namespace Hive
