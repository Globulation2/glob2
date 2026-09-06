#pragma once

#include <stack>
#include <vector>
#include <string>

struct Thunk;
struct Value;
struct Usl;

struct Thread
{
	struct Frame
	{
		typedef std::vector<Value*> Stack;
	
		Thunk* thunk;
		Stack stack;
		size_t nextInstr;
		
		Frame(Thunk* thunk):
			thunk(thunk)
		{
			nextInstr = 0;
		}
		
		void markForGC();
	};
	
	enum State {
		RUN,
		YIELD,
		STOP
	};
	
	typedef std::vector<Frame> Frames;
	
	Usl* usl;
	State state;
	Frames frames;
	
	Thread(Usl* usl, Thunk* thunk):
		usl(usl), state(RUN)
	{
		frames.push_back(thunk);
	}
	
	size_t run();
	size_t run(size_t steps);
	bool step();
	
	void markForGC();
};

