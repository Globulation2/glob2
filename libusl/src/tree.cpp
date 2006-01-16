#include "tree.h"

void Tree::Visitor::Visit(Tree& tree) {
	tree.Accept(self);
}
void Tree::Visitor::Visit(Trees::Sequence& seq) {
	foreach(iterator, seq.elements.begin(), seq.elements.end()) {
		(*iterator)->Accept(self);
	}
}
void Tree::ConstVisitor::Visit(const Tree& tree) {
	tree.Accept(self);
}
void Tree::ConstVisitor::Visit(const Trees::Sequence& seq) {
	foreach(iterator, seq.elements.begin(), seq.elements.end()) {
		(*iterator)->Accept(self);
	}
}
