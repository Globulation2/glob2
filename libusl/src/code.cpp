#include "code.h"
#include "native.h"
#include "interpreter.h"
#include "tree.h"
#include "debug.h"
#include "error.h"
#include "usl.h"

#include <sstream>

using std::ostringstream;
using std::string;

ThunkPrototype* thisMember(Prototype* outer)
{
	ThunkPrototype* thunk = new ThunkPrototype(0, outer); // TODO: GC
	thunk->body.push_back(new ThunkCode());
	thunk->body.push_back(new ParentCode());
	return thunk;
}

ThunkPrototype* methodMember(ScopePrototype* method)
{
	ThunkPrototype* thunk = new ThunkPrototype(0, method->outer);
	thunk->body.push_back(new ThunkCode());
	thunk->body.push_back(new ParentCode());
	thunk->body.push_back(new CreateCode<Function>(method));
	return thunk;
}


void Code::dump(std::ostream &stream) const
{
	stream << unmangle(typeid(*this).name());
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
	
	// get the thunk
	Thunk* thunk = dynamic_cast<Thunk*>(stack.back());
	stack.pop_back();
	
	if (frames.back().nextInstr == frames.back().thunk->thunkPrototype()->body.size())
		frames.pop_back();
	
	// push a new frame
	assert(thunk != 0); // TODO: This assert can be triggered by the user
	frames.push_back(thunk);
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
	ThunkPrototype* def = receiver->prototype->lookup(name);
	if (def == 0)
	{
		const Thread::Frame& frame = thread->frames.back();
		ostringstream message;
		message << "member <" << name << "> not found in ";
		receiver->dump(message);
		message << "(" << receiver->prototype << ")";
		throw Exception(thread->usl->debug.find(frame.thunk->thunkPrototype(), frame.nextInstr), message.str());
	}
	
	// create a thunk
	Thunk* thunk = new Thunk(&thread->usl->heap, def, receiver);
	
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
	assert(stack.size() >= 2);
	
	// get argument
	Value* argument = stack.back();
	stack.pop_back();
	
	// get the function
	Function* function = dynamic_cast<Function*>(stack.back());
	assert(function != 0); // TODO: This assert can be triggered by the user
	stack.pop_back();
	
	if (frames.back().nextInstr == frames.back().thunk->thunkPrototype()->body.size())
		frames.pop_back();

	// push a new frame
	Scope* scope = new Scope(&thread->usl->heap, function->prototype, function->outer);
	frames.push_back(scope);
	
	// put the argument on the stack
	frames.back().stack.push_back(argument);
}


ValCode::ValCode(size_t index):
	index(index)
{}

void ValCode::execute(Thread* thread)
{
	assert(thread->frames.size() > 0);
	
	Thread::Frame& frame = thread->frames.back();
	Thread::Frame::Stack& stack = frame.stack;
	Scope* scope = dynamic_cast<Scope*>(frame.thunk);
	
	assert(stack.size() > 0);
	assert(scope);
	assert(scope->locals.size() > index);
	
	scope->locals[index] = stack.back();
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
	
	Thunk* thunk = dynamic_cast<Thunk*>(value);
	assert(thunk != 0); // Should not fail if the parser is bug-free
	
	stack.push_back(thunk->outer);
}


void PopCode::execute(Thread* thread)
{
	thread->frames.back().stack.pop_back();
}


DupCode::DupCode(size_t index):
	index(index)
{}

void DupCode::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	stack.push_back(*(stack.rbegin() + index));
}


void ThunkCode::execute(Thread* thread)
{
	Thread::Frame& frame = thread->frames.back();
	frame.stack.push_back(frame.thunk);
}


NativeCode::NativeCode(const string& name):
	name(name)
{}

void NativeCode::dumpSpecific(std::ostream &stream) const
{
	stream << " " << name;
}


template <typename ThunkType>
CreateCode<ThunkType>::CreateCode(typename ThunkType::Prototype* prototype):
	prototype(prototype)
{}

template <typename ThunkType>
void CreateCode<ThunkType>::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	
	// get receiver
	Value* receiver = stack.back();
	stack.pop_back();
	
	assert(prototype->outer == 0 || prototype->outer == receiver->prototype); // Should not fail if the parser is bug-free
	
	// create a thunk
	ThunkType* thunk = new ThunkType(&thread->usl->heap, prototype, receiver);
	
	// put the thunk on the stack
	stack.push_back(thunk);
}

template <typename ThunkType>
void CreateCode<ThunkType>::dumpSpecific(std::ostream &stream) const
{
	stream << " " << prototype;
}

template struct CreateCode<Thunk>;
template struct CreateCode<Scope>;
template struct CreateCode<Function>;

