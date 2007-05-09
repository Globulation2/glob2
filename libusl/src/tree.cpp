#include "tree.h"
#include "code.h"

void ConstNode::generate(CodeVector* code)
{
	code->push_back(new ConstCode(value));
}

void LocalNode::generate(CodeVector* code)
{
	code->push_back(new LocalCode(local));
}

void ApplyNode::generate(CodeVector* code)
{
	receiver->generate(code);
	for (size_t i = 0; i < args.size(); i++)
		args[i]->generate(code);
	code->push_back(new ApplyCode(method, args.size()));
}
