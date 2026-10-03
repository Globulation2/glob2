// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
class Game;
namespace Script
{
class Observations;
}
namespace Hive
{
// Called at a simulation boundary. The only input to the isolated interpreter.
Json capture(Script::Observations &observations, Game &game, int team);
} // namespace Hive
