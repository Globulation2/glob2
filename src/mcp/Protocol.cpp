// SPDX-License-Identifier: GPL-3.0-or-later
#include "Protocol.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <limits>
#include <stdexcept>

namespace MCP
{
namespace
{
constexpr auto ModernVersion = "2026-07-28";
constexpr auto LegacyVersion = "2025-11-25";
const Json ServerInfo = {{"name", "globulation2-ui"}, {"version", "1.0"}};

bool validID(const Json &id)
{
	return id.is_string() || id.is_number_integer();
}

bool validImplementation(const Json &info)
{
	return info.is_object() && info.contains("name") && info["name"].is_string() &&
		   info.contains("version") && info["version"].is_string();
}

std::string decodeHeader(const std::string &value)
{
	if (!value.starts_with("=?base64?") || !value.ends_with("?="))
		return value;

	const auto encoded = value.substr(9, value.size() - 11);
	if (encoded.empty() || encoded.size() % 4)
		throw std::invalid_argument("Malformed encoded header");

	std::string decoded(encoded.size() / 4 * 3, '\0');
	int size = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(decoded.data()),
							   reinterpret_cast<const unsigned char *>(encoded.data()),
							   int(encoded.size()));
	if (size < 0)
		throw std::invalid_argument("Malformed encoded header");
	if (encoded.ends_with("="))
		--size;
	if (encoded.ends_with("=="))
		--size;
	decoded.resize(size);
	// Reject malformed padding and noncanonical encodings as well as invalid alphabet bytes.
	if (encodeBase64(decoded.data(), decoded.size()) != encoded)
		throw std::invalid_argument("Malformed encoded header");
	return decoded;
}

std::string clientName(const Json &params)
{
	std::string name = "Local MCP client";
	const auto meta = params.value("_meta", Json::object());
	if (meta.is_object() && meta.contains("io.modelcontextprotocol/clientInfo"))
		name = meta["io.modelcontextprotocol/clientInfo"]["name"].get<std::string>();
	if (name.size() > 128)
	{
		std::size_t end = 128;
		while (end && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
			--end;
		name.resize(end);
	}
	return name;
}
} // namespace

std::string encodeBase64(const void *bytes, std::size_t length)
{
	if (length > std::size_t(std::numeric_limits<int>::max() / 4 * 3))
		throw std::runtime_error("Encoded image too large");
	std::string out(4 * ((length + 2) / 3) + 1, '\0');
	if (length)
		EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()),
						static_cast<const unsigned char *>(bytes), int(length));
	out.resize(4 * ((length + 2) / 3));
	return out;
}

std::string randomToken()
{
	unsigned char bytes[32];
	if (RAND_bytes(bytes, sizeof(bytes)) != 1)
		throw std::runtime_error("Cannot generate controller token");
	return encodeBase64(bytes, sizeof(bytes));
}

Json Protocol::response(Json id, Json result, bool modern)
{
	if (modern)
	{
		result["resultType"] = "complete";
		result["_meta"]["io.modelcontextprotocol/serverInfo"] = ServerInfo;
	}
	return {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"result", std::move(result)}};
}

