// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
#include "online/PlatformClient.h"
#include "script/ScriptObservations.h"
#include <future>
#include <memory>
#include <map>
class GameGUI;
namespace Online
{
class OnlineStorage;
}
namespace Hive
{
void setExecutable(const char *path);
struct ClientEnvironment
{
	std::function<void(HttpFetch::Method, const std::string &, const Json &,
					   Online::PlatformClient::ResponseHandler)>
		request;
	std::function<Json(const Json &)> worker;
	std::function<std::uint64_t()> now;
	Online::OnlineStorage *storage = nullptr;
};
class Client
{
	GameGUI &gui;
	Online::PlatformClient &platform;
	Script::Observations observations;
	ClientEnvironment environment;
	void rest(HttpFetch::Method method, const std::string &path, const Json &body,
			  Online::PlatformClient::ResponseHandler callback);
	std::uint64_t now() const;
	Online::OnlineStorage &storage();
	std::string match, base, clientId, lease, cursor = "0";
	std::shared_ptr<bool> alive = std::make_shared<bool>(true);
	std::uint64_t lastPoll = 0, lastEvents = 0, lastLeaseAck = 0, lastAccount = 0, lastResult = 0;
	bool polling = false, enabled = false, working = false, ready = false,
		 uncertainDispatch = false, resultSending = false;
	Json pendingResult = Json::object(), pendingWakes = Json::array();
	struct Program
	{
		Json definition, state = nullptr, recent = nullptr;
		bool initialized = false, paused = false;
		unsigned nextTick = 0, random = 1;
	};
	std::map<std::string, Program> programs;
	std::future<Json> pending;
	Json current;
	std::string currentProgram, invocationLease, cancelledOperation;
	Json snapshot();
	void execute(const Json &operation);
	void start(const Json &request);
	void finish(const Json &result);
	bool save();
	void restore();
	void deliverResult();

  public:
	std::vector<std::string> reports;
	std::string progress;
	Json account = Json::object();
	Client(GameGUI &gui, Online::PlatformClient &platform, std::string match, int seat,
		   ClientEnvironment environment = {});
	~Client();
	void update(bool caughtUp);
	void command(const std::string &text, bool ongoing);
	void stop();
	void change(const std::string &id, const std::string &action);
	void buyCredits();
	bool available() const { return enabled; }
	Json standingOrders() const;
};
} // namespace Hive
