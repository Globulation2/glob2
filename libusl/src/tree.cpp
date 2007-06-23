#include "tree.h"
#include "code.h"
#include "debug.h"
#include <memory>
#include <sstream>

using namespace std;


void Node::generate(ScopePrototype* scope, FileDebugInfo* debug, Code* code)
{
	scope->body.push_back(code);
	
	if (debug != 0)
	{
		size_t address = scope->body.size();
		ScopeDebugInfo* scopeDebug = debug->get(scope);
		ScopeDebugInfo::Source2Address::iterator it = scopeDebug->source2Address.find(position);
		if (it != scopeDebug->source2Address.end())
		{
			scopeDebug->address2Source.erase(it->second);
			it->second = address;
		}
		else
		{
			scopeDebug->source2Address[position] = address;
		}
		scopeDebug->address2Source[address] = position;
	}
}


DefRefNode::~DefRefNode()
{
	delete value;
}

void DefRefNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	value->generate(this->scope, debug);
	Node::generate(this->scope, debug, new ReturnCode());
	Node::generate(scope, debug, new ScopeCode());
	Node::generate(scope, debug, new DefRefCode(this->scope));
}


void ConstNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ConstCode(value));
}


void ValRefNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ValRefCode(depth, index));
}


SelectNode::~SelectNode()
{
	delete receiver;
}

void SelectNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	receiver->generate(scope, debug);
	Node::generate(scope, debug, new SelectCode(name));
}


ApplyNode::~ApplyNode()
{
	delete receiver;
	delete argument;
}

void ApplyNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	receiver->generate(scope, debug);
	if (argument)
		argument->generate(scope, debug);
	Node::generate(scope, debug, new ApplyCode(argument != 0));
}


ValNode::~ValNode()
{
	delete value;
}

void ValNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	value->generate(scope, debug);
	Node::generate(scope, debug, new ValCode());
}


BlockNode::~BlockNode()
{
	for (Statements::iterator it = statements.begin(); it != statements.end(); ++it)
	{
		delete *it;
	}
	delete value;
}

void BlockNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	
	for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
	{
		Node* statement = *it;
		statement->generate(scope, debug);
		if (dynamic_cast<ExpressionNode*>(statement) != 0)
		{
			// if the statement is an expression, its result is ignored
			Node::generate(scope, debug, new PopCode());
		}
	}
	value->generate(scope, debug);
}


DefNode::~DefNode()
{
	delete body;
}

void DefNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	scope = this->scope;
	body->generate(scope, debug);
	Node::generate(scope, debug, new ReturnCode());
}


void ScopeNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ScopeCode());
}


ParentNode::~ParentNode()
{
	delete scope;
}

void ParentNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	this->scope->generate(scope, debug);
	Node::generate(scope, debug, new ParentCode());
}


ArrayNode::~ArrayNode()
{
	for (Elements::iterator it = elements.begin(); it != elements.end(); ++it)
	{
		delete *it;
	}
}

void ArrayNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		Node* element = *it;
		element->generate(scope, debug);
	}
	Node::generate(scope, debug, new ArrayCode(elements.size()));
}


void DefLookupNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	auto_ptr<ScopeNode> receiver(new ScopeNode(position));
	ScopePrototype* method;
	
	// TODO: this should be done in a compiler pass between parsing and code generation
	Prototype* prototype = scope;
	while (true)
	{
		method = prototype->lookup(name);
		if (method != 0)
			break;
		
		ScopePrototype* s = dynamic_cast<ScopePrototype*>(prototype);
		assert(s != 0);
		receiver.reset(new ParentNode(position, receiver.release()));
		prototype = s->outer;
		assert(prototype != 0); // TODO: throw a method not found exception
	}
	
	receiver->generate(scope, debug);
	Node::generate(scope, debug, new DefRefCode(method));
}


void NilPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	// TODO: check we really got nil
	Node::generate(scope, debug, new PopCode());
}


void ValPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ApplyCode(false));
	Node::generate(scope, debug, new ValCode());
}


TuplePatternNode::~TuplePatternNode()
{
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		delete *it;
	}
}

void TuplePatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ApplyCode(false));
	int index = 0;
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		Node::generate(scope, debug, new SelectCode("get", false));
		Node::generate(scope, debug, new ConstCode(new Integer(0, index))); // TODO: heap-alloc
		Node::generate(scope, debug, new SelectCode("this"));
		Node::generate(scope, debug, new ApplyCode());
		(*it)->generate(scope, debug);
		++index;
	}
}
