#include "types.h"
#include "interpreter.h"

#include <cassert>

Prototype Nil(0);
Value nil(0, &Nil);

Function::FunctionPrototype::FunctionPrototype():
	Prototype(0)
{
	// TODO: add some function methods
}

Function::FunctionPrototype Function::functionPrototype;


Tuple::TuplePrototype::TuplePrototype():
	Prototype(0)
{
	// TODO: add some tuple methods
}

Tuple::TuplePrototype Tuple::tuplePrototype;
