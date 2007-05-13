#include "types.h"
#include "interpreter.h"

#include <cassert>

void UserMethod::execute(Thread* thread)
{
	Thread::Frames& frames = thread->frames;
	Thread::Frame::Stack& stack = frames.back().stack;
	
	size_t argsSize = args.size();
	
	// create a new scope
	Scope* receiver = dynamic_cast<Scope*>(*(stack.rbegin() + argsSize));
	assert(receiver != 0);
	Scope* scope = new Scope(thread->heap, this, receiver);
	
	// copy arguments
	Thread::Frame::Stack::const_iterator arg = stack.end() - argsSize;
	for (size_t i = 0; i < argsSize; ++i)
	{
		scope->locals[args[i]] = *arg;
		++arg;
	}
	
	// pop arguments and receiver
	stack.resize(stack.size() - argsSize - 1);
	
	frames.push_back(scope);
}
