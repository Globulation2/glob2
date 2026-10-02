// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "SimVersion.h"

#include <FileManager.h>
#include <StreamBackend.h>
#include <Toolkit.h>

#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

#include "MatchSetup.h"
#include "Sha256.h"
#include "Version.h"

namespace Online
{
// Defined here rather than in MatchSetup.cpp so that SimVersion (used by the online
// client) links without the engine-side MatchSetup conversion.
MatchSetupError::MatchSetupError(Stage stage, std::string path, const std::string& message)
	: std::runtime_error((path.empty() ? std::string("/") : path) + ": " + message), stage(stage), path(std::move(path))
{
}

namespace
{
bool lowercaseHex64(const std::string& text)
{
	Sha256::Digest ignored;
	return parseSha256Hex(text, ignored);
}

bool readVirtualFile(const std::string& path, std::string& out)
{
	std::unique_ptr<GAGCore::StreamBackend> backend(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path));
	if (!backend || !backend->isValid())
		return false;
	backend->seekFromEnd(0);
	const std::size_t size = backend->getPosition();
	backend->seekFromStart(0);
	out.resize(size);
	return size == 0 || backend->readExact(out.data(), size);
}

std::string normalizeLineEndings(const std::string& text)
{
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i)
		if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n'))
			out.push_back(text[i]);
	return out;
}
}

std::string SimVersion::key() const
{
	return std::to_string(versionMinor) + "-" + std::to_string(netProtocol) + "-" + dataHash;
}

bool SimVersion::parseKey(const std::string& key, SimVersion& out)
{
	const auto first = key.find('-');
	const auto second = first == std::string::npos ? std::string::npos : key.find('-', first + 1);
	if (second == std::string::npos)
		return false;
	auto number = [](const std::string& text, std::uint32_t& value) {
		if (text.empty() || text.size() > 5)
			return false;
		value = 0;
		for (char c : text)
		{
			if (c < '0' || c > '9')
				return false;
			value = value * 10 + static_cast<std::uint32_t>(c - '0');
		}
		return value <= 65535;
	};
	SimVersion parsed;
	if (!number(key.substr(0, first), parsed.versionMinor) ||
	    !number(key.substr(first + 1, second - first - 1), parsed.netProtocol))
		return false;
	parsed.dataHash = key.substr(second + 1);
	if (!lowercaseHex64(parsed.dataHash))
		return false;
	out = parsed;
	return true;
}

nlohmann::json SimVersion::toJson() const
{
	return nlohmann::json{{"versionMinor", versionMinor}, {"netProtocol", netProtocol}, {"dataHash", dataHash}};
}

SimVersion SimVersion::fromJson(const nlohmann::json& value, const std::string& path)
{
	auto fail = [&](const std::string& where, const std::string& message) {
		throw MatchSetupError(MatchSetupError::Stage::Schema, path + where, message);
	};
	if (!value.is_object())
		fail("", "must be an object");
	for (const auto& item : value.items())
		if (item.key() != "versionMinor" && item.key() != "netProtocol" && item.key() != "dataHash")
			fail("/" + item.key(), "unknown property");
	SimVersion out;
	for (const char* name : {"versionMinor", "netProtocol"})
	{
		auto it = value.find(name);
		if (it == value.end())
			fail(std::string("/") + name, "is required");
		if (!it->is_number_integer() || it->get<std::int64_t>() < 0 || it->get<std::int64_t>() > 65535)
			fail(std::string("/") + name, "must be an integer in 0..65535");
		(std::string(name) == "versionMinor" ? out.versionMinor : out.netProtocol) =
			static_cast<std::uint32_t>(it->get<std::int64_t>());
	}
	auto hash = value.find("dataHash");
	if (hash == value.end())
		fail("/dataHash", "is required");
	if (!hash->is_string() || !lowercaseHex64(hash->get<std::string>()))
		fail("/dataHash", "must be 64 lowercase hex digits");
	out.dataHash = hash->get<std::string>();
	return out;
}

const std::vector<std::string>& simDataFiles()
{
	// Byte-wise sorted. A unit test checks this list against the directories the
	// engine loads, so a new strategy or script file cannot silently escape the hash.
	static const std::vector<std::string> files = {
		"data/maxima/2v2.strategy",
		"data/maxima/base.strategy",
		"data/maxima/duel.strategy",
		"data/maxima/ffa3.strategy",
		"data/maxima/ffa4.strategy",
		"data/maxima/ffa5plus.strategy",
		"data/nicowar.default.txt",
		"data/nicowar.txt",
		"data/usl/Glob2/Runtime/Game.usl",
		"data/usl/Language/Runtime/Classes.usl",
		"data/usl/Language/Runtime/Control.usl",
		"data/usl/Language/Runtime/If.usl",
	};
	return files;
}

std::string simDataHashOf(const std::vector<SimDataFile>& files)
{
	Sha256 hash;
	for (const auto& file : files)
	{
		hash.update(file.path);
		const std::uint8_t zero = 0;
		hash.update(&zero, 1);
		const std::string content = file.present ? normalizeLineEndings(file.content) : std::string();
		const std::uint64_t length = file.present ? content.size() : UINT64_MAX;
		std::uint8_t encoded[8];
		for (int i = 0; i < 8; ++i)
			encoded[i] = static_cast<std::uint8_t>(length >> (56 - 8 * i));
		hash.update(encoded, 8);
		hash.update(content);
	}
	return toHex(hash.finish());
}

const std::string& simDataHash()
{
	static std::once_flag once;
	static std::string value;
	std::call_once(once, [] {
		std::vector<SimDataFile> files;
		for (const auto& path : simDataFiles())
		{
			SimDataFile file;
			file.path = path;
			file.present = readVirtualFile(path, file.content);
			if (!file.present)
				std::cerr << "SimVersion: missing simulation data file " << path << "\n";
			files.push_back(std::move(file));
		}
		value = simDataHashOf(files);
	});
	return value;
}

SimVersion currentSimVersion()
{
	SimVersion version;
	version.versionMinor = VERSION_MINOR;
	version.netProtocol = NET_PROTOCOL_VERSION;
	version.dataHash = simDataHash();
	return version;
}

SimVersion SimVersion::local()
{
	if (!GAGCore::Toolkit::getFileManager())
	{
		SimVersion version;
		version.versionMinor = VERSION_MINOR;
		version.netProtocol = NET_PROTOCOL_VERSION;
		version.dataHash = std::string(64, '0');
		return version;
	}
	return currentSimVersion();
}
}
