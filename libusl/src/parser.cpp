#include "parser.h"

const Token::Type Parser::tokenTypes[TOKENTYPES] = {
	Token::Type(ID,      "an identifier",           "[[:alpha:]][[:alnum:]]*"),
	Token::Type(OP,      "an operator",             "[!#$%&*+-./:<=>?@\\^_\\|~]+"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(SBEG,    "a beginning of sequence", "\\(|\\[|\\{"),
	Token::Type(SEND,    "an end of sequence",      "\\)|\\]|\\}"),
	Token::Type(SSEP,    "a separator",             "[,;\\.]"),
	Token::Type(SPACE,   "a space",                 "[[:space:]]+"),
	Token::Type(COMMENT, "a comment",               "\\([\"\'])(\\\\|[^\\])*\\([\"\'])"),
	Token::Type(END,     "the end of the text",     "$"),
	Token::Type(ERROR,   "an invalid token",        ""),
};

Tree* Parser::Parse() {
	const Position position(next.position);
	std::vector<Tree*> root;
	while(next.type->id != END) {
		root.push_back(Next());
	}
	return new Trees::Sequence(position, root);
}

Tree* Parser::Next() {
	Tree* tree = Next2();
	while(next.type->id == ID
	   || next.type->id == OP
	   || next.type->id == STR
	   || next.type->id == NUM
	   || next.type->id == SBEG) {
		tree = new Trees::Apply(next.position, tree, Next2());
	}
	return tree;
}

Tree* Parser::Next2() {
	Tree* tree;
	switch(next.type->id) {
		case ID:
			tree = new Trees::Strings::Identifier(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case OP:
			tree = new Trees::Strings::Operator(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case STR: // TODO: parse strings correctly (remove escape chars)
			tree = new Trees::String(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case NUM: // TODO: parse numbers correctly and put the value in the tree
			tree = new Trees::Number(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case SBEG: {
			const char begin = *next.text;
			std::vector<Tree*> sequence;
			NextToken();
			if(next.type->id != SEND) {
				while(true) {
					sequence.push_back(Next());
					if(next.type->id == SSEP) {
						NextToken();
					}
					else {
						break;
					}
				}
				if(next.type->id != SEND) {
					Fail(std::string("unterminated sequence: found ") + next.type->desc + " while expecting " + tokenTypes[SEND].desc);
				}
			}
			const char found = *next.text;
			char expected;
			switch(begin) {
				case '(':
					expected = ')';
					break;
				case '[':
					expected = ']';
					break;
				case '{':
					expected = '}';
					break;
				default:
					Fail("parser bug: invalid beginning of sequence accepted");
			}
			if(found != expected) {
				Fail(std::string("wrong end of sequence: found ") + found + " while expecting " + expected);
			}
			tree = new Trees::Sequence(next.position, sequence);
			NextToken();
			break;
		}
		default:
			Fail(next.type->desc + " was found while expecting " + tokenTypes[ID].desc + ", " + tokenTypes[OP].desc + ", " + tokenTypes[STR].desc + ", " + tokenTypes[NUM].desc + " or " + tokenTypes[SBEG].desc);
	}
	return tree;
}

#include <iostream>
#include <fstream>
const char* LoadFile(const char* name) {
	std::filebuf file;
	file.open(name, std::ios::in);
	size_t length = file.pubseekoff(0, std::ios::end, std::ios::in);
	file.pubseekpos(0, std::ios::in);
	char* text = new char[length + 1];
	length = file.sgetn(text, length);
	file.close();
	text[length] = '\0';
	std::cout << name << ": " << length << " " << (length != 1 ? "bytes" : "byte") << std::endl;
	return text;
}

struct Indent {
	size_t indent;
	Indent(size_t val = 0): indent(val) {}
	operator size_t&() {
		return indent;
	}
	operator const size_t&() const {
		return indent;
	}
};

std::ostream& operator<<(std::ostream& os, const Indent& indent) {
	for(size_t i = 0; i < indent; ++i)
		os << ' ';
	return os;
}

struct Printer: Tree::ConstVisitor {
	Indent indent;
	Printer() {
		indent = 0;
	}
	void Print(const Tree* tree) {
		tree->Accept(self);
	}
	void Visit(const Trees::String& str) {
		std::cout << indent << str.content << std::endl;
	}
	void Visit(const Trees::Number& num) {
		std::cout << indent << num.content << std::endl;
	}
	void Visit(const Trees::Apply& apply) {
		++indent;
		Tree::ConstVisitor::Visit(apply);
		--indent;
	}
	void Visit(const Trees::Sequence& sequence) {
		std::cout << indent << "{" << std::endl;
		++indent;
		/*foreach(iterator, sequence.elements.begin(), sequence.elements.end()) {
			(*iterator)->Accept(self);
		}*/
		Tree::ConstVisitor::Visit(sequence);
		--indent;
		std::cout << indent << "}" << std::endl;
	}
};

struct Node {
	enum Type {
		NUMBER,
		OPERATION,
		OPERATOR,
		FUNCTOR,
		SUBEXPR
	};
	virtual operator Type() const = 0;
};

struct Expr: Node {
	virtual operator Type() const = 0;
};

struct Number: Expr  {
	Number(int i): i(i) {}
	const int i;
	virtual operator Type() const { return NUMBER; }
};

struct Operation: Expr {
	Operation(std::string op, const Expr* left, const Expr* right): op(op), left(left), right(right) {}
	const std::string op;
	const Expr* left;
	const Expr* right;
	virtual operator Type() const { return OPERATION; }
};

struct Operator: Node {
	Operator(const std::string op): op(op) {}
	const std::string op;
	virtual operator Type() const { return OPERATOR; }
};

struct Functor: Node {
	Functor(std::string op, const Expr* me): op(op), me(me) {}
	const std::string op;
	const Expr* me;
	virtual operator Type() const { return FUNCTOR; }
};

struct SubExpr: Expr {
	const Expr* expr;
	SubExpr(const Expr* expr): expr(expr) {}
	virtual operator Type() const { return SUBEXPR; }
};

struct Evaluator: Tree::ConstVisitor {
	Evaluator() {
	}
	Node* node;
	const Node* Evaluate(const Tree* tree) {
		tree->Accept(self);
		return node;
	}
	void Visit(const Trees::String& str) {
		assert(false);
	}
	void Visit(const Trees::Number& num) {
		node = new Number(atoi(num.content.c_str()));
	}
	void Visit(const Trees::Apply& apply) {
		const Node* left = Evaluate(apply.function);
		const Node* right = Evaluate(apply.argument);
		switch(*left) {
		case Node::NUMBER:
		case Node::SUBEXPR:
			switch(*right) {
			case Node::OPERATOR:
				node = new Functor(((const Operator*)right)->op, (const Expr*)left);
			default:
				assert(false);
			}
			break;
		case Node::FUNCTOR:
			switch(*right) {
			case Node::NUMBER:
			case Node::SUBEXPR:
				const Functor* func = (const Functor*)node;
				node = new Operation(func->op, func->me, (const Expr*)right);
				break;
			default:
				assert(false);
			}
			break;
		case Node::OPERATION:
			switch(*right) {
			case Node:::
				break;
			default:
				assert(false);
			}
			break;
		default:
			assert(false);
		}
	}
	
	/*void parseExpr(inputSequence, outputSequence)
	{
		// précédende à gauche
		int operand0 = inputSequence.pop();
		Operator op = inputSequence.pop();
		if (inputSequence.size() > 1)
			parseExpr(inputSequence, outputSequence);
		else
			outputSequence.push(inputSequence.pop);
		outputSequence.push(operand0);
		outputSequence.push(op);
		// précédende à droite
		if (inputSequence.size() > 3)
			parseExpr(inputSequence, outputSequence);
		else
			outputSequence.push(inputSequence.pop);
		Operator op = inputSequence.pop();
		int operand0 = inputSequence.pop();
		outputSequence.push(operand0);
		outputSequence.push(op);
	}*/
	
	void Visit(const Trees::Sequence& sequence) { // TODO: treat empty sequences
		foreach(it, sequence.elements.begin(), sequence.elements.end()) {
			Evaluate(*it);
		}
		switch(*node) {
		case Node::NUMBER:
		case Node::OPERATION:
			node = new SubExpr((const Expr*)node);
		case Node::SUBEXPR:
			break;
		default:
			assert(false);
		}
	}
};

int main(int argc, char* argv[]) {
	std::set_terminate(__gnu_cxx::__verbose_terminate_handler);
	const char* filename;
	if(argc >= 2) {
		filename = argv[1];
	}
	else {
		filename = "toto.usl";
	}
	const char* source = LoadFile(filename);
	Parser parser(source);
	bool status;
	Tree* tree;
	try {
		tree = parser.Parse();
		status = true;
	}
	catch(Parser::Error e) {
		status = false;
		std::cerr << "Parser error at line " << e.position.line << ", column " << e.position.column << ": " << e.what() << std::endl;
	}
	delete[] source;
	if(status) {
		Printer printer;
		printer.Print(tree);
	}
	return status;
}
