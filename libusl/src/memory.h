#pragma once

#include <vector>

struct Value;

struct Heap
{
	typedef std::vector<Value*> Values;
	
	Values values;
	
	void collectGarbage();
};

