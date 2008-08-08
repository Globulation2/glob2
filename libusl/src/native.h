#ifndef NATIVE_H
#define NATIVE_H

#include "usl.h"
#include "interpreter.h"
#include "code.h"
#include "types.h"

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lambda/lambda.hpp>

template<typename Result, typename Argument>
struct NativeFunction: NativeCode
{
	typedef boost::function<Result (Argument)> Function;
	Function function;

	NativeFunction(const std::string& name, const Function& function);
	
	void execute(Thread* thread);
};

template<typename Result, typename Receiver, typename Argument>
struct NativeMethod: NativeCode
{
	typedef boost::function<Result (Receiver, Argument)> Function;
	Function function;

	NativeMethod(const std::string& name, const Function& function);
	
	void execute(Thread* thread);
};

template<typename This>
struct NativeValuePrototype: Prototype
{
	NativeValuePrototype():
		Prototype(static_cast<Heap*>(0))
	{
		initialize();
	}
	
private:
	void initialize()
	{}
	
	template<typename Result, typename Argument>
	void addMethod(const std::string& name, boost::function<Result (This, Argument)> function)
	{
		NativeCode* native = new NativeMethod<Result, This, Argument>(name, function);
		Prototype::addMethod(native);
	}
};

template<typename This>
struct NativeValue: Value
{
	static NativeValuePrototype<This> prototype;
	
	NativeValue(Heap* heap, const This& value):
		Value(heap, &prototype),
		value(value)
	{}
	
	const This value;
	
	operator const This&() { return value; }
	
	void dumpSpecific(std::ostream &stream) const
	{
		stream << "= " << value;
	}
};
template<typename This> NativeValuePrototype<This> NativeValue<This>::prototype;


template<typename T>
inline const T& unbox(Thread* thread, Value*& value)
{
	NativeValue<T>* native = dynamic_cast<NativeValue<T>*>(value);
	if (native == 0)
		assert(false); // TODO: throw Exception
	return native->value;
}

template<>
inline Value*const& unbox<Value*>(Thread* thread, Value*& value) // ugly reference to constant pointer
{
	return value;
}


template<typename T>
inline Value* box(Thread* thread, const T& t)
{
	return new NativeValue<T>(&thread->usl->heap, t);
}

template<>
inline Value* box<Value*>(Thread* thread, Value*const& value) // ugly reference to constant pointer
{
	return value;
}

template<>
inline Value* box<bool>(Thread* thread, const bool& value)
{
	std::string name = value ? "true" : "false";
	return thread->usl->runtimeValues[name];
}


template<typename Result, typename Argument>
Value* execute(const boost::function<Result(Argument)>& function, Thread* thread, Value* value)
{
	const Argument& argument = unbox<Argument>(thread, value);
	Result result = function(argument);
	return box(thread, result);
}

template<typename Argument>
Value* execute(const boost::function<void(Argument)>& function, Thread* thread, Value* value)
{
	const Argument& argument = unbox<Argument>(thread, value);
	function(argument);
	return thread->usl->runtimeValues["nil"];
}


template<typename Result, typename Argument1, typename Argument2>
Value* execute(const boost::function<Result(Argument1, Argument2)>& function, Thread* thread, Value* value1, Value* value2)
{
	const Argument1& argument1 = unbox<Argument1>(thread, value1);
	const Argument2& argument2 = unbox<Argument2>(thread, value2);
	Result result = function(argument1, argument2);
	return box(thread, result);
}

template<typename Argument1, typename Argument2>
Value* execute(const boost::function<void(Argument1, Argument2)>& function, Thread* thread, Value* value1, Value* value2)
{
	const Argument1& argument1 = unbox<Argument1>(thread, value1);
	const Argument2& argument2 = unbox<Argument2>(thread, value2);
	function(argument1, argument2);
	return thread->usl->runtimeValues["nil"];
}


template<typename Result, typename Argument>
NativeFunction<Result, Argument>::NativeFunction(const std::string& name, const Function& function):
	NativeCode(name),
	function(function)
{}

template<typename Result, typename Argument>
void NativeFunction<Result, Argument>::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	stack.pop_back(); // ignore the receiver
	Value* argument = stack.back();
	stack.pop_back();
	Value* resultValue = ::execute(function, thread, argument);
	stack.push_back(resultValue);
}


template<typename Result, typename Receiver, typename Argument>
NativeMethod<Result, Receiver, Argument>::NativeMethod(const std::string& name, const Function& function):
	NativeCode(name),
	function(function)
{}

template<typename Result, typename Receiver, typename Argument>
void NativeMethod<Result, Receiver, Argument>::execute(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	Value* receiver = stack.back();
	stack.pop_back();
	Value* argument = stack.back();
	stack.pop_back();
	Value* resultValue = ::execute(function, thread, receiver, argument);
	stack.push_back(resultValue);
}


typedef NativeValue<int> Integer;
typedef NativeValue<std::string> String;

#endif // ndef NATIVE_H
