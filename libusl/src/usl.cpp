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

Value* run(Thread& thread, int& instCount)
{
	while (true)
	{
		Thread::Frame& frame = thread.frames.back();
		ThunkPrototype* thunk = frame.thunk->thunkPrototype();
		size_t nextInstr = frame.nextInstr;
		Code* code = thunk->body[nextInstr];
		frame.nextInstr++;
		
		cout << thunk;
		for (size_t i = 0; i < thread.frames.size(); ++i)
			cout << "[" << thread.frames[i].stack.size() << "]";
		cout << " " << thread.debugInfo->find(thunk, nextInstr) << ": ";
		code->dump(cout);
		cout << endl;
		
		code->execute(&thread);
		instCount++;
		
		while (true)
		{
			Thread::Frame& frame = thread.frames.back();
			if (frame.nextInstr < frame.thunk->thunkPrototype()->body.size())
				break;
			Value* retVal = frame.stack.back();
			thread.frames.pop_back();
			if (!thread.frames.empty())
			{
				thread.frames.back().stack.push_back(retVal);
			}
			else
			{
				return retVal;
			}
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
			block.generateMembers(code, &debug, &heap);
		}
		catch(Exception& e)
		{
			cout << e.position << ":" << e.what() << endl;
			return -1;
		}
	}
	
	cout << '\n';
	dumpCode(&heap, &debug, cout);
	
	Scope* root = new Scope(&heap, code, 0);
	Thread thread(&heap, &debug, root);
	thread.frames.push_back(Thread::Frame(root));
	
	int instCount = 0;
	Value* result;
	try
	{
		result = run(thread, instCount);
	}
	catch(Exception& e)
	{
		cout << e.position << ":" << e.what() << endl;
		return -1;
	}

	cout << "\n\n* result:\n";
	result->dump(cout);
	cout << endl;
	
	cout << "\n* stats:\n";
	cout << "heap size: " << heap.values.size() << "\tinst count: " << instCount << "\n";
	heap.garbageCollect(&thread);
	//cerr << "heap size: " << heap.values.size() << "\n";
}
