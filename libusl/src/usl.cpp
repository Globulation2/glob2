#include "usl.h"
#include "code.h"
#include "parser.h"
#include "error.h"
#include "interpreter.h"
#include <iostream>
#include <fstream>

using namespace std;

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


struct FileLoad: NativeMethod
{
	FileLoad():
		NativeMethod(0, "File::load", new ValPatternNode(Position(), "filename"))
	{
		body.push_back(new EvalCode());
	}
	
	Value* execute(Thread* thread, Value* receiver, Value* argument)
	{
		String* string = dynamic_cast<String*>(argument);
		assert(string); // TODO: throw
		
		const std::string& filename = string->value;
		
		Usl* usl = thread->usl;
		
		Value* value = usl->cache[filename];
		if (value == 0)
		{
			auto_ptr<ifstream> stream(usl->openFile(filename));
			Scope* scope = usl->compile(filename, *stream);
			return scope;
		}
		else
		{
			return value;
		}
	}
} load;


Usl::Usl()
{
	ScopePrototype* prototype = new ScopePrototype(&heap, 0);
	prototype->members["load"] = nativeMethodMember(&load);
	
	root = new Scope(&heap, prototype, 0);
}

void Usl::includeScript(const std::string& name, std::istream& stream)
{
	Thread* thread = createThread(name, stream);
	Value* result = thread->run();
	delete thread;
	
	Scope* scope = dynamic_cast<Scope*>(result);
	assert(scope);
	
	ScopePrototype* rootPrototype = root->scopePrototype();
	size_t index = rootPrototype->locals.size();
	rootPrototype->locals.push_back(name);
	root->locals.push_back(scope);
	
	Prototype::Members& members = scope->prototype->members;
	for (Prototype::Members::const_iterator it = members.begin(); it != members.end(); ++it)
	{
		const string& name = it->first;
		ThunkPrototype* getter = new ThunkPrototype(&heap, root->prototype);
		getter->body.push_back(new ThunkCode());
		getter->body.push_back(new ParentCode());
		getter->body.push_back(new ValRefCode(index));
		getter->body.push_back(new SelectCode(name));
		getter->body.push_back(new EvalCode());
		root->scopePrototype()->members[name] = getter;
	}
}

Thread* Usl::createThread(const std::string& name, std::istream& stream)
{
	Scope* scope = compile(name, stream);
	threads.push_back(this);
	Thread* thread = &threads.back();
	thread->frames.push_back(Thread::Frame(scope));
	return thread;
}

Scope* Usl::compile(const std::string& name, std::istream& stream)
{
	string source;
	char c;
	while (stream.get(c))
		source += c;
	
	Parser parser(name, source.c_str(), &heap);
	cout << source << endl;
	
	ExecutionBlock block = ExecutionBlock(Position());
	parser.parse(&block);
	block.dump(cout);
	cout << endl;
	
	ScopePrototype* prototype = new ScopePrototype(&heap, root->prototype);
	block.generateMembers(prototype, &debug, &heap);
	
	Scope* scope = new Scope(&heap, prototype, root);
	return scope;
}

ifstream* Usl::openFile(const string& name)
{
	return new ifstream(name.c_str());
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
		Thread* thread = usl.createThread(name, stream);
		stream.close();
		
		size_t steps = 1000000;
		Value* result = thread->run(steps);
		cout << endl;
		result->dump(cout);
		cout << endl;
	}
	catch(Exception& e)
	{
		cout << e.position << ":" << e.what() << endl;
		return -1;
	}
	
	return 0;
}*/
