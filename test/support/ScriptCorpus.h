// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Test.h"
#include "script/ScriptValue.h"
#include <bit>
#include <iomanip>
#include <locale>
#include <sstream>

namespace glob2test::script
{
inline void traceValue(std::ostream &output, const Script::Value &value, const std::string &path)
{
	switch (value.kind)
	{
	case Script::Value::Number:
		output << path << " number " << std::hex << std::setw(16) << std::setfill('0')
			   << std::bit_cast<std::uint64_t>(value.number) << std::dec << '\n';
		break;
	case Script::Value::Boolean:
		output << path << " bool " << (value.number != 0) << '\n';
		break;
	case Script::Value::Null:
		output << path << " null\n";
		break;
	case Script::Value::String:
		output << path << " string " << std::quoted(value.text) << '\n';
		break;
	case Script::Value::Array:
		output << path << " array " << value.items.size() << '\n';
		for (size_t i = 0; i < value.items.size(); ++i)
			traceValue(output, value.items[i], path + "/" + std::to_string(i));
		break;
	case Script::Value::Object:
		output << path << " object " << value.fields.size() << '\n';
		for (const auto &field : value.fields)
			traceValue(output, field.second, path + "/" + field.first);
		break;
	}
}
inline void retain(const std::string &name, const Script::Value &value)
{
	std::ostringstream trace;
	trace.imbue(std::locale::classic());
	traceValue(trace, value, "root");
	writeFile(artifactDir() / (name + ".value"), value.encode());
	writeFile(artifactDir() / (name + ".txt"), trace.str());
	expectGolden("javascript/" + name + ".txt", trace.str());
}
} // namespace glob2test::script
