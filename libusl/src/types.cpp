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


Tuple::TuplePrototype::TuplePrototype():
	Prototype(0)
{
	// TODO: add some tuple methods
}

Tuple::TuplePrototype Tuple::tuplePrototype;
