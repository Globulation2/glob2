#include "interpreter.h"

#include "types.h"
#include "usl.h"
#include "code.h"

#include <iostream>

using namespace std;

/*
Thread::RuntimeValues::RuntimeValues():
	trueValue(0),
	falseValue(0)
{}

Value* Thread::getRuntimeValue(Value*& cachedValue, const std::string& name)
{
	if (cachedValue == 0)
		cachedValue = getRootLocal(name);
	return cachedValue;
}

Value* Thread::getRootLocal(const std::string& name)
{
	ScopePrototype* rootPrototype = root->scopePrototype();
	ScopePrototype::Locals::const_iterator it = find(rootPrototype->locals.begin(), rootPrototype->locals.end(), name);
	assert(it != rootPrototype->locals.end());
	size_t index = it - rootPrototype->locals.begin();
	return root->locals[index];
}
*/

bool Thread::step()
{
	if (!frames.empty())
	{
		Thread::Frame& frame = frames.back();
		ThunkPrototype* thunk = frame.thunk->thunkPrototype();
		size_t nextInstr = frame.nextInstr;
		Code* code = thunk->body[nextInstr];
		frame.nextInstr++;
		
		cout << thunk;
		for (size_t i = 0; i < frames.size(); ++i)
			cout << "[" << frames[i].stack.size() << "]";
		cout << " " << usl->debug.find(thunk, nextInstr) << ": ";
		code->dump(cout);
		cout << endl;
		
		code->execute(this);
		
		while (true)
		{
			Thread::Frame& frame = frames.back();
			if (frame.nextInstr < frame.thunk->thunkPrototype()->body.size())
				return true;
			Value* retVal = frame.stack.back();
			frames.pop_back();
			if (!frames.empty())
			{
				frames.back().stack.push_back(retVal);
			}
			else
			{
				return true;
			}
		}
	}
	else
	{
		return false;
	}
}

void Thread::run(size_t& steps)
{
	while (steps && step())
	{
		--steps;
	}
}

void Thread::run()
{
	while (step())
		;
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
