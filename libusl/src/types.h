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
	
	void dump(std::ostream &stream) const { stream << typeid(*this).name() << " "; dumpSpecific(stream); }
	
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

struct ScopePrototype;
struct Prototype: Value
{
	typedef std::map<std::string, ScopePrototype*> Methods;
	
	Methods methods;
	
	Prototype(Heap* heap):
		Value(heap, 0)
	{ // TODO: MetaPrototype
	}
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		stream << ": ";
		using namespace std;
		using namespace __gnu_cxx;
		transform(methods.begin(), methods.end(), ostream_iterator<string>(stream, " "), select1st<Methods::value_type>());
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		using namespace __gnu_cxx;
		for_each(methods.begin(), methods.end(), compose1(mem_fun(&Value::markForGC), select2nd<Methods::value_type>()));
	}
	
	virtual ScopePrototype* lookup(const std::string& name) const
	{
		Methods::const_iterator method = methods.find(name);
		if (method != methods.end())
			return method->second;
		else
			return 0;
	}
};

struct Code;
struct ScopePrototype: Prototype
{
	typedef std::vector<std::string> Locals;
	typedef std::vector<Code*> Body;
	
	Prototype* outer;
	Locals locals;
	Body body;
	
	ScopePrototype(Heap* heap, Prototype* outer):
		Prototype(heap),
		outer(outer)
	{}
	
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

struct Scope: Value
{
	typedef std::vector<Value*> Locals;
	
	Value* outer;
	Locals locals;
	
	Scope(Heap* heap, ScopePrototype* prototype, Value* outer):
		Value(heap, prototype),
		outer(outer)
	{}
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		using namespace std;
		using namespace __gnu_cxx;
		std::ostream* s = &stream;
		for(Locals::const_iterator it = locals.begin(); it != locals.end(); ++it)
		{
			const Value* local = *it;
			local->dump(stream);
		}
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		using namespace __gnu_cxx;
		for_each(locals.begin(), locals.end(), mem_fun(&Value::markForGC));
	}
	
	ScopePrototype* def()
	{
		return static_cast<ScopePrototype*>(prototype);
	}
};

struct Function: Value
{
	struct FunctionPrototype: Prototype
	{
		FunctionPrototype();
	};
	static FunctionPrototype functionPrototype;
	
	Value* receiver;
	ScopePrototype* method;
	
	Function(Heap* heap, Value* receiver, ScopePrototype* method):
		Value(heap, &functionPrototype),
		receiver(receiver),
		method(method)
	{
		assert(method->outer == receiver->prototype);
	}
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


struct Array: Value
{
	struct ArrayPrototype: Prototype
	{
		ArrayPrototype();
	};
	static ArrayPrototype arrayPrototype;
	
	typedef std::vector<Value*> Values;
	
	Values values;
	
	Array(Heap* heap):
		Value(heap, &arrayPrototype)
	{ }
	
	virtual void dumpSpecific(std::ostream& stream) const
	{
		stream << values.size() << " values";
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		for_each(values.begin(), values.end(), mem_fun(&Value::markForGC));
	}
};

#endif // ndef TYPES_H
