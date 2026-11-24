// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tools.h"
#include "Guide.h"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <stdexcept>

namespace MCP
{
namespace
{
// SDL scancode names are ASCII by default. Match SDL's case-insensitive lookup
// without consulting its mutable, main-thread-only name table during validation.
std::string normalizedScancodeName(std::string name)
{
	for (auto &c : name)
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
	return name;
}
using ScancodeNames = std::map<std::string, SDL_Scancode>;
std::shared_ptr<const ScancodeNames> snapshotScancodeNames()
{
	auto names = std::make_shared<ScancodeNames>();
	// Registration happens on the application thread, before the listener starts.
	// emplace preserves SDL's first-match behavior for duplicate names.
	for (int value = 1; value < SDL_SCANCODE_COUNT; ++value)
	{
		const auto scancode = static_cast<SDL_Scancode>(value);
		const char *name = SDL_GetScancodeName(scancode);
		if (name && *name)
			names->emplace(normalizedScancodeName(name), scancode);
	}
	return names;
}
// Validate the deliberately small, local schema vocabulary used by these tools.
// This is not a general JSON Schema engine; extension tools must use this subset.
void validate(const Json &schema, const Json &value, const std::string &path)
{
	// Name the offending field and the broken rule, so clients can correct the call.
	auto fail = [&](const std::string &reason)
	{ throw std::invalid_argument("Invalid argument: " + path + ": " + reason); };
	const auto type = schema.value("type", "");
	if ((type == "object" && !value.is_object()) || (type == "array" && !value.is_array()) ||
		(type == "string" && !value.is_string()) || (type == "boolean" && !value.is_boolean()) ||
		(type == "integer" && !value.is_number_integer()) ||
		(type == "number" && !value.is_number()))
		fail("expected " + type);
	if (schema.contains("enum") &&
		std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end())
		fail("expected one of " + schema["enum"].dump());
	if (value.is_number())
	{
		const double n = value.get<double>();
		if (!std::isfinite(n) ||
			(schema.contains("minimum") && n < schema["minimum"].get<double>()) ||
			(schema.contains("maximum") && n > schema["maximum"].get<double>()))
			fail("out of range");
	}
	if (value.is_string() && schema.contains("maxLength") &&
		std::size_t(std::count_if(value.get_ref<const std::string &>().begin(),
								  value.get_ref<const std::string &>().end(),
								  [](unsigned char c) { return (c & 0xc0) != 0x80; })) >
			schema["maxLength"].get<std::size_t>())
		fail("longer than " + schema["maxLength"].dump() + " characters");
	if (value.is_array())
	{
		if ((schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) ||
			(schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()))
			fail("wrong number of items");
		// An array schema without items accepts any elements, as in JSON Schema.
		if (schema.contains("items"))
			for (const auto &item : value)
				validate(schema["items"], item, path + "[]");
	}
	if (value.is_object())
	{
		for (const auto &key : schema.value("required", Json::array()))
			if (!value.contains(key.get<std::string>()))
				fail("missing required property \"" + key.get<std::string>() + "\"");
		const auto properties = schema.value("properties", Json::object());
		for (const auto &[key, item] : value.items())
		{
			if (!properties.contains(key))
			{
				if (!schema.value("additionalProperties", true))
					fail("unknown property \"" + key + "\"");
			}
			else
				validate(properties[key], item, path + "." + key);
		}
	}
}
Json object(Json properties, Json required = Json::array())
{
	// A braced {} argument arrives as JSON null; MCP clients require an object and an array.
	if (properties.is_null())
		properties = Json::object();
	if (required.is_null())
		required = Json::array();
	return {{"type", "object"},
			{"properties", properties},
			{"required", required},
			{"additionalProperties", false}};
}
Json enumeration(std::initializer_list<const char *> values)
{
	return {{"type", "string"}, {"enum", values}};
}
} // namespace
void ToolRegistry::add(Tool tool)
{
	if (tool.name.empty() || tool.description.empty() || !tool.handler ||
		!tool.inputSchema.is_object() || tool.inputSchema.value("type", Json()) != "object" ||
		!tool.outputSchema.is_object() || !tool.annotations.is_object() ||
		tools.contains(tool.name))
		throw std::logic_error("Invalid or duplicate MCP tool");
	tools.emplace(tool.name, std::move(tool));
}
Json ToolRegistry::list() const
{
	Json result = Json::array();
	for (const auto &[name, t] : tools)
	{
		Json entry = {{"name", name},
					  {"description", t.description},
					  {"inputSchema", t.inputSchema},
					  {"annotations", t.annotations}};
		// A bare object schema adds nothing; listing it would cost every client context.
		if (t.outputSchema != Json{{"type", "object"}})
			entry["outputSchema"] = t.outputSchema;
		result.push_back(std::move(entry));
	}
	return result;
}
std::shared_ptr<Request> ToolRegistry::call(ExecutionContext &context, const std::string &name,
											Json args, std::string client) const
{
	const auto found = tools.find(name);
	if (found == tools.end())
		throw std::invalid_argument("Unknown tool: " + name +
									"; call tools/list for the available tools");
	validate(found->second.inputSchema, args, name);
	if (found->second.validate)
		found->second.validate(args);
	auto request = found->second.handler(context, std::move(args), std::move(client));
	if (!request)
		throw std::logic_error("MCP tool handler returned no request: " + name);
	return request;
}
namespace
{
// Each tool's MCP annotations, stated per tool (hints for clients: read-only tools may run
// in parallel; only idempotent ones may be retried after a dropped connection).
Json annotationsFor(const std::string &name)
{
	static const std::set<std::string> readOnly = {"ui_status",	   "ui_screenshot", "ui_inspect", "ui_wait",
												   "game_world",   "game_stats",	"game_wait",  "hive_status",
												   "hive_api",	   "hive_result",	"game_guide"};
	// Can destroy, erase, cancel or send arbitrary input to the game.
	static const std::set<std::string> destructive = {"game_building", "game_zone", "hive_run",	   "hive_program",
													  "ui_act",		   "ui_click",	"ui_mouse_button", "ui_drag",
													  "ui_key",		   "ui_type_text"};
	// Repeating the call changes nothing more (Hive calls are deduplicated by operation_key).
	static const std::set<std::string> idempotent = {"game_view",		  "game_alliance",	   "game_zone",
													 "ui_renew_control",  "ui_release_inputs", "ui_release_control",
													 "hive_run",		  "hive_program"};
	// Raw input can reach other people (online matches and their menus).
	static const std::set<std::string> openWorld = {"ui_act", "ui_click", "ui_mouse_button", "ui_drag", "ui_key",
													"ui_type_text"};
	const bool reads = readOnly.count(name) > 0;
	return {{"readOnlyHint", reads},
			{"destructiveHint", !reads && destructive.count(name) > 0},
			{"idempotentHint", reads || idempotent.count(name) > 0},
			{"openWorldHint", openWorld.count(name) > 0}};
}
} // namespace
void registerUITools(ToolRegistry &registry)
{
	const auto scancodeNames = snapshotScancodeNames();
	const Json number = {{"type", "number"}, {"minimum", -10000}, {"maximum", 10000}};
	const Json coordinate = {{"type", "number"}, {"minimum", 0}};
	const Json token = {{"type", "string"},
						{"maxLength", 128},
						{"description", "Token from ui_acquire_control"}};
	const Json key = {{"type", "string"}, {"maxLength", 64}};
	const Json button = enumeration({"left", "middle", "right"});
	const Json action = enumeration({"press", "down", "up"});
	static constexpr const char *CompactDescription =
		"Default true: skip layout containers and default fields; false for the full tree";
	auto add = [&](const char *name, const char *description, Json properties, Json required,
				   bool input, bool coordinates = false)
	{
		if (input)
		{
			properties["token"] = token;
			required.push_back("token");
			properties["capture_after"] = {
				{"type", "boolean"},
				{"default", false},
				{"description", "Also return a screenshot taken after the input"}};
		}
		if (input || std::string(name) == "ui_screenshot" || std::string(name) == "ui_inspect")
		{
			properties["max_width"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 16384}};
			properties["max_height"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 16384}};
		}
		if (coordinates)
		{
			properties["viewport_id"] = {{"type", "integer"}, {"minimum", 0}};
			properties["capture_id"] = {
				{"type", "integer"},
				{"minimum", 1},
				{"description", "x/y are pixels of this ui_screenshot"}};
			properties["space"] = {
				{"type", "string"},
				{"enum", {"native", "logical"}},
				{"description", "Without capture_id: native pixels (default) or logical, as "
								"ui_inspect bounds"}};
		}
		const std::string toolName = name;
		Json output = {{"type", "object"}};
		if (toolName == "ui_status" || toolName == "ui_acquire_control" ||
			toolName == "ui_screenshot")
			output = {{"type", "object"},
					   {"properties",
						{{"width", {{"type", "integer"}, {"minimum", 0}}},
						 {"height", {{"type", "integer"}, {"minimum", 0}}},
						 {"viewport_id", {{"type", "integer"}, {"minimum", 0}}},
						 {"frame_id", {{"type", "integer"}, {"minimum", 0}}},
						 {"available", {{"type", "boolean"}}},
						 {"controller", {{"type", "string"}}},
						 {"controlled", {{"type", "boolean"}}},
						 {"dispatched", {{"type", "boolean"}}},
						 {"input_started", {{"type", "boolean"}}},
						 {"error", {{"type", "string"}}}}}};
		if (std::string(name) == "ui_acquire_control")
		{
			output["properties"]["token"] = token;
			output["properties"]["lease_seconds"] = {{"type", "integer"}, {"minimum", 1}};
		}
		if (toolName == "ui_inspect" || toolName == "ui_act" || toolName == "ui_wait" ||
			toolName == "ui_acquire_control")
			properties["compact"] = {{"type", "boolean"},
									 {"default", true},
									 {"description", CompactDescription}};
		registry.add(
			{name,
			 description,
			 object(properties, required),
			 std::move(output),
			 annotationsFor(name),
			 [name = std::string(name)](ExecutionContext &context, Json args, std::string client)
			 { return context.submit(name, std::move(args), std::move(client)); },
			 [name = std::string(name), scancodeNames](const Json &args)
			 {
				 if (name == "ui_wait")
				 {
					 const auto &condition = args.at("condition");
					 if (condition.empty())
						 throw std::invalid_argument(
							 "ui_wait.condition is empty; provide one, for example "
							 "{\"screen\": \"game_session\"}");
					 for (const char *state :
						  {"enabled", "selected", "focused", "blocked", "visible", "value"})
						 if (condition.contains(state) && !condition.contains("key"))
							 throw std::invalid_argument(
								 "ui_wait.condition." + std::string(state) +
								 " needs the control's key in condition.key");
				 }
				 if (name == "hive_program")
				 {
					 const auto action = args.at("action").get<std::string>();
					 auto require = [&](const char *key)
					 {
						 if (!args.contains(key))
							 throw std::invalid_argument(
								 "hive_program." + std::string(key) + " is required for action \"" +
								 action + "\"");
					 };
					 if (action != "list")
						 require("name");
					 if (action != "list" && action != "inspect")
					 {
						 for (const char *key : {"token", "session", "operation_key"})
							 require(key);
						 if (args.at("name").get_ref<const std::string &>().empty())
							 throw std::invalid_argument("Program name must not be empty");
					 }
					 if (action == "install" || action == "replace")
						 require("source");
					 if (action == "replace")
						 require("expected_revision");
				 }
				 if (name == "ui_click" || name == "ui_move")
				 {
					 const bool point = args.contains("x") || args.contains("y");
					 if (args.contains("key") == point)
						 throw std::invalid_argument(
							 name + ": give x and y, or the key (and surface) of an inspected "
									"control to use its centre");
					 if (point && !(args.contains("x") && args.contains("y")))
						 throw std::invalid_argument(name + ": x and y go together");
					 if (args.contains("key") && (args.contains("capture_id") || args.contains("space")))
						 throw std::invalid_argument(
							 name + ": key targets the control's centre; capture_id and space "
									"apply only to x/y");
					 if (args.contains("surface") && !args.contains("key"))
						 throw std::invalid_argument(name + ".surface needs key");
				 }
				 if (args.contains("capture_id") && args.value("space", "native") == "logical")
					 throw std::invalid_argument(
						 name + ": capture_id makes x/y screenshot pixels; omit space or capture_id");
				 if (name == "ui_act" && args.contains("wait_for"))
				 {
					 const auto &until = args["wait_for"];
					 const bool named = until.is_object() && until.size() == 1 &&
										until.contains("screen") && until["screen"].is_string() &&
										!until["screen"].get_ref<const std::string &>().empty() &&
										until["screen"].get_ref<const std::string &>().size() <= 128;
					 if (!(until.is_string() && until.get_ref<const std::string &>() == "next_screen") && !named)
						 throw std::invalid_argument(
							 "ui_act.wait_for: use \"next_screen\" or {\"screen\": NAME}");
					 if (args.value("capture_after", false))
						 throw std::invalid_argument(
							 "ui_act.wait_for cannot be combined with capture_after; call "
							 "ui_screenshot once it returns");
				 }
				 if (name == "ui_type_text")
				 {
					 const auto &text = args["text"].get_ref<const std::string &>();
					 if (text.size() > 65536 || text.find('\0') != std::string::npos)
						 throw std::invalid_argument(
							 "Text exceeds 65536 UTF-8 bytes or contains NUL");
				 }
				 if (name == "ui_key")
				 {
					 const auto &key = args["key"].get_ref<const std::string &>();
					 if (key.find('\0') != std::string::npos ||
						 !scancodeNames->contains(normalizedScancodeName(key)))
						 throw std::invalid_argument(
							 "ui_key.key: unknown SDL scancode name \"" + key +
							 "\"; use names such as A, 1, Return, Escape, Space, Tab, Up, F1 "
							 "or Left Ctrl");
					 for (const auto &value : args.value("modifiers", Json::array()))
					 {
						 const auto &key = value.get_ref<const std::string &>();
						 const auto found = scancodeNames->find(normalizedScancodeName(key));
						 if (key.find('\0') != std::string::npos || found == scancodeNames->end() ||
							 found->second < SDL_SCANCODE_LCTRL ||
							 found->second > SDL_SCANCODE_RGUI)
							 throw std::invalid_argument(
								 "ui_key.modifiers: \"" + key +
								 "\" is not a modifier; use Left Ctrl, Left Shift, Left Alt, "
								 "Left GUI or their Right variants");
					 }
				 }
			 }});
	};
	registry.add(
		{"game_guide",
		 "Start here if you do not know Globulation 2. Explains the game (indirect control, food, "
		 "victory), menus, the in-game HUD, placing and selecting on the map, control tokens and "
		 "errors, plus links to the source and docs. topic defaults to overview; search finds "
		 "paragraphs across topics.",
		 object({{"search", {{"type", "string"}, {"maxLength", 256}}}, {"topic", [] {
					  Json names = Json::array();
					  for (const auto &t : guideTopics())
						  names.push_back(t);
					  return Json{{"type", "string"}, {"enum", names}, {"default", "overview"}};
				  }()}}),
		 {{"type", "object"}},
		 {{"readOnlyHint", true},
		  {"destructiveHint", false},
		  {"idempotentHint", true},
		  {"openWorldHint", false}},
		 [](ExecutionContext &, Json args, std::string client)
		 {
			 auto request = std::make_shared<Request>();
			 request->tool = "game_guide";
			 request->client = std::move(client);
			 Result result;
			 result.data = args.contains("search")
							   ? guideSearch(args.at("search").get<std::string>(), args.value("topic", std::string()))
							   : guide(args.value("topic", "overview"));
			 request->completion.set_value(std::move(result));
			 return request;
		 },
		 {}});
	add("ui_status", "Check the Globulation 2 game window: available (false while minimized or behind an OS "
		"dialog), window size, viewport_id and who holds control. No token needed.", {},
		{}, false);
	add("ui_screenshot",
		"PNG of the next rendered game frame, 1280 px wide unless max_width/max_height are set. Pass "
		"the returned capture_id to ui_click/ui_drag/ui_scroll/ui_move so x/y are pixels of "
		"this image. Needed for the map canvas.",
		{{"token", token}}, {}, false);
	add("ui_acquire_control",
		"Take exclusive input control and get the token that every input tool requires. The "
		"lease lasts 300 s after your last token-bearing call. Fails while another client holds "
		"control. inspect=true also returns the current screen's controls.",
		{{"inspect", {{"type", "boolean"}, {"default", false}}}}, {}, false);
	add("ui_release_control", "Release held keys/buttons and give up control (frees the lease for others).", {{"token", token}},
		{"token"}, false);
	add("ui_renew_control", "Extend your control lease by another 300 seconds without sending input.",
		{{"token", token}}, {"token"}, false);
	const Json identity = {{"type", "integer"}, {"minimum", 1}};
	Json filter = {{"surface", identity},
				   {"subtree", {{"type", "string"}, {"maxLength", 512}}},
				   {"token", token}};
	auto inspect = filter;
	inspect["screenshot"] = {{"type", "boolean"}, {"default", false}};
	add("ui_inspect",
		"Read the current screen: its name (inspection.screen) and surfaces, each with an "
		"instance number and controls (key, label, role, actions, value, options or min/max, "
		"revision). Feed surface/key/revision to ui_act. Filter with surface or subtree "
		"key. Bounds are logical pixels (ui_click space=logical), not screenshot pixels. No token needed; "
		"a token renews the lease.",
		inspect, {}, false);
	auto act = Json{{"surface", identity},
					{"key", {{"type", "string"}, {"maxLength", 512}}},
					{"revision", identity},
					{"action", enumeration({"activate", "select", "set_value", "scroll"})},
					{"value", Json::object()},
					{"inspect", {{"type", "boolean"}, {"default", true}}},
					{"result_subtree", {{"type", "string"}, {"maxLength", 512}}},
					{"wait_for",
					 {{"description", "\"next_screen\" or {\"screen\": NAME}: return once that screen shows"}}},
					{"timeout_ms", {{"type", "integer"}, {"minimum", 1}, {"maximum", 30000}, {"default", 10000}}}};
	add("ui_act",
		"Operate a control from ui_inspect by surface (instance), key and revision. action "
		"must be one the control lists: activate, select (value: option label or zero-based "
		"index), set_value (value matching the control, within min..max) or scroll (value: "
		"signed logical pixels). Returns the inspection once consumed (inspect=false: screen "
		"only; result_subtree trims it). wait_for holds the reply until a screen change "
		"(timeout_ms). Never retry automatically.",
		act, {"surface", "key", "revision", "action"}, true);
	auto wait = filter;
	wait["inspect"] = {{"type", "boolean"}, {"default", true}};
	wait["timeout_ms"] = {
		{"type", "integer"}, {"minimum", 1}, {"maximum", 30000}, {"default", 10000}};
	wait["condition"] = object({{"screen", {{"type", "string"}, {"maxLength", 128}}},
								{"key", {{"type", "string"}, {"maxLength", 512}}},
								{"enabled", {{"type", "boolean"}}},
								{"selected", {{"type", "boolean"}}},
								{"focused", {{"type", "boolean"}}},
								{"blocked", {{"type", "boolean"}}},
								{"visible", {{"type", "boolean"}}},
								{"value", Json::object()},
								{"revision_after", {{"type", "integer"}, {"minimum", 0}}}});
	add("ui_wait",
		"Wait (default 10 s, max 30 s) until a condition holds, e.g. {\"screen\": "
		"\"game_session\"} after starting a match, or a control key with enabled/value/"
		"revision_after. Does not block input; "
		"on timeout returns the latest snapshot with satisfied=false.",
		wait, {"condition"}, false);

	add("game_world",
		"Your colony in map tiles: status (top-bar text; units_free and win_chance as numbers), "
		"alerts (losses, conversions, attacks with tiles), losses, buildings (id, type, top-left "
		"x/y, size, site, workers, stock/capacity, Inn happiness), unit counts, known resources "
		"(nearest tile, largest fields; area restricts), other teams' visible and remembered buildings, recent placements, "
		"placeable names. detail brief: status, alerts, losses, one line per building; "
		"alerts: no buildings; full adds unit lists. since_tick: only newer alerts. No token.",
		{{"detail", enumeration({"brief", "alerts", "summary", "full"})},
		 {"since_tick", {{"type", "integer"}, {"minimum", 0}}},
		 {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 100}}},
		 {"resource_radius", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}, {"default", 24}}},
		 {"resource_fields", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}, {"default", 3}}},
		 {"area", object({{"x", {{"type", "integer"}, {"minimum", 0}}},
						  {"y", {{"type", "integer"}, {"minimum", 0}}},
						  {"width", {{"type", "integer"}, {"minimum", 1}}},
						  {"height", {{"type", "integer"}, {"minimum", 1}}}},
						 {"x", "y", "width", "height"})}},
		{}, false);
	add("game_stats",
		"Charts over time: your colony in a match; every team on end_game. list: true names "
		"metrics, split shows parts, points (2-300). No token.",
		{{"list", {{"type", "boolean"}}},
		 {"metrics", {{"type", "array"}, {"items", {{"type", "string"}}}}},
		 {"points", {{"type", "integer"}}},
		 {"split", {{"type", "boolean"}}}},
		{}, false);
	add("game_place",
		"Place a building site or flag by tile, as a click would: building is a game_world "
		"placeable name, x/y its top-left tile; near searches nearby for room and returns "
		"where it went. workers/builders/range (flags) set the create order's requests (capped, "
		"reported under settings); priority follows once it appears. Needs token.",
		{{"token", token},
		 {"building", {{"type", "string"}, {"maxLength", 64}}},
		 {"x", {{"type", "integer"}, {"minimum", 0}}},
		 {"y", {{"type", "integer"}, {"minimum", 0}}},
		 {"near", {{"type", "boolean"}, {"default", false}}},
		 {"search_radius", {{"type", "integer"}, {"minimum", 1}, {"maximum", 32}, {"default", 12}}},
		 {"workers", {{"type", "integer"}, {"minimum", 0}, {"maximum", 20}}},
		 {"builders", {{"type", "integer"}, {"minimum", 0}, {"maximum", 20}}},
		 {"range", {{"type", "integer"}, {"minimum", 0}, {"maximum", 32767}}},
		 {"priority", enumeration({"low", "medium", "high"})}},
		{"token", "building", "x", "y"}, false);
	add("game_building",
		"Change one of your buildings or flags by its game_world id and generation: workers "
		"(units requested), priority, production ratios (Swarm), range (flags; both capped at the "
		"building's maximum, reported), or an action (move takes the flag's x/y). Needs token.",
		{{"token", token},
		 {"id", {{"type", "integer"}, {"minimum", 0}}},
		 {"generation", {{"type", "integer"}, {"minimum", 0}}},
		 {"workers", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}},
		 {"priority", enumeration({"low", "medium", "high"})},
		 {"production", object({{"workers", {{"type", "integer"}, {"minimum", 0}, {"maximum", 16}}},
								{"explorers", {{"type", "integer"}, {"minimum", 0}, {"maximum", 16}}},
								{"warriors", {{"type", "integer"}, {"minimum", 0}, {"maximum", 16}}}})},
		 {"range", {{"type", "integer"}, {"minimum", 0}, {"maximum", 64}}},
		 {"action", enumeration({"upgrade", "repair", "cancel_upgrade", "destroy", "cancel_destroy", "move"})},
		 {"x", {{"type", "integer"}, {"minimum", 0}}},
		 {"y", {{"type", "integer"}, {"minimum", 0}}},
		 {"builders", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}},
		{"token", "id", "generation"}, false);
	add("game_zone",
		"Paint or erase a zone over a tile rectangle: forbidden (units keep out), guard "
		"(warriors defend it) or clearing (workers clear resources). Needs token.",
		{{"token", token},
		 {"zone", enumeration({"forbidden", "guard", "clearing"})},
		 {"mode", enumeration({"paint", "erase"})},
		 {"x", {{"type", "integer"}, {"minimum", 0}}},
		 {"y", {{"type", "integer"}, {"minimum", 0}}},
		 {"width", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}},
		 {"height", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}}},
		{"token", "zone", "x", "y", "width", "height"}, false);
	add("game_view",
		"Centre the camera on a tile; returns its screen point (tile_screen) for clicking "
		"what is there; zoom 1 is 100%, 0 fits the map; width/height frame that rectangle. "
		"Needs token.",
		{{"token", token},
		 {"x", {{"type", "integer"}, {"minimum", 0}}},
		 {"y", {{"type", "integer"}, {"minimum", 0}}},
		 {"width", {{"type", "integer"}, {"minimum", 1}, {"maximum", 256}}},
		 {"height", {{"type", "integer"}, {"minimum", 1}, {"maximum", 256}}},
		 {"zoom", {{"type", "number"}, {"minimum", 0}, {"maximum", 5}}}},
		{"token", "x", "y"}, false);
	add("game_chat",
		"Send an in-game chat message as your player, as the chat box's Send would. "
		"Plain text on one line, at most 254 bytes of UTF-8; control characters and "
		"whitespace-only text are refused. "
		"Needs token.",
		{{"token", token}, {"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 254}}}},
		{"token", "text"}, false);
	add("game_time",
		"Pause the local match while you think, resume, or step ticks (30/s) and re-pause. "
		"speed sets game speed: -3..9 are 0.25, 0.5, 0.75, 1, 1.25, 1.6, 2, 2.5, 4, 5, 8, 13, 40x; "
		"10 is unlimited. action defaults to status (no token unless speed).",
		{{"action", enumeration({"status", "pause", "resume", "step"})},
		 {"token", token},
		 {"ticks", {{"type", "integer"}, {"minimum", 1}, {"maximum", 3000}}},
		 {"speed", {{"type", "integer"}, {"minimum", -3}, {"maximum", 10}}}},
		{}, false);
	add("game_wait",
		"Let game time pass until ticks elapse (30/s), an until event hits your colony "
		"(default: all but building_completed) or timeout_ms; returns why, plus game_world "
		"alerts since. Use instead of polling. No token.",
		{{"ticks", {{"type", "integer"}, {"minimum", 1}, {"maximum", 9000}, {"default", 300}}},
		 {"until", {{"type", "array"},
					{"maxItems", 6},
					{"items", enumeration({"attack", "building_completed", "building_lost", "conversion",
										   "enemy_sighted", "hive_failure"})}}},
		 {"timeout_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 30000}}}},
		{}, false);
	add("game_alliance",
		"The Alliance dialog: with only token, lists teams and your settings; with team, sets "
		"allied or shared map, food (Inn) or market vision (unset fields stay). Teams Fixed "
		"locks only allied. Needs token.",
		{{"token", token},
		 {"team", {{"type", "integer"}, {"minimum", 0}, {"maximum", 31}}},
		 {"allied", {{"type", "boolean"}}},
		 {"share_map", {{"type", "boolean"}}},
		 {"share_food_vision", {{"type", "boolean"}}},
		 {"share_market", {{"type", "boolean"}}}},
		{"token"}, false);
	add("hive_status", "Whether colony scripting (Hive) is available in the current local match, and its "
		"session id, required by hive_run/hive_result/hive_program. No token needed.", {}, {},
		false);
	add("hive_api", "The JavaScript reference for Hive colony scripts, by section: no arguments lists "
		"sections; section reads one; search finds paragraphs. Read it before hive_run.",
		{{"section", {{"type", "string"}, {"maxLength", 128}}}, {"search", {{"type", "string"}, {"maxLength", 256}}}},
		{}, false);
	const Json operationKey = {{"type", "string"}, {"maxLength", 128}};
	const Json source = {{"type", "string"}, {"maxLength", 131072}};
	const Json session = {{"type", "string"}, {"maxLength", 128}};
	add("hive_run",
		"Run one JavaScript step(ctx) script for your colony, to read exact world state or "
		"issue orders. Needs token, the hive_status session and a new operation_key; "
		"wait=true returns the finished result, else read hive_result.",
		{{"token", token},
		 {"session", session},
		 {"operation_key", operationKey},
		 {"source", source},
		 {"wait", {{"type", "boolean"}}}},
		{"token", "session", "operation_key", "source"}, false);
	add("hive_result", "Read a hive_run or hive_program operation's output, errors and order results by "
		"session and operation_key. Never re-runs it; wait=true waits for the end.",
		{{"session", session},
		 {"operation_key", operationKey},
		 {"wait", {{"type", "boolean"}}}},
		{"session", "operation_key"},
		false);
	add("hive_program",
		"Manage standing scripts that rerun every interval_ticks for this match: list, inspect, "
		"install, replace (expected_revision), pause, resume, cancel. Changes need token, session "
		"and operation_key.",
		{{"action",
		  enumeration({"list", "inspect", "install", "replace", "pause", "resume", "cancel"})},
		 {"token", token},
		 {"session", session},
		 {"operation_key", operationKey},
		 {"name", {{"type", "string"}, {"maxLength", 128}}},
		 {"source", source},
		 {"description", {{"type", "string"}, {"maxLength", 2048}}},
		 {"expected_revision", {{"type", "integer"}, {"minimum", 1}}},
		 {"interval_ticks", {{"type", "integer"}, {"minimum", 25}, {"maximum", 1000000}}}},
		{"action"}, false);

	Json xy = {{"x", coordinate}, {"y", coordinate}};
	// Pointing at an inspected control needs no pixel arithmetic.
	auto pointAt = xy;
	pointAt["surface"] = identity;
	pointAt["key"] = {{"type", "string"}, {"maxLength", 512}};
	add("ui_move", "Move the pointer (hover) to x/y, or to the centre of the control key "
		"(surface optional) from ui_inspect.", pointAt, {}, true, true);
	auto click = pointAt;
	click["button"] = button;
	click["button"]["default"] = "left";
	click["count"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 2}, {"default", 1}};
	add("ui_click", "Click x/y, or the centre of the control key (surface optional) from "
		"ui_inspect, e.g. to place or select on the map. x/y are native pixels unless "
		"capture_id or space=logical. Right button cancels placement.", click, {}, true,
		true);
	auto mouse = xy;
	mouse["button"] = button;
	mouse["action"] = enumeration({"down", "up"});
	add("ui_mouse_button",
		"Press (down) or release (up) a mouse button at x/y across calls. Holds expire after 5 s. "
		"Prefer ui_click or ui_drag.", mouse,
		{"x", "y", "button", "action"}, true, true);
	auto scroll = xy;
	scroll["dx"] = number;
	scroll["dy"] = number;
	add("ui_scroll",
		"Mouse wheel at x/y: zooms the map, scrolls lists. Positive dy is up (zoom in), "
		"positive dx right; dx defaults to 0.",
		scroll, {"x", "y", "dy"}, true, true);
	add("ui_drag", "Drag the mouse through points over duration_ms (default 250, maximum 5000): paints "
		"zones or pans the map.",
		{{"points",
		  {{"type", "array"},
		   {"minItems", 2},
		   {"maxItems", 128},
		   {"items", object(xy, {"x", "y"})}}},
		 {"button", button},
		 {"duration_ms",
		  {{"type", "integer"}, {"minimum", 1}, {"maximum", 5000}, {"default", 250}}}},
		{"points"}, true, true);
	add("ui_key",
		"Press, hold or release a key by SDL scancode name (A, Return, Escape, Space, Left "
		"Ctrl). Escape cancels placement or selection, then opens the game menu. "
		"modifiers are extra scancode names; action defaults to press.",
		{{"key", key},
		 {"action", action},
		 {"modifiers", {{"type", "array"}, {"maxItems", 8}, {"items", key}}}},
		{"key"}, true);
	add("ui_type_text",
		"Type UTF-8 text into the focused text field (focus it first, e.g. with ui_act).",
		{{"text", {{"type", "string"}, {"maxLength", 65536}}}}, {"text"}, true);
	add("ui_release_inputs", "Release every key and mouse button you hold down; you keep control.", {}, {},
		true);
}
// Why a ui_act target cannot be used, phrased with the next step; empty when valid.
std::string actionTargetProblem(const Json &snapshot, const Json &a, Json *resolved)
{
	const auto key = a["key"].get<std::string>();
	const auto surfaceId = a["surface"].dump();
	const Json *surface = nullptr;
	std::string surfaces;
	for (const auto &s : snapshot["surfaces"])
	{
		surfaces += (surfaces.empty() ? "" : ", ") + s["instance"].dump();
		if (s["instance"] == a["surface"])
			surface = &s;
	}
	if (!surface)
		return "Unknown surface " + surfaceId + ": not shown on screen \"" +
			   snapshot.value("screen", "") + "\" (surfaces now: " +
			   (surfaces.empty() ? "none" : surfaces) +
			   "). A dialog may cover it (close it first), or the screen changed; ui_inspect again";
	const Json *found = nullptr;
	auto find = [&](auto &&self, const Json &items) -> void
	{
		for (const auto &item : items)
		{
			if (!found && item["key"] == key)
				found = &item;
			self(self, item["children"]);
		}
	};
	find(find, (*surface)["controls"]);
	if (!found)
	{
		std::string elsewhere;
		for (const auto &s : snapshot["surfaces"])
		{
			const Json *other = nullptr;
			auto search = [&](auto &&self, const Json &items) -> void
			{
				for (const auto &item : items)
				{
					if (!other && item["key"] == key)
						other = &item;
					self(self, item["children"]);
				}
			};
			search(search, s["controls"]);
			if (other)
				elsewhere = s["instance"].dump();
		}
		return "Unknown control: no control with key \"" + key + "\" on surface " + surfaceId +
			   (elsewhere.empty() ? "; ui_inspect again to find the current controls"
								  : "; it is on surface " + elsewhere + " now");
	}
	const auto &item = *found;
	if (item["revision"] != a["revision"])
		return "Stale control: \"" + key + "\" is at revision " + item["revision"].dump() +
			   ", not " + a["revision"].dump() +
			   ". Its state changed since you inspected it; check it, then act with the new "
			   "revision";
	if (item["enabled"] != true)
		return "Disabled control: \"" + key +
			   "\" cannot be used now (for example a building the rules or lesson do not allow "
			   "yet)";
	if (item["visible"]["w"].get<int>() <= 0 || item["visible"]["h"].get<int>() <= 0)
		return "Hidden control: \"" + key +
			   "\" is scrolled out of view or covered; scroll its container (ui_act scroll) "
			   "until it is visible";
	if (item["blocked"] != false)
		return "Blocked control: a dialog or overlay covers \"" + key +
			   "\"; close or answer it first";
	const auto action = a["action"].get<std::string>();
	const auto &actions = item["actions"];
	if (actions.is_array() && std::find(actions.begin(), actions.end(), action) == actions.end())
		return "Unsupported action: \"" + key + "\" accepts " + actions.dump() + ", not \"" +
			   action + "\"";
	// Check the value against what the control lists, before any input is dispatched.
	Json value = a.contains("value") ? a["value"] : Json();
	const auto options = item.value("options", Json::array());
	if (action == "select" && !options.empty())
	{
		const auto count = options.size();
		auto listed = [&]
		{
			std::string text;
			for (std::size_t i = 0; i < count && i < 40; ++i)
				text += (i ? ", " : "") + std::to_string(i) + " " + options[i].dump();
			if (count > 40)
				text += ", ... (" + std::to_string(count) + " in all)";
			return text;
		};
		if (value.is_string())
		{
			// A label: exact match first, else a unique case-insensitive one.
			auto lower = [](std::string text)
			{
				for (auto &c : text)
					c = char(std::tolower(static_cast<unsigned char>(c)));
				return text;
			};
			const auto wanted = value.get<std::string>();
			std::optional<std::size_t> exact, loose;
			int looseMatches = 0;
			for (std::size_t i = 0; i < count; ++i)
			{
				if (!options[i].is_string())
					continue;
				const auto &label = options[i].get_ref<const std::string &>();
				if (!exact && label == wanted)
					exact = i;
				else if (lower(label) == lower(wanted))
				{
					loose = i;
					++looseMatches;
				}
			}
			if (exact)
				value = *exact;
			else if (looseMatches == 1)
				value = *loose;
			else
				return "Unknown option " + value.dump() + " for \"" + key + "\"; its options are " +
					   listed() + ". select takes one of these labels or its index";
		}
		if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() >= double(count))
			return "Option out of range for \"" + key + "\": select takes an option label or an index 0.." +
				   std::to_string(count - 1) + " (" + listed() + ")";
		const auto index = value.get<std::size_t>();
		for (const auto &disabled : item.value("disabled_options", Json::array()))
			if (disabled == index)
				return "Unavailable option: " + options[index].dump() + " (index " + std::to_string(index) +
					   ") of \"" + key + "\" cannot be chosen now; its options are " + listed();
	}
	else if (action == "select" && value.is_string())
		return "\"" + key + "\" lists no options to match " + value.dump() +
			   "; select takes a zero-based index";
	if (action == "set_value" && item.contains("min") && item.contains("max"))
	{
		const auto &low = item["min"], &high = item["max"];
		if (!value.is_number_integer() || value.get<double>() < low.get<double>() ||
			value.get<double>() > high.get<double>())
			return "Value out of range for \"" + key + "\": set_value takes an integer " + low.dump() + ".." +
				   high.dump() +
				   (item.value("step", 1) > 1 ? " in steps of " + item["step"].dump() : std::string()) +
				   (value.is_null() ? std::string() : ", not " + value.dump());
	}
	if (resolved)
		*resolved = std::move(value);
	return {};
}
std::string controlCentre(const Json &snapshot, const Json &a, Json &point)
{
	const auto key = a["key"].get<std::string>();
	Json found;
	std::string where;
	int matches = 0;
	for (const auto &surface : snapshot["surfaces"])
	{
		if (a.contains("surface") && surface["instance"] != a["surface"])
			continue;
		auto search = [&](auto &&self, const Json &items) -> void
		{
			for (const auto &item : items)
			{
				if (item.value("key", "") == key)
				{
					if (!matches++)
						found = item;
					where += (where.empty() ? "" : ", ") + surface["instance"].dump();
				}
				if (item.contains("children"))
					self(self, item["children"]);
			}
		};
		if (surface.contains("controls"))
			search(search, surface["controls"]);
	}
	if (!matches)
		return "Unknown control: no control with key \"" + key + "\"" +
			   (a.contains("surface") ? " on surface " + a["surface"].dump() : std::string(" on screen")) +
			   "; ui_inspect to find the current controls";
	if (matches > 1)
		return "Ambiguous control: \"" + key + "\" appears more than once (surfaces " + where +
			   "); pass surface, or click by x/y";
	const auto visible = found.value("visible", Json::object());
	const int w = visible.value("w", 0), h = visible.value("h", 0);
	if (w <= 0 || h <= 0)
		return "Hidden control: \"" + key +
			   "\" is scrolled out of view or covered; scroll its container (ui_act scroll) until it is visible";
	if (found.value("blocked", false))
		return "Blocked control: a dialog or overlay covers \"" + key + "\"; close or answer it first";
	point = {{"x", visible.value("x", 0) + w / 2.0}, {"y", visible.value("y", 0) + h / 2.0}};
	return {};
}
Json compactInspection(const Json &inspection)
{
	auto controls = [](auto &&self, const Json &items) -> Json
	{
		Json out = Json::array();
		for (const auto &item : items)
		{
			auto children = self(self, item.value("children", Json::array()));
			const bool inert = item.value("key", "").empty() && item.value("label", "").empty() &&
							   item.value("actions", Json::array()).empty() &&
							   item.value("value", Json()).is_null();
			if (inert)
			{
				// Layout containers carry no meaning of their own; keep their contents.
				for (auto &child : children)
					out.push_back(std::move(child));
				continue;
			}
			Json c = Json::object();
			for (const char *field : {"key", "label", "role"})
				if (!item.value(field, "").empty())
					c[field] = item[field];
			if (!item.value("actions", Json::array()).empty())
			{
				c["actions"] = item["actions"];
				c["revision"] = item.value("revision", 0);
			}
			if (item.contains("value") && !item["value"].is_null())
				c["value"] = item["value"];
			// Choices list their options (select takes a label or index); lists, whose rows
			// are children, and long catalogues give only the count.
			if (item.contains("options") && item["options"].is_array())
			{
				const auto &options = item["options"];
				if (item.value("role", "") == "list" || options.size() > 24)
					c["option_count"] = options.size();
				else
					c["options"] = options;
				if (item.contains("disabled_options"))
					c["disabled_options"] = item["disabled_options"];
				if (item.contains("value_label") && item["value_label"] != item.value("label", Json()))
					c["value_label"] = item["value_label"];
			}
			for (const char *field : {"min", "max"})
				if (item.contains(field))
					c[field] = item[field];
			if (item.contains("step") && item["step"] != 1)
				c["step"] = item["step"];
			if (!item.value("enabled", true))
				c["enabled"] = false;
			for (const char *flag : {"selected", "focused", "blocked"})
				if (item.value(flag, false))
					c[flag] = true;
			c["bounds"] = item.value("bounds", Json::object());
			const auto visible = item.value("visible", Json::object());
			if (visible.value("w", 0) <= 0 || visible.value("h", 0) <= 0)
				c["hidden"] = true;
			else if (visible != c["bounds"])
				c["visible"] = visible;
			if (!children.empty())
				c["children"] = std::move(children);
			out.push_back(std::move(c));
		}
		// Off-screen controls (a long scrolled list) collapse into one entry naming them,
		// so a picker of 160 tiles is not 25 KB: scroll their container to reach one.
		Json kept = Json::array(), run = Json::array();
		const auto flush = [&]
		{
			if (run.size() == 1)
				kept.push_back(run[0]);
			else if (!run.empty())
			{
				Json names = Json::array();
				for (const auto &h : run)
					if (h.contains("key"))
						names.push_back(h.value("label", std::string()).empty()
											? h["key"].get<std::string>()
											: h["key"].get<std::string>() + " " + h["label"].get<std::string>());
				kept.push_back({{"hidden_controls", run.size()}, {"keys", names},
								{"note", "Scrolled out of view: ui_act scroll on their container, then act"}});
			}
			run = Json::array();
		};
		for (auto &c : out)
			if (c.value("hidden", false) && !c.contains("children") || (c.value("hidden", false) && c["children"].empty()))
				run.push_back(std::move(c));
			else if (c.value("hidden", false))
			{
				// A hidden composite (a tile with its label and preview): keep only its key/label.
				Json brief = {{"hidden", true}};
				for (const char *f : {"key", "label"})
					if (c.contains(f))
						brief[f] = c[f];
				if (!brief.contains("label"))
					for (const auto &child : c["children"])
						if (child.contains("label"))
						{
							brief["label"] = child["label"];
							break;
						}
				run.push_back(std::move(brief));
			}
			else
			{
				flush();
				kept.push_back(std::move(c));
			}
		flush();
		return kept;
	};
	Json result = inspection;
	for (auto &surface : result["surfaces"])
	{
		surface["controls"] = controls(controls, surface.value("controls", Json::array()));
		if (surface.contains("viewport") && surface["viewport"] == surface.value("bounds", Json()))
			surface.erase("viewport");
	}
	return result;
}
Json toolResult(Result result, const std::string &png)
{
	Json data = std::move(result.data);
	if (!result.error.empty())
		data["error"] = result.error;
	Json content = Json::array(
		{{{"type", "text"}, {"text", data.dump(-1, ' ', false, Json::error_handler_t::replace)}}});
	if (!png.empty())
		content.push_back({{"type", "image"}, {"mimeType", "image/png"}, {"data", png}});
	return {{"content", content}, {"structuredContent", data}, {"isError", !result.error.empty()}};
}
} // namespace MCP
