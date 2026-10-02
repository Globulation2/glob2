// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// A content-addressed store of maps and saved games in the user directory, keyed by
// the SHA-256 of the decompressed bytes (MatchSetup map.hash). Files are named
// <hash>.map.gz or <hash>.game.gz, which is what resolveMatchMap looks for in a cache
// directory. This is the minimal cache LAN games need; the online client's map
// catalog (plan section I) will reuse or replace it.

#include <string>

namespace Online
{
	class MapCache
	{
	public:
		/// An absolute directory; created on the first store.
		explicit MapCache(std::string directory);
		/// <first FileManager directory>/cache/maps, normally the user directory.
		static std::string defaultDirectory();

		const std::string& directory() const { return root; }
		/// The cached file with this content hash, or empty.
		std::string find(const std::string& hash) const;
		/// Checks that `contents` (decompressed bytes) hash to `hash`, then stores them
		/// gzip-compressed. Returns the stored path, or empty with `error` set.
		std::string store(const std::string& hash, bool savedGame, const std::string& contents,
		                  std::string* error = nullptr) const;

	private:
		std::string root;
	};

	/// The decompressed bytes of a map or save file, read through the Toolkit
	/// FileManager (".gz" is inflated). False if the file cannot be read.
	bool readMapBytes(const std::string& path, std::string& bytes);
}
