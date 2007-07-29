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

void Node::dump(std::ostream &stream, unsigned indent) const
{
	for (unsigned i = 0; i < indent; ++i)
		stream << '\t';
	stream << position.line << ":" << position.column << ": " << typeid(*this).name();
	dumpSpecific(stream, indent);
}

void Node::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
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

void ParentNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	scope->dump(stream, indent + 1);
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

void DefRefNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	value->dump(stream, indent + 1);
}


void ConstNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ConstCode(value));
}

void ConstNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << ' ';
	value->dump(stream);
	stream << '\n';
}


void ValRefNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	this->scope->generate(scope, debug);
	Node::generate(scope, debug, new ValRefCode(index));
}

void ValRefNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << " " << index;
	stream << '\n';
}


EvalNode::~EvalNode()
{
	delete thunk;
}

void EvalNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	thunk->generate(scope, debug);
	Node::generate(scope, debug, new EvalCode());
}

void EvalNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	thunk->dump(stream, indent + 1);
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

void SelectNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << " " << name;
	stream << '\n';
	receiver->dump(stream, indent + 1);
}


ApplyNode::~ApplyNode()
{
	delete function;
	delete argument;
}

void ApplyNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	function->generate(scope, debug);
	argument->generate(scope, debug);
	Node::generate(scope, debug, new ApplyCode());
}

void ApplyNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	function->dump(stream, indent + 1);
	argument->dump(stream, indent + 1);
}


ValNode::~ValNode()
{
	delete value;
}

void ValNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	value->generate(scope, debug);
	Node::generate(scope, debug, new ValCode());
	
	Node::generate(getter, debug, new ScopeCode());
	Node::generate(getter, debug, new ParentCode());
	Node::generate(getter, debug, new ValRefCode(index));
	Node::generate(getter, debug, new ReturnCode());
}

void ValNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	value->dump(stream, indent + 1);
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

void BlockNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
	{
		(*it)->dump(stream, indent + 1);
	}
	value->dump(stream, indent + 1);
}


DefNode::~DefNode()
{
	delete body;
}

void DefNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	body->generate(this->scope, debug);
	Node::generate(this->scope, debug, new ReturnCode());
}

void DefNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	body->dump(stream, indent + 1);
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

void ArrayNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		(*it)->dump(stream, indent + 1);
	}
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
	Node::generate(scope, debug, new EvalCode());
}

void DefLookupNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
}


void IgnorePatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new PopCode());
}


void NilPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	// TODO: check we really got nil
	Node::generate(scope, debug, new PopCode());
}


void ValPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new EvalCode());
	Node::generate(scope, debug, new ValCode());
	
	if (getter)
	{
		size_t index = find(scope->locals.begin(), scope->locals.end(), name) - scope->locals.begin();
		assert(index < scope->locals.size());
		
		Node::generate(getter, debug, new ScopeCode());
		Node::generate(getter, debug, new ParentCode());
		Node::generate(getter, debug, new ValRefCode(index));
		Node::generate(getter, debug, new ReturnCode());
	}
}

void ValPatternNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
}


void DefPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ValCode());
	
	size_t index = find(scope->locals.begin(), scope->locals.end(), name) - scope->locals.begin();
	assert(index < scope->locals.size());

	Node::generate(getter, debug, new ScopeCode());
	Node::generate(getter, debug, new ParentCode());
	Node::generate(getter, debug, new ValRefCode(index));
	Node::generate(getter, debug, new EvalCode());
	Node::generate(getter, debug, new ReturnCode());
}

void DefPatternNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
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
	Node::generate(scope, debug, new EvalCode());
	Node::generate(scope, debug, new SelectCode("get"));
	int index = 0;
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		ScopePrototype* arg = new ScopePrototype(0, scope); // TODO: heap-alloc
		Node::generate(arg, debug, new ConstCode(new Integer(0, index))); // TODO: heap-alloc
		Node::generate(arg, debug, new ReturnCode());
		
		Node::generate(scope, debug, new DupCode());
		Node::generate(scope, debug, new ScopeCode());
		Node::generate(scope, debug, new DefRefCode(arg));
		Node::generate(scope, debug, new ApplyCode());
		(*it)->generate(scope, debug);
		++index;
	}
	Node::generate(scope, debug, new PopCode());
}

void TuplePatternNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	for (Members::const_iterator it = members.begin(); it != members.end(); ++it)
	{
		(*it)->dump(stream, indent + 1);
	}
}


FunNode::~FunNode()
{
	delete body;
}

void FunNode::generate(ScopePrototype* scope, FileDebugInfo* debug)
{
	Node::generate(scope, debug, new ScopeCode());
	Node::generate(scope, debug, new FunCode(method));
	arg->generate(method, debug);
	body->generate(method, debug);
	Node::generate(method, debug, new ReturnCode());
}

void FunNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	body->dump(stream, indent + 1);
}
