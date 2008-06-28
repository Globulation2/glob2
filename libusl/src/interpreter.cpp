#include "interpreter.h"
#include "types.h"

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
