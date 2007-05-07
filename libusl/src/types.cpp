#include "types.h"
#include "interpreter.h"

void Method::execute(Thread* thread)
{
	// build new frame
	Thread::Frames& frames = thread->frames;
	Frame& caller = frames.top();
	frames.push(Frame());
	Frame& callee = frames.top();
	
	// fill frame
	size_t callerArg = caller.stack.size();
	callee.scope = new Scope(this, 0);
	callee.nextInstr = address;
	
	// fetch arguments
	for (size_t i = 0; i < args.size(); i++)
		callee.scope->locals[args[i]] = caller.stack[--callerArg];
	callee.scope->parent = caller.stack[--callerArg];
	caller.stack.resize(callerArg);
}
