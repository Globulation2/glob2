#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "tree.h"

struct Heap;

struct Parser: Lexer
{
	Parser(const std::string& filename, const char* src, Heap* heap):
		Lexer(filename, src),
		heap(heap)
	{}
	
	void parse(BlockNode* block);

private:
	void statements(BlockNode* block);
	Node* statement();
	DecNode* declaration(DecNode::Type type);
	DecNode* declaration(const Position& position, DecNode::Type type, const std::string& name);
	ExpressionNode* declaration2(const Position& position);
	PatternNode* pattern();
	void expressions(BlockNode* block);
	ExpressionNode* expression();
	ExpressionNode* prefixedExpression();
	ExpressionNode* methodCompositionExpression(ExpressionNode* first);
	ExpressionNode* pathExpression(ExpressionNode* first);
	ExpressionNode* simpleExpression();
	void newlines();
	std::string identifier();
	void accept(TokenType type);

	Heap* heap;
};

#endif // ndef PARSER_H
