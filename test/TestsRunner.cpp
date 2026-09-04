// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

/*********************************************************
 *
 * This file is based on the howto
 * http://cppunit.sourceforge.net/doc/1.11.6/money_example.html
 * by Baptiste Lepilleur
 *
 ********************************************************/

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/XmlOutputter.h>

int main(int argc, char* argv[])
{
	// Get the top level suite from the registry
	CppUnit::Test *suite =
			CppUnit::TestFactoryRegistry::getRegistry().makeTest();

	// Adds the test to the list of test to run
	CppUnit::TextUi::TestRunner runner;
	runner.addTest(suite);

	// made the outputter actually write to an XML-file that is needed for hudson
	std::ofstream xmlFileOut("testResults.xml");
	CppUnit::XmlOutputter xmlOut(&runner.result(), xmlFileOut);

	// Run the tests.
	bool wasSucessful = runner.run();

	xmlOut.write();

	// Return error code 1 if one of the tests failed.
	return wasSucessful ? 0 : 1;
}
