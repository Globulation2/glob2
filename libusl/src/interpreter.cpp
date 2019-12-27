#include "interpreter.h"

#include "types.h"
#include "usl.h"
#include "code.h"

#include <iostream>

using namespace std;

bool Thread::step()
{
	if (state == RUN)
	{
		Thread::Frame& frame = frames.back();
		ThunkPrototype* thunk = frame.thunk->thunkPrototype();
		size_t nextInstr = frame.nextInstr;
		Code* code = thunk->body[nextInstr];
		frame.nextInstr++;

		// Uncomment to get *verbose* debug info on scripting
		/*cout << thunk;
		for (size_t i = 0; i < frames.size(); ++i)
			cout << "[" << frames[i].stack.size() << "]";
		cout << " " << usl->debug.find(thunk, nextInstr) << ": ";
		code->dump(cout);
		cout << endl;*/

		code->execute(this);

		while (true)
		{
			Thread::Frame& frame = frames.back();
			if (frame.nextInstr < frame.thunk->thunkPrototype()->body.size())
				break;
			Value* retVal = frame.stack.back();
			frames.pop_back();
			if (!frames.empty())
			{
				frames.back().stack.push_back(retVal);
			}
			else
			{
				#ifdef DEBUG_USL
					retVal->dump(cout);
					cout << endl;
				#endif
				state = STOP;
				break;
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

size_t Thread::run(size_t maxSteps)
{
	size_t steps;
	for (steps = 0; steps < maxSteps; ++steps)
	{
		if (!step())
			break;
	}
	return steps;
}

size_t Thread::run()
{
	size_t steps;
	for (steps = 0; true; ++steps)
	{
		if (!step())
			break;
	}
	return steps;
}

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
	thunk->markForGC();
}
