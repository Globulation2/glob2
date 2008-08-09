#include "types.h"

#include "code.h"
#include "interpreter.h"
#include "debug.h"
#include "usl.h"
#include "native.h"

#include <cassert>
#include <algorithm>

using namespace std;

void Value::dump(std::ostream &stream) const
{
	stream << unmangle(typeid(*this).name()) << "(" << prototype << ")" << " ";
	dumpSpecific(stream);
}


Prototype Nil(0);
Value nil(0, &Nil);


Prototype::Prototype(Heap* heap):
	Value(heap, 0)
{}

void Prototype::addMethod(NativeCode* native)
{
	ScopePrototype* scope = new ScopePrototype(static_cast<Heap*>(0), this); // TODO: GC
	/*
	if (arguments > 0)
	{
		scope->body.push_back(new EvalCode()); // evaluate the argument
		if (arguments > 1)
		{
			assert(false); // TODO
		}
	}
	else
	{
		scope->body.push_back(new PopCode()); // dump the argument
	}
	if (receiver)
	{
		scope->body.push_back(new ThunkCode()); // get the current thunk
		scope->body.push_back(new ParentCode()); // get the parent value
	}
	*/
	native->prologue(scope);
	scope->body.push_back(native); // run the method
	native->epilogue(scope);
	
	members[native->name] = methodMember(scope);
}


ThunkPrototype::ThunkPrototype(Heap* heap, Prototype* outer):
	Prototype(heap),
	outer(outer)
{}

/*
struct ScopeSize: NativeThunk
{
	ScopeSize():
		NativeThunk(0, "Scope::size")
	{}
	
	Value* execute(Thread* thread, Value* receiver)
	{
		Scope* scope = dynamic_cast<Scope*>(receiver);
		assert(scope);
		return new Integer(&thread->usl->heap, scope->locals.size());
	}
} scopeSize;

struct ScopeAt: NativeMethod
{
	ScopeAt():
		NativeMethod(0, "Scope::at", new ValPatternNode(Position(), "index"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Scope* scope = dynamic_cast<Scope*>(receiver);
		assert(scope);
		
		Integer* index = dynamic_cast<Integer*>(argument);
		assert(index);

		size_t i = index->value;
		assert(i >= 0);
		assert(i < scope->locals.size());

		return scope->locals[i];
	}
} scopeAt;


struct ScopeMetaPrototype: NativeThunk
{
	ScopeMetaPrototype():
		NativeThunk(0, "Scope::metaPrototype")
	{}
	
	Value* execute(Thread* thread, Value* receiver)
	{
		Scope* scope = dynamic_cast<Scope*>(receiver);
		assert(scope);
		return new MetaPrototype(&thread->usl->heap, scope->scopePrototype(), scope->outer);
	}
} scopeMetaPrototype;
*/
ScopePrototype::ScopePrototype(Heap* heap, Prototype* outer):
	ThunkPrototype(heap, outer)
{
//	members["size"] = &scopeSize;
//	members["at"] = nativeMethodMember(&scopeAt);
//	members["prototype"] = &scopeMetaPrototype;	
}

ScopePrototype::~ScopePrototype()
{
	for (Body::iterator it = body.begin(); it != body.end(); ++it)
		delete *it;
}


Scope::Scope(Heap* heap, ScopePrototype* prototype, Value* outer):
	Thunk(heap, prototype, outer),
	locals(prototype->locals.size(), 0)
{}
	
/*
NativeThunk::NativeThunk(Prototype* outer, const std::string& name):
	ThunkPrototype(0, outer),
	name(name)
{
	body.push_back(new ThunkCode());
	body.push_back(new ParentCode());
	body.push_back(new NativeThunkCode(this));
}


NativeMethod::NativeMethod(Prototype* outer, const std::string& name, PatternNode* argument):
	ScopePrototype(0, outer),
	name(name)
{
	argument->generate(this, 0, (Heap*) 0);
	delete argument;
	body.push_back(new ThunkCode());
	body.push_back(new ParentCode());
	body.push_back(new ThunkCode());
	body.push_back(new ValRefCode(0));
	body.push_back(new NativeMethodCode(this));
}


#include <iostream>
struct PrototypeWith: NativeMethod
{
	PrototypeWith():
		NativeMethod(0, "Prototype::with", new ValPatternNode(Position(), "that"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		MetaPrototype* thisProt = dynamic_cast<MetaPrototype*>(receiver);
		assert(thisProt);
		
		MetaPrototype* thatProt = dynamic_cast<MetaPrototype*>(argument);
		if (thatProt == 0)
		{
			Scope* scope = dynamic_cast<Scope*>(argument);
			assert(scope); // TODO: exception
			thatProt = new MetaPrototype(&thread->usl->heap, scope->scopePrototype(), scope->outer);
		}
		assert(thatProt);

		assert(dynamic_cast<Function*>(thisProt) == 0); // TODO: exception
		ScopePrototype* target = new ScopePrototype(*thatProt->prototype);
		target->body.push_back(new PopCode());
		std::copy(thisProt->prototype->body.begin(), thisProt->prototype->body.end(), std::back_inserter(target->body));
		/*foreach var in this
			copy var in composedThunk
		return composedThunk;/
		
		if (dynamic_cast<Function*>(thatProt))
		{
			return new Function(&thread->usl->heap, target, thatProt->outer);
		}
		else
		{
			return new Scope(&thread->usl->heap, target, thatProt->outer);
		}
	}
} prototypeWith;
*/
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

