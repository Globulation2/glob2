#ifndef NATIVE_H
#define NATIVE_H

#include "usl.h"
#include "interpreter.h"
#include "code.h"
#include "types.h"

#include <boost/function.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/mpl/assert.hpp>

template<typename Function>
struct NativeFunction: NativeCode
{
	typedef boost::function<Function> BoostFunction;
	BoostFunction function;

	NativeFunction(const std::string& name, const BoostFunction& function, bool receiver = false);
	
	void prologue(ThunkPrototype* thunk);
	void execute(Thread* thread);
	
	bool receiver;
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
	/// specialize this method to add members to a native value prototype
	void initialize()
	{}
	
	template<typename Function>
	void addMethod(const std::string& name, const boost::function<Function>& function)
	{
		BOOST_MPL_ASSERT(( boost::is_same<This, typename boost::function<Function>::arg1_type> ));
		NativeCode* native = new NativeFunction<Function>(name, function, true);
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
inline const T& pop(Thread* thread)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	Value* value = stack.back();
	stack.pop_back();
	return unbox<T>(thread, value);
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

template<typename T>
inline void push(Thread* thread, const T& t)
{
	Thread::Frame::Stack& stack = thread->frames.back().stack;
	Value* value = box(thread, t);
	stack.push_back(value);
}


template<typename Result>
void execute(const boost::function<Result(void)>& function, Thread* thread)
{
	const Result& result = function();
	push(thread, result);
}

inline void execute(const boost::function<void(void)>& function, Thread* thread)
{
	function();
	push(thread, &nil);
}


template<typename Result, typename Argument>
void execute(const boost::function<Result(Argument)>& function, Thread* thread)
{
	const Argument& argument = pop<Argument>(thread);
	const Result& result = function(argument);
	push(thread, result);
}

template<typename Argument>
void execute(const boost::function<void(Argument)>& function, Thread* thread)
{
	const Argument& argument = pop<Argument>(thread);
	function(argument);
	push(thread, &nil);
}


template<typename Result, typename Argument1, typename Argument2>
void execute(const boost::function<Result(Argument1, Argument2)>& function, Thread* thread)
{
	const Argument1& argument1 = pop<Argument1>(thread);
	const Argument2& argument2 = pop<Argument2>(thread);
	const Result& result = function(argument1, argument2);
	push(thread, result);
}

template<typename Argument1, typename Argument2>
void execute(const boost::function<void(Argument1, Argument2)>& function, Thread* thread)
{
	const Argument1& argument1 = pop<Argument1>(thread);
	const Argument2& argument2 = pop<Argument2>(thread);
	function(argument1, argument2);
	push(thread, &nil);
}


template<typename Result, typename Argument1, typename Argument2, typename Argument3>
void execute(const boost::function<Result(Argument1, Argument2, Argument3)>& function, Thread* thread)
{
	const Argument1& argument1 = pop<Argument1>(thread);
	const Argument2& argument2 = pop<Argument2>(thread);
	const Argument3& argument3 = pop<Argument3>(thread);
	const Result& result = function(argument1, argument2, argument3);
	push(thread, result);
}

template<typename Argument1, typename Argument2, typename Argument3>
void execute(const boost::function<void(Argument1, Argument2, Argument3)>& function, Thread* thread)
{
	const Argument1& argument1 = pop<Argument1>(thread);
	const Argument2& argument2 = pop<Argument2>(thread);
	const Argument3& argument3 = pop<Argument3>(thread);
	function(argument1, argument2, argument3);
	push(thread, &nil);
}




template<typename Function>
NativeFunction<Function>::NativeFunction(const std::string& name, const BoostFunction& function, bool receiver):
	NativeCode(name),
	function(function),
	receiver(receiver)
{}

template<typename Function>
void NativeFunction<Function>::prologue(ThunkPrototype* thunk)
{
	size_t arguments = BoostFunction::arity;
	if (receiver)
		--arguments;
	
	if (arguments > 0)
	{
		thunk->body.push_back(new EvalCode()); // evaluate the argument
		if (arguments > 1)
		{
			assert(false); // TODO
		}
	}
	else
	{
		thunk->body.push_back(new PopCode()); // dump the argument
	}
	
	if (receiver)
	{
		thunk->body.push_back(new ThunkCode()); // get the current thunk
		thunk->body.push_back(new ParentCode()); // get the parent value
	}
}

template<typename Function>
void NativeFunction<Function>::execute(Thread* thread)
{
	::execute(function, thread);
}


typedef NativeValue<int> Integer;
typedef NativeValue<std::string> String;


template<>
inline void NativeValuePrototype<int>::initialize()
{
	addMethod<int (int     )>("_-",                   -  boost::lambda::_1);
	addMethod<int (int, int)>("+" , boost::lambda::_1 +  boost::lambda::_2);
	addMethod<int (int, int)>("-" , boost::lambda::_1 -  boost::lambda::_2);
	addMethod<int (int, int)>("*" , boost::lambda::_1 *  boost::lambda::_2);
	addMethod<bool(int, int)>("<" , boost::lambda::_1 <  boost::lambda::_2);
	addMethod<bool(int, int)>(">" , boost::lambda::_1 >  boost::lambda::_2);
	addMethod<bool(int, int)>("<=", boost::lambda::_1 <= boost::lambda::_2);
	addMethod<bool(int, int)>(">=", boost::lambda::_1 >= boost::lambda::_2);
	addMethod<bool(int, int)>("=" , boost::lambda::_1 == boost::lambda::_2);
	addMethod<bool(int, int)>("!=", boost::lambda::_1 != boost::lambda::_2);
}

#endif // ndef NATIVE_H
