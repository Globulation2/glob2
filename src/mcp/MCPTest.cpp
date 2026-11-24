// SPDX-License-Identifier: GPL-3.0-or-later
#include "Availability.h"
#include "OnlineUIFixtures.h"
#include <chrono>
#if GLOB2_DESKTOP_MCP
// Establish WinSock2 before SDL fixtures can include Windows headers.
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "Protocol.h"
#include "Guide.h"
#include "HttpServer.h"
#include "UIService.h"
#include <ScreenStack.h>
#include <ui/Controls.h>
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "CommandLine.h"
#include "hive/LocalCommander.h"
#include "hive/HiveWorker.h"
#include "Player.h"
#include <algorithm>
#include <thread>
#include <fstream>
#include <FileManager.h>
#include <SDL3_image/SDL_image.h>
using namespace MCP;
namespace
{
struct Context : ExecutionContext
{
	std::shared_ptr<Request> submit(const std::string &name, Json args, std::string client) override
	{
		auto r = std::make_shared<Request>();
		r->tool = name;
		r->arguments = args;
		r->client = client;
		return r;
	}
};
Json modern(const std::string &method, Json params = Json::object())
{
	params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
					   {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
	return {{"jsonrpc", "2.0"}, {"id", 7}, {"method", method}, {"params", params}};
}
} // namespace
TEST_CASE("MCP/protocol adapters validate metadata and discover all tools")
{
	ToolRegistry registry;
	registerUITools(registry);
	Context context;
	Protocol p(registry, context);
	auto discover =
		p.receive(modern("server/discover").dump(), {"2026-07-28", "server/discover", ""});
	CHECK(discover.status == 200);
	CHECK(discover.body["result"]["resultType"] == "complete");
	auto listed = p.receive(modern("tools/list").dump(), {"2026-07-28", "tools/list", ""});
	CHECK(listed.body["result"]["tools"].size() == 30);
	CHECK(listed.body["result"].contains("cacheScope"));
	CHECK(
		p.receive(modern("tools/list").dump(), {"2026-07-28", "wrong", ""}).body["error"]["code"] ==
		-32020);
	auto missing = modern("tools/list");
	missing["params"]["_meta"].erase("io.modelcontextprotocol/clientCapabilities");
	CHECK(p.receive(missing.dump(), {"2026-07-28", "tools/list", ""}).body["error"]["code"] ==
		  -32602);
	auto fractional = modern("tools/list");
	fractional["id"] = 1.5;
	CHECK(p.receive(fractional.dump(), {"2026-07-28", "tools/list", ""}).body["error"]["code"] ==
		  -32600);
	auto malformed = modern("tools/list");
	malformed["params"] = 42;
	auto rejected = p.receive(malformed.dump(), {"2026-07-28", "tools/list", ""});
	CHECK(rejected.body["id"] == 7);
	CHECK(rejected.body["error"]["code"] == -32602);
	auto nullID = modern("tools/list");
	nullID["id"] = nullptr;
	CHECK(p.receive(nullID.dump(), {"2026-07-28", "tools/list", ""}).body["error"]["code"] ==
		  -32600);
	CHECK(p.receive("[]", {}).body["error"]["code"] == -32600);
	CHECK(p.receive("{", {}).body["error"]["code"] == -32700);
	CHECK(p.receive(modern("unknown").dump(), {"2026-07-28", "unknown", ""}).status == 404);
	CHECK(p.receive(modern("tools/list").dump(), {"2099-01-01", "tools/list", ""})
			  .body["error"]["code"] == -32022);
	Json legacy = {{"jsonrpc", "2.0"},
				   {"id", 1},
				   {"method", "initialize"},
				   {"params",
					{{"protocolVersion", "2025-11-25"},
					 {"capabilities", Json::object()},
					 {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}};
	CHECK(p.receive(legacy.dump(), {}).body["result"]["protocolVersion"] == "2025-11-25");
	Json badLegacy = {
		{"jsonrpc", "2.0"},
		{"id", 99},
		{"method", "tools/call"},
		{"params",
		 {{"name", "ui_status"}, {"_meta", {{"io.modelcontextprotocol/clientInfo", 42}}}}}};
	auto badMetadata = p.receive(badLegacy.dump(), {"2025-11-25", "", ""});
	CHECK(badMetadata.body["id"] == 99);
	CHECK(badMetadata.body["error"]["code"] == -32602);
	CHECK(p.receive(R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
					{"2025-11-25", "", ""})
			  .status == 202);
	auto call = modern("tools/call",
					   {{"name", "ui_move"}, {"arguments", {{"token", "x"}, {"x", 1}, {"y", 2}}}});
	CHECK(p.receive(call.dump(), {"2026-07-28", "tools/call", "ui_move"}).pending);
	CHECK(p.receive(call.dump(), {"2026-07-28", "tools/call", "=?base64?dWlfbW92ZQ==?="}).pending);
	call["params"]["arguments"]["x"] = -1;
	CHECK(p.receive(call.dump(), {"2026-07-28", "tools/call", "ui_move"}).body["error"]["code"] ==
		  -32602);
	CHECK(allowedHost("127.0.0.1:27202", 27202));
	CHECK_FALSE(allowedHost("evil.com:27202", 27202));
	CHECK(allowedOrigin("", 27202));
	CHECK_FALSE(allowedOrigin("null", 27202));
	CHECK_FALSE(allowedOrigin("https://evil.com", 27202));
	CHECK(p.receive(call.dump(), {"2026-07-28", "tools/call", "=?base64?dWlfbW92ZQ===?="})
			  .body["error"]["code"] == -32020);
	auto duplicate = p.receive(modern("tools/list").dump(), {"2026-07-28", "tools/list", "", true});
	CHECK(duplicate.body["id"] == 7);
	CHECK(duplicate.body["error"]["code"] == -32020);
	registry.add({"broken",
				  "Broken extension handler",
				  {{"type", "object"}},
				  {{"type", "object"}},
				  Json::object(),
				  [](ExecutionContext &, Json, std::string) { return std::shared_ptr<Request>{}; },
				  {}});
	CHECK_THROWS_AS(registry.call(context, "broken", Json::object(), "test"), std::logic_error);
	auto keyArguments = [](const std::string &key, Json modifiers = Json::array())
	{ return Json{{"token", "controller"}, {"key", key}, {"modifiers", std::move(modifiers)}}; };
	CHECK(registry.call(context, "ui_key", keyArguments("Return"), "test"));
	// Exercise the immutable validator from a worker, using SDL's ASCII aliases.
	auto workerValidation =
		std::async(std::launch::async,
				   [&]
				   {
					   return registry.call(
						   context, "ui_key",
						   keyArguments("a", Json::array({"left CTRL", "rIgHt sHiFt"})), "worker");
				   });
	CHECK(workerValidation.get());
	CHECK_THROWS_AS(registry.call(context, "ui_key", keyArguments("Imaginary Key"), "test"),
					std::invalid_argument);
	CHECK_THROWS_AS(registry.call(context, "ui_key", keyArguments("A", Json::array({"A"})), "test"),
					std::invalid_argument);
	CHECK_THROWS_AS(registry.call(context, "ui_key",
								  keyArguments("A", Json::array({"Missing Modifier"})), "test"),
					std::invalid_argument);
	CHECK_THROWS_AS(registry.call(context, "ui_key", keyArguments(std::string("A\0", 2)), "test"),
					std::invalid_argument);
	CHECK(randomToken() != randomToken());
}
TEST_CASE("MCP/tool schemas are well formed for strict clients")
{
	// Strict clients (Claude Code among them) reject the whole tool list when any
	// object schema carries null properties or required members.
	ToolRegistry registry;
	registerUITools(registry);
	Context context;
	Protocol p(registry, context);
	auto requireWellFormed = [](auto &&self, const Json &schema, const std::string &path) -> void
	{
		INFO(path);
		REQUIRE(schema.is_object());
		if (schema.value("type", "") == "object")
		{
			CHECK(schema.contains("properties"));
			CHECK(schema.value("properties", Json()).is_object());
			if (schema.contains("required"))
				CHECK(schema["required"].is_array());
			const Json properties = schema.value("properties", Json::object());
			for (const auto &[key, property] : properties.items())
				self(self, property, path + "." + key);
		}
		if (schema.contains("items"))
			self(self, schema["items"], path + "[]");
	};
	auto checkListed = [&](const Json &tools)
	{
		REQUIRE(tools.is_array());
		REQUIRE_FALSE(tools.empty());
		for (const auto &tool : tools)
		{
			requireWellFormed(requireWellFormed, tool["inputSchema"], tool["name"].get<std::string>());
			if (tool.contains("outputSchema"))
				requireWellFormed(requireWellFormed, tool["outputSchema"],
								  tool["name"].get<std::string>() + "->");
		}
	};
	checkListed(p.receive(modern("tools/list").dump(), {"2026-07-28", "tools/list", ""})
					.body["result"]["tools"]);
	const Json legacyList = {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/list"}};
	checkListed(p.receive(legacyList.dump(), {"2025-11-25", "", ""}).body["result"]["tools"]);
}
TEST_CASE("MCP/argument errors name the field and the broken rule")
{
	ToolRegistry registry;
	registerUITools(registry);
	Context context;
	auto message = [&](const std::string &tool, const Json &arguments)
	{
		try
		{
			registry.call(context, tool, arguments, "test");
		}
		catch (const std::invalid_argument &error)
		{
			return std::string(error.what());
		}
		return std::string("accepted");
	};
	// Drag points are objects; a client sending coordinate pairs needs to see why.
	const auto pairs = message("ui_drag", {{"token", "t"}, {"points", {{1, 2}, {3, 4}}}});
	CHECK(pairs.find("ui_drag.points[]") != std::string::npos);
	CHECK(pairs.find("expected object") != std::string::npos);
	const auto missing = message("ui_drag", {{"token", "t"}, {"points", {{{"x", 1}}, {{"x", 2}, {"y", 3}}}}});
	CHECK(missing.find("ui_drag.points[]") != std::string::npos);
	CHECK(missing.find("missing required property \"y\"") != std::string::npos);
	const auto unknown = message("ui_status", {{"token", "t"}});
	CHECK(unknown.find("unknown property \"token\"") != std::string::npos);
	const auto range = message("ui_move", {{"token", "t"}, {"x", -1}, {"y", 2}});
	CHECK(range.find("ui_move.x") != std::string::npos);
	CHECK(range.find("out of range") != std::string::npos);
	const auto choice = message("ui_click", {{"token", "t"}, {"x", 1}, {"y", 2}, {"button", "thumb"}});
	CHECK(choice.find("ui_click.button") != std::string::npos);
	CHECK(choice.find("expected one of") != std::string::npos);
}
TEST_CASE("MCP/initialize and discover describe the game and link its source for both versions")
{
	ToolRegistry registry;
	registerUITools(registry);
	Context context;
	Protocol p(registry, context);
	Json legacy = {{"jsonrpc", "2.0"},
				   {"id", 1},
				   {"method", "initialize"},
				   {"params",
					{{"protocolVersion", "2025-11-25"},
					 {"capabilities", Json::object()},
					 {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}};
	const auto initialized = p.receive(legacy.dump(), {}).body["result"];
	const auto discovered =
		p.receive(modern("server/discover").dump(), {"2026-07-28", "server/discover", ""})
			.body["result"];
	for (const auto &result : {initialized, discovered})
	{
		REQUIRE(result["instructions"].is_string());
		const auto text = result["instructions"].get<std::string>();
		// An agent that knows nothing must learn what the game is, where to start
		// and where the source and documentation live.
		CHECK(text.find("Globulation 2") != std::string::npos);
		CHECK(text.find("game_guide") != std::string::npos);
		CHECK(text.find("ui_acquire_control") != std::string::npos);
		CHECK(text.find(RepositoryUrl) != std::string::npos);
		CHECK(text.find(ServerGuideUrl) != std::string::npos);
		CHECK(result["serverInfo"]["name"] == "globulation2-ui");
		CHECK(result["serverInfo"]["websiteUrl"] == RepositoryUrl);
		CHECK_FALSE(result["serverInfo"].value("description", "").empty());
	}
	// Modern results still carry server metadata alongside the instructions.
	CHECK(discovered["_meta"]["io.modelcontextprotocol/serverInfo"]["websiteUrl"] ==
		  RepositoryUrl);
}
TEST_CASE("MCP/tools explain themselves within a discovery size budget")
{
	ToolRegistry registry;
	registerUITools(registry);
	const auto tools = registry.list();
	bool guide = false;
	for (const auto &tool : tools)
	{
		INFO(tool["name"].get<std::string>());
		// At least a sentence of purpose; one-line fragments left agents guessing.
		CHECK(tool["description"].get<std::string>().size() >= 60);
		CHECK(tool["inputSchema"]["type"] == "object");
		if (tool.contains("outputSchema"))
			CHECK(tool["outputSchema"].contains("properties"));
		const auto &properties = tool["inputSchema"]["properties"];
		if (properties.contains("token"))
			CHECK_FALSE(properties["token"].value("description", "").empty());
		guide = guide || tool["name"] == "game_guide";
	}
	CHECK(guide);
	// Every client pays for discovery in context; keep it bounded. 25 KB covers the 29
	// tools including the game_* map and time tools.
	const auto bytes = Json{{"tools", tools}}.dump().size();
	INFO("tools/list bytes: " << bytes);
	CHECK(bytes < 25000);
	// Time steps and zoom are bounded by the schema, before reaching the game.
	Context context;
	CHECK_THROWS_WITH_AS(registry.call(context, "game_time", {{"action", "step"}, {"ticks", 0}}, "test"),
						 doctest::Contains("ticks"), std::invalid_argument);
	CHECK_THROWS_AS(registry.call(context, "game_time", {{"action", "rewind"}}, "test"),
					std::invalid_argument);
	CHECK_THROWS_WITH_AS(registry.call(context, "game_view", {{"token", "t"}, {"x", 1}, {"y", 1}, {"zoom", 6}}, "test"),
						 doctest::Contains("zoom"), std::invalid_argument);
}
TEST_CASE("MCP/game guide serves every topic with links and rejects unknown topics")
{
	ToolRegistry registry;
	registerUITools(registry);
	Context context;
	auto read = [&](Json args)
	{
		auto request = registry.call(context, "game_guide", std::move(args), "test");
		auto future = request->completion.get_future();
		REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
		return future.get();
	};
	const auto overview = read(Json::object());
	CHECK(overview.error.empty());
	CHECK(overview.data["topic"] == "overview");
	REQUIRE(guideTopics().size() >= 8);
	for (const auto &topic : guideTopics())
	{
		INFO(topic);
		const auto result = read({{"topic", topic}});
		CHECK(result.data["topic"] == topic);
		CHECK(result.data["links"]["repository"] == RepositoryUrl);
		CHECK(result.data["links"]["mcp_server_guide"] == ServerGuideUrl);
		CHECK_FALSE(result.data["links"].value("tutorial_lessons", "").empty());
		CHECK(result.data["other_topics"].size() == guideTopics().size() - 1);
		if (topic != "links")
			CHECK(result.data["text"].get<std::string>().size() > 200);
	}
	CHECK_THROWS_WITH_AS(registry.call(context, "game_guide", {{"topic", "secrets"}}, "test"),
						 doctest::Contains("overview"), std::invalid_argument);
	// The guide needs no control token and is reachable over the protocol.
	Protocol p(registry, context);
	auto call = modern("tools/call", {{"name", "game_guide"}, {"arguments", {{"topic", "hud"}}}});
	CHECK(p.receive(call.dump(), {"2026-07-28", "tools/call", "game_guide"}).pending);
}
TEST_CASE("MCP/compact inspection keeps meaning and drops layout noise")
{
	const Json box = {{"x", 0}, {"y", 0}, {"w", 10}, {"h", 10}};
	auto control = [&](std::string key, std::string label, Json actions,
					   Json children = Json::array())
	{
		return Json{{"key", key},		  {"label", label},	  {"role", "button"},
					{"actions", actions}, {"revision", 4},	  {"enabled", true},
					{"blocked", false},	  {"focused", false}, {"selected", false},
					{"value", nullptr},	  {"bounds", box},	  {"visible", box},
					{"children", children}};
	};
	auto hidden = control("menu/9", "Hidden", {"activate"});
	hidden["visible"] = {{"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}};
	auto disabled = control("menu/2", "Load", {"activate"});
	disabled["enabled"] = false;
	auto container = control("", "", Json::array(),
							 {control("menu/1", "Tutorial", {"activate"}), hidden, disabled,
							  control("", "Caption", Json::array())});
	container["role"] = "column";
	Json surface = {{"instance", 3}, {"revision", 1}, {"bounds", box}, {"viewport", box}};
	surface["controls"] = Json::array({container});
	const Json full = {{"screen", "main_menu"}, {"surfaces", Json::array({surface})}};
	const auto compact = compactInspection(full);
	CHECK(compact["screen"] == "main_menu");
	CHECK(compact["surfaces"][0]["instance"] == 3);
	const auto &controls = compact["surfaces"][0]["controls"];
	// The keyless, labelless, inert column is replaced by its children.
	REQUIRE(controls.size() == 4);
	CHECK(controls[0] == Json{{"key", "menu/1"},
							  {"label", "Tutorial"},
							  {"role", "button"},
							  {"actions", {"activate"}},
							  {"revision", 4},
							  {"bounds", box}});
	CHECK(controls[1]["hidden"] == true);
	CHECK(controls[2]["enabled"] == false);
	CHECK(controls[3]["label"] == "Caption");
	CHECK_FALSE(controls[3].contains("revision"));
	CHECK(compact.dump().size() < full.dump().size() / 2);
}
TEST_CASE("MCP/rejected semantic targets say which check failed and what to do")
{
	const Json box = {{"x", 0}, {"y", 0}, {"w", 10}, {"h", 10}};
	auto item = [&](std::string key, int revision)
	{
		return Json{{"key", key},
					{"label", key},
					{"actions", {"activate"}},
					{"revision", revision},
					{"enabled", true},
					{"blocked", false},
					{"visible", box},
					{"children", Json::array()}};
	};
	Json first = {{"instance", 5}};
	first["controls"] = Json::array({item("start", 7), item("quit", 2)});
	Json second = {{"instance", 6}};
	second["controls"] = Json::array({item("ok", 1)});
	Json snapshot = {{"screen", "main_menu"}, {"surfaces", Json::array({first, second})}};
	auto act = [](int surface, std::string key, int revision, std::string action = "activate")
	{
		return Json{{"surface", surface}, {"key", key}, {"revision", revision}, {"action", action}};
	};
	auto problem = [&](const Json &arguments) { return actionTargetProblem(snapshot, arguments); };
	auto contains = [](const std::string &text, std::initializer_list<const char *> parts)
	{
		INFO(text);
		for (const auto *part : parts)
			if (text.find(part) == std::string::npos)
				return false;
		return true;
	};
	CHECK(problem(act(5, "start", 7)).empty());
	CHECK(contains(problem(act(9, "start", 7)), {"surface 9", "5, 6", "ui_inspect"}));
	CHECK(contains(problem(act(5, "ok", 1)), {"\"ok\"", "surface 6"}));
	CHECK(contains(problem(act(5, "missing", 1)), {"\"missing\"", "ui_inspect"}));
	CHECK(contains(problem(act(5, "start", 6)), {"revision 7", "not 6"}));
	CHECK(contains(problem(act(5, "start", 7, "set_value")), {"[\"activate\"]", "set_value"}));
	auto &start = snapshot["surfaces"][0]["controls"][0];
	start["enabled"] = false;
	CHECK(contains(problem(act(5, "start", 7)), {"Disabled"}));
	start["enabled"] = true;
	start["visible"]["h"] = 0;
	CHECK(contains(problem(act(5, "start", 7)), {"Hidden", "scroll"}));
	start["visible"] = box;
	start["blocked"] = true;
	CHECK(contains(problem(act(5, "start", 7)), {"Blocked", "dialog"}));
}
TEST_CASE("MCP/argument errors name the field and suggest valid input")
{
	ToolRegistry registry;
	registerUITools(registry);
	Context context;
	auto message = [&](const std::string &tool, Json args)
	{
		try
		{
			registry.call(context, tool, std::move(args), "test");
		}
		catch (const std::invalid_argument &e)
		{
			return std::string(e.what());
		}
		return std::string();
	};
	const auto key = message("ui_key", {{"token", "t"}, {"key", "Enter key"}});
	CHECK(key.find("Enter key") != std::string::npos);
	CHECK(key.find("Return") != std::string::npos);
	const auto modifier =
		message("ui_key", {{"token", "t"}, {"key", "A"}, {"modifiers", Json::array({"A"})}});
	CHECK(modifier.find("Left Ctrl") != std::string::npos);
	const auto empty = message("ui_wait", {{"condition", Json::object()}});
	CHECK(empty.find("game_session") != std::string::npos);
	const auto state = message("ui_wait", {{"condition", {{"enabled", true}}}});
	CHECK(state.find("condition.key") != std::string::npos);
	const auto program = message("hive_program", {{"action", "inspect"}});
	CHECK(program.find("hive_program.name") != std::string::npos);
	CHECK(message("no_such_tool", Json::object()).find("tools/list") != std::string::npos);
}
namespace
{
// These completions must be ready after the explicit pump/dispatch/frame above them.
// Check readiness before get(): a broken completion path should fail, never hang.
Result completed(std::future<Result> &future)
{
	REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
	return future.get();
}

void waitForListener(UIService &service, ListenerState expected)
{
	const auto deadline = Clock::now() + std::chrono::seconds(2);
	do
	{
		std::vector<SDL_Event> cleanup;
		service.pump(cleanup, true, false);
		if (service.listenerState() == expected && service.retiringServers.empty())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while (Clock::now() < deadline);
	REQUIRE(service.retiringServers.empty());
	REQUIRE(service.listenerState() == expected);
}

unsigned short unusedLoopbackPort()
{
	boost::asio::io_context io;
	boost::asio::ip::tcp::acceptor reservation(io,
											   {boost::asio::ip::make_address_v4("127.0.0.1"), 0});
	return reservation.local_endpoint().port();
}
} // namespace

TEST_CASE("MCP/HTTP restrictions bounded bodies restart and disconnect cancellation [network]")
{
	namespace asio = boost::asio;
	namespace http = boost::beast::http;
	using Tcp = asio::ip::tcp;
	ToolRegistry registry;
	registerUITools(registry);
	struct Immediate : Context
	{
		std::atomic<bool> hold{false};
		std::atomic<bool> cancelledFailure{false};
		// The shared_ptr atomic specialization is unavailable in Apple's libc++.
		std::shared_ptr<Request> last;
		std::shared_ptr<Request> submit(const std::string &name, Json args,
										std::string client) override
		{
			auto r = Context::submit(name, args, client);
			if (hold)
				std::atomic_store(&last, r);
			else
			{
				Result result;
				if (cancelledFailure)
				{
					r->cancelled = true; // UI errors retire pending consumer work.
					result.error = "Controller lease lost";
				}
				r->completion.set_value(std::move(result));
			}
			return r;
		}
	} context;
	Protocol protocol(registry, context);
	auto server = std::make_unique<HttpServer>(protocol, 0);
	const auto port = static_cast<unsigned short>(server->boundPort());
	const auto host = "localhost:" + std::to_string(port);
	auto send = [&](std::string host, std::string origin, std::string body,
					http::verb method = http::verb::post,
					std::vector<std::pair<std::string, std::string>> extraHeaders = {},
					std::string accept = "application/json, text/event-stream",
					std::string contentType = "application/json",
					std::string mcpMethod = "tools/list", std::string toolName = "")
	{
		asio::io_context io;
		Tcp::socket socket(io);
		socket.connect({asio::ip::make_address_v4("127.0.0.1"), port});
		http::request<http::string_body> request(method, "/mcp", 11);
		request.set("Host", host);
		if (!origin.empty())
			request.set("Origin", origin);
		request.set("Content-Type", contentType);
		request.set("Accept", accept);
		request.set("MCP-Protocol-Version", "2026-07-28");
		request.set("Mcp-Method", mcpMethod);
		if (!toolName.empty())
			request.set("Mcp-Name", toolName);
		for (const auto &[name, value] : extraHeaders)
			request.insert(name, value);
		request.body() = body.size() > 1024 * 1024 ? "" : body;
		request.prepare_payload();
		if (body.size() > 1024 * 1024)
			request.set(http::field::content_length, std::to_string(body.size()));
		http::write(socket, request);
		boost::beast::flat_buffer buffer;
		http::response<http::string_body> reply;
		http::read(socket, buffer, reply);
		return reply;
	};
	CHECK(send(host, "", modern("tools/list").dump()).result_int() == 200);
	CHECK(send("evil.example:" + std::to_string(port), "", modern("tools/list").dump())
			  .result_int() == 403);
	CHECK(send(host, "https://evil.example", modern("tools/list").dump()).result_int() == 403);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {{"Origin", ""}})
			  .result_int() == 403);
	CHECK(send(host, "http://localhost:" + std::to_string(port), modern("tools/list").dump(),
			   http::verb::post, {{"Origin", "http://localhost:" + std::to_string(port)}})
			  .result_int() == 403);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post,
			   {{"Mcp-Method", "tools/list"}})
			  .result_int() == 400);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {},
			   "xapplication/jsonx, text/event-stream")
			  .result_int() == 406);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {},
			   "application/json;q=0, text/event-stream")
			  .result_int() == 406);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {},
			   "application/json;q=invalid, text/event-stream")
			  .result_int() == 406);
	for (const auto quality : {"0.5", "1", "1e-1"})
		CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {},
				   std::string("application/json;q=") + quality + ", text/event-stream")
				  .result_int() == 200);
	for (const auto quality : {"+1", "1junk", "nan", "1.1", "-0.1", ""})
		CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {},
				   std::string("application/json;q=") + quality + ", text/event-stream")
				  .result_int() == 406);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post, {},
			   "APPLICATION/JSON, TEXT/EVENT-STREAM", "APPLICATION/JSON; charset=utf-8")
			  .result_int() == 200);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post,
			   {{"Accept", "text/event-stream"}}, "application/json")
			  .result_int() == 200);
	CHECK(send(host, "", modern("tools/list").dump(), http::verb::post,
			   {{"Content-Type", "application/json"}})
			  .result_int() == 415);
	CHECK(send(host, "", "", http::verb::get).result_int() == 405);
	CHECK(send(host, "", "{").result_int() == 400);
	CHECK(send(host, "", std::string(1024 * 1024 + 1, ' ')).result_int() == 413);
	server.reset();
	server = std::make_unique<HttpServer>(protocol, port);
	CHECK(send(host, "", modern("tools/list").dump()).result_int() == 200);
	context.cancelledFailure = true;
	auto failedCall =
		send(host, "",
			 modern("tools/call",
					{{"name", "ui_move"}, {"arguments", {{"token", "lost"}, {"x", 1}, {"y", 2}}}})
				 .dump(),
			 http::verb::post, {}, "application/json, text/event-stream", "application/json",
			 "tools/call", "ui_move");
	REQUIRE(failedCall.result_int() == 200);
	const auto failure = Json::parse(failedCall.body());
	CHECK(failure["id"] == 7);
	CHECK(failure["result"]["isError"] == true);
	CHECK(failure["result"]["structuredContent"]["error"] == "Controller lease lost");
	context.cancelledFailure = false;
	context.hold = true;
	auto pending = [&](bool shutdown)
	{
		asio::io_context io;
		Tcp::socket socket(io);
		socket.connect({asio::ip::make_address_v4("127.0.0.1"), port});
		http::request<http::string_body> request(http::verb::post, "/mcp", 11);
		request.set("Host", host);
		request.set("Content-Type", "application/json");
		request.set("Accept", "application/json, text/event-stream");
		request.set("MCP-Protocol-Version", "2026-07-28");
		request.set("Mcp-Method", "tools/call");
		request.set("Mcp-Name", "ui_move");
		request.body() = modern("tools/call", {{"name", "ui_move"},
											   {"arguments", {{"token", "x"}, {"x", 1}, {"y", 2}}}})
							 .dump();
		request.prepare_payload();
		http::write(socket, request);
		const auto until = Clock::now() + std::chrono::seconds(2);
		while (!std::atomic_load(&context.last) && Clock::now() < until)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		REQUIRE(std::atomic_load(&context.last));
		auto held = std::atomic_load(&context.last);
		if (shutdown)
		{
			server.reset();
			CHECK(held->cancelled);
		}
		return held;
	};
	auto cancelled = pending(false); // Connection destruction is the cancellation signal.
	const auto until = Clock::now() + std::chrono::seconds(2);
	while (!cancelled->cancelled && Clock::now() < until)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	CHECK(cancelled->cancelled);
	CHECK_THROWS_AS(HttpServer(protocol, port), boost::system::system_error);
	std::atomic_store(&context.last, std::shared_ptr<Request>{});
	(void)pending(true);
}
TEST_CASE("MCP/leases input dispatch cancellation viewport and capture [network]")
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .width = 640, .height = 480, .beforeLoad = [](GlobalContainer &g) {
			 g.settings.rendererBackend = GAGCore::RendererBackend::Software;
		 }});
	auto &gfx = *globalContainer->gfx;
	UIService service(gfx);
	const auto port = unusedLoopbackPort();
	service.configure(true, port);
	waitForListener(service, ListenerState::Running);
	REQUIRE(service.status() == "Running");
	auto execute = [&](const std::string &tool, Json args = Json::object())
	{
		auto request = service.submit(tool, args, "test client");
		auto result = request->completion.get_future();
		std::vector<SDL_Event> events;
		service.pump(events, true);
		service.afterDispatch();
		REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
		return completed(result);
	};
	auto acquired = execute("ui_acquire_control");
	CHECK_FALSE(acquired.data.contains("inspection"));
	const auto token = acquired.data["token"].get<std::string>();
	// Refusals say how to proceed, not only what failed.
	const auto contested = execute("ui_acquire_control").error;
	CHECK(contested.find("test client") != std::string::npos);
	CHECK(contested.find("Revoke control") != std::string::npos);
	const auto lost = execute("ui_move", {{"token", "wrong"}, {"x", 10}, {"y", 20}}).error;
	CHECK(lost.find("ui_acquire_control") != std::string::npos);
	const auto outside = execute("ui_click", {{"token", token}, {"x", 5000}, {"y", 20}}).error;
	CHECK(outside.find("640x480") != std::string::npos);
	CHECK(outside.find("capture_id") != std::string::npos);
	auto request =
		service.submit("ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}}, "test");
	auto future = request->completion.get_future();
	std::vector<SDL_Event> events;
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	CHECK(events[0].type == SDL_EVENT_KEY_DOWN);
	CHECK(future.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	service.afterDispatch();
	REQUIRE(completed(future).error.empty());
	service.revoke();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	CHECK(events[0].type == SDL_EVENT_KEY_UP);
	service.afterDispatch();
	auto next = execute("ui_acquire_control").data["token"].get<std::string>();
	auto post = service.submit(
		"ui_move", {{"token", next}, {"x", 20}, {"y", 30}, {"capture_after", true}}, "test");
	auto postResult = post->completion.get_future();
	events.clear();
	service.pump(events, true);
	service.afterDispatch();
	CHECK(postResult.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	gfx.drawFilledRect(0, 0, 640, 480, GAGCore::Color(10, 20, 30));
	gfx.nextFrame();
	REQUIRE(completed(postResult).pixels);
	CHECK_FALSE(execute("ui_click", {{"token", next}, {"x", 0}, {"y", 0}, {"viewport_id", 999}})
					.error.empty());
	auto cancelled =
		service.submit("ui_drag",
					   {{"token", next},
						{"points", Json::array({{{"x", 10}, {"y", 20}}, {{"x", 50}, {"y", 60}}})}},
					   "test");
	auto stopped = cancelled->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 2);
	service.afterDispatch();
	cancelled->cancelled = true;
	events.clear();
	service.pump(events, true);
	CHECK_FALSE(completed(stopped).error.empty());
	REQUIRE(events.size() == 1);
	CHECK(events[0].type == SDL_EVENT_MOUSE_BUTTON_UP);
	service.afterDispatch();
	auto image = service.submit("ui_screenshot", Json::object(), "test");
	auto pixels = image->completion.get_future();
	events.clear();
	service.pump(events, true);
	gfx.drawFilledRect(0, 0, 640, 20, GAGCore::Color(255, 0, 0));
	gfx.nextFrame();
	REQUIRE(pixels.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
	auto capture = completed(pixels);
	REQUIRE(capture.pixels);
	CHECK(capture.pixels->w == 640);
	CHECK(capture.pixels->h == 480);
	auto pending = service.submit("ui_move", {{"token", next}, {"x", 20}, {"y", 20}}, "test");
	auto pendingResult = pending->completion.get_future();
	service.configure(false, port);
	CHECK_FALSE(completed(pendingResult).error.empty());
	CHECK(service.status() == "Disabled");
	service.configure(true, port);
	waitForListener(service, ListenerState::Running);
	auto lease = execute("ui_acquire_control").data["token"].get<std::string>();
	service.renewed -= std::chrono::seconds(299);
	const auto beforeAnonymous = service.renewed;
	CHECK(execute("ui_inspect").error.empty());
	CHECK(service.renewed == beforeAnonymous);
	// Renewal is checked against a mark taken after backdating the lease, so
	// the checks below never bound a wall-clock duration.
	auto backdated = Clock::now();
	CHECK(execute("ui_inspect", {{"token", lease}}).error.empty());
	CHECK(service.renewed >= backdated);
	service.renewed -= std::chrono::seconds(299);
	backdated = Clock::now();
	auto duringHumanInput = service.submit("ui_inspect", {{"token", lease}}, "test");
	auto inspectedDuringInput = duringHumanInput->completion.get_future();
	events.clear();
	SDL_Event humanMotion{};
	humanMotion.type = SDL_EVENT_MOUSE_MOTION;
	humanMotion.motion.x = 10;
	humanMotion.motion.y = 20;
	events.push_back(humanMotion);
	service.pump(events, true, false); // Buffered human frames do not admit remote actions.
	CHECK(completed(inspectedDuringInput).error.empty());
	CHECK(service.renewed >= backdated);
	auto renewalDuringInput = service.submit("ui_renew_control", {{"token", lease}}, "test");
	auto renewedDuringInput = renewalDuringInput->completion.get_future();
	service.pump(events, true, false);
	CHECK(completed(renewedDuringInput).error.empty());
	events.clear();
	SDL_Event focusLost{};
	focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	events.push_back(focusLost);
	service.pump(events, true);
	service.afterDispatch();
	CHECK(execute("ui_renew_control", {{"token", lease}}).error.empty());
	auto scaled =
		service.submit("ui_screenshot", {{"max_width", 320}, {"max_height", 240}}, "test");
	auto scaledPixels = scaled->completion.get_future();
	events.clear();
	service.pump(events, true);
	gfx.nextFrame();
	auto scaledResult = completed(scaledPixels);
	REQUIRE(scaledResult.error.empty());
	CHECK(scaledResult.data["width"] == 320);
	CHECK(scaledResult.data["height"] == 240);
	CHECK(scaledResult.pixels->w == 640); // Resizing/encoding belongs to the HTTP worker.
	const auto captureId = scaledResult.data.at("capture_id");
	auto mapped = service.submit(
		"ui_move", {{"token", lease}, {"capture_id", captureId}, {"x", 50}, {"y", 30}}, "test");
	auto mappedDone = mapped->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	CHECK(events[0].motion.x == 100);
	CHECK(events[0].motion.y == 60);
	service.afterDispatch();
	CHECK(completed(mappedDone).error.empty());
	CHECK_FALSE(
		execute("ui_move", {{"token", lease}, {"capture_id", captureId}, {"x", 320}, {"y", 0}})
			.error.empty());

	execute("ui_key", {{"token", lease}, {"key", "Left Ctrl"}, {"action", "down"}});
	service.keys[SDL_SCANCODE_LCTRL] -= std::chrono::seconds(6);
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	CHECK(events[0].type == SDL_EVENT_KEY_UP);
	service.afterDispatch();
	service.humanKeys.insert(SDL_SCANCODE_A);
	execute("ui_key", {{"token", lease}, {"key", "A"}, {"action", "down"}});
	service.revoke();
	events.clear();
	service.pump(events, true);
	CHECK(events.empty()); // The human still owns A: revocation must not emit its key-up.
	service.humanKeys.clear();
	lease = execute("ui_acquire_control").data["token"].get<std::string>();
	service.renewed -= std::chrono::seconds(299);
	const auto keyBackdated = Clock::now();
	REQUIRE(execute("ui_key", {{"token", lease}, {"key", "A"}}).error.empty());
	CHECK(service.renewed >= keyBackdated);
	service.renewed -= std::chrono::seconds(301);
	CHECK_FALSE(execute("ui_key", {{"token", lease}, {"key", "A"}}).error.empty());
	std::vector<std::future<Result>> queued;
	for (int i = 0; i < 65; ++i)
		queued.push_back(
			service.submit("ui_status", Json::object(), "test")->completion.get_future());
	CHECK_FALSE(completed(queued.back()).error.empty());
	service.configure(false, port);
	for (std::size_t i = 0; i < 64; ++i)
		CHECK_FALSE(completed(queued[i]).error.empty());
	waitForListener(service, ListenerState::Disabled);
	boost::asio::io_context io;
	boost::asio::ip::tcp::acceptor conflict(io,
											{boost::asio::ip::make_address_v4("127.0.0.1"), port});
	service.configure(true, port);
	waitForListener(service, ListenerState::Error);
	CHECK(service.status().starts_with("Error:"));
	conflict.close();
	service.configure(true, port, true);
	waitForListener(service, ListenerState::Running);
	CHECK(service.status() == "Running");
}

TEST_CASE("MCP/server stays off during a quick match among players without MCP [network]")
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .width = 640, .height = 480, .beforeLoad = [](GlobalContainer &g) {
			 g.settings.rendererBackend = GAGCore::RendererBackend::Software;
		 }});
	UIService service(*globalContainer->gfx);
	const int port = unusedLoopbackPort();
	// Switching MCP on while the match starts or runs must not open the listener.
	auto hold = std::make_unique<UnassistedMatchHold>();
	service.configure(true, port);
	CHECK(service.listenerState() == ListenerState::Disabled);
	// The match ends: the next configure (every frame) starts it.
	hold.reset();
	CHECK(!suspendedForUnassistedMatch());
	service.configure(true, port);
	waitForListener(service, ListenerState::Running);
	CHECK(service.listenerState() == ListenerState::Running);
	// A match starting with MCP already on stops it again, until its last hold goes.
	hold = std::make_unique<UnassistedMatchHold>();
	{
		UnassistedMatchHold results;
		hold.reset();
		service.configure(true, port);
		CHECK(service.listenerState() == ListenerState::Disabled);
	}
	CHECK(!suspendedForUnassistedMatch());
}

