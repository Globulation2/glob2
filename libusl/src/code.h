#ifndef BYTECODE_H
#define BYTECODE_H

#include "interpreter.h"
#include <cassert>


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
		thread->frames.back().stack.push_back(value);
	}
	
	Value* value;
};

struct LocalCode: Code
{
	LocalCode(size_t depth, const std::string& local):
		depth(depth),
		local(local)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		Scope* scope = frame.scope;
		for (size_t i = 0; i < depth; ++i)
		{
			scope = scope->parent;
		}
		frame.stack.push_back(scope->lookup(local));
	}
	
	size_t depth;
	std::string local;
};

struct ApplyCode: Code
{
	ApplyCode(const std::string& name, size_t argCount):
		name(name),
		argCount(argCount)
	{}
	
	void execute(Thread* thread)
	{
		// fetch receiver
		Value* receiver = *(thread->frames.back().stack.end() - argCount - 1);
		
		// fetch method
		Method* method = receiver->prototype->lookup(name);
		assert(argCount == method->args.size());
		
		method->execute(thread);
	}
		
	const std::string name;
	size_t argCount;
};

struct ValueCode: Code
{
	ValueCode(const std::string& local):
		local(local)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		Thread::Frame::Stack& stack = frame.stack;
		frame.scope->locals[local] = stack.back();
		stack.pop_back();
	}
	
	const std::string local;
};

struct PopCode: Code
{
	void execute(Thread* thread)
	{
		thread->frames.back().stack.pop_back();
	}
};

struct ScopeCode: Code
{
	ScopeCode(UserMethod* method):
		method(method)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		frame.stack.push_back(new Scope(thread->heap, method, frame.scope));
	}
	
	UserMethod* method;
};

struct ReturnCode: Code
{
	void execute(Thread* thread)
	{
		Value* value = thread->frames.back().stack.back();
		thread->frames.pop_back();
		thread->frames.back().stack.push_back(value);
	}
};

#endif // ndef BYTECODE_H
