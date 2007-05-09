#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stack>
#include <vector>

struct Scope;
struct Value;

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
};

struct Thread
{
	typedef std::stack<Frame> Frames;
	
	Frames frames;
};

#endif // ndef INTERPRETER_H
