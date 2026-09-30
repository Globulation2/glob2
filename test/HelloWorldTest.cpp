// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

#include <iostream>

#include "Glob2Test.h"

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

class HelloWorldTest
{

public:
	HelloWorldTest()
	{
	}
	~HelloWorldTest()
	{
	}

protected:
	void testHelloWorld(void)
	{
		std::cout << "Hello, world!" << std::endl;
	}
};
TEST_SUITE("HelloWorld")
{
	TEST_CASE_FIXTURE(HelloWorldTest, "HelloWorld") { testHelloWorld(); }
}
