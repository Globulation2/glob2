#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stack>
#include <vector>
#include <string>

struct Thunk;
struct Scope;
struct Value;
struct Heap;
struct DebugInfo;

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
	struct RuntimeValues
	{
		RuntimeValues();
		Value* trueValue;
		Value* falseValue;
	};
	
	typedef std::vector<Frame> Frames;
	
	Heap* heap;
	DebugInfo* debugInfo;
	Scope* root;
	RuntimeValues runtimeValues;
	Frames frames;
	
	Thread(Heap* heap, DebugInfo* debugInfo, Scope* root):
		heap(heap),
		debugInfo(debugInfo),
		root(root)
	{}
	
	Value* getRuntimeValue(Value*& cachedValue, const std::string& name);
	Value* getRootLocal(const std::string& name);
	
	void markForGC();
};

#endif // ndef INTERPRETER_H
