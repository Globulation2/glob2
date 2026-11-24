// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
#include <functional>
#include <future>
#include <memory>
namespace Hive
{
void setExecutable(const char *path);
Json runWorker(const Json &request);
// A detached job owns only filtered JSON. Destroying a match never waits for it
// or leaves a pointer into the engine on the worker thread.
// makeRequest runs on the worker thread first (the snapshot is built there); it must
// own everything it reads. Its exception fails the run like a worker error.
std::future<Json> startWorker(std::function<Json()> makeRequest);
} // namespace Hive
