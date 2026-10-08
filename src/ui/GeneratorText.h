// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
#include <Toolkit.h>
#include <StringTable.h>
inline std::string generatorText(const GeneratorDefinition &definition, const char *text)
{
	if (!text)
		return {};
	auto *table = GAGCore::Toolkit::getStringTable();
	if (definition.translate) {
        auto translated=definition.translate(table->getString("[language-code]"), text);
        if(translated!=text || !table->doesStringExist(std::string("[")+text+"]"))return translated;
    }
	return table->getString(std::string("[") + text + "]");
}
inline std::string generatorName(const GenerationRequest &request)
{
	return generatorText(request.definition(), request.definition().nameKey);
}
