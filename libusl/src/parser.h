#ifndef PARSER_H
#define PARSER_H

#include "scanner.h"
#include "tree.h"
#include <sstream>
#include <stdexcept>

class Parser {
	enum TokenType {
		ID,
		OP,
		STR,
		NUM,
		SBEG,
		SEND,
		SSEP,
		SPACE,
		COMMENT,
		END,
		ERROR,
		TOKENTYPES,
	};
	static const Token::Type tokenTypes[TOKENTYPES];
public:
	Parser(const char* text): scanner(tokenTypes, tokenTypes + TOKENTYPES, text), next(NextToken()) {}
	Tree* Parse();
	struct Error: std::runtime_error {
		const Position position;
		Error(const Position& position, const std::string& message): std::runtime_error(message), position(position) {}
	};
private:
	Tree* Next();
	void Fail(const std::string& message) {
		throw Error(next.position, message);
	}
private:
	const Token& NextToken() {
		do {
			next = scanner.Next();
		} while(next.type->id == SPACE || next.type->id == COMMENT);
		//std::cout << next.position.line << ", " << next.position.column << ": " << next.type->desc << " <" << std::string(next.text, next.length) << ">" << std::endl;
		return next;
	}
	Scanner scanner;
	Token next;
};

#endif // ndef PARSER_H
