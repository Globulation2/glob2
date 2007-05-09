#ifndef TREE_H
#define TREE_H

#include "types.h"

struct Code;
typedef std::vector<Code*> CodeVector;

struct Node
{
	virtual ~Node() {}
	virtual void generate(CodeVector* code) = 0;
};

struct ConstNode: Node
{
	ConstNode(Value* value):
		value(value)
	{}
	
	void generate(CodeVector* code);
	
	Value* value;
};

struct LocalNode: Node
{
	LocalNode(const std::string& local):
		local(local)
	{}
	
	void generate(CodeVector* code);
	
	std::string local;
};

struct ApplyNode: Node
{
	ApplyNode(Node* receiver, const std::string& method):
		receiver(receiver),
		method(method)
	{}
	
	void generate(CodeVector* code);
	
	Node *receiver;
	const std::string method;
	std::vector<Node*> args;
};

#endif // ndef TREE_H
