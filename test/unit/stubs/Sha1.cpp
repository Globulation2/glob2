// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-1 is compiled as C++ in the game (src/yog/YOGServerPasswordRegistry.cpp includes
// the C source). glob2-unit-tests does not link that object, so libgag's streams get
// the same C++-mangled definitions from here.
#include "../../../gnupg/sha1.c"
