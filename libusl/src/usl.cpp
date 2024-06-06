#include "usl.h"
#include "code.h"
#include "parser.h"
#include "error.h"
#include "interpreter.h"
#include "native.h"
#include <iostream>
#include <fstream>
#include <memory>

using std::ostream;
using std::ifstream;
using std::string;
using std::unique_ptr;
using std::cout;
using std::endl;

void dumpCode(ThunkPrototype* thunk, ThunkDebugInfo* debug, ostream& stream)
{
	stream << thunk << " ";
	thunk->dump(stream);
	stream << '\n';
	for (size_t i = 0; i < thunk->body.size(); ++i)
	{
		stream << debug->find(i) << ": ";
		thunk->body[i]->dump(stream);
		stream << '\n';
	}
	stream << '\n';
}

void dumpCode(Heap* heap, DebugInfo* debug, ostream& stream)
{
	for (Heap::Values::iterator values = heap->values.begin(); values != heap->values.end(); ++values)
	{
		ThunkPrototype* thunk = dynamic_cast<ThunkPrototype*>(*values);
		if (thunk != 0)
		{
			ThunkDebugInfo* thunkDebug = debug->get(thunk);
			dumpCode(thunk, thunkDebug, stream);
		}
	}
}


struct Load: NativeCode
{
	Load():
		NativeCode("load")
	{}
	
	void prologue(ThunkPrototype* thunk)
	{
		thunk->body.push_back(new EvalCode()); // evaluate the argument
	}

	void execute(Thread* thread)
	{
		Thread::Frame& frame = thread->frames.back();
		Thread::Frame::Stack& stack = frame.stack;
		
		Value* argument = stack.back();
		stack.pop_back();
	
		const string& filename = unbox<string>(thread, argument);
		
		Usl* usl = thread->usl;
		
		unique_ptr<ifstream> stream(usl->openFile(filename));
		Scope* scope = usl->compile(filename, *stream);
		if (frame.nextInstr >= frame.thunk->thunkPrototype()->body.size())
			thread->frames.pop_back(); // tail-call optimisation
		thread->frames.push_back(scope);
	}
};


struct Yield: NativeCode
{
	Yield():
		NativeCode("yield")
	{}
	
	void prologue(ThunkPrototype* thunk)
	{
		thunk->body.push_back(new PopCode()); // ignore the argument
	}
	
	void epilogue(ThunkPrototype* thunk)
	{
		thunk->body.push_back(new ConstCode(&nil)); // return nil
	}
	
	void execute(Thread* thread)
	{
		thread->state = Thread::YIELD;
	}
};

void print(Value* value)
{
	value->dump(cout);
	cout << endl;
}

Usl::Usl()
{
	prototype = std::make_unique<ScopePrototype>(&heap, nullptr);
	prototype->addMethod(new Load());
	prototype->addMethod(new Yield());
	prototype->addMethod(new NativeFunction<void(Value*)>("print", print));

	/*std::dynamic_pointer_cast<ScopePrototype>(rootPrototype).get()*/
	root = std::make_unique<Scope>(&heap, dynamic_cast<ScopePrototype*>(prototype.get()), nullptr);
}

Usl::~Usl() {}

/*Usl::~Usl()
{
	delete root->prototype;
	delete root;
}

void swap(Usl& first, Usl& second)
{
	using std::swap;
	swap(first.root, second.root);
}

Usl& Usl::operator=(Usl other)
{
	swap(*this, other);
	return *this;
}*/

void Usl::markGarbage() const
{
	root->markForGC();
//	for_each(threads.begin(), threads.end(), mem_fun_ref(&Thread::markForGC));
}

void Usl::collectGarbage()
{
	// mark
	markGarbage();

	// sweep
	heap.collectGarbage();
}

