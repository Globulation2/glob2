#include "MiniCppUnit.h"

int main()
{
	return TestFixtureFactory::theInstance().runTests() ? 0 : -1;
}	
