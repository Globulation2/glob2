// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include "CommandLine.h"
namespace Script
{
std::string readSource(const std::string &path);
}
int runScriptCommand(const Cli::Request &request);
