// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AI.h"

namespace AINames
{
	std::string getAIText(int id);
	std::string getAIDescription(int id);

	/// Resolve a CLI-friendly AI name (case-insensitive) to its
	/// AI::ImplementitionID value (1..6). Returns -1 on unknown.
	/// Used by --ai-types and --matchup parsers in GlobalContainer.cpp;
	/// keep the name table here to avoid drift between the two CLIs.
	int parseAIName(const std::string& name);
}
