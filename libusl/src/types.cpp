#include "types.h"

#include "code.h"
#include "interpreter.h"
#include "tree.h"
#include "debug.h"

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


ThunkPrototype::ThunkPrototype(Heap* heap, Prototype* outer):
	Prototype(heap),
	outer(outer)
{}


struct ScopeSize: NativeThunk
{
	ScopeSize():
		NativeThunk(0, "Scope::size")
	{}
	
	Value* execute(Thread* thread, Value* receiver)
	{
		Scope* scope = dynamic_cast<Scope*>(receiver);
		assert(scope);
		return new Integer(thread->heap, scope->locals.size());
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
		return new MetaPrototype(thread->heap, scope->scopePrototype(), scope->outer);
	}
} scopeMetaPrototype;

ScopePrototype::ScopePrototype(Heap* heap, Prototype* outer):
	ThunkPrototype(heap, outer)
{
	members["size"] = &scopeSize;
	members["at"] = nativeMethodMember(&scopeAt);
	members["prototype"] = &scopeMetaPrototype;	
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
			thatProt = new MetaPrototype(thread->heap, scope->scopePrototype(), scope->outer);
		}
		assert(thatProt);

		assert(dynamic_cast<Function*>(thisProt) == 0); // TODO: exception
		ScopePrototype* target = new ScopePrototype(*thatProt->prototype);
		target->body.push_back(new PopCode());
		std::copy(thisProt->prototype->body.begin(), thisProt->prototype->body.end(), std::back_inserter(target->body));
		/*foreach var in this
			copy var in composedThunk
		return composedThunk;*/
		
		if (dynamic_cast<Function*>(thatProt))
		{
			return new Function(thread->heap, target, thatProt->outer);
		}
		else
		{
			return new Scope(thread->heap, target, thatProt->outer);
		}
	}
} prototypeWith;

struct MetaPrototypePrototype: Prototype
{
	MetaPrototypePrototype():
		Prototype(0)
	{
		members["with"] = nativeMethodMember(&prototypeWith);
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

struct IntegerSub: NativeMethod
{
	IntegerSub():
		NativeMethod(&Integer::integerPrototype, "Integer::-", new ValPatternNode(Position(), "that"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Integer* thisInt = dynamic_cast<Integer*>(receiver);
		Integer* thatInt = dynamic_cast<Integer*>(argument);
		
		assert(thisInt);
		assert(thatInt);
		
		return new Integer(thread->heap, thisInt->value - thatInt->value);
	}
} integerSub;

struct IntegerLessThan: NativeMethod
{
	IntegerLessThan():
		NativeMethod(&Integer::integerPrototype, "Integer::<", new ValPatternNode(Position(), "that"))
	{}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		Integer* thisInt = dynamic_cast<Integer*>(receiver);
		Integer* thatInt = dynamic_cast<Integer*>(argument);
		
		assert(thisInt);
		assert(thatInt);
		
		bool result = thisInt->value < thatInt->value;
		string resultName(result ? "true" : "false");
		Value*& resultValue(result ? thread->runtimeValues.trueValue : thread->runtimeValues.falseValue);
		return thread->getRuntimeValue(resultValue, resultName);
	}
} integerLessThan;

Integer::IntegerPrototype::IntegerPrototype():
	Prototype(0)
{
	members["+"] = nativeMethodMember(&integerAdd);
	members["-"] = nativeMethodMember(&integerSub);
	members["<"] = nativeMethodMember(&integerLessThan);
}

Integer::IntegerPrototype Integer::integerPrototype;

