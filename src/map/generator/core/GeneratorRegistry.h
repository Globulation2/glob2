// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorDefinition.h"
#include <string>
class GeneratorRegistry
{
	std::vector<GeneratorDefinition> definitions;

  public:
	explicit GeneratorRegistry(std::vector<GeneratorDefinition> definitions);
	const GeneratorDefinition *find(int id) const;
	const GeneratorDefinition &at(int id) const;
	/// The numeric id of the generator with this string id; throws for an unknown one.
	int idOf(const std::string &id) const;
	std::vector<int> methods(bool editor = true) const;
	int selectionIndex(int id, bool editor = true) const;
	static const GeneratorRegistry &builtins();
};