TEST_CASE("MCP/a quick match among players without MCP holds the server off until its result goes")
{
	glob2test::HeadlessGlobals globals({.loadStrings = true});
	REQUIRE(!suspendedForUnassistedMatch());
	{
		auto match = OnlineUIFixtures::startingMatch();
		// From assignment, before the engine exists: the start screen is covered too.
		CHECK(suspendedForUnassistedMatch());
		auto result = match->result();
		match.reset();
		// The results screen keeps the result, and with it the hold.
		CHECK(suspendedForUnassistedMatch());
	}
	CHECK(!suspendedForUnassistedMatch());
	{
		auto assisted = OnlineUIFixtures::startingMatch(true);
		CHECK(!suspendedForUnassistedMatch());
	}
}

TEST_CASE("MCP/input boundaries preserve observation and cancel truncated batches [network]")
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .width = 640, .height = 480, .beforeLoad = [](GlobalContainer &g) {
			 g.settings.rendererBackend = GAGCore::RendererBackend::Software;
		 }});
	UIService service(*globalContainer->gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	REQUIRE(service.listenerState() == ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", Json::object(), "boundary client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();

	auto maintenance = service.submit("ui_status", Json::object(), "observer");
	auto maintained = maintenance->completion.get_future();
	events.clear();
	service.pump(events, true, false);
	const auto inspected = completed(maintained);
	CHECK(inspected.error.empty());
	CHECK(inspected.data["available"] == true);
	CHECK(events.empty());

	SDL_Event human{};
	human.type = SDL_EVENT_KEY_DOWN;
	human.key.scancode = SDL_SCANCODE_B;
	events = {human};
	service.pump(events, true, false);
	CHECK(service.humanKeys.contains(SDL_SCANCODE_B));
	CHECK(events.size() == 1);
	human.type = SDL_EVENT_KEY_UP;
	events = {human};
	service.pump(events, true, false);
	CHECK_FALSE(service.humanKeys.contains(SDL_SCANCODE_B));

	auto held = service.submit("ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}},
							   "boundary client");
	auto heldResult = held->completion.get_future();
	events.clear();
	service.pump(events, true, false);
	CHECK(events.empty());
	CHECK(heldResult.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	service.afterDispatch();
	CHECK(completed(heldResult).error.empty());

	auto release = service.submit("ui_release_inputs", {{"token", token}}, "boundary client");
	auto released = release->completion.get_future();
	events.clear();
	service.pump(events, false, false);
	CHECK(events.empty());
	service.drainCleanup(events);
	REQUIRE(events.size() == 1);
	CHECK(events.front().type == SDL_EVENT_KEY_UP);
	service.afterDispatch();
	CHECK(completed(released).error.empty());

	auto status = service.submit("ui_status", Json::object(), "observer");
	auto statusResult = status->completion.get_future();
	events.clear();
	service.pump(events, false, false);
	auto unavailable = completed(statusResult);
	CHECK(unavailable.error.empty());
	CHECK(unavailable.data["available"] == false);
	auto screenshot = service.submit("ui_screenshot", Json::object(), "observer");
	auto screenshotResult = screenshot->completion.get_future();
	service.pump(events, false, false);
	CHECK_FALSE(completed(screenshotResult).error.empty());

	auto chord = service.submit("ui_key",
								{{"token", token},
								 {"key", "A"},
								 {"action", "down"},
								 {"modifiers", Json::array({"Left Ctrl"})}},
								"boundary client");
	auto truncated = chord->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() >= 2);
	// A screen transition delivered only the prefix. The omitted key-up must not leak holds.
	service.afterDispatch(false, 1);
	CHECK_FALSE(completed(truncated).error.empty());
	events.clear();
	service.pump(events, true, false);
	CHECK(service.keys.empty());
	CHECK(events.empty());
	REQUIRE(service.cleanup.size() == 2);
	// Cleanup must wait for the tracked dispatch hook, and its remaining tail
	// survives another screen transition even without an active request.
	service.pump(events, true);
	REQUIRE(events.size() == 2);
	service.afterDispatch(false, 1);
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	CHECK(events.front().type == SDL_EVENT_KEY_UP);
	service.afterDispatch();

	auto oldHold = service.submit("ui_key", {{"token", token}, {"key", "B"}, {"action", "down"}},
								  "boundary client");
	auto oldHeld = oldHold->completion.get_future();
	events.clear();
	service.pump(events, true);
	service.afterDispatch();
	REQUIRE(completed(oldHeld).error.empty());
	service.keys[SDL_SCANCODE_B] -= std::chrono::seconds(6);
	auto press = service.submit(
		"ui_key", {{"token", token}, {"key", "A"}, {"modifiers", Json::array({"Left Ctrl"})}},
		"boundary client");
	auto pressed = press->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 5); // Cleanup UP, modifier DOWN, A DOWN, A UP, modifier UP.
	service.afterDispatch(false, 3);
	CHECK_FALSE(completed(pressed).error.empty());
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 2);
	CHECK(events[0].type == SDL_EVENT_KEY_UP);
	CHECK(events[1].type == SDL_EVENT_KEY_UP);
	service.afterDispatch();

	auto post =
		service.submit("ui_move", {{"token", token}, {"x", 10}, {"y", 20}, {"capture_after", true}},
					   "boundary client");
	auto captured = post->completion.get_future();
	events.clear();
	service.pump(events, true);
	// A dispatched action can open a native dialog before capture starts.
	service.afterDispatch(true, events.size(), false);
	const auto captureFailure = completed(captured);
	CHECK_FALSE(captureFailure.error.empty());
	CHECK(captureFailure.data["dispatched"] == true);
	CHECK_FALSE(captureFailure.pixels);

	auto reentrant = service.enqueue(
		[&]
		{
			service.revoke();
			return Result{};
		},
		"extension");
	auto reentered = reentrant->completion.get_future();
	events.clear();
	service.pump(events, true);
	CHECK_FALSE(completed(reentered).error.empty());
}

