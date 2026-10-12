// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tools.h"
#include <cmath>
#include <algorithm>
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
	auto fail = [&] { throw std::invalid_argument("Invalid argument: " + path); };
	const auto type = schema.value("type", "");
	if ((type == "object" && !value.is_object()) || (type == "array" && !value.is_array()) ||
		(type == "string" && !value.is_string()) || (type == "boolean" && !value.is_boolean()) ||
		(type == "integer" && !value.is_number_integer()) ||
		(type == "number" && !value.is_number()))
		fail();
	if (schema.contains("enum") &&
		std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end())
		fail();
	if (value.is_number())
	{
		const double n = value.get<double>();
		if (!std::isfinite(n) ||
			(schema.contains("minimum") && n < schema["minimum"].get<double>()) ||
			(schema.contains("maximum") && n > schema["maximum"].get<double>()))
			fail();
	}
	if (value.is_string() && schema.contains("maxLength") &&
		std::size_t(std::count_if(value.get_ref<const std::string &>().begin(),
								  value.get_ref<const std::string &>().end(),
								  [](unsigned char c) { return (c & 0xc0) != 0x80; })) >
			schema["maxLength"].get<std::size_t>())
		fail();
	if (value.is_array())
	{
		if ((schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) ||
			(schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()))
			fail();
		for (const auto &item : value)
			validate(schema.at("items"), item, path + "[]");
	}
	if (value.is_object())
	{
		for (const auto &key : schema.value("required", Json::array()))
			if (!value.contains(key.get<std::string>()))
				fail();
		const auto properties = schema.value("properties", Json::object());
		for (const auto &[key, item] : value.items())
		{
			if (!properties.contains(key))
			{
				if (!schema.value("additionalProperties", true))
					fail();
			}
			else
				validate(properties[key], item, path + "." + key);
		}
	}
}
Json object(Json properties, Json required = Json::array())
{
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
		result.push_back({{"name", name},
						  {"description", t.description},
						  {"inputSchema", t.inputSchema},
						  {"outputSchema", t.outputSchema},
						  {"annotations", t.annotations}});
	return result;
}
std::shared_ptr<Request> ToolRegistry::call(ExecutionContext &context, const std::string &name,
											Json args, std::string client) const
{
	const auto found = tools.find(name);
	if (found == tools.end())
		throw std::invalid_argument("Unknown tool: " + name);
	validate(found->second.inputSchema, args, name);
	if (found->second.validate)
		found->second.validate(args);
	auto request = found->second.handler(context, std::move(args), std::move(client));
	if (!request)
		throw std::logic_error("MCP tool handler returned no request: " + name);
	return request;
}
void registerUITools(ToolRegistry &registry)
{
	const auto scancodeNames = snapshotScancodeNames();
	const Json number = {{"type", "number"}, {"minimum", -10000}, {"maximum", 10000}};
	const Json coordinate = {{"type", "number"}, {"minimum", 0}};
	const Json token = {{"type", "string"}, {"maxLength", 128}};
	const Json key = {{"type", "string"}, {"maxLength", 64}};
	const Json button = enumeration({"left", "middle", "right"});
	const Json action = enumeration({"press", "down", "up"});
	auto add = [&](const char *name, const char *description, Json properties, Json required,
				   bool input, bool coordinates = false)
	{
		if (input)
		{
			properties["token"] = token;
			required.push_back("token");
			properties["capture_after"] = {{"type", "boolean"}, {"default", false}};
		}
		if (coordinates)
			properties["viewport_id"] = {{"type", "integer"}, {"minimum", 0}};
		Json output = {{"type", "object"},
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
		registry.add(
			{name,
			 description,
			 object(properties, required),
			 std::move(output),
			 {{"readOnlyHint", !input && std::string(name) != "ui_acquire_control" &&
								   std::string(name) != "ui_release_control"},
			  {"destructiveHint", input},
			  {"idempotentHint", false},
			  {"openWorldHint", false}},
			 [name = std::string(name)](ExecutionContext &context, Json args, std::string client)
			 { return context.submit(name, std::move(args), std::move(client)); },
			 [name = std::string(name), scancodeNames](const Json &args)
			 {
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
						 throw std::invalid_argument("Unknown SDL scancode name");
					 for (const auto &value : args.value("modifiers", Json::array()))
					 {
						 const auto &key = value.get_ref<const std::string &>();
						 const auto found = scancodeNames->find(normalizedScancodeName(key));
						 if (key.find('\0') != std::string::npos || found == scancodeNames->end() ||
							 found->second < SDL_SCANCODE_LCTRL ||
							 found->second > SDL_SCANCODE_RGUI)
							 throw std::invalid_argument("Unknown modifier scancode name");
					 }
				 }
			 }});
	};
	add("ui_status", "Game viewport, frame and controller status. Does not require control.", {},
		{}, false);
	add("ui_screenshot",
		"Return the next rendered game frame as an inline PNG. Coordinates are image pixels from "
		"the top left.",
		{}, {}, false);
	add("ui_acquire_control",
		"Acquire exclusive MCP input control. Returns a token with a 60 second idle lease. Human "
		"input stays active.",
		{}, {}, false);
	add("ui_release_control", "Release control and all remote-held inputs.", {{"token", token}},
		{"token"}, false);
	Json xy = {{"x", coordinate}, {"y", coordinate}};
	add("ui_move", "Move the game pointer in screenshot pixels.", xy, {"x", "y"}, true, true);
	auto click = xy;
	click["button"] = button;
	click["button"]["default"] = "left";
	click["count"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 2}, {"default", 1}};
	add("ui_click", "Single or double click; button defaults to left.", click, {"x", "y"}, true,
		true);
	auto mouse = xy;
	mouse["button"] = button;
	mouse["action"] = enumeration({"down", "up"});
	add("ui_mouse_button",
		"Hold or release a mouse button. Holds expire after 5 seconds unless refreshed.", mouse,
		{"x", "y", "button", "action"}, true, true);
	auto scroll = xy;
	scroll["dx"] = number;
	scroll["dy"] = number;
	add("ui_scroll",
		"Scroll at the supplied image coordinates; positive dy scrolls up, positive dx right.",
		scroll, {"x", "y", "dx", "dy"}, true, true);
	add("ui_drag", "Drag through image coordinates over duration_ms (default 250, maximum 5000).",
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
		"Press, hold or release an SDL scancode name (for example A, Return, Left Ctrl). modifiers "
		"are additional scancode names; action defaults to press.",
		{{"key", key},
		 {"action", action},
		 {"modifiers", {{"type", "array"}, {"maxItems", 8}, {"items", key}}}},
		{"key"}, true);
	add("ui_type_text",
		"Enter UTF-8 text in the focused game text control. Does not use the clipboard or OS "
		"keyboard.",
		{{"text", {{"type", "string"}, {"maxLength", 65536}}}}, {"text"}, true);
	add("ui_release_inputs", "Release all keys and mouse buttons held by this controller.", {}, {},
		true);
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
