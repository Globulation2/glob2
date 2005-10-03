#include "tree.h"

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