TEST_CASE("MCP/staged release cancellation does not acknowledge undelivered cleanup [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	UIService service(*globalContainer->gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", {}, "release client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();
	auto key = service.submit("ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}},
							  "release client");
	auto keyed = key->completion.get_future();
	events.clear();
	service.pump(events, true);
	service.afterDispatch();
	CHECK(completed(keyed).data["dispatched"] == true);
	auto release = service.submit("ui_release_inputs", {{"token", token}}, "release client");
	auto released = release->completion.get_future();
	events.clear();
	service.pump(events, false, false);
	CHECK(events.empty());
	CHECK(service.dispatched);
	CHECK_FALSE(service.awaitingInputDispatch());
	REQUIRE(service.cleanup.size() == 1);
	CHECK(released.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	release->cancelled = true;
	service.pump(events, false, false);
	const auto cancelled = completed(released);
	CHECK_FALSE(cancelled.error.empty());
	CHECK(cancelled.data["dispatched"] == false);
	CHECK(cancelled.data["input_started"] == true);
	CHECK(service.keys.empty());
	service.drainCleanup(events);
	REQUIRE(events.size() == 1);
	CHECK(events.front().type == SDL_EVENT_KEY_UP);
	service.afterDispatch();
	CHECK(service.cleanup.empty());

	auto capturedKey = service.submit(
		"ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}, {"capture_after", true}},
		"release client");
	auto captured = capturedKey->completion.get_future();
	events.clear();
	service.pump(events, true);
	service.afterDispatch();
	REQUIRE(service.capturing);
	REQUIRE(service.dispatchAcknowledged);
	service.keys[SDL_SCANCODE_A] -= std::chrono::seconds(6);
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	// Cleanup interruption cancels capture, but does not undo the original DOWN.
	service.afterDispatch(false, 0);
	const auto captureError = completed(captured);
	CHECK_FALSE(captureError.error.empty());
	CHECK(captureError.data["dispatched"] == true);
	events.clear();
	service.drainCleanup(events);
	REQUIRE(events.size() == 1);
	service.afterDispatch();
	CHECK(service.keys.empty());
	CHECK(service.cleanup.empty());
}

TEST_CASE("MCP/key events honor SDL keycode options for modifier chords [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	struct HintRestore
	{
		std::optional<std::string> previous;
		HintRestore()
		{
			if (const auto *value = SDL_GetHint(SDL_HINT_KEYCODE_OPTIONS))
				previous = value;
		}
		~HintRestore()
		{
			if (previous)
				SDL_SetHintWithPriority(SDL_HINT_KEYCODE_OPTIONS, previous->c_str(),
										SDL_HINT_OVERRIDE);
			else
				SDL_ResetHint(SDL_HINT_KEYCODE_OPTIONS);
		}
	} hint;
	REQUIRE(SDL_SetHintWithPriority(SDL_HINT_KEYCODE_OPTIONS, "hide_numpad", SDL_HINT_OVERRIDE));
	UIService service(*globalContainer->gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", {}, "keycode client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();
	for (const auto *name : {"A", "Keypad 4"})
	{
		auto key = service.submit(
			"ui_key", {{"token", token}, {"key", name}, {"modifiers", Json::array({"Left Shift"})}},
			"keycode client");
		auto keyed = key->completion.get_future();
		events.clear();
		service.pump(events, true);
		const auto scan = SDL_GetScancodeFromName(name);
		const auto down = std::find_if(
			events.begin(), events.end(), [scan](const SDL_Event &event)
			{ return event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == scan; });
		REQUIRE(down != events.end());
		CHECK((down->key.mod & SDL_KMOD_SHIFT) != 0);
		CHECK(down->key.key == SDL_GetKeyFromScancode(scan, down->key.mod, true));
		// These cases distinguish KEY-event representation from character lookup.
		CHECK(down->key.key != SDL_GetKeyFromScancode(scan, down->key.mod, false));
		service.afterDispatch();
		CHECK(completed(keyed).error.empty());
	}
}

TEST_CASE("MCP/held input and drag clocks begin at actual dispatch [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	UIService service(*globalContainer->gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", {}, "delayed client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();
	auto drag =
		service.submit("ui_drag",
					   {{"token", token},
						{"points", Json::array({{{"x", 10}, {"y", 20}}, {{"x", 50}, {"y", 60}}})}},
					   "delayed client");
	auto dragged = drag->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 2);
	// Simulate queue delay without sleeping: pending DOWN must not expire, and
	// time before its receipt must not count toward the 250 ms gesture duration.
	service.dragStart -= std::chrono::seconds(2);
	service.buttons[SDL_BUTTON_LEFT] -= std::chrono::seconds(6);
	const auto pressBackdated = Clock::now();
	events.clear();
	service.pump(events, true, false);
	CHECK(events.empty());
	CHECK(service.buttons.contains(SDL_BUTTON_LEFT));
	service.afterDispatch();
	CHECK(service.dragStart >= pressBackdated);
	CHECK(service.buttons[SDL_BUTTON_LEFT] >= pressBackdated);
	service.dragStart -= std::chrono::milliseconds(100);
	events.clear();
	service.pump(events, true);
	CHECK_FALSE(service.dispatched);
	CHECK(dragged.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	REQUIRE(events.size() == 1);
	const auto cancelMotion = service.dispatchCancellation();
	const auto completeMotion = service.dispatchCompletion();
	const auto motionCount = events.size();
	service.buttons[SDL_BUTTON_LEFT] -= std::chrono::seconds(6);
	events.clear();
	service.pump(events, true, false);
	const auto expired = completed(dragged);
	CHECK(expired.error.find("held mouse button expired") != std::string::npos);
	CHECK(drag->cancelled);
	CHECK(cancelMotion());
	CHECK_FALSE(service.buttons.contains(SDL_BUTTON_LEFT));
	CHECK(service.temporaryButton == 0);
	completeMotion(0, motionCount);
	events.clear();
	service.drainCleanup(events);
	REQUIRE(events.size() == 1);
	CHECK(events.front().type == SDL_EVENT_MOUSE_BUTTON_UP);
	service.afterDispatch();

	auto key = service.submit("ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}},
							  "delayed client");
	auto keyed = key->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	service.keys[SDL_SCANCODE_A] -= std::chrono::seconds(6);
	const auto keyPressBackdated = Clock::now();
	events.clear();
	service.pump(events, true, false);
	CHECK(events.empty());
	CHECK(service.keys.contains(SDL_SCANCODE_A));
	service.afterDispatch();
	CHECK(completed(keyed).error.empty());
	CHECK(service.keys[SDL_SCANCODE_A] >= keyPressBackdated);
}

TEST_CASE("MCP/pending coordinates cancel when viewport changes before consumption [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	auto &gfx = *globalContainer->gfx;
	UIService service(gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", {}, "resize client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();
	const auto viewport = service.viewport;
	auto move = service.submit("ui_move",
							   {{"token", token}, {"x", 20}, {"y", 30}, {"viewport_id", viewport}},
							   "resize client");
	auto moved = move->completion.get_future();
	events.clear();
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	const auto cancel = service.dispatchCancellation();
	const auto complete = service.dispatchCompletion();
	CHECK_FALSE(cancel());
	REQUIRE(gfx.resizeViewport(gfx.getDrawableW() + 32, gfx.getDrawableH() + 32));
	CHECK(cancel());
	CHECK(move->cancelled);
	complete(0, events.size());
	const auto result = completed(moved);
	CHECK(result.error.find("Stale viewport") != std::string::npos);
	CHECK(result.data["dispatched"] == false);
	CHECK(result.data["input_started"] == true);
	CHECK(result.data["viewport_id"] != viewport);
	CHECK_FALSE(service.awaitingInputDispatch());
}

TEST_CASE("MCP/pending keys cancel when UI becomes unavailable before consumption [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	UIService service(*globalContainer->gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", {}, "dialog client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();
	for (const bool observeInPump : {false, true})
	{
		auto key = service.submit("ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}},
								  "dialog client");
		auto keyed = key->completion.get_future();
		events.clear();
		service.pump(events, true);
		REQUIRE(events.size() == 1);
		const auto cancel = service.dispatchCancellation();
		const auto complete = service.dispatchCompletion();
		if (observeInPump)
		{
			events.clear();
			service.pump(events, false, false);
		}
		else
		{
			// Model a dialog opening later in the same frame, before receipt polling.
			service.uiAvailable = false;
		}
		CHECK(cancel());
		CHECK(key->cancelled);
		complete(0, 1);
		const auto result = completed(keyed);
		CHECK(result.error.find("unavailable") != std::string::npos);
		CHECK(result.data["dispatched"] == false);
		CHECK(service.keys.empty());
		CHECK_FALSE(service.awaitingInputDispatch());
		events.clear();
		service.drainCleanup(events);
		REQUIRE(events.size() == 1);
		CHECK(events.front().type == SDL_EVENT_KEY_UP);
		service.afterDispatch();
		CHECK(service.cleanup.empty());
	}
}

TEST_CASE("MCP/hidden suspension resolves buffered receipts before later work [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	UIService service(*globalContainer->gfx);
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	std::vector<SDL_Event> events;
	auto acquire = service.submit("ui_acquire_control", {}, "hidden client");
	auto acquired = acquire->completion.get_future();
	service.pump(events, true);
	const auto token = completed(acquired).data["token"].get<std::string>();
	struct Deferred : GAGGUI::Screen
	{
		std::shared_ptr<GAGCore::InputDispatchReceipt> receipt;
		int updates = 0;
		bool defersExecutionInput() const override { return true; }
		void beginAdditionalInput(std::shared_ptr<GAGCore::InputDispatchReceipt> value) override
		{
			receipt = std::move(value);
		}
		void handleExecutionEvent(SDL_Event) override {}
		void updateExecution(Uint32) override { ++updates; }
		void suspendExecution() override
		{
			if (receipt)
			{
				receipt->consumed = 0;
				receipt.reset();
			}
		}
	};
	GAGGUI::ScreenStack screens(*globalContainer->gfx);
	auto owned = std::make_unique<Deferred>();
	auto *screen = owned.get();
	screens.push(std::move(owned));
	GAGGUI::ScreenStack::AdditionalInput additional;
	additional.prepare = [&](std::vector<SDL_Event> &extra) { service.pump(extra, true); };
	additional.completion = [&] { return service.dispatchCompletion(); };
	additional.cancellation = [&] { return service.dispatchCancellation(); };
	auto move =
		service.submit("ui_move", {{"token", token}, {"x", 20}, {"y", 30}}, "hidden client");
	auto moved = move->completion.get_future();
	screens.frame(0, {}, false, additional);
	REQUIRE(service.awaitingInputDispatch());
	CHECK(moved.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	const auto oldCompletion = service.dispatchCompletion();
	const auto updates = screen->updates;
	screens.suspendExecution();
	screens.pollInputReceipts();
	CHECK_FALSE(completed(moved).error.empty());
	CHECK_FALSE(service.awaitingInputDispatch());
	CHECK(screen->updates == updates);
	auto status = service.submit("ui_status", {}, "observer");
	auto inspected = status->completion.get_future();
	events.clear();
	service.pump(events, false, false);
	CHECK(completed(inspected).data["available"] == false);

	auto key = service.submit("ui_key", {{"token", token}, {"key", "A"}, {"action", "down"}},
							  "hidden client");
	auto keyed = key->completion.get_future();
	events.clear();
	service.pump(events, true);
	const auto keyCompletion = service.dispatchCompletion();
	oldCompletion(0, 1);
	CHECK(keyed.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	CHECK(service.awaitingInputDispatch());
	keyCompletion(events.size(), events.size());
	CHECK(completed(keyed).error.empty());
}

TEST_CASE("MCP/preferences retain desktop defaults and reject malformed ports")
{
	glob2test::HeadlessGlobals globals;
	Settings saved;
	CHECK_FALSE(saved.mcpEnabled);
	CHECK(saved.mcpPort == 27202);
	saved.mcpEnabled = true;
	saved.mcpPort = 28123;
	REQUIRE(saved.save("mcp-preferences.txt"));
	Settings loaded;
	loaded.load("mcp-preferences.txt");
	CHECK(loaded.mcpEnabled);
	CHECK(loaded.mcpPort == 28123);
	const auto invalidFile = std::filesystem::path(globalContainer->fileManager->getDir(0)) /
							 "mcp-invalid-preferences.txt";
	// glob2 play --mcp-port enables the listener for one run without saving it.
	Settings launched;
	launched.mcpLaunchPort = 28124;
	CHECK(launched.mcpActive());
	CHECK(launched.mcpActivePort() == 28124);
	REQUIRE(launched.save("mcp-launch-preferences.txt"));
	Settings relaunched;
	relaunched.load("mcp-launch-preferences.txt");
	CHECK_FALSE(relaunched.mcpActive());
	CHECK(relaunched.mcpActivePort() == 27202);
	// Editing the MCP settings adopts what the player saw as the saved choice.
	launched.adoptMcpLaunchOverride();
	CHECK(launched.mcpEnabled);
	CHECK(launched.mcpPort == 28124);
	CHECK(launched.mcpLaunchPort == 0);
	const bool savedEnabled = globalContainer->settings.mcpEnabled;
	globalContainer->applyCommand(Cli::parse({"play", "--mcp-port", "28125"}));
	CHECK(globalContainer->settings.mcpLaunchPort == 28125);
	CHECK(globalContainer->settings.mcpEnabled == savedEnabled);
	CHECK(globalContainer->settings.mcpActive());
	globalContainer->settings.mcpLaunchPort = 0;
	for (const auto *invalidPort : {"0", "65536", "27202trailing", "-1", ""})
	{
		std::ofstream output(invalidFile);
		output << "mcpEnabled=true\nmcpPort=" << invalidPort << '\n';
		output.close();
		Settings malformed;
		malformed.load("mcp-invalid-preferences.txt");
		CHECK_FALSE(malformed.mcpEnabled);
		CHECK(malformed.mcpPort == 27202);
	}
}

namespace
{
void verifyCapture(GAGCore::RendererBackend backend)
{
	using namespace GAGCore;
	glob2test::HeadlessGlobals globals(
		{.display = true, .width = 640, .height = 480, .beforeLoad = [&](GlobalContainer &g) {
			 g.settings.rendererBackend = backend;
		 }});
	auto &gfx = *globalContainer->gfx;
	INFO(gfx.backendSwitchStatus().reason);
	REQUIRE(gfx.backendSwitchStatus().active == backend);
	std::shared_ptr<SDL_Surface> image;
	std::string error;
	bool completed = false;
	gfx.requestMemoryCapture(
		[&](auto pixels, auto, auto why)
		{
			image = pixels;
			error = why;
			completed = true;
		});
	const auto deadline = Clock::now() + std::chrono::seconds(10);
	while (!completed && Clock::now() < deadline)
	{
		if (gfx.beginRenderFrame())
		{
			gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), Color(0, 0, 255));
			gfx.drawFilledRect(0, 0, gfx.getW(), 20, Color(255, 0, 0));
			gfx.nextFrame();
		}
		SDL_Delay(1);
	}
	REQUIRE(completed);
	REQUIRE_MESSAGE(image, error);
	CHECK(image->w == gfx.getDrawableW());
	CHECK(image->h == gfx.getDrawableH());
	Uint8 r, g, b, a;
	REQUIRE(SDL_ReadSurfacePixel(image.get(), image->w / 2, 5, &r, &g, &b, &a));
	CHECK(r == 255);
	CHECK(b == 0);
	REQUIRE(SDL_ReadSurfacePixel(image.get(), image->w / 2, image->h - 5, &r, &g, &b, &a));
	CHECK(r == 0);
	CHECK(b == 255);
	REQUIRE(
		IMG_SavePNG(image.get(), (glob2test::artifactDir() / "mcp-frame.png").string().c_str()));
	// Recording and the independent MCP readback share the renderer capture queue.
	auto &recording = GAGCore::Recording::recorder();
	recording.options.encoder = GAGCore::Recording::EncoderPreference::Software;
	glob2test::TempDir recordingDirectory("mcp-recording");
	const auto movie = recordingDirectory.path / "simultaneous.mp4";
	REQUIRE(recording.start(movie.string()));
	bool concurrent = false;
	gfx.requestMemoryCapture(
		[&](auto pixels, auto, auto why)
		{
			REQUIRE_MESSAGE(pixels, why);
			concurrent = true;
		});
	auto until = Clock::now() + std::chrono::seconds(10);
	while (!concurrent && Clock::now() < until)
	{
		if (gfx.beginRenderFrame())
		{
			gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), Color(20, 40, 60));
			gfx.nextFrame();
		}
		SDL_Delay(1);
	}
	REQUIRE(concurrent);
	for (int i = 0; i < 8; ++i)
	{
		if (gfx.beginRenderFrame())
		{
			gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), Color(20, 40, 60));
			gfx.nextFrame();
		}
		SDL_Delay(20);
	}
	gfx.pollCaptures();
	recording.stop();
	recording.shutdown();
	CHECK(recording.status().state == GAGCore::Recording::State::Complete);
	REQUIRE(std::filesystem::exists(movie));
	std::filesystem::copy_file(movie, glob2test::artifactDir() / "simultaneous.mp4",
							   std::filesystem::copy_options::overwrite_existing);
	bool cancelled = false;
	auto id = gfx.requestMemoryCapture(
		[&](auto pixels, auto, auto why)
		{
			CHECK_FALSE(pixels);
			CHECK_FALSE(why.empty());
			cancelled = true;
		});
	gfx.cancelMemoryCapture(id);
	CHECK(cancelled);
}
} // namespace
TEST_CASE("MCP/software capture dimensions orientation cancellation [display][artifacts]")
{
	verifyCapture(GAGCore::RendererBackend::Software);
}
#ifdef HAVE_OPENGL
TEST_CASE("MCP/OpenGL capture dimensions orientation cancellation [display][artifacts]")
{
	verifyCapture(GAGCore::RendererBackend::LegacyGL);
}
#endif
TEST_CASE("MCP/SDL GPU capture dimensions orientation cancellation [display][artifacts]")
{
	verifyCapture(GAGCore::RendererBackend::SDLGPU);
}

