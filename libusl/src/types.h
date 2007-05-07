#ifndef TYPES_H
#define TYPES_H

#include <cassert>
#include <map>
#include <string>
#include <vector>

struct Thread;
struct Executable
{
	std::vector<std::string> args;
	virtual size_t execute(size_t programCounter, Thread* stack) = 0;
	virtual ~Executable() {}
};

struct Prototype;
struct Value
{
	Prototype* proto;
	
	Value(Prototype* proto):
		proto(proto)
	{ }
	
	virtual ~Value() { }
};

struct Prototype: Value
{
	typedef std::map<std::string, Executable*> Methods;
	
	Prototype* parent;
	Methods methods;
	
	Prototype(Prototype* parent):
		Value(0), parent(parent)
	{ // TODO: MetaPrototype
	}
	
	Executable *lookup(const std::string &method)
	{
		Methods::const_iterator methodIt = methods.find(method);
		if (methodIt == methods.end())
		{
			if (parent)
				return parent->lookup(method);
			else
				assert(false); // TODO
				//throw UnknownMethodException(method);
		}
		else
			return methodIt->second;
	}
};

struct Method: Prototype, Executable
{
	size_t address;
	
	Method(Prototype* parent):
		Prototype(parent)
	{ }
	
	
	size_t execute(size_t returnAddress, Thread* stack);
};

struct Scope: Value
{
	typedef std::map<std::string, Value*> Locals;
	
	Value* parent;
	Locals locals;
	
	Scope(Method* method, Value* parent):
		Value(method), parent(parent)
	{
	}
};

#endif // ndef TYPES_H
