#include "types.h"

#include "code.h"
#include "interpreter.h"
#include "debug.h"
#include "usl.h"

#include <cassert>
#include <algorithm>
#include <typeinfo>


void Heap::collectGarbage()
{
	using std::for_each;
	using std::mem_fn;

	// filter copy, delete unrefs
	Values marked;
	for (size_t i = 0; i < values.size(); i++)
	{
		if (values[i]->marked)
			marked.push_back(values[i]);
		else
			delete values[i];
	}

	// clean heap
	swap(values, marked);
	for_each(values.begin(), values.end(), mem_fn(&Value::clearGCMark));
}


void Value::dump(std::ostream &stream) const
{
	stream << unmangle(typeid(*this).name()) << "(" << prototype << ")" << " ";
	dumpSpecific(stream);
}


Prototype Nil(0);
Value nil(0, &Nil);


Prototype::Prototype(Heap* heap):
	Value(heap, 0),
	heap(heap)
{
}

void Prototype::addMethod(NativeCode* native)
{
	assert(heap);
	ScopePrototype* scope = new ScopePrototype(heap, this); // TODO: GC
	native->prologue(scope);
	scope->body.push_back(native); // run the method
	native->epilogue(scope);
	
	members[native->name] = methodMember(scope);
}


ThunkPrototype::ThunkPrototype(Heap* heap, Prototype* outer):
	Prototype(heap),
	outer(outer)
{}

ThunkPrototype::~ThunkPrototype()
{
	for (Body::iterator it = body.begin(); it != body.end(); ++it)
		delete *it;
}

ScopePrototype::ScopePrototype(Heap* heap, Prototype* outer):
	ThunkPrototype(heap, outer)
{
//	members["size"] = &scopeSize;
//	members["at"] = nativeMethodMember(&scopeAt);
//	members["prototype"] = &scopeMetaPrototype;	
}

Scope::Scope(Heap* heap, ScopePrototype* prototype, Value* outer):
	Thunk(heap, prototype, outer),
	locals(prototype->locals.size(), 0)
{}
	
struct MetaPrototypePrototype: Prototype
{
	MetaPrototypePrototype():
		Prototype(0)
	{
//		members["with"] = nativeMethodMember(&prototypeWith);
	}
} metaPrototypePrototype;

MetaPrototype::MetaPrototype(Heap* heap, Prototype* prototype, Value* outer):
	Value(heap, &metaPrototypePrototype),
	prototype(prototype),
	outer(outer)
{
}


Function::Function(Heap* heap, Prototype* prototype, Value* outer):
	MetaPrototype(heap, prototype, outer)
{}

