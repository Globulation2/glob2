#ifndef TYPES_H
#define TYPES_H

#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <ext/functional>
#include "interpreter.h"

struct Prototype;

struct Value
{
	Prototype* proto;
	bool marked;
	
	Value(Thread *thread, Prototype* proto):
		proto(proto)
	{
		marked = false;
		if (thread)
			thread->heap.push_back(this);
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

struct Method;
struct Prototype: Value
{
	typedef std::map<std::string, Method*> Methods;
	
	Prototype* parent;
	Methods methods;
	
	Prototype(Thread *thread, Prototype* parent):
		Value(thread, 0), parent(parent)
	{ // TODO: MetaPrototype
	}
	
	virtual void propagateMarkForGC()
	{
		using namespace std;
		using namespace __gnu_cxx;
		for_each(methods.begin(), methods.end(), compose1(mem_fun(&Value::markForGC), select2nd<map<string, Value*>::value_type>()));
	}
	
	Method* lookup(const std::string &method) const
	{
		Methods::const_iterator methodIt = methods.find(method);
		if (methodIt != methods.end())
		{
			return methodIt->second;
		}
		else if (parent != 0)
		{
			return parent->lookup(method);
		}
		else
		{
			assert(false); // TODO
			//throw UnknownMethodException(method);
		}
	}
};

struct Thread;
struct Method: Prototype
{
	typedef std::vector<std::string> Args;
	
	Args args;
	
	Method(Thread *thread, Prototype* parent):
		Prototype(thread, parent)
	{
		methods["."] = this;
	}
	
	virtual void execute(Thread* stack) = 0;
};

struct Scope: Value
{
	typedef std::map<std::string, Value*> Locals;
	
	Scope* parent;
	Locals locals;
	
	Scope(Thread *thread, Method* method, Scope* parent):
		Value(thread, method),
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
		Locals::const_iterator localIt = locals.find(name);
		if (localIt != locals.end())
		{
			return localIt->second;
		}
		else if (parent != 0)
		{
			return parent->lookup(name);
		}
		else
		{
			return 0;
		}
	}

};

#endif // ndef TYPES_H
