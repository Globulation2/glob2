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
	typedef std::map<std::string, ScopePrototype*> Members;
	
	Members members;
	
	Prototype(Heap* heap):
		Value(heap, 0)
	{ // TODO: MetaPrototype
	}
	
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
	
	virtual ScopePrototype* lookup(const std::string& name) const
	{
		Members::const_iterator method = members.find(name);
		if (method != members.end())
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

struct PatternNode;
struct Method: ScopePrototype
{
	Method(Heap* heap, Prototype* outer, PatternNode* argument);
};

struct NativeMethod: Method
{
	NativeMethod(Prototype* outer, const std::string& name, PatternNode* argument);
	std::string name;
	virtual Value* execute(Thread* thread, Value* receiver, Value* argument) = 0;
};

struct Thunk: Value
{
	Value* receiver;
	ScopePrototype* method;
	
	Thunk(Heap* heap, Value* receiver, ScopePrototype* method):
		Value(heap, 0),
		receiver(receiver),
		method(method)
	{}
	
protected:
	Thunk(Heap* heap, Prototype* prototype, Value* receiver, ScopePrototype* method):
		Value(heap, prototype),
		receiver(receiver),
		method(method)
	{
		assert(method->outer == receiver->prototype);
	}
};

struct Function: Thunk
{
	struct FunctionPrototype: Prototype
	{
		FunctionPrototype();
	};
	static FunctionPrototype functionPrototype;
	
	Function(Heap* heap, Value* receiver, ScopePrototype* method):
		Thunk(heap, &functionPrototype, receiver, method)
	{}
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
