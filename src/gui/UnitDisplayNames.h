// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <string>

//! Localized display name for a unit type (WORKER / WARRIOR / EXPLORER).
//! UI/display layer only — sim code must use the UnitConsts enum value.
std::string getUnitName(int type);
