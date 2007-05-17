#ifndef TYPES_H
#define TYPES_H

#include "memory.h"

#include <map>
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

struct Method;
struct Prototype: Value
{
	typedef std::map<std::string, Method*> Methods;
	
	Prototype* parent;
	Methods methods;
	
	Prototype(Heap* heap, Prototype* parent):
		Value(heap, 0), parent(parent)
	{ // TODO: MetaPrototype
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		using namespace __gnu_cxx;
		if (parent != 0)
			parent->markForGC();
		for_each(methods.begin(), methods.end(), compose1(mem_fun(&Value::markForGC), select2nd<map<string, Value*>::value_type>()));
	}
	
	virtual Method* lookup(const std::string &method) const
	{
		return methods.find(method)->second;
	}
};
extern Prototype Nil;

struct Thread;
struct Method: Prototype
{
	typedef std::vector<std::string> Args;
	
	Args args;
	
	Method(Heap* heap, Prototype* parent):
		Prototype(heap, parent)
	{
		methods["."] = this;
	}
	
	virtual void execute(Thread* thread) = 0;
};

struct Code;
struct UserMethod: Method
{
	typedef std::vector<Code*> Body;
	
	Body body;
	
	UserMethod(Heap* heap, Prototype* parent):
		Method(heap, parent)
	{
	}
	
	void execute(Thread* thread);
};

struct Scope: Value
{
	typedef std::map<std::string, Value*> Locals;
	
	Scope* parent;
	Locals locals;
	
	Scope(Heap* heap, UserMethod* method, Scope* parent):
		Value(heap, method),
		parent(parent)
	{
		locals["."] = this;
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		using namespace __gnu_cxx;
		for_each(locals.begin(), locals.end(), compose1(mem_fun(&Value::markForGC), select2nd<map<string, Value*>::value_type>()));
	}
	
	Value* lookup(const std::string& name) const
	{
		return locals.find(name)->second;
	}
	
	UserMethod* method()
	{
		return static_cast<UserMethod*>(prototype);
	}
};

#endif // ndef TYPES_H
