#ifndef USL_H
#define USL_H

#include "memory.h"
#include "debug.h"

#include <iostream>

struct Thread;
struct Scope;

struct Usl
{
	Usl();
	virtual ~Usl() {}
	
	void includeScript(const std::string& name, std::istream& source);
	Thread* createThread(const std::string& name, std::istream& source);
	
	virtual std::ifstream* openFile(const std::string& name);
	
	typedef std::map<std::string, Value*> Cache;
	
	DebugInfo debug;
	Heap heap;
	Scope* root;
	Cache cache;
	std::vector<Thread> threads;
	
	void run();
	bool run(size_t& steps);
	
private:
	Scope* compile(const std::string& name, std::istream& source);
	friend struct FileLoad;
};

#endif // ndef USL_H
