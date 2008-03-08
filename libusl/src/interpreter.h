#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stack>
#include <vector>

struct Scope;
struct Value;
struct Heap;

struct Thread
{
	struct Frame
	{
		typedef std::vector<Value*> Stack;
	
		Scope* scope;
		Stack stack;
		size_t nextInstr;
		
		Frame(Scope* scope):
			scope(scope)
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
