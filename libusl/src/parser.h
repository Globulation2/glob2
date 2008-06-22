#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "tree.h"

struct Heap;

struct Parser: Lexer
{
	Parser(const char* src, Heap* heap):
		Lexer(src),
		heap(heap)
	{}
	
	BlockNode* parse();

private:
	void statements(BlockNode* block);
	Node* statement();
	DecNode* declaration(DecNode::Type type);
	DecNode* declaration(const Position& position, DecNode::Type type, const std::string& name);
	PatternNode* pattern();
	Node* expression();
	Node* expression(const Position& position, const std::string& id);
	Node* expression(Node* first);
	ApplyNode* selectAndApply(const Position& position, std::auto_ptr<Node> receiver, const std::string& method);
	Node* simple();
	void newlines();
	std::string identifier();
	void accept(TokenType type);

	Heap* heap;
};

#endif // ndef PARSER_H
