// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
class GenerationWorld;
struct GenerationRequest;
struct GeneratorDefinition;
std::string validateGenerationRequest(const GenerationRequest &, const GeneratorDefinition &);
std::string validateGeneratedWorld(const GenerationWorld &, const GenerationRequest &,
								   const GeneratorDefinition &);
