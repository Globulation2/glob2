#include "interpreter.h"
#include "types.h"

void Thread::markForGC()
{
	using namespace std;
	using namespace __gnu_cxx;
	
	// mark all frames in stack
	for_each(frames.begin(), frames.end(), mem_fun_ref(&Frame::markForGC));
}

void Thread::Frame::markForGC()
{
	using namespace std;
	using namespace __gnu_cxx;
	
	// mark all variables in frame
	for_each(stack.begin(), stack.end(), mem_fun(&Value::markForGC));
	scope->markForGC();
}
