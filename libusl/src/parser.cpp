#include "parser.h"

const Token::Type Parser::tokenTypes[TOKENTYPES] = {
	Token::Type(ID,      "an identifier",           "[[:alpha:]][[:alnum:]]*"),
	Token::Type(OP,      "an operator",             "[!#$%&*+-./:<=>?@\\^_\\|~]+"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(SBEG,    "a beginning of sequence", "\\(|\\[|\\{"),
	Token::Type(SEND,    "an end of sequence",      "\\)|\\]|\\}"),
	Token::Type(SSEP,    "a separator",             "[,;]"),
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
		case STR:
			tree = new Trees::String(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case NUM:
			tree = new Trees::Number(next.position);
			NextToken();
			break;
		case SBEG: {
			const char begin = *next.text;
			std::vector<Tree*> sequence;
			NextToken();
			if(next.type->id != SEND) {
				loop {
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
	while(next.type->id == ID
		|| next.type->id == OP
		|| next.type->id == STR
		|| next.type->id == NUM
		|| next.type->id == SBEG) {
		tree = new Trees::Apply(next.position, tree, Next());
	}
	return tree;
}

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

struct Printer: Tree::ConstVisitor {
	void Print(const Tree* tree) {
		tree->Accept(self);
	}
	void Visit(const Trees::String& str) {
		std::cout << str.content << std::endl;
	}
	void Visit(const Trees::Number& num) {
		std::cout << "" << std::endl;
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
