#ifndef BYTECODE_H
#define BYTECODE_H

#include <string>
#include "types.h"

struct Value;
struct Scope;

struct Bytecode
{
	virtual ~Bytecode() { }
	virtual void execute(Thread* thread) = 0;
};

struct ConstBytecode: Bytecode
{
	ConstBytecode(Value* value):
		value(value)
	{}
	
	void execute(Thread* thread);
	
	Value* value;
};

struct ApplyBytecode: Bytecode
{
	ApplyBytecode(const std::string& method, size_t argCount):
		method(method),
		argCount(argCount)
	{}
	
	void execute(Thread* thread);
	
	const std::string method;
	size_t argCount;
};

#endif // ndef BYTECODE_H
