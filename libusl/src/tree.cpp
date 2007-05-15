#include "tree.h"
#include "code.h"

void ConstNode::generate(UserMethod* method)
{
	method->body.push_back(new ConstCode(value));
}

void LocalNode::generate(UserMethod* method)
{
	method->body.push_back(new LocalCode(depth, local));
}

void ApplyNode::generate(UserMethod* method)
{
	receiver->generate(method);
	for (size_t i = 0; i < args.size(); i++)
		args[i]->generate(method);
	method->body.push_back(new ApplyCode(name, args.size()));
}

void ValueNode::generate(UserMethod* method)
{
	value->generate(method);
	method->body.push_back(new ValueCode(local));
}
