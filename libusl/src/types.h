#ifndef TYPES_H
#define TYPES_H

#include "memory.h"

#include <cassert>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <vector>
#include <ext/functional>

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
struct Prototype: Value
{
	typedef std::map<std::string, ThunkPrototype*> Members;
	
	Members members;
	
	Prototype(Heap* heap);
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		stream << ": ";
		using namespace std;
		using namespace __gnu_cxx;
		transform(members.begin(), members.end(), ostream_iterator<string>(stream, " "), select1st<Members::value_type>());
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		using namespace __gnu_cxx;
		for_each(members.begin(), members.end(), compose1(mem_fun(&Value::markForGC), select2nd<Members::value_type>()));
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
		using namespace std;
		using namespace __gnu_cxx;
		for_each(locals.begin(), locals.end(), mem_fun(&Value::markForGC));
	}
	
	ScopePrototype* scopePrototype() const
	{
		return static_cast<ScopePrototype*>(prototype);
	}
};

struct NativeThunk: ThunkPrototype
{
	std::string name;
	NativeThunk(Prototype* outer, const std::string& name);
	virtual Value* execute(Thread* thread, Value* receiver) = 0;
};

struct PatternNode;
struct NativeMethod: ScopePrototype
{
	std::string name;
	NativeMethod(Prototype* outer, const std::string& name, PatternNode* argument);
	virtual Value* execute(Thread* thread, Value* receiver, Value* argument) = 0;
};

struct Function: Value
{
	typedef ScopePrototype Prototype;

	Function(Heap* heap, Prototype* prototype, Value* outer);
	
	Prototype* prototype;
	Value* outer;
};

struct Integer: Value
{
	struct IntegerPrototype: Prototype
	{
		IntegerPrototype();
	};
	static IntegerPrototype integerPrototype;
	
	int value;
	
	Integer(Heap* heap, int value):
		Value(heap, &integerPrototype),
		value(value)
	{}
	
	virtual void dumpSpecific(std::ostream& stream) const { stream << "= " << value; }
};

#endif // ndef TYPES_H
