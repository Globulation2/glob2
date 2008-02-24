#include "types.h"

#include "code.h"
#include "interpreter.h"
#include "tree.h"

#include <cassert>


Prototype Nil(0);
Value nil(0, &Nil);


ScopePrototype::ScopePrototype(Heap* heap, Prototype* outer):
	Prototype(heap),
	outer(outer)
{}

ScopePrototype::~ScopePrototype()
{
	for (Body::iterator it = body.begin(); it != body.end(); ++it)
		delete *it;
}


Method::Method(Heap* heap, Prototype* outer):
ScopePrototype(heap, outer)
{}


NativeMethod::NativeMethod(Prototype* outer, const std::string& name, PatternNode* argument):
	Method(0, outer),
	name(name)
{
	argument->generate(this, 0, (Heap*) 0);
	delete argument;
	body.push_back(new ScopeCode());
	body.push_back(new ParentCode());
	body.push_back(new ScopeCode());
	body.push_back(new ValRefCode(0));
	body.push_back(new NativeCode(this));
	body.push_back(new ReturnCode());
}

Function::FunctionPrototype::FunctionPrototype():
	Prototype(0)
{
	members["apply"] = thisMember(this);
}

Function::FunctionPrototype Function::functionPrototype;


struct IntegerAdd: NativeMethod
{
	IntegerAdd():
		NativeMethod(&Integer::integerPrototype, "Integer::+", new ValPatternNode(Position(), "that"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Integer* thisInt = dynamic_cast<Integer*>(receiver);
		Integer* thatInt = dynamic_cast<Integer*>(argument);
		
		assert(thisInt);
		assert(thatInt);
		
		return new Integer(thread->heap, thisInt->value + thatInt->value);
	}
} integerAdd;

Integer::IntegerPrototype::IntegerPrototype():
	Prototype(0)
{
	members["+"] = nativeMethodMember(&integerAdd);
}

Integer::IntegerPrototype Integer::integerPrototype;

