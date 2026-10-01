// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace Script {std::string readSource(const std::string& path);}
int runScriptCommand(int argc,char** argv);