TEST_CASE("MCP/semantic consumption independent waits and target revisions [network]")
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .width = 640, .height = 480, .beforeLoad = [](GlobalContainer &g) {
			 g.settings.rendererBackend = GAGCore::RendererBackend::Software;
		 }});
	GAGGUI::ui::Theme theme;
	GAGGUI::ui::FixedTextMeasurer measurer(8, 16);
	int count = 0;
	bool enabled = true;
	GAGGUI::ui::Host host(theme,
						  [&](const auto &)
						  {
							  GAGGUI::ui::ButtonOptions options;
							  options.enabled = enabled;
							  return GAGGUI::ui::button("act", "Act " + std::to_string(count), [&] { ++count; host.invalidate(); }, options);
						  });
	host.setMeasurer(&measurer);
	host.setPresentation(GAGGUI::ui::Presentation::forSurface(640, 480));
	host.layoutIfNeeded();
	UIService service(*globalContainer->gfx);
	service.inspectionHosts = [&] { return std::vector<GAGGUI::ui::Host *>{&host}; };
	service.inspectionScreen = [] { return std::string("fixture"); };
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	auto execute = [&](const std::string &tool, Json args)
	{
		auto request = service.submit(tool, std::move(args), "test");
		auto future = request->completion.get_future();
		std::vector<SDL_Event> events;
		service.pump(events, true);
		for (const auto &event : events)
			host.event(event);
		service.afterDispatch();
		return completed(future);
	};
	const auto acquired = execute("ui_acquire_control", {{"inspect", true}});
	CHECK(acquired.data["lease_seconds"] == 300);
	CHECK(acquired.data["inspection"]["screen"] == "fixture");
	CHECK(acquired.data["inspection"]["surfaces"][0]["controls"][0]["label"] == "Act 0");
	const auto token = acquired.data.at("token");
	CHECK(execute("ui_renew_control", {{"token", token}}).error.empty());
	CHECK_FALSE(execute("ui_inspect", {{"token", "wrong"}}).error.empty());
	auto inspected = execute("ui_inspect", {{"token", token}});
	REQUIRE(inspected.error.empty());
	const auto surface = inspected.data["inspection"]["surfaces"][0];
	const auto control = surface["controls"][0];
	auto waiting = service.submit(
		"ui_wait",
		{{"surface", surface["instance"]}, {"condition", {{"key", "act"}, {"enabled", false}}}},
		"observer");
	auto observed = waiting->completion.get_future();
	auto action = Json{{"surface", surface["instance"]},
					   {"key", "act"},
					   {"revision", control["revision"]},
					   {"action", "activate"},
					   {"token", token}};
	const auto performed = execute("ui_act", action);
	CHECK(performed.error.empty());
	CHECK(performed.data["inspection"]["screen"] == "fixture");
	const auto updated = performed.data["inspection"]["surfaces"][0]["controls"][0];
	CHECK(updated["label"] == "Act 1");
	CHECK(updated["revision"] != control["revision"]);
	action["revision"] = updated["revision"];
	CHECK(count == 1);
	CHECK(observed.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	auto stale = service.submit("ui_act", action, "test");
	auto completion = stale->completion.get_future();
	std::vector<SDL_Event> events;
	service.pump(events, true);
	REQUIRE(events.size() == 1);
	enabled = false;
	host.invalidate();
	host.event(events[0]);
	service.afterDispatch();
	CHECK_FALSE(completed(completion).error.empty());
	CHECK(count == 1);
	events.clear();
	service.pump(events, true);
	service.afterDispatch();
	CHECK(completed(observed).data["satisfied"] == true);

	// A failing post-action read cannot turn confirmed delivery into a failed
	// mutation that callers might replay after uncertain acknowledgement.
	enabled = true;
	host.invalidate();
	const auto refreshed = execute("ui_inspect", {{"token", token}});
	action["revision"] = refreshed.data["inspection"]["surfaces"][0]["controls"][0]["revision"];
	service.inspectionHosts = [&]() -> std::vector<GAGGUI::ui::Host *>
	{
		if (count > 1)
			throw std::runtime_error("Fixture inspection failed after consumption");
		return {&host};
	};
	const auto consumed = execute("ui_act", action);
	CHECK(consumed.error.empty());
	CHECK(consumed.data["dispatched"] == true);
	CHECK(consumed.data["input_started"] == true);
	CHECK(consumed.data["inspection_error"] == "Fixture inspection failed after consumption");
	CHECK(count == 2);
	CHECK(execute("ui_release_control", {{"token", token}}).error.empty());
	const auto granted = execute("ui_acquire_control", {{"inspect", true}});
	CHECK(granted.error.empty());
	CHECK(granted.data["inspection_error"] == "Fixture inspection failed after consumption");
	CHECK(granted.data.contains("token"));
	CHECK(execute("ui_renew_control", {{"token", granted.data["token"]}}).error.empty());
}
TEST_CASE("MCP/HTTP wait admission reserves connections for satisfying actions [network]")
{
	namespace asio = boost::asio;
	namespace http = boost::beast::http;
	using Tcp = asio::ip::tcp;
	glob2test::HeadlessGlobals globals(
		{.display = true, .width = 320, .height = 240, .beforeLoad = [](GlobalContainer &g) {
			g.settings.rendererBackend = GAGCore::RendererBackend::Software;
		}});
	GAGGUI::ui::Theme theme;
	GAGGUI::ui::FixedTextMeasurer measurer(8, 16);
	int count = 0;
	GAGGUI::ui::Host host(theme, [&](const auto &) {
		GAGGUI::ui::ButtonOptions options; options.enabled = count == 0;
		return GAGGUI::ui::button("act", "Act", [&] { ++count; host.invalidate(); }, options);
	});
	host.setMeasurer(&measurer);
	host.setPresentation(GAGGUI::ui::Presentation::forSurface(320, 240));
	host.layoutIfNeeded();
	UIService service(*globalContainer->gfx);
	service.inspectionHosts = [&] { return std::vector<GAGGUI::ui::Host *>{&host}; };
	service.inspectionScreen = [] { return std::string("wait-admission"); };
	const auto port = unusedLoopbackPort();
	service.configure(true, port);
	waitForListener(service, ListenerState::Running);
	auto pump = [&] {
		std::vector<SDL_Event> events;
		service.pump(events, true);
		for (const auto &event : events) host.event(event);
		service.afterDispatch();
	};
	auto acquisition = service.submit("ui_acquire_control", {}, "HTTP fixture");
	auto granted = acquisition->completion.get_future();
	pump();
	const auto token = completed(granted).data.at("token");
	const auto snapshot = host.inspect();
	const auto target = snapshot.controls.at(0);
	asio::io_context clients;
	struct Client
	{
		Tcp::socket socket;
		boost::beast::flat_buffer buffer;
		http::response_parser<http::string_body> parser;
		std::optional<Json> response;
		explicit Client(asio::io_context &io) : socket(io) {}
		void poll()
		{
			if (response) return;
			boost::system::error_code error;
			http::read(socket, buffer, parser, error);
			if (error == asio::error::would_block || error == asio::error::try_again) return;
			if (error) throw boost::system::system_error(error);
			response = Json::parse(parser.get().body());
		}
	};
	auto send = [&](const std::string &tool, Json args) {
		auto client = std::make_unique<Client>(clients);
		client->socket.connect({asio::ip::make_address_v4("127.0.0.1"),
			static_cast<unsigned short>(port)});
		http::request<http::string_body> request(http::verb::post, "/mcp", 11);
		request.set(http::field::host, "localhost:" + std::to_string(port));
		request.set(http::field::content_type, "application/json");
		request.set(http::field::accept, "application/json, text/event-stream");
		request.set("MCP-Protocol-Version", "2025-11-25");
		request.body() = Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
			{"params", {{"name", tool}, {"arguments", std::move(args)}}}}.dump();
		request.prepare_payload();
		http::write(client->socket, request);
		client->socket.non_blocking(true);
		return client;
	};
	std::vector<std::unique_ptr<Client>> waits;
	for (int i = 0; i < 16; ++i)
		waits.push_back(send("ui_wait", {{"timeout_ms", 30000},
			{"condition", {{"key", "act"}, {"enabled", false}}}}));
	auto finished = [&] { return std::count_if(waits.begin(), waits.end(),
		[](const auto &client) { return client->response.has_value(); }); };
	auto deadline = Clock::now() + std::chrono::seconds(10);
	while (finished() < 8 && Clock::now() < deadline)
	{
		pump();
		for (auto &client : waits) client->poll();
		SDL_Delay(1);
	}
	REQUIRE(finished() == 8);
	for (const auto &client : waits)
		if (client->response)
		{
			CHECK(client->response->at("result").at("isError") == true);
			CHECK(client->response->at("result").at("structuredContent").at("error").get<std::string>().find("maximum 8") != std::string::npos);
		}
	CHECK(count == 0); // Eight admitted waits still await this exact mutation.
	auto action = send("ui_act", {{"token", token}, {"surface", snapshot.instance},
		{"key", "act"}, {"revision", target.revision}, {"action", "activate"}});
	deadline = Clock::now() + std::chrono::seconds(10);
	while ((!action->response || finished() < 16) && Clock::now() < deadline)
	{
		pump();
		action->poll();
		for (auto &client : waits) client->poll();
		SDL_Delay(1);
	}
	REQUIRE(action->response);
	CHECK(action->response->at("result").at("isError") == false);
	CHECK(count == 1);
	REQUIRE(finished() == 16);
	int satisfied = 0;
	for (const auto &client : waits)
		if (!client->response->at("result").at("isError").get<bool>())
		{
			CHECK(client->response->at("result").at("structuredContent").at("satisfied") == true);
			++satisfied;
		}
	CHECK(satisfied == 8);
}

