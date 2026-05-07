// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone runner for libusl/test/*.usl fixtures.
//
// Wraps each script in `result := { <source> }`, includes it via Usl, and
// prints the bound `result` value. Pointers from Value::dump are stripped
// before diffing two runs (use sed 's/0x[0-9a-f]*/PTR/g').

#include "usl.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int runFile(const char* path)
{
	std::ifstream in(path);
	if (!in)
	{
		std::cerr << "cannot open " << path << '\n';
		return 1;
	}

	std::stringstream wrapped;
	wrapped << "result := {\n" << in.rdbuf() << "\n}\n";

	Usl usl;
	usl.includeScript(path, wrapped);

	Value* result = usl.getConstant("result");
	if (result)
		result->dump(std::cout);
	else
		std::cout << "(no result)";
	std::cout << '\n';
	return 0;
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cerr << "usage: " << argv[0] << " <file.usl>...\n";
		return 1;
	}
	int rc = 0;
	for (int i = 1; i < argc; ++i)
	{
		std::cout << "=== " << argv[i] << " ===\n";
		try
		{
			if (runFile(argv[i]) != 0)
				rc = 1;
		}
		catch (const std::exception& e)
		{
			std::cerr << "ERROR in " << argv[i] << ": " << e.what() << '\n';
			rc = 1;
		}
	}
	return rc;
}
