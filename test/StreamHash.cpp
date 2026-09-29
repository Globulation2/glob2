// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-1 is compiled as C++ in the game. Keep the same linkage for streams in
// standalone harnesses that do not link the game's password registry.
// As an archive member this is only selected if a harness lacks its own copy.
#include "../gnupg/sha1.c"
