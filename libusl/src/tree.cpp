#include "tree.h"
#include "code.h"
#include <memory>
#include <sstream>

using namespace std;


DefRefNode::~DefRefNode()
{
	delete value;
}

void DefRefNode::generate(ScopePrototype* scope)
{
	value->generate(this->scope);
	this->scope->body.push_back(new ReturnCode());
	scope->body.push_back(new ScopeCode());
	scope->body.push_back(new DefRefCode(this->scope));
}


void ConstNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ConstCode(value));
}


void ValRefNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ValRefCode(depth, index));
}


SelectNode::~SelectNode()
{
	delete receiver;
}

void SelectNode::generate(ScopePrototype* scope)
{
	receiver->generate(scope);
	scope->body.push_back(new SelectCode(name));
}


ApplyNode::~ApplyNode()
{
	delete receiver;
	delete argument;
}

void ApplyNode::generate(ScopePrototype* scope)
{
	receiver->generate(scope);
	if (argument)
		argument->generate(scope);
	scope->body.push_back(new ApplyCode(argument != 0));
}


ValNode::~ValNode()
{
	delete value;
}

void ValNode::generate(ScopePrototype* scope)
{
	value->generate(scope);
	scope->body.push_back(new ValCode());
}


BlockNode::~BlockNode()
{
	for (Statements::iterator it = statements.begin(); it != statements.end(); ++it)
	{
		delete *it;
	}
	delete value;
}

void BlockNode::generate(ScopePrototype* scope)
{
	
	for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
	{
		Node* statement = *it;
		statement->generate(scope);
		if (dynamic_cast<ExpressionNode*>(statement) != 0)
		{
			// if the statement is an expression, its result is ignored
			scope->body.push_back(new PopCode());
		}
	}
	value->generate(scope);
}


DefNode::~DefNode()
{
	delete body;
}

void DefNode::generate(ScopePrototype* scope)
{
	scope = this->scope;
	body->generate(scope);
	scope->body.push_back(new ReturnCode());
}


void ScopeNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ScopeCode());
}


ParentNode::~ParentNode()
{
	delete scope;
}

void ParentNode::generate(ScopePrototype* scope)
{
	this->scope->generate(scope);
	scope->body.push_back(new ParentCode());
}


ArrayNode::~ArrayNode()
{
	for (Elements::iterator it = elements.begin(); it != elements.end(); ++it)
	{
		delete *it;
	}
}

void ArrayNode::generate(ScopePrototype* scope)
{
	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		Node* element = *it;
		element->generate(scope);
	}
	scope->body.push_back(new ArrayCode(elements.size()));
}


void DefLookupNode::generate(ScopePrototype* scope)
{
	auto_ptr<ScopeNode> receiver(new ScopeNode());
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
		receiver.reset(new ParentNode(receiver.release()));
		prototype = s->outer;
		assert(prototype != 0); // TODO: throw a method not found exception
	}
	
	receiver->generate(scope);
	scope->body.push_back(new DefRefCode(method));
}


void NilPatternNode::generate(ScopePrototype* scope)
{
	// TODO
	scope->body.push_back(new PopCode());
}


void ValPatternNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ApplyCode(false));
	scope->body.push_back(new ValCode());
}


TuplePatternNode::~TuplePatternNode()
{
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		delete *it;
	}
}

void TuplePatternNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ApplyCode(false));
	int index = 0;
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		scope->body.push_back(new SelectCode("get", false));
		scope->body.push_back(new ConstCode(new Integer(0, index))); // TODO: heap-alloc
		scope->body.push_back(new SelectCode("this"));
		scope->body.push_back(new ApplyCode());
		(*it)->generate(scope);
		++index;
	}
}
