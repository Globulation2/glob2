// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <limits>

namespace GAGCore
{
	class StreamBackend;
	//! File loaders bound both compressed input and expanded data to 256 MiB.
	inline constexpr size_t MAX_COMPRESSED_GAME_FILE_BYTES = 256u * 1024u * 1024u;

	//! Gzip-compresses input at level with zero timestamp and OS=unknown.
	//! Output is deterministic for the same zlib encoder and input.
	//! Returns false (leaving output unspecified) on a zlib failure.
	bool gzipCompress(const std::string& input, int level, std::string& output);
	//! Inflates a gzip stream produced by gzipCompress (or any conforming gzip
	//! encoder). Returns false for corrupt or truncated input.
	bool gzipDecompress(const std::string& input, std::string& output,
		size_t maxOutput = std::numeric_limits<size_t>::max());

	//! Atomically replaces the literal filesystem path `path` (no FileManager
	//! directory search) with contents gzip-compressed at level. Mirrors
	//! FileManager::writeGzipAtomic's exclusive-temporary-file/rename mechanics,
	//! for callers (such as CLI tools) that must write an exact path rather than
	//! one resolved through FileManager's search directories.
	bool writeGzipAtomicToPath(const std::string& path, const std::string& contents, int level = 6);

	//! Opens the literal filesystem path `path` for reading (no FileManager
	//! directory search), transparently gzip-decompressing it into a seekable
	//! in-memory backend when path ends in ".gz". Never returns nullptr; an
	//! invalid backend means the file is missing, unreadable, or its gzip data
	//! is corrupt or truncated.
	StreamBackend *openInflatingFileStreamBackend(const std::string& path);
}
