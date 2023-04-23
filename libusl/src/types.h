#ifndef TYPES_H
#define TYPES_H

#include "memory.h"

#include <cassert>
#include <algorithm>
#include <typeinfo>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <vector>
#include <functional>

struct Prototype;
struct Value
{
	Prototype* prototype;
	bool marked;
	
	Value(Heap* heap, Prototype* prototype):
		prototype(prototype)
	{
		marked = false;
		if (heap != 0)
		{
			heap->values.push_back(this);
		}
	}
	
	virtual ~Value() { }
	
	void dump(std::ostream &stream) const;
	
	virtual void dumpSpecific(std::ostream &stream) const { }
	
	virtual void propagateMarkForGC() { }
	
	void markForGC()
	{
		if (!marked)
		{
			marked = true;
			propagateMarkForGC();
		}
	}
	
	void clearGCMark() { marked = false; }
};
extern Value nil;

struct ThunkPrototype;
struct NativeCode;
struct Prototype: Value
{
	typedef std::map<std::string, ThunkPrototype*> Members;
	
	Members members;
	
	Prototype(Heap* heap);
	
	void addMethod(NativeCode* native);
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		stream << ": ";
		using std::transform;
		using std::ostream_iterator;
		using std::string;
		transform(members.begin(), members.end(), ostream_iterator<string>(stream, " "), [](auto& member) {return member.first; });
	}
	
	virtual void propagateMarkForGC()
	{
		using std::for_each;
		for_each(members.begin(), members.end(), [this](auto& member) {dynamic_cast<Value*>(member.second)->markForGC(); });
	}
	
	virtual ThunkPrototype* lookup(const std::string& name) const
	{
		Members::const_iterator method = members.find(name);
		if (method != members.end())
			return method->second;
		else
			return 0;
	}
};

struct Code;
struct ThunkPrototype: Prototype
{
	typedef std::vector<Code*> Body;

	Prototype* outer;
	Body body;

	ThunkPrototype(Heap* heap, Prototype* outer);
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		stream << body.size() << " codes";
	}
	
	virtual void propagateMarkForGC()
	{
		if (outer != 0)
			outer->markForGC();
		Prototype::propagateMarkForGC();
	}
};

struct Thunk: Value
{
	typedef ThunkPrototype Prototype;
	Value* outer;

	Thunk(Heap* heap, ThunkPrototype* prototype, Value* outer):
		Value(heap, prototype),
		outer(outer)
	{}

	ThunkPrototype* thunkPrototype()
	{
		return static_cast<ThunkPrototype*>(prototype);
	}
};

struct ScopePrototype: ThunkPrototype
{
	typedef std::vector<std::string> Locals;
	
	Locals locals;
	
	ScopePrototype(Heap* heap, Prototype* outer);
	virtual ~ScopePrototype();
};

struct Scope: Thunk
{
	typedef ScopePrototype Prototype;
	typedef std::vector<Value*> Locals;
	
	Locals locals;
	
	Scope(Heap* heap, ScopePrototype* prototype, Value* outer);
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		for(Locals::const_iterator it = locals.begin(); it != locals.end(); ++it)
		{
			const Value* local = *it;
			if (local == 0)
				stream << "0(" << scopePrototype()->locals[it - locals.begin()] << ")";
			else
				local->dump(stream);
		}
	}
	
	virtual void propagateMarkForGC()
	{
		using std::for_each;
		using std::mem_fun;
		for_each(locals.begin(), locals.end(), mem_fun(&Value::markForGC));
	}
	
	ScopePrototype* scopePrototype() const
	{
		return static_cast<ScopePrototype*>(prototype);
	}
};

struct MetaPrototype: Value
{
	typedef ScopePrototype Prototype;

	MetaPrototype(Heap* heap, Prototype* prototype, Value* outer);
	
	Prototype* prototype; // this is the prototype of the target, not of this meta object
	Value* outer;
};

struct Function: MetaPrototype
{
	typedef ScopePrototype Prototype;

	Function(Heap* heap, Prototype* prototype, Value* outer);
};

#endif // ndef TYPES_H
