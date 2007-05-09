#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stack>
#include <vector>

struct Scope;
struct Value;

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
	
	typedef std::vector<Value*> Heap;
	typedef std::vector<Frame> Frames;
	
	Heap heap;
	Frames frames;
	
	void markForGC();
	void garbageCollect();
	Frame &topFrame() { return *frames.rbegin(); }
};

#endif // ndef INTERPRETER_H
