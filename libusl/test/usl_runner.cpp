// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone runner for libusl/test/*.usl fixtures.
//
// Wraps each script in `result := { <source> }`, includes it via Usl, and
// prints the bound `result` value. Pointers from Value::dump are stripped
// before diffing two runs (use sed 's/0x[0-9a-f]*/PTR/g').
//
// Use `-p <file>` (repeatable) to load a prelude via Usl::includeScript
// before each test. Preludes' top-level definitions become available in
// every subsequent test's scope.

#include "usl.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int runFile(const char* path, const std::vector<const char*>& preludes)
{
	Usl usl;

	for (const char* prelude : preludes)
	{
		std::ifstream pin(prelude);
		if (!pin)
		{
			std::cerr << "cannot open prelude " << prelude << '\n';
			return 1;
		}
		usl.includeScript(prelude, pin);
	}

	std::ifstream in(path);
	if (!in)
	{
		std::cerr << "cannot open " << path << '\n';
		return 1;
	}

	std::stringstream wrapped;
	wrapped << "result := {\n" << in.rdbuf() << "\n}\n";
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
	std::vector<const char*> preludes;
	std::vector<const char*> tests;

	for (int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		if (a == "-p" && i + 1 < argc)
			preludes.push_back(argv[++i]);
		else
			tests.push_back(argv[i]);
	}

	if (tests.empty())
	{
		std::cerr << "usage: " << argv[0] << " [-p <prelude.usl>]... <file.usl>...\n";
		return 1;
	}

	int rc = 0;
	for (const char* path : tests)
	{
		std::cout << "=== " << path << " ===\n";
		try
		{
			if (runFile(path, preludes) != 0)
				rc = 1;
		}
		catch (const std::exception& e)
		{
			std::cerr << "ERROR in " << path << ": " << e.what() << '\n';
			rc = 1;
		}
	}
	return rc;
}