TEST_CASE("MCP/game tools read without control and act only under the lease [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	UIService service(*globalContainer->gfx);
	std::vector<std::string> reached;
	service.domainHandler = [&](const auto &tool, const auto &)
	{
		reached.push_back(tool);
		return Result{};
	};
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	auto submit = [&](const std::string &tool, Json args)
	{
		auto request = service.submit(tool, std::move(args), "game client");
		return std::make_pair(request, request->completion.get_future());
	};
	auto execute = [&](const std::string &tool, Json args)
	{
		auto [request, future] = submit(tool, std::move(args));
		std::vector<SDL_Event> events;
		service.pump(events, true);
		service.afterDispatch();
		return completed(future);
	};
	// Reading the world needs no control, like hive_status.
	CHECK(execute("game_world", Json::object()).error.empty());
	CHECK(execute("game_stats", {{"metrics", {"population"}}}).error.empty());
	REQUIRE(reached == std::vector<std::string>{"game_world", "game_stats"});
	reached.erase(reached.begin() + 1);
	// Placing and moving the camera are input: they need the current token.
	CHECK_FALSE(execute("game_place", {{"token", "stale"}, {"building", "inn"}, {"x", 1}, {"y", 1}})
					.error.empty());
	CHECK_FALSE(execute("game_view", {{"token", "stale"}, {"x", 1}, {"y", 1}}).error.empty());
	CHECK(reached.size() == 1);
	const auto token = execute("ui_acquire_control", Json::object()).data.at("token");
	CHECK(execute("game_place", {{"token", token}, {"building", "inn"}, {"x", 1}, {"y", 1}})
			  .error.empty());
	CHECK(execute("game_view", {{"token", token}, {"x", 1}, {"y", 1}}).error.empty());
	CHECK_FALSE(execute("game_building", {{"token", "stale"}, {"id", 0}, {"generation", 1}}).error.empty());
	CHECK(execute("game_building", {{"token", token}, {"id", 0}, {"generation", 1}}).error.empty());
	CHECK(execute("game_zone", {{"token", token}, {"zone", "guard"}, {"x", 1}, {"y", 1},
								{"width", 2}, {"height", 2}}).error.empty());
	CHECK_FALSE(execute("game_chat", {{"token", "stale"}, {"text", "hi"}}).error.empty());
	CHECK(execute("game_chat", {{"token", token}, {"text", "hi"}}).error.empty());
	CHECK(reached == std::vector<std::string>{"game_world", "game_place", "game_view",
											  "game_building", "game_zone", "game_chat"});
	// game_time status is a read; pausing, resuming and stepping are control.
	CHECK(execute("game_time", {{"action", "status"}}).error.empty());
	CHECK_FALSE(execute("game_time", {{"action", "pause"}}).error.empty());
	CHECK_FALSE(execute("game_time", {{"action", "step"}, {"token", "stale"}, {"ticks", 5}}).error.empty());
	CHECK(execute("game_time", {{"action", "step"}, {"token", token}, {"ticks", 5}}).error.empty());
	CHECK(execute("game_view", {{"token", token}, {"x", 1}, {"y", 1}, {"zoom", 0.5}}).error.empty());
	CHECK(reached.size() == 9);
	CHECK(reached.back() == "game_view");

	// A control that has disappeared satisfies a wait for it to be not visible...
	const auto gone = execute("ui_wait", {{"timeout_ms", 2000},
										  {"condition", {{"key", "no/such/control"}, {"visible", false}}}});
	CHECK(gone.error.empty());
	CHECK(gone.data.at("satisfied") == true);
	// ...but not one that also requires a property only a present control can have.
	auto [pending, future] = submit("ui_wait", {{"timeout_ms", 1100},
		{"condition", {{"key", "no/such/control"}, {"visible", false}, {"enabled", true}}}});
	const auto deadline = Clock::now() + std::chrono::seconds(3);
	while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
		   Clock::now() < deadline)
	{
		std::vector<SDL_Event> events;
		service.pump(events, true);
		service.afterDispatch();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	const auto partial = completed(future);
	CHECK(partial.data.at("satisfied") == false);
}
TEST_CASE("MCP/standing orders survive idle control lease expiry [network]")
{
	glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480});
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
	world.gui.localPlayer = world.gui.localTeamNo = 0;
	world.game.players[0]->type = BasePlayer::P_LOCAL;
	Hive::LocalCommander commander(world.gui, [](Json input)
	{
		std::promise<Json> result;
		result.set_value(Hive::invoke(input));
		return result.get_future();
	});
	UIService service(*globalContainer->gfx);
	service.domainHandler = [&](const auto &tool, const auto &arguments)
	{
		Result result;
		result.data = commander.call(tool, arguments);
		return result;
	};
	service.configure(true, unusedLoopbackPort());
	waitForListener(service, ListenerState::Running);
	auto execute = [&](const std::string &tool, Json args)
	{
		auto request = service.submit(tool, std::move(args), "standing-order client");
		auto future = request->completion.get_future();
		std::vector<SDL_Event> events;
		service.pump(events, true);
		service.afterDispatch();
		return completed(future);
	};
	const auto token = execute("ui_acquire_control", Json::object()).data.at("token");
	const auto session = commander.status().at("session");
	REQUIRE(execute("hive_program", {{"token", token}, {"session", session},
		{"action", "install"}, {"operation_key", "expiry-install"}, {"name", "Continue"},
		{"interval_ticks", 25}, {"source", "function step(ctx){return {output:{tick:ctx.tick},orders:[]}}"}}).error.empty());
	commander.update();
	commander.update();
	REQUIRE(commander.standingOrders().size() == 1);
	service.renewed -= std::chrono::seconds(301);
	CHECK_FALSE(execute("ui_renew_control", {{"token", token}}).error.empty());
	CHECK(service.token.empty());
	world.game.stepCounter += 25;
	world.game.finishOrderPhase(false);
	commander.update();
	commander.update();
	const auto program = commander.standingOrders().at(0);
	CHECK_FALSE(program.at("paused").get<bool>());
	CHECK(program.at("recent").at("status") == "completed");
	CHECK(program.at("recent").at("observation_tick") == world.game.stepCounter);
}
#endif
