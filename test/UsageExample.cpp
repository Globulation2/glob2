/*
 * This file provides a very simple example on how to use MiniCppUnit
 *
 * Any feedback to the autors is really appreciated
 *
 * Authors: Pau Arum� (parumi@iua.upf.es) and David Garcia (dgarcia@iua.upf.es)
 */

#include "MiniCppUnit.h"
#include <math.h>

class MyTestsExample : public TestFixture<MyTestsExample>
{
public:
	TEST_FIXTURE( MyTestsExample )
	{
		TEST_CASE( testAssert );
		TEST_CASE( testAssertMessage );
		TEST_CASE( testAdditionInts );
		TEST_CASE( testDoubles );
		TEST_CASE( testFloats );
		TEST_CASE( testLongDoubles );
		TEST_CASE( testNotANumber );
		TEST_CASE( testComparisonWithEpsilon );
		TEST_CASE( testException );
		TEST_CASE( testStringAddition );
	}
	void testAssert()
	{
		ASSERT(3==1+2);
	}
	void testAssertMessage()
	{
		ASSERT_MESSAGE( 2==1+1, "2 should be 1 plus 1 ");
	}
	void testAdditionInts()
	{
		ASSERT_EQUALS( 2, 1+1 );
	}
	void testDoubles()
	{
		double expected = 10.00002;
		double result =   10.00001;
		ASSERT_EQUALS(expected, result);

		// Notice that minicppunit uses an scaled epsilon
		// that depends on the expected value.
		// So, the previous assert passes but the following,
		// if descomented, should fail!
		double expected2 = 0.00002;
		double result2 =   0.00001;
		//ASSERT_EQUALS(expected2, result2); 
	}
	void testFloats()
	{
		float expected = 0.000002;
		float result =   0.000001;
		ASSERT_EQUALS(expected, result);
	}
	void testLongDoubles()
	{
		long double expected = 0.000002;
		long double result =   0.000001;
		ASSERT_EQUALS(expected, result);
	}
	void testNotANumber()
	{
		double expected = 0.0/floor(0.0);
		double result =   0.0/floor(0.0);
		ASSERT_EQUALS(expected, result);
		
	}
	void testComparisonWithEpsilon()
	{
		long double expected = 0.02;
		long double result =   0.01;
		ASSERT_EQUALS_EPSILON(expected, result, 0.05);
	}
	void testException()
	{
		try
		{
			someCodeThatShouldThrowException();
			FAIL( "Should have rised an exception" );
		} catch (std::exception & e)
		{
			// maybe assert for string in e.what()
		}
		
	}
	void testStringAddition()
	{
		std::string result("Hello");
		result += " World";
		ASSERT_EQUALS("Hello World", result);
	}
	
private:
	void someCodeThatShouldThrowException()
	{
		throw std::exception();
	}
};

REGISTER_FIXTURE( MyTestsExample );