Reply Protocol::receive(const std::string &body, const Headers &headers)
{
	Reply reply;
	reply.modern = headers.version != LegacyVersion;
	auto error = [&](int status, int code, const std::string &message, Json data = nullptr)
	{
		reply.status = status;
		reply.body = {{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", message}}}};
		if (!reply.id.is_null() || !reply.modern)
			reply.body["id"] = reply.id;
		if (!data.is_null())
			reply.body["error"]["data"] = std::move(data);
		return reply;
	};

	Json request;
	try
	{
		request = Json::parse(body);
	}
	catch (const Json::exception &)
	{
		return error(400, -32700, "Invalid JSON");
	}
	// Preserve any usable ID even when another envelope field is malformed.
	if (request.is_object() && request.contains("id") && validID(request["id"]))
		reply.id = request["id"];
	if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" ||
		!request.contains("method") || !request["method"].is_string() ||
		request.contains("result") || request.contains("error") ||
		(request.contains("id") && !validID(request["id"])))
		return error(400, -32600, "Invalid JSON-RPC request");
	if (request.contains("params") && !request["params"].is_object())
		return error(400, -32602, "Parameters must be an object");

	if (headers.duplicated)
		return error(400, -32020, "Duplicate MCP metadata header");

	const auto method = request["method"].get<std::string>();
	const auto params = request.value("params", Json::object());
	auto version = headers.version;
	// Legacy initialize is the sole exception to the required version header.
	if (method == "initialize" && version.empty())
		version = LegacyVersion;
	reply.modern = version != LegacyVersion;
	if (version.empty())
		return error(400, -32020, "Missing MCP-Protocol-Version header");
	if (version != ModernVersion && version != LegacyVersion)
		return error(400, -32022, "Unsupported protocol version",
					 {{"supported", {ModernVersion, LegacyVersion}}, {"requested", version}});

	const auto meta = params.value("_meta", Json::object());
	if (!meta.is_object())
		return error(400, -32602, "Metadata must be an object");
	if (meta.contains("io.modelcontextprotocol/clientInfo") &&
		!validImplementation(meta["io.modelcontextprotocol/clientInfo"]))
		return error(400, -32602, "Invalid clientInfo");

	if (reply.modern && request.contains("id"))
	{
		if (meta.value("io.modelcontextprotocol/protocolVersion", Json()) != version ||
			headers.method != method)
			return error(400, -32020, "Protocol version or method header mismatch");
		if (!meta.contains("io.modelcontextprotocol/clientCapabilities") ||
			!meta["io.modelcontextprotocol/clientCapabilities"].is_object())
			return error(400, -32602, "Missing client capabilities metadata");
		if (method == "tools/call")
		{
			try
			{
				if (headers.name.empty() ||
					params.value("name", Json()) != decodeHeader(headers.name))
					return error(400, -32020, "Tool name header mismatch");
			}
			catch (const std::invalid_argument &e)
			{
				return error(400, -32020, e.what());
			}
		}
	}
	if (!request.contains("id"))
	{
		if (!reply.modern && method == "notifications/initialized")
		{
			reply.status = 202;
			return reply;
		}
		return error(400, -32601, "Unsupported notification");
	}

	Json result;
	if (method == "initialize" && !reply.modern)
	{
		if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string() ||
			!params.contains("capabilities") || !params["capabilities"].is_object() ||
			!params.contains("clientInfo") || !validImplementation(params["clientInfo"]))
			return error(400, -32602, "Invalid initialization parameters");
		result = {{"protocolVersion", LegacyVersion},
				  {"capabilities", {{"tools", Json::object()}}},
				  {"serverInfo", ServerInfo}};
	}
	else if (method == "server/discover" && reply.modern)
		result = {{"supportedVersions", {ModernVersion, LegacyVersion}},
				  {"capabilities", {{"tools", Json::object()}}},
				  {"ttlMs", 0},
				  {"cacheScope", "public"}};
	else if (method == "ping" && !reply.modern)
		result = Json::object();
	else if (method == "tools/list")
	{
		if (params.contains("cursor"))
			return error(400, -32602, "No pagination cursor is supported");
		result = {{"tools", registry.list()}};
		if (reply.modern)
		{
			result["ttlMs"] = 0;
			result["cacheScope"] = "public";
		}
	}
	else if (method == "tools/call")
	{
		if (!params.contains("name") || !params["name"].is_string())
			return error(400, -32602, "Missing tool name");
		try
		{
			reply.pending =
				registry.call(context, params["name"], params.value("arguments", Json::object()),
							  clientName(params));
		}
		catch (const std::invalid_argument &e)
		{
			return error(400, -32602, e.what());
		}
		catch (const std::exception &e)
		{
			return error(500, -32603, e.what());
		}
		return reply;
	}
	else
		return error(reply.modern ? 404 : 200, -32601, "Method not found");

	reply.body = response(reply.id, std::move(result), reply.modern);
	return reply;
}
} // namespace MCP
