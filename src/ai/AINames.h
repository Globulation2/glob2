// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <string>

namespace AINames
{
	/// Sentinel returned by parseAIName() when the supplied name does
	/// not match any known AI::ImplementitionID. Distinct from any
	/// valid AI id (which are 0..AI::SIZE-1); callers compare with `== AI_UNKNOWN_NAME`.
	static const int AI_UNKNOWN_NAME = -1;

	std::string getAIText(int id);
	std::string getAIDescription(int id);

	/// Resolve a CLI-friendly AI name (case-insensitive) to its
	/// AI::ImplementitionID value (1..AI::SIZE-1). Returns AI_UNKNOWN_NAME on unknown.
	/// Used by --ai-types and --matchup parsers in GlobalContainer.cpp;
	/// keep the name table here to avoid drift between the two CLIs.
	int parseAIName(const std::string& name);

	/// Comma-separated list of the CLI-friendly AI names accepted by
	/// parseAIName(), e.g. "numbi, castor, ...". Derived from the same
	/// table so --ai-types/--matchup error and --help text never drift.
	std::string validAINames();
}
