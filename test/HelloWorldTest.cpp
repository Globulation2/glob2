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

#include <iostream>

#include <cppunit/extensions/HelperMacros.h>

/*********************************************************
 *
 * This file is meant to demonstrate what it takes to implement a new Test.
 * There are actually no tests with assertions in this example. See
 * http://sourceforge.net/apps/mediawiki/cppunit/index.php?title=Main_Page
 * for more information.
 *
 * This file is based on
 * http://pantras.free.fr/articles/helloworld.html
 *
 ********************************************************/

class HelloWorldTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(HelloWorldTest);
		CPPUNIT_TEST(testHelloWorld);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp(void)
	{
	}
	void tearDown(void)
	{
	}

protected:
	void testHelloWorld(void)
	{
		std::cout << "Hello, world!" << std::endl;
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(HelloWorldTest);
