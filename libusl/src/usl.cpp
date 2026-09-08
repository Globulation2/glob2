#include "usl.h"
#include "code.h"
#include "parser.h"
#include "position.h"
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

	// Remove prototype and root as they will be automatically deleted anyway.
	for (auto it = heap.values.begin(); it != heap.values.end();)
	{
		if (*it == prototype.get() || *it == root.get())
			it = heap.values.erase(it);
		else
			it++;
	}
}

Usl::~Usl() {
	collectGarbage();
}

void Usl::markGarbage()
{
	root->markForGC();
	for (auto& thread : threads) thread.markForGC();
}

void Usl::collectGarbage()
{
	// mark
	markGarbage();

	// sweep
	heap.collectGarbage();
	// These two roots are owned outside heap.values, so the heap sweep does not
	// clear their marks. Without this, later collections skip their children and
	// reclaim live bridge constants (including gui) and runtime definitions.
	root->clearGCMark();
	prototype->clearGCMark();
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
	CodeGen cg{&debug, &heap};
	block.generateMembers(prototype, cg);
	
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
	collectGarbage();
	return total;
}
