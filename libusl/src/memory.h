#ifndef MEMORY_H
#define MEMORY_H

#include <stack>
#include <vector>

struct Value;
struct Thread;

struct Heap
{
	typedef std::vector<Value*> Values;
	
	Values values;
	
	void garbageCollect(Thread* thread);
};

#endif // ndef MEMORY_H
