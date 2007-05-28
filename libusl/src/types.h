#ifndef TYPES_H
#define TYPES_H

#include "memory.h"

#include <map>
#include <string>
#include <vector>
#include <ostream>
#include <iterator>
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

struct Definition;
struct Prototype: Value
{
	typedef std::map<std::string, Definition*> Methods;
	
	Prototype* parent;
	Methods methods;
	
	Prototype(Heap* heap, Prototype* parent):
		Value(heap, 0), parent(parent)
	{ // TODO: MetaPrototype
	}
	
	virtual void dumpSpecific(std::ostream &stream)
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
		if (parent != 0)
			parent->markForGC();
		for_each(methods.begin(), methods.end(), compose1(mem_fun(&Value::markForGC), select2nd<Methods::value_type>()));
	}
	
	virtual Definition* lookup(const std::string &method) const
	{
		return methods.find(method)->second;
	}
};

struct Code;
struct Definition: Prototype
{
	typedef std::vector<std::string> Locals;
	typedef std::vector<Code*> Body;
	
	Locals locals;
	Body body;
	
	Definition(Heap* heap, Prototype* parent):
		Prototype(heap, parent)
	{
		methods["."] = this;
	}
	
	virtual void dumpSpecific(std::ostream &stream)
	{
		stream << body.size() << " codes";
	}
};

struct Scope: Value
{
	typedef std::vector<Value*> Locals;
	
	Value* parent;
	Locals locals;
	
	Scope(Heap* heap, Definition* method, Value* parent):
		Value(heap, method),
		parent(parent)
	{}
	
	virtual void dumpSpecific(std::ostream& stream)
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
	
	Definition* def()
	{
		return static_cast<Definition*>(prototype);
	}
};

struct Tuple: Value
{
	typedef std::vector<Value*> Values;
	
	Values values;
	
	Tuple(Heap* heap):
		Value(heap, &tuplePrototype)
	{ }
	
	virtual void dumpSpecific(std::ostream &stream)
	{
		stream << values.size() << " values";
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		for_each(values.begin(), values.end(), mem_fun(&Value::markForGC));
	}
	
	struct TuplePrototype: Prototype
	{
		TuplePrototype();
	};
	static TuplePrototype tuplePrototype;
};

#endif // ndef TYPES_H
