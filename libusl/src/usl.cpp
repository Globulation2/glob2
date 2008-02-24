#include "parser.h"
#include "types.h"
#include "debug.h"
#include "code.h"
#include "interpreter.h"
#include <iostream>
#include <fstream>

using namespace std;

void dumpCode(ScopePrototype* scope, FileDebugInfo* debug, ostream& stream)
{
	stream << scope << '\n';
	for (size_t i = 0; i < scope->body.size(); ++i)
	{
		Position pos = debug->find(scope, i);
		stream << pos.line << ":" << pos.column << ": ";
		scope->body[i]->dump(stream);
		stream << '\n';
	}
	stream << '\n';
}

void dumpCode(Heap* heap, FileDebugInfo* debug, ostream& stream)
{
	for (Heap::Values::iterator values = heap->values.begin(); values != heap->values.end(); ++values)
	{
		ScopePrototype* scope = dynamic_cast<ScopePrototype*>(*values);
		if (scope != 0)
			dumpCode(scope, debug, stream);
	}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		cerr << "Wrong number of arguments" << endl;
		return 1;
	}
	
	string file = argv[1];
	
	ifstream ifs(file.c_str());
	if (!ifs.good())
	{
		cerr << "Can't open file " << file << endl;
		return 2;
	}
	
	cout << "Testing " << argv[1] << "\n\n";
	
	string source;
	while (true)
	{
		char c = ifs.get();
		if (ifs.eof() || !ifs.good())
			break;
		source += c;
	}
	ifs.close();
	
	cout << "* source:\n" << source << "\n";

	Heap heap;
	ScopePrototype* root = new ScopePrototype(&heap, 0);
	ProgramDebugInfo debug;
	
	Parser parser(source.c_str(), &heap);
	Node* node;
	try
	{
		node = parser.parse();
	}
	catch(Parser::Exception& e)
	{
		cout << "Parser error @" << e.token.position.line << ":" << e.token.position.column << ":" << endl;
		cout << "Found: " << e.token.type->desc << endl;
		cout << "Expected: " << e.what() << endl;
		return -1;
	}

	node->dump(cout);
	node->generate(root, debug.get(file), &heap);
	delete node;
	
	cout << '\n';
	dumpCode(&heap, debug.get(file), cout);
	
	Thread thread(&heap);
	thread.frames.push_back(Thread::Frame(new Scope(&heap, root, 0)));
	
	int instCount = 0;
	while (thread.frames.size() > 1 || thread.frames.front().nextInstr < root->body.size())
	{
		Thread::Frame& frame = thread.frames.back();
		ScopePrototype* scope = frame.scope->def();
		
		for (size_t i = 0; i < thread.frames.size(); ++i)
			cout << "[" << thread.frames[i].scope->locals.size() << "," << thread.frames[i].stack.size() << "]";
		
		FilePosition position = debug.find(scope, frame.nextInstr);
		cout << " " << position.file << ":" << position.position.line << ":" << position.position.column << ": ";
		
		Code* code = scope->body[frame.nextInstr++];
		code->dump(cout);
		cout << endl;
		code->execute(&thread);
		
		instCount++;
	}
	cout << "\n\n* result:\n";
	thread.frames.back().stack.back()->dump(cout);
	cout << endl;
	
	thread.frames.pop_back();
	
	cout << "\n* stats:\n";
	cout << "heap size: " << heap.values.size() << "\tinst count: " << instCount << "\n";
	heap.garbageCollect(&thread);
	//cerr << "heap size: " << heap.values.size() << "\n";
}
