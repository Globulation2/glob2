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


void ConstNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Node::generate(scope, debug, new ConstCode(value));
}

void ConstNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << ' ';
	value->dump(stream);
	stream << '\n';
}


EvalNode::~EvalNode()
{
	delete thunk;
}

void EvalNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	thunk->generate(scope, debug, heap);
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

void SelectNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	receiver->generate(scope, debug, heap);
	Node::generate(scope, debug, new SelectCode(name));
}

void SelectNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
	receiver->dump(stream, indent + 1);
}


ApplyNode::~ApplyNode()
{
	delete function;
	delete argument;
}

void ApplyNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	ScopePrototype* arg = new ScopePrototype(heap, scope);
	argument->generate(arg, debug, heap);
	Node::generate(arg, debug, new ReturnCode());
	
	function->generate(scope, debug, heap);
	Node::generate(scope, debug, new ScopeCode());
	Node::generate(scope, debug, new DefRefCode(arg));
	Node::generate(scope, debug, new ApplyCode());
}

void ApplyNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	function->dump(stream, indent + 1);
	argument->dump(stream, indent + 1);
}


DefNode::~DefNode()
{
	delete body;
}

void DefNode::define(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	ScopePrototype* def = new ScopePrototype(heap, scope);
	scope->members[name] = def;
}

void DefNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	ScopePrototype* def = scope->members[name];
	body->generate(def, debug, heap);
	Node::generate(def, debug, new ReturnCode());
}

void DefNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
	body->dump(stream, indent + 1);
}


ValNode::~ValNode()
{
	delete value;
}

void ValNode::define(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	DefNode::define(scope, debug, heap);
}

void ValNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	value->generate(scope, debug, heap);
	Node::generate(scope, debug, new ValCode());
	
	size_t index = scope->locals.size();
	scope->locals.push_back(name);
	
	ScopePrototype* getter = scope->members[name];
	Node::generate(getter, debug, new ScopeCode());
	Node::generate(getter, debug, new ParentCode());
	Node::generate(getter, debug, new ValRefCode(index));
	Node::generate(getter, debug, new ReturnCode());
}

void ValNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
	value->dump(stream, indent + 1);
}


BlockNode::~BlockNode()
{
	for (Statements::iterator it = statements.begin(); it != statements.end(); ++it)
	{
		delete *it;
	}
}

void BlockNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	ScopePrototype* block = new ScopePrototype(heap, scope);
	
	// get the returned value
	ExpressionNode* value = 0;
	Statements::const_iterator last = statements.end();
	if (!statements.empty())
	{
		value = dynamic_cast<ExpressionNode*>(statements.back());
		if (value != 0)
			--last;
	}
	
	block->members["this"] = thisMember(block);
	for (Statements::const_iterator it = statements.begin(); it != last; ++it)
	{
		DefNode* def = dynamic_cast<DefNode*>(*it);
		if (def != 0)
			def->define(block, debug, heap);
	}
	
	for (Statements::const_iterator it = statements.begin(); it != last; ++it)
	{
		Node* statement = *it;
		statement->generate(block, debug, heap);
		if (dynamic_cast<ExpressionNode*>(statement) != 0)
		{
			// if the statement is an expression, its result is ignored
			Node::generate(block, debug, new PopCode());
		}
	}
	
	if (value != 0)
		value->generate(block, debug, heap);
	else
		Node::generate(block, debug, new ConstCode(&nil));
	Node::generate(block, debug, new ReturnCode());
	
	Node::generate(scope, debug, new ScopeCode());
	Node::generate(scope, debug, new DefRefCode(block));
	Node::generate(scope, debug, new EvalCode());
}

void BlockNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
	{
		(*it)->dump(stream, indent + 1);
	}
}


ArrayNode::~ArrayNode()
{
	for (Elements::iterator it = elements.begin(); it != elements.end(); ++it)
	{
		delete *it;
	}
}

void ArrayNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		ScopePrototype* element = new ScopePrototype(heap, scope);
		(*it)->generate(element, debug, heap);
		Node::generate(element, debug, new ReturnCode());
		
		Node::generate(scope, debug, new ScopeCode());
		Node::generate(scope, debug, new DefRefCode(element));
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


void DefLookupNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Node::generate(scope, debug, new ScopeCode());
	
	ScopePrototype* method;
	Prototype* prototype = scope;
	while (true)
	{
		method = prototype->lookup(name);
		if (method != 0)
			break;
		
		ScopePrototype* s = dynamic_cast<ScopePrototype*>(prototype);
		assert(s != 0);
		
		Node::generate(scope, debug, new ParentCode());
		
		prototype = s->outer;
		assert(prototype != 0); // TODO: throw a method not found exception
	}
	
	Node::generate(scope, debug, new DefRefCode(method));
	Node::generate(scope, debug, new EvalCode());
}

void DefLookupNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
}


void IgnorePatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Node::generate(scope, debug, new PopCode());
}


void NilPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	// TODO: check we really got nil
	Node::generate(scope, debug, new PopCode());
}


void ValPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Node::generate(scope, debug, new EvalCode());
	Node::generate(scope, debug, new ValCode());
	
	size_t index = scope->members.size();
	scope->locals.push_back(name);
	
	ScopePrototype* getter = new ScopePrototype(heap, scope);
	scope->members[name] = getter;
	
	Node::generate(getter, debug, new ScopeCode());
	Node::generate(getter, debug, new ParentCode());
	Node::generate(getter, debug, new ValRefCode(index));
	Node::generate(getter, debug, new ReturnCode());
}

void ValPatternNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
}


void DefPatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Node::generate(scope, debug, new ValCode());
	
	size_t index = scope->members.size();
	scope->locals.push_back(name);
	
	ScopePrototype* getter = new ScopePrototype(heap, scope);
	scope->members[name] = getter;
	
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

void TuplePatternNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Node::generate(scope, debug, new EvalCode());
	Node::generate(scope, debug, new SelectCode("get"));
	int index = 0;
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		ScopePrototype* arg = new ScopePrototype(heap, scope);
		Node::generate(arg, debug, new ConstCode(new Integer(heap, index)));
		Node::generate(arg, debug, new ReturnCode());
		
		Node::generate(scope, debug, new DupCode());
		Node::generate(scope, debug, new ScopeCode());
		Node::generate(scope, debug, new DefRefCode(arg));
		Node::generate(scope, debug, new ApplyCode());
		(*it)->generate(scope, debug, heap);
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

void FunNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	Method* method = new Method(heap, scope);
	arg->generate(method, debug, heap);
	body->generate(method, debug, heap);
	Node::generate(method, debug, new ReturnCode());
	
	Node::generate(scope, debug, new ScopeCode());
	Node::generate(scope, debug, new FunCode(method));
}

void FunNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	arg->dump(stream, indent + 1);
	body->dump(stream, indent + 1);
}
