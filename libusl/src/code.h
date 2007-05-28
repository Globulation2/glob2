#ifndef BYTECODE_H
#define BYTECODE_H

#include "interpreter.h"
#include <cassert>
#include <ostream>


struct Code
{
	virtual ~Code() { }
	virtual void execute(Thread* thread) = 0;
	virtual void dump(std::ostream &stream) { stream << typeid(*this).name(); }
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

struct ValRefCode: Code
{
	ValRefCode(size_t depth, size_t index):
		depth(depth),
		index(index)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		Scope* scope = frame.scope;
		for (size_t i = 0; i < depth; ++i)
		{
			// scope = static_cast<Scope*>(scope->parent); // Should be safe if the parser is bug-free
			scope = dynamic_cast<Scope*>(scope->parent);
			assert(scope); // Should not fail if the parser is bug-free
		}
		assert(index < scope->locals.size()); // Should not fail if the parser is bug-free
		frame.stack.push_back(scope->locals[index]);
	}
	
	size_t depth;
	size_t index;
};

struct ApplyCode: Code
{
	ApplyCode(const std::string& name):
		name(name)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frames& frames = thread->frames;
		Thread::Frame::Stack& stack = frames.back().stack;
		
		// get argument
		Value* argument = stack.back();
		stack.pop_back();
		
		// get receiver
		Value* receiver = stack.back();
		stack.pop_back();
		
		// get method
		ScopePrototype* method = receiver->prototype->lookup(name);
		
		// create a new scope
		Scope* scope = new Scope(thread->heap, method, receiver);
		
		// put the argument in the scope
		scope->locals.push_back(argument);
		
		// push a new frame
		frames.push_back(scope);
	}
	
	const std::string name;
};

struct ValCode: Code
{
	ValCode(size_t index):
		index(index)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		Thread::Frame::Stack& stack = frame.stack;
		frame.scope->locals.push_back(stack.back());
		stack.pop_back();
	}
	
	size_t index;
};

struct ParentCode: Code
{
	ParentCode()
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		assert(frame.scope->parent);
		frame.stack.push_back(frame.scope->parent);
	}
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
	ScopeCode(ScopePrototype* prototype):
		prototype(prototype)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		frame.stack.push_back(new Scope(thread->heap, prototype, frame.scope));
	}
	
	ScopePrototype* prototype;
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

struct TupleCode: Code
{
	TupleCode(size_t size):
		size(size)
	{}
	
	void execute(Thread* thread)
	{
		Tuple* tuple = new Tuple(thread->heap);
		Thread::Frame::Stack &stack = thread->frames.back().stack;
		Thread::Frame::Stack::const_iterator stackEnd = stack.end();
		std::copy(stackEnd - size, stackEnd, std::back_inserter(tuple->values));
		stack.resize(stack.size() - size);
	}
	
	size_t size;
};

struct NativeCode: Code
{
	struct Operation: ScopePrototype
	{
		Operation(Prototype* parent, const std::string& name, bool lazy):
			ScopePrototype(0, parent),
			name(name)
		{
			body.push_back(new ParentCode());
			body.push_back(new ValRefCode(0, 0));
			if (!lazy)
			{
				body.push_back(new ConstCode(&nil));
				body.push_back(new ApplyCode("."));
			}
			body.push_back(new NativeCode(this));
			body.push_back(new ReturnCode());
		}
		
		virtual Value* execute(Thread* thread, Value* receiver, Value* argument) = 0;
		
		std::string name;
	};
	
	NativeCode(Operation* operation):
		operation(operation)
	{}
	
	void execute(Thread* thread)
	{
		Thread::Frame::Stack& stack = thread->frames.back().stack;
		
		Value* argument = stack.back();
		stack.pop_back();
		Value* receiver = stack.back();
		stack.pop_back();
		
		stack.push_back(operation->execute(thread, receiver, argument));
	}
	
	Operation* operation;
};

#endif // ndef BYTECODE_H
