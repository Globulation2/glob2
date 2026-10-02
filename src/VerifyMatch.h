// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// --verify-match: replays a relay match record through the engine path live clients
// run and judges the checksums the clients reported. The command-line contract is in
// docs/development/headless-replays.md ("Verifying a match record").

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <SDL_stdinc.h>

#include "MatchRecord.h"
#include "MatchSetup.h"
#include "OrderValidation.h"

/// A friend of Engine, like HeadlessRunner.
struct MatchVerifier
{
	struct Verdict
	{
		std::string verdict; ///< "verified", "diverged" or "unverifiable"
		std::vector<int> seats; ///< diverged: seats whose reports differ from the verifier
		std::string reason;     ///< unverifiable: why
		std::size_t compared = 0;                     ///< reports compared
		std::map<int, std::uint32_t> firstDivergence; ///< seat -> first differing tick
		std::map<std::uint32_t, Uint32> checksums;    ///< tick -> verifier checksum, 0..endTick
		/// Human orders the engine checked while replaying the record, per seat: the
		/// counts every live client computed too (voice aside, which is not recorded).
		OrderValidation::Audit orders;
	};

	/// The verdict rule: verified when every reporting seat matched at every tick it
	/// reported; diverged{seats} when some seats differ and at least one matched;
	/// unverifiable when no seat matched, no report could be compared, or
	/// `versionProblem` is set.
	static Verdict judge(const Turn::MatchRecord& record, const std::map<std::uint32_t, Uint32>& checksums,
	                     const std::string& versionProblem);

	/// Verifies `record` (whose setup is `setup`) against the map at `mapPath`, writing
	/// result.json, verdict.json, checksums.txt, match.replay and artifacts.json into
	/// `output`. Needs a loaded GlobalContainer; sets it up for a structured headless
	/// run. Throws std::invalid_argument for a map that does not fit the setup.
	static Verdict verify(const Turn::MatchRecord& record, const Online::MatchSetup& setup, const std::string& mapPath,
	                      const std::filesystem::path& output);

	/// The command line: checks the record and setup, creates the process's
	/// GlobalContainer and verifies. Bad input throws std::invalid_argument.
	static int run(const std::string& recordPath, const std::string& mapPath, const std::filesystem::path& output,
	               const std::string& profile);
};
