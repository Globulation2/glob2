#ifndef TREE_H
#define TREE_H

#include "common.h"
#include "position.h"
#include <vector>

namespace Trees {
	struct String;
	namespace Strings {
		struct Identifier;
		struct Operator;
	};
	struct Number;
	namespace Numbers {
		struct Integer;
		struct Float;
	};
	struct Apply;
	struct Sequence;
};

struct Tree {
	struct Visitor {
		virtual void Visit(Tree& tree);
		virtual void Visit(Trees::String& str) = 0;
		virtual void Visit(Trees::Strings::Identifier& id) { Visit((Trees::String&)id); }
		virtual void Visit(Trees::Strings::Operator& op) { Visit((Trees::String&)op); }
		virtual void Visit(Trees::Number& num) = 0;
		virtual void Visit(Trees::Apply& apply);
		virtual void Visit(Trees::Sequence& sequence);
	};
	
	struct ConstVisitor {
		virtual void Visit(const Tree& tree);
		virtual void Visit(const Trees::String& str) = 0;
		virtual void Visit(const Trees::Strings::Identifier& id) { Visit((const Trees::String&)id); }
		virtual void Visit(const Trees::Strings::Operator& op) { Visit((const Trees::String&)op); }
		virtual void Visit(const Trees::Number& num) = 0;
		virtual void Visit(const Trees::Apply& apply);
		virtual void Visit(const Trees::Sequence& sequence);
	};
	
	virtual void Accept(Visitor& visitor) = 0;
	virtual void Accept(ConstVisitor& visitor) const = 0;
	virtual ~Tree() {}
protected:
	Tree(const Position& position): position(position) {}
private:
	const Position position;
};

namespace Trees {
	
	struct String: public Tree {
		const std::string content;
		String(const Position& position, const std::string& content): Tree(position), content(content) {}
		void Accept(Tree::Visitor& visitor) { return visitor.Visit(self); }
		void Accept(Tree::ConstVisitor& visitor) const { return visitor.Visit(self); }
	};
	
	namespace Strings {
		struct Identifier: public String {
			Identifier(const Position& position, const std::string& content): String(position, content) {}
			void Accept(Tree::Visitor& visitor) { return visitor.Visit(self); }
			void Accept(Tree::ConstVisitor& visitor) const { return visitor.Visit(self); }
		};
		struct Operator: public String {
			Operator(const Position& position, const std::string& content): String(position, content) {}
			void Accept(Tree::Visitor& visitor) { return visitor.Visit(self); }
			void Accept(Tree::ConstVisitor& visitor) const { return visitor.Visit(self); }
		};
	};
	
	struct Number: public Tree {
		Number(const Position& position): Tree(position) {}
		void Accept(Tree::Visitor& visitor) { return visitor.Visit(self); }
		void Accept(Tree::ConstVisitor& visitor) const { return visitor.Visit(self); }
	};
	
	struct Apply: public Tree {
		Tree* function;
		Tree* argument;
		Apply(const Position& position, Tree* function, Tree* argument): Tree(position), function(function), argument(argument) {}
		void Accept(Tree::Visitor& visitor) { return visitor.Visit(self); }
		void Accept(Tree::ConstVisitor& visitor) const { return visitor.Visit(self); }
	};
	
	struct Sequence: public Tree {
		typedef std::vector<Tree*> tree_vector;
		const tree_vector elements;
		template<typename Iterator>
		Sequence(const Position& position, Iterator begin, Iterator end): Tree(position), elements(begin, end) {}
		template<typename Container>
		Sequence(const Position& position, Container elements): Tree(position), elements(elements.begin(), elements.end()) {}
		void Accept(Tree::Visitor& visitor) { return visitor.Visit(self); }
		void Accept(Tree::ConstVisitor& visitor) const { return visitor.Visit(self); }
	};
	
};

void Tree::Visitor::Visit(Tree& tree) {
	tree.Accept(self);
}
void Tree::Visitor::Visit(Trees::Apply& apply) {
	apply.function->Accept(self);
	apply.argument->Accept(self);
}
void Tree::Visitor::Visit(Trees::Sequence& sequence) {
	foreach(iterator, sequence.elements.begin(), sequence.elements.end()) {
		(*iterator)->Accept(self);
	}
}
void Tree::ConstVisitor::Visit(const Tree& tree) {
	tree.Accept(self);
}
void Tree::ConstVisitor::Visit(const Trees::Apply& apply) {
	apply.function->Accept(self);
	apply.argument->Accept(self);
}
void Tree::ConstVisitor::Visit(const Trees::Sequence& sequence) {
	foreach(iterator, sequence.elements.begin(), sequence.elements.end()) {
		(*iterator)->Accept(self);
	}
}

#endif // ndef TREE_H
