// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <nlohmann/json_fwd.hpp>
#include "scripting/javascript/ScriptValue.h"
namespace Hive
{
using Json = nlohmann::json;
Json json(const Script::Value &value);
Script::Value value(const Json &json, unsigned depth = 0);
// Pure worker entry point. Input contains only already-filtered observations.
Json invoke(const Json &request);
int workerMain();
} // namespace Hive
