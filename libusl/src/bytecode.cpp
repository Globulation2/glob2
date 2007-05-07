#include "bytecode.h"
#include "interpreter.h"

size_t ConstBytecode::execute(size_t programCounter, Thread* thread)
{
	thread->frames.top().stack.push_back(value);
	return programCounter + 1;
}

size_t ApplyBytecode::execute(size_t programCounter, Thread* thread)
{
	// fetch receiver
	Value* receiver = *(thread->frames.top().stack.end() - argCount - 1);
	
	// fetch method
	Executable* executable = receiver->proto->lookup(method);
	assert(argCount == executable->args.size());
	
	return executable->execute(programCounter + 1, thread);
}
