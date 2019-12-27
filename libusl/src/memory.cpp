#include "memory.h"
#include "interpreter.h"
#include "types.h"

using namespace std;

void Heap::collectGarbage()
{
	// filter copy, delete unrefs
	Values marked;
	for (size_t i = 0; i < values.size(); i++)
	{
		if (values[i]->marked)
			marked.push_back(values[i]);
		else
			delete values[i];
	}

	// clean heap
	swap(values, marked);
	for_each(values.begin(), values.end(), mem_fun(&Value::clearGCMark));
}
