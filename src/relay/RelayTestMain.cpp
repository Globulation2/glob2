// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Entry point of glob2-relay-tests (scons role=relay relay-tests). The relay role
// builds no SDL or engine code, so these tests have their own small doctest main.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
