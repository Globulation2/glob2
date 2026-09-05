// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <iostream>

#define checkInvariant(x) \
    if (!(x)) \
    { \
        std::cerr << "Invariant failed: " << #x << std::endl; \
        return false;\
    } \

#define checkInvariantText(x, text) \
    if (!(x)) \
    { \
        std::cerr << "Invariant failed: " << #x << text << std::endl; \
        return false;\
    } \

