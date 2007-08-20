#include "types.h"

#include "code.h"
#include "interpreter.h"
#include "tree.h"

#include <cassert>


Prototype Nil(0);
Value nil(0, &Nil);


Method::Method(Heap* heap, Prototype* outer):
	ScopePrototype(heap, outer)
{}


NativeMethod::NativeMethod(Prototype* outer, const std::string& name, PatternNode* argument):
	Method(0, outer),
	name(name)
{
	argument->generate(this, 0, (Heap*) 0);
	body.push_back(new ScopeCode());
	body.push_back(new ParentCode());
	body.push_back(new ScopeCode());
	body.push_back(new ValRefCode(0));
	body.push_back(new NativeCode(this));
	body.push_back(new ReturnCode());
}

/*
struct FunctionApply: NativeCode::Operation
{
	FunctionApply():
		NativeCode::Operation(&Integer::integerPrototype, "Function::apply", false)
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Integer* thisInt = dynamic_cast<Integer*>(receiver);
		Integer* thatInt = dynamic_cast<Integer*>(argument);
		
		assert(thisInt);
		assert(thatInt);
		
		return new Integer(thread->heap, thisInt->value + thatInt->value);
	}
} functionApply;
*/

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
	//members["this"] = thisMethod(&integerPrototype);
	members["+"] = nativeMethodMember(&integerAdd);
}

Integer::IntegerPrototype Integer::integerPrototype;


struct ArrayGet: NativeMethod
{
	ArrayGet():
		NativeMethod(&Array::arrayPrototype, "Array::get", new ValPatternNode(Position(), "index"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Array* array = dynamic_cast<Array*>(receiver);
		Integer* index = dynamic_cast<Integer*>(argument);
		
		assert(array);
		assert(index);
		
		assert(index->value >= 0);
		assert(size_t(index->value) < array->values.size());
		
		return array->values[index->value];
	}
} arrayGet;

Array::ArrayPrototype::ArrayPrototype():
	Prototype(0)
{
	members["get"] = nativeMethodMember(&arrayGet);
}

Array::ArrayPrototype Array::arrayPrototype;
