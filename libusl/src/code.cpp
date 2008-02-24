#include "code.h"
#include "interpreter.h"
#include "tree.h"

ScopePrototype* thisMember(Prototype* outer)
{
	ScopePrototype* thunk = new ScopePrototype(0, outer); // TODO: GC
	thunk->body.push_back(new ScopeCode());
	thunk->body.push_back(new ParentCode());
	thunk->body.push_back(new ReturnCode());
	return thunk;
}
/*
struct ScopeGet: NativeMethod
{
	ScopeGet(Prototype* outer):
		NativeMethod(outer, "Scope::get", new ValPatternNode(Position(), "index"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Scope* scope = dynamic_cast<Scope*>(receiver);
		Integer* index = dynamic_cast<Integer*>(argument);
		
		assert(scope);
		assert(index);
		
		assert(index->value >= 0);
		assert(size_t(index->value) < scope->locals.size());
		// TODO get a thunk
		
		return scope->locals[index->value];
	}
};

ScopePrototype* getMember(Prototype* outer)
{
	return nativeMethodMember(new ScopeGet(outer));
}
*/
ScopePrototype* nativeMethodMember(Method* method)
{
	ScopePrototype* thunk = new ScopePrototype(0, method->outer); // TODO: GC
	thunk->body.push_back(new ScopeCode());
	thunk->body.push_back(new ParentCode());
	thunk->body.push_back(new FunCode(method));
	thunk->body.push_back(new ReturnCode());
	return thunk;
}


void Code::dump(std::ostream &stream) const
{
	stream << typeid(*this).name();
	dumpSpecific(stream);
}


ConstCode::ConstCode(Value* value):
	value(value)
{}

void ConstCode::execute(Thread* thread)
{
	thread->frames.back().stack.push_back(value);
}

void ConstCode::dumpSpecific(std::ostream &stream) const
{
	stream << " ";
	value->dump(stream);
}


ValRefCode::ValRefCode(size_t index):
	index(index)
{}

void ValRefCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	Value* value = stack.back();
	stack.pop_back();
	
	Scope* scope = dynamic_cast<Scope*>(value);
	assert(scope != 0); // Should not fail if the parser is bug-free
	
	stack.push_back(scope->locals[index]);
}

void ValRefCode::dumpSpecific(std::ostream &stream) const
{
	stream << " " << index;
}


void EvalCode::execute(Thread* thread)
{
	Thread::Frames& frames = thread->frames;
	Thread::Frame::Stack& stack = frames.back().stack;
	
	assert(stack.size() >= 1);
	
	// get the function
	Thunk* thunk = dynamic_cast<Thunk*>(stack.back());
	assert(thunk != 0);
	stack.pop_back();
	
	// create a new scope
	Scope* scope = new Scope(thread->heap, thunk->method, thunk->receiver);
	scope->locals.resize(thunk->method->locals.size());
	
	// push a new frame
	frames.push_back(scope);
}


SelectCode::SelectCode(const std::string& name):
	name(name)
{}

void SelectCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	// get receiver
	Value* receiver = stack.back();
	stack.pop_back();
	
	// get definition
	ScopePrototype* def = receiver->prototype->lookup(name);
	assert(def != 0);
	
	// create a thunk
	Thunk* thunk = new Thunk(thread->heap, receiver, def);
	
	// put the thunk on the stack
	stack.push_back(thunk);
}

void SelectCode::dumpSpecific(std::ostream &stream) const
{
	stream << " " << name;
}


void ApplyCode::execute(Thread* thread)
{
	Thread::Frames& frames = thread->frames;
	Thread::Frame::Stack& stack = frames.back().stack;
	
	// get argument
	Value* argument = stack.back();
	stack.pop_back();
	
	// push a new frame
	EvalCode::execute(thread);
	
	// put the argument on the stack
	frames.back().stack.push_back(argument);
}


ValCode::ValCode(size_t index):
	index(index)
{}

void ValCode::execute(Thread* thread)
{
	assert(thread->frames.size() > 0);
	assert(thread->frames.back().stack.size() > 0);
	assert(thread->frames.back().scope->locals.size() > index);
	
	Thread::Frame& frame = thread->frames.back();
	Thread::Frame::Stack& stack = frame.stack;
	frame.scope->locals[index] = stack.back();
	stack.pop_back();
}

void ValCode::dumpSpecific(std::ostream &stream) const
{
	stream << " " << index;
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


void DupCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	stack.push_back(stack.back());
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


NativeCode::NativeCode(NativeMethod* method):
	method(method)
{}

void NativeCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	Value* argument = stack.back();
	stack.pop_back();
	Value* receiver = stack.back();
	stack.pop_back();
	
	stack.push_back(method->execute(thread, receiver, argument));
}

void NativeCode::dumpSpecific(std::ostream &stream) const
{
	stream << " " << method->name;
}


DefRefCode::DefRefCode(ScopePrototype* def):
	def(def)
{}

void DefRefCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	// get receiver
	Value* receiver = stack.back();
	stack.pop_back();
	
	assert(def->outer == receiver->prototype); // Should not fail if the parser is bug-free
	
	// create a thunk
	Thunk* thunk = new Thunk(thread->heap, receiver, def);
	
	// put the function on the stack
	stack.push_back(thunk);
}

void DefRefCode::dumpSpecific(std::ostream &stream) const
{
	stream << " " << def;
}


FunCode::FunCode(Method* method):
	method(method)
{}

void FunCode::execute(Thread* thread)
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
