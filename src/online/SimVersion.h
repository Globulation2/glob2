// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The identity of a deterministic simulation build (docs/multiplayer/turn-protocol.md,
// "Simulation version"). Two builds with the same SimVersion must produce identical
// games from the same MatchSetup and orders; rooms, queues, AI ratings and verifiers
// are partitioned by it. The JSON shape is SimVersion in platform/packages/protocol.

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Online
{
	struct SimVersion
	{
		std::uint32_t versionMinor = 0; ///< VERSION_MINOR (src/Version.h)
		std::uint32_t netProtocol = 0;  ///< NET_PROTOCOL_VERSION (src/Version.h)
		std::string dataHash;           ///< 64 lowercase hex digits, see simDataHash()

		/// "<versionMinor>-<netProtocol>-<dataHash>", the protocol package's simVersionKey.
		/// The relay copies it from the ticket into MatchRecord::simVersion.
		std::string key() const;
		static bool parseKey(const std::string& key, SimVersion& out);

		nlohmann::json toJson() const;
		/// Validates the shape (all three fields, ranges, hash pattern, nothing else);
		/// throws MatchSetupError on a violation.
		static SimVersion fromJson(const nlohmann::json& value, const std::string& path = "");

		/// This build's SimVersion for the platform (session.hello): currentSimVersion()
		/// once the Toolkit file system is up. Before that (only in tests that never
		/// initialize it) the data hash is 64 zeros, which the platform answers with
		/// simSupported=false.
		static SimVersion local();

		bool operator==(const SimVersion& o) const
		{
			return versionMinor == o.versionMinor && netProtocol == o.netProtocol && dataHash == o.dataHash;
		}
		bool operator!=(const SimVersion& o) const { return !(*this == o); }
	};

	/// The data files whose contents feed the simulation data hash, as paths relative
	/// to the data root, in hashing (byte-wise sorted) order. Everything else the
	/// simulation depends on is compiled in and covered by VERSION_MINOR and
	/// NET_PROTOCOL_VERSION.
	const std::vector<std::string>& simDataFiles();

	/// Hashes simDataFiles() through the Toolkit FileManager (so the same files are
	/// found on every platform, including the browser's packaged file system):
	///
	///   SHA-256 over, for each file in order:
	///     path bytes || 0x00 || u64 big-endian length || content
	///
	/// where content is the file's bytes with every CR LF pair replaced by LF, so a
	/// Windows checkout with automatic line-ending conversion hashes identically. A
	/// missing file contributes its path, 0x00 and the length 0xFFFFFFFFFFFFFFFF.
	/// Requires an initialized Toolkit; the result is computed once and cached.
	const std::string& simDataHash();

	/// The data hash of explicit (path, content) pairs, for tests and tools. Same
	/// encoding as simDataHash(); `present = false` marks a missing file.
	struct SimDataFile
	{
		std::string path;
		std::string content;
		bool present = true;
	};
	std::string simDataHashOf(const std::vector<SimDataFile>& files);

	/// This build's SimVersion.
	SimVersion currentSimVersion();
}
