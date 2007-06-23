#include "types.h"
#include "code.h"
#include "interpreter.h"

#include <cassert>


Prototype Nil(0);
Value nil(0, &Nil);


Function::FunctionPrototype::FunctionPrototype():
	Prototype(0)
{
	ScopePrototype* applyMethod = new ScopePrototype(0, this);
	ScopePrototype::Body& applyBody = applyMethod->body;
	applyBody.push_back(new ValCode());
	applyBody.push_back(new ScopeCode());
	applyBody.push_back(new ParentCode());
	applyBody.push_back(new ValRefCode(0, 0));
	applyBody.push_back(new ApplyCode());
	applyBody.push_back(new ReturnCode());
	methods["apply"] = applyMethod;
}

Function::FunctionPrototype Function::functionPrototype;


struct IntegerAdd: NativeCode::Operation
{
	IntegerAdd():
		NativeCode::Operation(&Integer::integerPrototype, "Integer::+", false)
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
	methods["this"] = thisMethod(&integerPrototype);
	methods["+"] = &integerAdd;
}

Integer::IntegerPrototype Integer::integerPrototype;


struct ArrayGet: NativeCode::Operation
{
	ArrayGet():
		NativeCode::Operation(&Array::arrayPrototype, "Array::get", false)
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Array* array = dynamic_cast<Array*>(receiver);
		Integer* index = dynamic_cast<Integer*>(argument);
		
		assert(array);
		assert(index);
		
		assert(index->value >= 0);
		assert(index->value < array->values.size());
		
		return array->values[index->value];
	}
} arrayGet;

Array::ArrayPrototype::ArrayPrototype():
	Prototype(0)
{
	methods["get"] = &arrayGet;
}

Array::ArrayPrototype Array::arrayPrototype;
