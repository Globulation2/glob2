// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <string>

class Team;

//! Display name for a team — the first player's name, or a localized
//! "[Uncontrolled]" placeholder when no player owns the team. UI-only;
//! sim code must use Team::getFirstPlayerName (which may return empty).
std::string displayPlayerName(const Team& team);
//! The same from a team's first player name (empty when uncontrolled).
std::string displayPlayerName(const std::string& firstPlayerName);
