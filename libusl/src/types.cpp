#include "types.h"
#include "interpreter.h"

#include <cassert>

Prototype Nil(0, 0);
Value nil(0, &Nil);

Tuple::TuplePrototype::TuplePrototype():
	Prototype(0, 0)
{
	// TODO: add some tuple methods
}

Tuple::TuplePrototype Tuple::tuplePrototype;
