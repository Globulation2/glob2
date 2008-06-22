#include "tree.h"
#include "code.h"
#include "debug.h"
#include "error.h"
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
	Node::generate(scope, debug, new EvalCode());
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


DecNode::~DecNode()
{
	delete body;
}

void DecNode::declare(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	scope->members[name] = new ScopePrototype(heap, scope);
	
	if (type == VAR)
	{
		// TODO: setter
	}
}

void DecNode::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	switch (type) {
	case AUTO:
	case DEF:
		{
			ScopePrototype* def = scope->members[name];
			assert(def);
			body->generate(def, debug, heap);
			Node::generate(def, debug, new ReturnCode());

			Node::generate(scope, debug, new ConstCode(&nil));
		}
		break;
	case VAR:
	case VAL:
		{
			size_t index = scope->locals.size();
			scope->locals.push_back(name);
			
			body->generate(scope, debug, heap);
			Node::generate(scope, debug, new DupCode());
			Node::generate(scope, debug, new ValCode(index));
			
			ScopePrototype* getter = scope->members[name];
			Node::generate(getter, debug, new ScopeCode());
			Node::generate(getter, debug, new ParentCode());
			Node::generate(getter, debug, new ValRefCode(index));
			Node::generate(getter, debug, new ReturnCode());
		}
		break;
	}
}

void DecNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << "," << type << ")";
	stream << '\n';
	body->dump(stream, indent + 1);
}


BlockNode::~BlockNode()
{
	for (Elements::iterator it = elements.begin(); it != elements.end(); ++it)
	{
		delete *it;
	}
}

void BlockNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		(*it)->dump(stream, indent + 1);
	}
}


void ExecutionBlock::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	if (!elements.empty())
	{
		ScopePrototype* block = new ScopePrototype(heap, scope);
		block->members["this"] = thisMember(block);

		for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
		{
			DecNode* dec = dynamic_cast<DecNode*>(*it);
			if (dec != 0)
				dec->declare(block, debug, heap);
		}
	
		for (Elements::const_iterator it = elements.begin(); it != elements.end() - 1; ++it)
		{
			(*it)->generate(block, debug, heap);
			Node::generate(block, debug, new PopCode());
		}
	
		elements.back()->generate(block, debug, heap);
		Node::generate(block, debug, new ReturnCode());
	
		Node::generate(scope, debug, new ScopeCode());
		Node::generate(scope, debug, new DefRefCode(block));
		Node::generate(scope, debug, new EvalCode());
	}
	else
	{
		Node::generate(scope, debug, new ConstCode(&nil));
	}
}


void RecordBlock::generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap)
{
	ScopePrototype* block = new ScopePrototype(heap, scope);
	block->members["this"] = thisMember(block);
	//block->members["get"] = getMember(block);

	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		DecNode* dec = dynamic_cast<DecNode*>(*it);
		if (dec)
			dec->declare(block, debug, heap);
	}

	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		size_t index = block->locals.size();

		Node* element = *it;
		element->generate(block, debug, heap);

		ScopePrototype* getter;

		DecNode* dec = dynamic_cast<DecNode*>(element);
		if (dec)
		{
			getter = block->members[dec->name];
		}
		else
		{
			block->locals.push_back("");
			Node::generate(block, debug, new ValCode(index));

			getter = new ScopePrototype(heap, block);
			Node::generate(getter, debug, new ScopeCode());
			Node::generate(getter, debug, new ParentCode());
			Node::generate(getter, debug, new ValRefCode(index));
			Node::generate(getter, debug, new ReturnCode());
		}

		stringstream str;
		str << index;
		block->members[str.str()] = getter;
	}

	Node::generate(block, debug, new ScopeCode());
	Node::generate(block, debug, new ReturnCode());

	Node::generate(scope, debug, new ScopeCode());
	Node::generate(scope, debug, new DefRefCode(block));
	Node::generate(scope, debug, new EvalCode());
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
		
		if (prototype == 0)
		{
			ostringstream message;
			message << "Program error @" << position.line << ":" << position.column << ":" << endl;
			message << "Declaration not found: " << name << endl;
			throw Exception(position, message.str());
		}
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
	size_t index = scope->members.size();
	scope->locals.push_back(name);
	
	Node::generate(scope, debug, new EvalCode());
	Node::generate(scope, debug, new ValCode(index));
	
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
	size_t index = scope->members.size();
	scope->locals.push_back(name);
	
	Node::generate(scope, debug, new ValCode(index));
	
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
	int index = 0;
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		stringstream str;
		str << index;

		Node::generate(scope, debug, new DupCode());
		Node::generate(scope, debug, new SelectCode(str.str()));
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
	delete arg;
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
