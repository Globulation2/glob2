// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
#include <future>
#include <memory>
namespace Hive
{
void setExecutable(const char *path);
Json runWorker(const Json &request);
// A detached job owns only filtered JSON. Destroying a match never waits for it
// or leaves a pointer into the engine on the worker thread.
// tiles, when given, becomes request.snapshot.tiles on the worker thread (a cached map
// the main thread need not copy).
std::future<Json> startWorker(Json request, std::shared_ptr<const Json> tiles = nullptr);
} // namespace Hive
