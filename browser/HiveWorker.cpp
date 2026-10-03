// SPDX-License-Identifier: GPL-3.0-or-later
#include "hive/HiveWorker.h"
#include <emscripten.h>
extern "C" EMSCRIPTEN_KEEPALIVE const char *glob2_hive_invoke(const char *input)
{
	static std::string output;
	try
	{
		output = Hive::invoke(Hive::Json::parse(input)).dump();
	}
	catch (...)
	{
		output = "{\"ok\":false}";
	}
	return output.c_str();
}
