// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

// Versioned, production command entry points. Return -1 when this is a legacy invocation.
int runHeadlessCommand(int argc, char **argv);
int runMapStudy(int argc, char **argv);
namespace Headless
{
std::string quote(const std::string &value);
void writeJson(const std::string &path, const std::string &json);
}
