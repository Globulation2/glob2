#ifndef TREE_H
#define TREE_H

#include "types.h"

struct Code;

struct Node
{
	virtual ~Node() {}
	virtual void generate(UserMethod* method) = 0;
};

struct ExpressionNode: Node
{};

struct ConstNode: ExpressionNode
{
	ConstNode(Value* value):
		value(value)
	{}
	
	void generate(UserMethod* method);
	
	Value* value;
};

struct LocalNode: ExpressionNode
{
	LocalNode(const std::string& local):
		local(local)
	{}
	
	void generate(UserMethod* method);
	
	std::string local;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(Node* receiver, const std::string& name):
		receiver(receiver),
		name(name)
	{}
	
	void generate(UserMethod* method);
	
	Node* receiver;
	const std::string name;
	std::vector<Node*> args;
};

struct ValueNode: Node
{
	ValueNode(const std::string& local, Node* value):
		local(local),
		value(value)
	{}
	
	void generate(UserMethod* method);
	
	const std::string local;
	Node* value;
};

#endif // ndef TREE_H
