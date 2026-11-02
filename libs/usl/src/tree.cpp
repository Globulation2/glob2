#include "tree.h"
#include "code.h"
#include "debug.h"
#include "position.h"
#include "types.h"
#include <sstream>

using std::stringstream;
using std::ostringstream;
using std::endl;


void Node::emit(ThunkPrototype* thunk, CodeGen& cg, Code* code)
{
	thunk->body.push_back(code);

	if (cg.debug != 0)
	{
		size_t address = thunk->body.size();
		ThunkDebugInfo* scopeDebug = cg.debug->get(thunk);
		ThunkDebugInfo::Source2Address::iterator it = scopeDebug->source2Address.find(position);
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
	stream << position.line << ":" << position.column << ": " << unmangle(typeid(*this).name());
	dumpSpecific(stream, indent);
}

void Node::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
}


void ExpressionNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	generate(static_cast<ThunkPrototype*>(scope), cg);
}


void ConstNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	emit(thunk, cg, new ConstCode(value));
}

void ConstNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << ' ';
	value->dump(stream);
	stream << '\n';
}


SelectNode::~SelectNode()
{
	delete receiver;
}

void SelectNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	receiver->generate(thunk, cg);
	emit(thunk, cg, new SelectCode(name));
	emit(thunk, cg, new EvalCode());
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

void ApplyNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	ThunkPrototype* arg = new ThunkPrototype(cg.heap, thunk);
	argument->generate(arg, cg);

	function->generate(thunk, cg);
	emit(thunk, cg, new ThunkCode());
	emit(thunk, cg, new CreateCode<Thunk>(arg));
	emit(thunk, cg, new ApplyCode());
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

void DecNode::declare(ScopePrototype* scope, CodeGen& cg)
{
	scope->members[name] = new ThunkPrototype(cg.heap, scope);

	if (type == VAR)
	{
		// TODO: setter
	}
}

void DecNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	ScopePrototype* scope = dynamic_cast<ScopePrototype*>(thunk);
	assert(scope);
	generate(scope, cg);
}

void DecNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	switch (type) {
	case DEF:
		{
			ThunkPrototype* def = scope->members[name];
			assert(def);
			body->generate(def, cg);

			emit(scope, cg, new ConstCode(&nil));
		}
		break;
	case AUTO: // TODO: optimize constant AUTO
	case VAR:
	case VAL:
		{
			size_t index = scope->locals.size();
			scope->locals.push_back(name);

			body->generate(scope, cg);
			emit(scope, cg, new DupCode(0));
			emit(scope, cg, new ValCode(index));

			ThunkPrototype* getter = scope->members[name];
			emit(getter, cg, new ThunkCode());
			emit(getter, cg, new ParentCode());
			emit(getter, cg, new ValRefCode(index));
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

void BlockNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	ScopePrototype* scope = new ScopePrototype(cg.heap, thunk);
	scope->members["this"] = thisMember(scope);
	generateMembers(scope, cg);
	emit(thunk, cg, new ThunkCode());
	emit(thunk, cg, new CreateCode<Scope>(scope));
	emit(thunk, cg, new EvalCode());
}


void ExecutionBlock::generateMembers(ScopePrototype* scope, CodeGen& cg)
{
	if (!elements.empty())
	{
		for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
		{
			DecNode* dec = dynamic_cast<DecNode*>(*it);
			if (dec != 0)
				dec->declare(scope, cg);
		}

		for (Elements::const_iterator it = elements.begin(); it != elements.end() - 1; ++it)
		{
			Node* element = *it;
			element->generate(scope, cg);
			emit(scope, cg, new PopCode());
		}

		elements.back()->generate(scope, cg);
	}
	else
	{
		emit(scope, cg, new ConstCode(&nil));
	}
}


void RecordBlock::generateMembers(ScopePrototype* scope, CodeGen& cg)
{
	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		DecNode* dec = dynamic_cast<DecNode*>(*it);
		if (dec != 0)
			dec->declare(scope, cg);
	}

	for (Elements::const_iterator it = elements.begin(); it != elements.end(); ++it)
	{
		size_t index = scope->locals.size();

		Node* element = *it;
		element->generate(scope, cg);

		ThunkPrototype* getter;

		DecNode* dec = dynamic_cast<DecNode*>(element);
		if (dec)
		{
			emit(scope, cg, new PopCode());

			getter = scope->members[dec->name];
		}
		else
		{
			scope->locals.push_back("");
			emit(scope, cg, new ValCode(index));

			getter = new ThunkPrototype(cg.heap, scope);
			emit(getter, cg, new ThunkCode());
			emit(getter, cg, new ParentCode());
			emit(getter, cg, new ValRefCode(index));
		}

		stringstream str;
		str << index;
		scope->members[str.str()] = getter;
	}

	emit(scope, cg, new ThunkCode());
}


void DefLookupNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	emit(thunk, cg, new ThunkCode());

	ThunkPrototype* member;
	Prototype* prototype = thunk;
	while (true)
	{
		member = prototype->lookup(name);
		if (member != 0)
			break;

		ThunkPrototype* t = dynamic_cast<ThunkPrototype*>(prototype);
		assert(t != 0);

		emit(thunk, cg, new ParentCode());

		prototype = t->outer;

		if (prototype == 0)
		{
			ostringstream message;
			message << "Declaration not found: " << name << endl;
			throw Exception(position, message.str());
		}
	}

	emit(thunk, cg, new CreateCode<Thunk>(member));
	emit(thunk, cg, new EvalCode());
}

void DefLookupNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
}


void IgnorePatternNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	emit(scope, cg, new PopCode());
}


void NilPatternNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	// TODO: check we really got nil
	emit(scope, cg, new PopCode());
}


void ValPatternNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	size_t index = scope->locals.size();
	scope->locals.push_back(name);

	emit(scope, cg, new EvalCode());
	emit(scope, cg, new ValCode(index));

	ScopePrototype* getter = new ScopePrototype(cg.heap, scope);
	scope->members[name] = getter;

	emit(getter, cg, new ThunkCode());
	emit(getter, cg, new ParentCode());
	emit(getter, cg, new ValRefCode(index));
}

void ValPatternNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << "(" << name << ")";
	stream << '\n';
}


void DefPatternNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	size_t index = scope->members.size();
	scope->locals.push_back(name);

	emit(scope, cg, new ValCode(index));

	ScopePrototype* getter = new ScopePrototype(cg.heap, scope);
	scope->members[name] = getter;

	emit(getter, cg, new ThunkCode());
	emit(getter, cg, new ParentCode());
	emit(getter, cg, new ValRefCode(index));
	emit(getter, cg, new EvalCode());
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

void TuplePatternNode::generate(ScopePrototype* scope, CodeGen& cg)
{
	emit(scope, cg, new EvalCode());
	int index = 0;
	for (Members::iterator it = members.begin(); it != members.end(); ++it)
	{
		stringstream str;
		str << index;

		emit(scope, cg, new DupCode(0));
		emit(scope, cg, new SelectCode(str.str()));
		(*it)->generate(scope, cg);
		++index;
	}
	emit(scope, cg, new PopCode());
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

void FunNode::generate(ThunkPrototype* thunk, CodeGen& cg)
{
	ScopePrototype* scope = new ScopePrototype(cg.heap, thunk);
	arg->generate(scope, cg);
	BlockNode* block = dynamic_cast<BlockNode*>(body);
	if (block == 0)
		body->generate(scope, cg);
	else
		block->generateMembers(scope, cg);

	emit(thunk, cg, new ThunkCode());
	emit(thunk, cg, new CreateCode<Function>(scope));
}

void FunNode::dumpSpecific(std::ostream &stream, unsigned indent) const
{
	stream << '\n';
	arg->dump(stream, indent + 1);
	body->dump(stream, indent + 1);
}
