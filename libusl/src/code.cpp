#include "code.h"
#include "interpreter.h"

ScopePrototype* thisMethod(Prototype* outer)
{
	ScopePrototype* method = new ScopePrototype(0, outer);
	method->body.push_back(new PopCode());
	method->body.push_back(new ScopeCode());
	method->body.push_back(new ParentCode());
	method->body.push_back(new ReturnCode());
	return method;
}


void Code::dump(std::ostream &stream)
{
	stream << typeid(*this).name();
}


ConstCode::ConstCode(Value* value):
	value(value)
{}

void ConstCode::execute(Thread* thread)
{
	thread->frames.back().stack.push_back(value);
}


ValRefCode::ValRefCode(size_t depth, size_t index):
	depth(depth),
	index(index)
{}

void ValRefCode::execute(Thread* thread)
{
	Thread::Frame& frame = thread->frames.back();
	Scope* scope = frame.scope;
	for (size_t i = 0; i < depth; ++i)
	{
		// scope = static_cast<Scope*>(scope->outer); // Should be safe if the parser is bug-free
		scope = dynamic_cast<Scope*>(scope->outer);
		assert(scope); // Should not fail if the parser is bug-free
	}
	assert(index < scope->locals.size()); // Should not fail if the parser is bug-free
	frame.stack.push_back(scope->locals[index]);
}


SelectCode::SelectCode(const std::string& name, bool pop):
	name(name),
	pop(pop)
{}

#include <iostream>
void SelectCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	// get receiver
	Value* receiver = stack.back();
	if (pop)
		stack.pop_back();
	
	// get method
	ScopePrototype* method = receiver->prototype->lookup(name);
	
	std::cerr << typeid(*receiver).name() << std::endl;
	assert(method != 0);
	
	// create a function
	Function* function = new Function(thread->heap, receiver, method);
	
	// put the function on the stack
	stack.push_back(function);
}


ApplyCode::ApplyCode(bool arg):
	arg(arg)
{}

void ApplyCode::execute(Thread* thread)
{
	Thread::Frames& frames = thread->frames;
	Thread::Frame::Stack& stack = frames.back().stack;
	
	// get argument
	Value* argument;
	if (arg)
	{
		argument = stack.back();
		stack.pop_back();
	}
	
	// get the function
	Function* function = dynamic_cast<Function*>(stack.back());
	stack.pop_back();
	
	assert(function != 0);
	
	// create a new scope
	Scope* scope = new Scope(thread->heap, function->method, function->receiver);
	
	// push a new frame
	frames.push_back(scope);
	
	// put the argument on the stack
	if (arg)
		frames.back().stack.push_back(argument);
	else
		frames.back().stack.push_back(&nil);
}


void ValCode::execute(Thread* thread)
{
	Thread::Frame& frame = thread->frames.back();
	Thread::Frame::Stack& stack = frame.stack;
	frame.scope->locals.push_back(stack.back());
	stack.pop_back();
}


void ParentCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	Value* value = stack.back();
	stack.pop_back();
	
	Scope* scope = dynamic_cast<Scope*>(value);
	assert(scope != 0); // Should not fail if the parser is bug-free
	
	stack.push_back(scope->outer);
}


void PopCode::execute(Thread* thread)
{
	thread->frames.back().stack.pop_back();
}


void ScopeCode::execute(Thread* thread)
{
	Thread::Frame& frame = thread->frames.back();
	frame.stack.push_back(frame.scope);
}


void ReturnCode::execute(Thread* thread)
{
	Value* value = thread->frames.back().stack.back();
	thread->frames.pop_back();
	thread->frames.back().stack.push_back(value);
}


ArrayCode::ArrayCode(size_t size):
	size(size)
{}

void ArrayCode::execute(Thread* thread)
{
	Array* array = new Array(thread->heap);
	Thread::Frame::Stack &stack = thread->frames.back().stack;
	Thread::Frame::Stack::const_iterator stackEnd = stack.end();
	std::copy(stackEnd - size, stackEnd, std::back_inserter(array->values));
	stack.resize(stack.size() - size);
	stack.push_back(array);
}


NativeCode::Operation::Operation(Prototype* outer, const std::string& name, bool lazy):
	ScopePrototype(0, outer),
	name(name)
{
	body.push_back(new ValCode());
	body.push_back(new ScopeCode());
	body.push_back(new ParentCode());
	body.push_back(new ValRefCode(0, 0));
	if (!lazy)
	{
		body.push_back(new ApplyCode(false));
	}
	body.push_back(new NativeCode(this));
	body.push_back(new ReturnCode());
}


NativeCode::NativeCode(Operation* operation):
	operation(operation)
{}

void NativeCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	Value* argument = stack.back();
	stack.pop_back();
	Value* receiver = stack.back();
	stack.pop_back();
	
	stack.push_back(operation->execute(thread, receiver, argument));
}


DefRefCode::DefRefCode(ScopePrototype* method):
	method(method)
{}

void DefRefCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	// get receiver
	Value* receiver = stack.back();
	stack.pop_back();
	
	assert(method->outer == receiver->prototype); // Should not fail if the parser is bug-free
	
	// create a function
	Function* function = new Function(thread->heap, receiver, method);
	
	// put the function on the stack
	stack.push_back(function);
}
