// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <string>

//! Localized built-in name; developer definitions use their authored display name.
class UnitCatalog;
std::string getUnitName(int type);

std::string getUnitName(int type, const UnitCatalog& catalog);
