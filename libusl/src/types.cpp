#include "types.h"
#include "interpreter.h"

#include <cassert>

Prototype Nil(0, 0);
Value nil(0, &Nil);

void UserMethod::execute(Thread* thread)
{
	Thread::Frames& frames = thread->frames;
	Thread::Frame::Stack& stack = frames.back().stack;
	size_t argsSize = args.size();
	Thread::Frame::Stack::const_iterator arg = stack.end() - argsSize;
	
	// get the receiver
	Scope* receiver = dynamic_cast<Scope*>(*arg++);
	assert(receiver != 0);
	
	// create a new scope
	Scope* scope = new Scope(thread->heap, this, receiver);
	for (size_t i = 0; i < argsSize; ++i)
	{
		scope->locals[args[i]] = *arg++;
	}
	
	// pop arguments and receiver
	stack.resize(stack.size() - argsSize - 1);
	
	// push a new frame
	frames.push_back(scope);
}
