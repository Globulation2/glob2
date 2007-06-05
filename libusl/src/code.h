#ifndef BYTECODE_H
#define BYTECODE_H

#include <cassert>
#include <ostream>
#include "types.h"

class Thread;
class Value;
class ScopePrototype;
class Prototype;
class Operation;


struct Code
{
	virtual ~Code() { }
	virtual void execute(Thread* thread) = 0;
	virtual void dump(std::ostream &stream);
};

struct ConstCode: Code
{
	ConstCode(Value* value);
	
	virtual void execute(Thread* thread);
	
	Value* value;
};

struct ValRefCode: Code
{
	ValRefCode(size_t depth, size_t index);
	
	virtual void execute(Thread* thread);
	
	size_t depth;
	size_t index;
};

struct SelectCode: Code
{
	SelectCode(const std::string& name);
	
	virtual void execute(Thread* thread);
	
	std::string name;
};

struct ApplyCode: Code
{
	virtual void execute(Thread* thread);
};

struct ValCode: Code
{
	virtual void execute(Thread* thread);
};

struct ParentCode: Code
{
	virtual void execute(Thread* thread);
};

struct PopCode: Code
{
	virtual void execute(Thread* thread);
};

struct ScopeCode: Code
{
	virtual void execute(Thread* thread);
};

struct ReturnCode: Code
{
	virtual void execute(Thread* thread);
};

struct TupleCode: Code
{
	TupleCode(size_t size);
	
	virtual void execute(Thread* thread);
	
	size_t size;
};

struct NativeCode: Code
{
	struct Operation: ScopePrototype
	{
		Operation(Prototype* outer, const std::string& name, bool lazy);
		
		virtual Value* execute(Thread* thread, Value* receiver, Value* argument) = 0;
		
		std::string name;
	};
	
	NativeCode(Operation* operation);
	
	virtual void execute(Thread* thread);
	
	Operation* operation;
};

struct DefRefCode: Code
{
	DefRefCode(ScopePrototype* method);
	
	virtual void execute(Thread* thread);
	
	ScopePrototype* method;
};

#endif // ndef BYTECODE_H
