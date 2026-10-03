// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// libgag's BinaryOutputStream can hash what it writes, so its object needs the SHA-1
// routines. The game gets them from YOGServerPasswordRegistry.cpp, which the relay
// does not link; compile them here, as C++, for the same linkage.
#include "gnupg/sha1.c"
