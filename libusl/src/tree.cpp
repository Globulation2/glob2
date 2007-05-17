#include "tree.h"
#include "code.h"

void LazyNode::generate(UserMethod* method)
{
	value->generate(this->method);
	this->method->body.push_back(new ReturnCode());
	method->body.push_back(new ScopeCode(this->method));
}

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

void ValNode::generate(UserMethod* method)
{
	value->generate(method);
	method->body.push_back(new ValueCode(local));
}
