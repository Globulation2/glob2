#include "types.h"
#include "interpreter.h"

size_t Method::execute(size_t returnAddress, Thread* thread)
{
	// build new frame
	Thread::Frames& frames = thread->frames;
	Frame& caller = frames.top();
	frames.push(Frame());
	Frame& callee = frames.top();
	
	// fill frame
	size_t callerArg = caller.stack.size();
	callee.scope = new Scope(this, 0);
	callee.returnAddress = returnAddress;
	
	// fetch arguments
	for (size_t i = 0; i < args.size(); i++)
		callee.scope->locals[args[i]] = caller.stack[--callerArg];
	callee.scope->parent = caller.stack[--callerArg];
	caller.stack.resize(callerArg);
	
	// we have to jump to the method
	return address;
}
