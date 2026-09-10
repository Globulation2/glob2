// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
class Game;
struct GenerationRequest;
struct GeneratorDefinition;
std::string validateGenerationRequest(const GenerationRequest &, const GeneratorDefinition &);
std::string validateGeneratedWorld(const Game &, const GenerationRequest &,
								   const GeneratorDefinition &);
