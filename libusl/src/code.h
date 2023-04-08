#ifndef CODE_H
#define CODE_H

#include <cassert>
#include <ostream>

struct Thread;
struct Value;
struct Prototype;
struct ThunkPrototype;
struct ScopePrototype;

ThunkPrototype* thisMember(Prototype* outer);
ThunkPrototype* methodMember(ScopePrototype* method);

struct Code
{
	virtual ~Code() { }
	virtual void execute(Thread* thread) = 0;
	void dump(std::ostream &stream) const;
	virtual void dumpSpecific(std::ostream &stream) const {};
};

struct ConstCode: Code
{
	ConstCode(Value* value);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	Value* value;
};

struct ValRefCode: Code
{
	ValRefCode(size_t index);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	size_t index;
};

struct EvalCode: Code
{
	virtual void execute(Thread* thread);
};

struct SelectCode: Code
{
	SelectCode(const std::string& name);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	std::string name;
};

struct ApplyCode: Code
{
	virtual void execute(Thread* thread);
};

struct ValCode: Code
{
	ValCode(size_t index);

	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	size_t index;
};

struct ParentCode: Code
{
	virtual void execute(Thread* thread);
};

struct PopCode: Code
{
	virtual void execute(Thread* thread);
};

struct DupCode: Code
{
	DupCode(size_t index);

	virtual void execute(Thread* thread);
	
	size_t index;
};

struct ThunkCode: Code
{
	virtual void execute(Thread* thread);
};

struct NativeCode: Code
{
	NativeCode(const std::string& name);
	
	virtual void prologue(ThunkPrototype* thunk) {}
	virtual void epilogue(ThunkPrototype* thunk) {}
	virtual void dumpSpecific(std::ostream &stream) const;
	
	std::string name;
};

template<typename ThunkType>
struct CreateCode: Code
{
	CreateCode(typename ThunkType::Prototype* prototype);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	typename ThunkType::Prototype* prototype;
};

#endif // ndef CODE_H
