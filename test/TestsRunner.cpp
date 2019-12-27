/*
 Copyright (C) 2010 Leo Wandersleb

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

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

#include "GlobalContainer.h"
GlobalContainer *globalContainer = new GlobalContainer;

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
