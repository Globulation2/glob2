#ifndef USL_H
#define USL_H

#include "memory.h"
#include "debug.h"

#include <istream>

struct Thread;
struct Scope;

struct Usl
{
	Usl();
	virtual ~Usl() {}
	
	void collectGarbage();
	void includeScript(const std::string& name, std::istream& source);
	void createThread(const std::string& name, std::istream& source);
	void addGlobal(const std::string& name, Value* value);
	Value* getGlobal(const std::string& name);
	
	virtual std::ifstream* openFile(const std::string& name);
	
	typedef std::map<std::string, Value*> LoadedScripts;
	typedef std::map<std::string, Value*> RuntimeValues;
	typedef std::vector<Thread> Threads;
	
	DebugInfo debug;
	Heap heap;
	Scope* root;
	LoadedScripts loadedScripts;
	RuntimeValues runtimeValues;
	Threads threads;
	
	/// Run one thread (round-robin over all threads) for a maximum of steps bytecodes executions
	size_t run(size_t steps);
	
private:
	Scope* compile(const std::string& name, std::istream& source);
	Thread* createThread(Scope* scope);
	friend struct Load;
};

#endif // ndef USL_H
