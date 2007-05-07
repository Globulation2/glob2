#include "bytecode.h"
#include "interpreter.h"

void ConstBytecode::execute(Thread* thread)
{
	thread->frames.top().stack.push_back(value);
}

void ApplyBytecode::execute(Thread* thread)
{
	// fetch receiver
	Value* receiver = *(thread->frames.top().stack.end() - argCount - 1);
	
	// fetch method
	Executable* executable = receiver->proto->lookup(method);
	assert(argCount == executable->args.size());
	
	executable->execute(thread);
}
