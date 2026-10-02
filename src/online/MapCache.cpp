// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MapCache.h"

#include <filesystem>
#include <memory>
#include <system_error>

#include <FileManager.h>
#include <GzipUtil.h>
#include <StreamBackend.h>
#include <Toolkit.h>

#include "Sha256.h"

namespace Online
{
MapCache::MapCache(std::string directory) : root(std::move(directory)) {}

std::string MapCache::defaultDirectory()
{
	auto* files = GAGCore::Toolkit::getFileManager();
	const std::string base = files && files->getDirCount() ? files->getDir(0) : std::string(".");
	return base + "/cache/maps";
}

std::string MapCache::find(const std::string& hash) const
{
	std::error_code ignored;
	for (const char* suffix : {".map.gz", ".map", ".game.gz", ".game"})
	{
		const std::string path = root + "/" + hash + suffix;
		if (std::filesystem::is_regular_file(path, ignored))
			return path;
	}
	return {};
}

std::string MapCache::store(const std::string& hash, bool savedGame, const std::string& contents,
                            std::string* error) const
{
	auto fail = [error](const std::string& message) {
		if (error)
			*error = message;
		return std::string();
	};
	Sha256::Digest expected;
	if (!parseSha256Hex(hash, expected))
		return fail("invalid map hash");
	if (Sha256::of(contents) != expected)
		return fail("the received map does not match its hash");
	std::error_code ec;
	std::filesystem::create_directories(root, ec);
	if (ec)
		return fail("cannot create the map cache: " + ec.message());
	const std::string path = root + "/" + hash + (savedGame ? ".game.gz" : ".map.gz");
	if (!GAGCore::writeGzipAtomicToPath(path, contents))
		return fail("cannot write " + path);
	return path;
}

bool readMapBytes(const std::string& path, std::string& bytes)
{
	std::unique_ptr<GAGCore::StreamBackend> backend(
		GAGCore::Toolkit::getFileManager()->openInflatingInputStreamBackend(path));
	if (!backend || !backend->isValid())
		return false;
	backend->seekFromEnd(0);
	const std::size_t size = backend->getPosition();
	backend->seekFromStart(0);
	bytes.assign(size, '\0');
	return !size || backend->readExact(bytes.data(), size);
}
}
