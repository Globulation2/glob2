#ifndef TYPES_H
#define TYPES_H

#include <cassert>
#include <map>
#include <string>
#include <vector>

struct Prototype;
struct Value
{
	Prototype* proto;
	
	Value(Prototype* proto):
		proto(proto)
	{ }
	
	virtual ~Value() { }
};

struct Method;
struct Prototype: Value
{
	typedef std::map<std::string, Method*> Methods;
	
	Prototype* parent;
	Methods methods;
	
	Prototype(Prototype* parent):
		Value(0), parent(parent)
	{ // TODO: MetaPrototype
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
	
	Method(Prototype* parent):
		Prototype(parent)
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
	
	Scope(Method* method, Scope* parent):
		Value(method),
		parent(parent)
	{
		locals["."] = this;
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
