#include "interpreter.h"
#include "types.h"

using namespace std;
using namespace __gnu_cxx;

void Thread::Frame::markForGC()
{
	// mark all variables in frame
	for_each(stack.begin(), stack.end(), mem_fun(&Value::markForGC));
	scope->markForGC();
}

void Thread::markForGC()
{
	// mark all frames in stack
	for_each(frames.begin(), frames.end(), mem_fun_ref(&Frame::markForGC));
}

void Thread::garbageCollect()
{
	// mark all objects in heap
	markForGC();
	
	// filter copy, delete unrefs
	Heap toKeep;
	for (size_t i = 0; i < heap.size(); i++)
	{
		if (heap[i]->marked)
			toKeep.push_back(heap[i]);
		else
			delete heap[i];
	}
	
	// clean heap
	heap.resize(toKeep.size());
	copy(toKeep.begin(), toKeep.end(), heap.begin());
	for_each(heap.begin(), heap.end(), mem_fun(&Value::clearGCMark));
}

