// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

// Which optional sources this executable was built with (scons compile_profile=core|full,
// see docs/development/build-options.md). Code that needs to know asks here rather than
// including the generated header, so its object stays identical in both profiles.
namespace CompileProfile
{
// "full" or "core".
const char *name();
bool isFull();
bool hasGenerators();
bool hasEditor();
// "<what> is not compiled into this build (compile_profile=core); rebuild with compile_profile=full".
std::string missing(const std::string &what);
// Throws std::runtime_error naming the role unless this is the full profile. Online roles
// whose results others rely on (agents, verifiers, rated matches) call it.
void requireFull(const std::string &role);
}
