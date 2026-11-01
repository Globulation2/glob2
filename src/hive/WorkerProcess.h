// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
#include <future>
namespace Hive
{
void setExecutable(const char *path);
Json runWorker(const Json &request);
// A detached job owns only filtered JSON. Destroying a match never waits for it
// or leaves a pointer into the engine on the worker thread.
std::future<Json> startWorker(Json request);
} // namespace Hive
