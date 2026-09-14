// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
class Game;
struct GenerationRequest;
struct GenerationResult;

// Snapshot analysis only: no simulation steps, and all scorer cache writes are restored.
std::string describeMap(Game &game, const GenerationRequest *request = nullptr,
						const GenerationResult *generation = nullptr);
