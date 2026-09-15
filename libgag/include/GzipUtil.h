// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace GAGCore
{
	class StreamBackend;

	//! Gzip-compresses input at level. The gzip header carries no timestamp or OS
	//! byte, so the same input and level always produce identical bytes on every
	//! platform. Returns false (leaving output unspecified) on a zlib failure.
	bool gzipCompress(const std::string& input, int level, std::string& output);
	//! Inflates a gzip stream produced by gzipCompress (or any conforming gzip
	//! encoder). Returns false for corrupt or truncated input.
	bool gzipDecompress(const std::string& input, std::string& output);

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
