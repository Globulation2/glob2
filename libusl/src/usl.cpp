#include "parser.h"
#include "types.h"
#include "debug.h"
#include "code.h"
#include "interpreter.h"
#include "error.h"
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
		Position pos = debug->find(i);
		stream << pos.line << ":" << pos.column << ": ";
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

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		cerr << "Wrong number of arguments" << endl;
		return 1;
	}
	
	Heap heap;
	ScopePrototype* code = new ScopePrototype(&heap, 0);
	DebugInfo debug;
	
	{
		ExecutionBlock block = ExecutionBlock(Position());
	
		for (int i = 1; i < argc; ++i)
		{
			string file = argv[i];
	
			ifstream ifs(file.c_str());
			if (!ifs.good())
			{
				cerr << "Can't open file " << file << endl;
				return 2;
			}
	
			cout << "Parsing " << argv[i] << "\n\n";
	
			string source;
			while (true)
			{
				char c = ifs.get();
				if (ifs.eof() || !ifs.good())
					break;
				source += c;
			}
			ifs.close();

			Parser parser(file, source.c_str(), &heap);
			try
			{
				parser.parse(&block);
			}
			catch(Exception& e)
			{
				cout << e.position << ":" << e.what() << endl;
				return -1;
			}
		}
	
		try
		{
			block.dump(cout);
			block.generate(code, &debug, &heap);
		}
		catch(Exception& e)
		{
			cout << e.position << ":" << e.what() << endl;
			return -1;
		}
	}
	
	cout << '\n';
	dumpCode(&heap, &debug, cout);
	
	Thread thread(&heap);
	thread.frames.push_back(Thread::Frame(new Scope(&heap, code, 0)));
	
	int instCount = 0;
	while (thread.frames.size() > 1 || thread.frames.front().nextInstr < code->body.size())
	{
		Thread::Frame& frame = thread.frames.back();
		ThunkPrototype* thunk = frame.thunk->thunkPrototype();
		cout << thunk;
		
		for (size_t i = 0; i < thread.frames.size(); ++i)
			cout << "[" << thread.frames[i].stack.size() << "]";
		
		Position position = debug.find(thunk, frame.nextInstr);
		cout << " " << position.filename << ":" << position.line << ":" << position.column << ": ";
		
		Code* code = thunk->body[frame.nextInstr++];
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