void Usl::includeScript(const std::string& name, std::istream& stream)
{
	Scope* scope = compile(name, stream);
	ScopePrototype* scopePrototype = scope->scopePrototype();
	Thread* thread = createThread(scope);
	thread->run();
	
	ScopePrototype* rootPrototype = root->scopePrototype();
	size_t index = rootPrototype->locals.size();
	rootPrototype->locals.push_back(name);
	root->locals.push_back(scope);
	
	for (Prototype::Members::const_iterator it = scopePrototype->members.begin(); it != scopePrototype->members.end(); ++it)
	{
		const string& name = it->first;
		ThunkPrototype* getter = new ThunkPrototype(&heap, rootPrototype);
		getter->body.push_back(new ThunkCode());
		getter->body.push_back(new ParentCode());
		getter->body.push_back(new ValRefCode(index));
		getter->body.push_back(new SelectCode(name));
		getter->body.push_back(new EvalCode());
		rootPrototype->members[name] = getter;
	}
	
	for (size_t i = 0; i < scopePrototype->locals.size(); ++i)
	{
		setConstant(scopePrototype->locals[i], scope->locals[i]);
	}
}

void Usl::createThread(const std::string& name, std::istream& stream)
{
	createThread(compile(name, stream));
}

Thread* Usl::createThread(Scope* scope)
{
	threads.push_back(Thread(this, scope));
	return &threads.back();
}

void Usl::setConstant(const std::string& name, Value* value)
{
	ScopePrototype* prototype = root->scopePrototype();
	size_t index = prototype->locals.size();

	prototype->locals.push_back(name);
	root->locals.push_back(value);

	ThunkPrototype*& getter = prototype->members[name];
	if (getter == 0)
	{
		getter = new ThunkPrototype(&heap, prototype);
		getter->body.push_back(new ThunkCode());
		getter->body.push_back(new ParentCode());
		getter->body.push_back(new ValRefCode(index));
	}
}

Value* Usl::getConstant(const std::string& name) const
{
	ScopePrototype::Locals& locals = root->scopePrototype()->locals;
	ScopePrototype::Locals::const_iterator it = find(locals.begin(), locals.end(), name);
	if (it == locals.end())
		return 0;
	size_t index = it - locals.begin();
	return root->locals[index];
}

Scope* Usl::compile(const std::string& name, std::istream& stream)
{
	string source;
	char c;
	while (stream.get(c))
		source += c;
	
	Parser parser(name, source.c_str(), &heap);
	#ifdef DEBUG_USL
		cout << source << endl;
	#endif
	
	ExecutionBlock block = ExecutionBlock(Position());
	parser.parse(&block);
	#ifdef DEBUG_USL
		block.dump(cout);
		cout << endl;
	#endif
	
	ScopePrototype* prototype = new ScopePrototype(&heap, root->prototype);
	block.generateMembers(prototype, &debug, &heap);
	
	Scope* scope = new Scope(&heap, prototype, root.get());
	return scope;
}

ifstream* Usl::openFile(const string& name)
{
	return new ifstream(name.c_str());
}

size_t Usl::run(size_t steps)
{
	size_t total = 0;
	
	for (Threads::iterator it = threads.begin(); it != threads.end(); ++it)
	{
		if (it->state == Thread::YIELD)
			it->state = Thread::RUN;
		total += it->run(steps);
	}
	
	// TODO: garbageCollect
	
	return total;
}

/*
int main(int argc, char** argv)
{
	if (argc < 2)
	{
		cerr << "Wrong number of arguments" << endl;
		return 1;
	}
	
	Usl usl;
	
	try
	{
		int i;
		ifstream stream;
		for (i = 1; i < argc - 1; ++i)
		{
			const char* name = argv[i];
			stream.open(name);
			usl.includeScript(name, stream);
			stream.close();
		}
		
		const char* name = argv[i];
		stream.open(name);
		usl.createThread(name, stream);
		stream.close();
		
		size_t steps = 1000000;
		usl.run(steps);
	}
	catch(Exception& e)
	{
		cout << e.position << ":" << e.what() << endl;
		return -1;
	}
	
	return 0;
}
*/
