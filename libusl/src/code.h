#ifndef BYTECODE_H
#define BYTECODE_H

#include <string>
#include "interpreter.h"


struct Code
{
	virtual ~Code() { }
	virtual void execute(Thread* thread) = 0;
};

struct ConstCode: Code
{
	ConstCode(Value* value):
		value(value)
	{}
	
	void execute(Thread* thread)
	{
		thread->topFrame().stack.push_back(value);
	}
	
	Value* value;
};

struct LocalCode: Code
{
	LocalCode(const std::string& local):
		local(local)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->topFrame();
		frame.stack.push_back(frame.scope->lookup(local));
	}
	
	const std::string local;
};

struct ApplyCode: Code
{
	ApplyCode(const std::string& method, size_t argCount):
		method(method),
		argCount(argCount)
	{}
	
	void execute(Thread* thread)
	{
		// fetch receiver
		Value* receiver = *(thread->topFrame().stack.end() - argCount - 1);
		
		// fetch method
		Method* method = receiver->proto->lookup(this->method);
		assert(argCount == method->args.size());
		
		method->execute(thread);
	}
		
	const std::string method;
	size_t argCount;
};

struct ValueCode: Code
{
	ValueCode(const std::string& local):
		local(local)
	{}
	
	void execute(Thread* thread)
	{
		Frame& frame = thread->frames.top();
		Frame::Stack& stack = frame.stack;
		size_t stackSize = stack.size();
		frame.scope->locals[local] = stack[--stackSize];
		stack.resize(stackSize);
	}
	
	const std::string local;
};

#endif // ndef BYTECODE_H
