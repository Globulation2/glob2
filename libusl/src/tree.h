#ifndef TREE_H
#define TREE_H

#include "token.h"
#include <vector>
#include <string>

namespace Trees {
	struct Token;
	struct Sequence;
};

struct Tree {
	struct Visitor {
		virtual ~Visitor() {}
		virtual void Visit(Tree& tree);
		virtual void Visit(Trees::Token& tok) = 0;
		virtual void Visit(Trees::Sequence& seq);
	};
	
	struct ConstVisitor {
		virtual ~ConstVisitor() {}
		virtual void Visit(const Tree& tree);
		virtual void Visit(const Trees::Token& tok) = 0;
		virtual void Visit(const Trees::Sequence& seq);
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

	struct Token: public Tree {
		const ::Token& token;
		Token(const ::Token& token): Tree(token.position), token(token) {}
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

struct TreeType: Tree::ConstVisitor {
	enum Type {
		Token,
		Sequence
	};
	Type result;
	TreeType(const Tree* tree) {
		tree->Accept(self);
	}
	operator Type() const {
		return result;
	}
	void Visit(const Trees::Token& tok) {
		result = Token;
	}
	void Visit(const Trees::Sequence& sequence) {
		result = Sequence;
	}
};

#endif // ndef TREE_H
