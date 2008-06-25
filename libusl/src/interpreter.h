#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stack>
#include <vector>

struct Thunk;
struct Value;
struct Heap;

struct Thread
{
	struct Frame
	{
		typedef std::vector<Value*> Stack;
	
		Thunk* thunk;
		Stack stack;
		size_t nextInstr;
		
		Frame(Thunk* thunk):
			thunk(thunk)
		{
			nextInstr = 0;
		}
		
		void markForGC();
	};
	
	typedef std::vector<Frame> Frames;
	
	Heap* heap;
	Frames frames;
	
	Thread(Heap* heap):
		heap(heap)
	{}
	
	void markForGC();
};

#endif // ndef INTERPRETER_H
