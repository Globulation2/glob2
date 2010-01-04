#ifndef MEMORY_H
#define MEMORY_H

#include <vector>

struct Value;

struct Heap
{
	typedef std::vector<Value*> Values;
	
	Values values;
	
	void collectGarbage();
};

#endif // ndef MEMORY_H
