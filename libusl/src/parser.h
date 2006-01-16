#ifndef PARSER_H
#define PARSER_H

#include "scanner.h"
#include <stdexcept>

class Parser {
public:
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
private:
	static const Token::Type tokenTypes[TOKENTYPES];
public:
	Parser(const char* text): scanner(tokenTypes, tokenTypes + TOKENTYPES, text), token(NextToken()) {}
	struct Error: std::runtime_error {
		const Position position;
		Error(const Position& position, const std::string& message): std::runtime_error(message), position(position) {}
	};
	void Fail(const std::string& message) {
		throw Error(token.position, message);
	}
	const Token& NextToken() {
		do {
			token = scanner.Next();
		} while(token.type->id == SPACE || token.type->id == COMMENT);
		//std::cout << token.position.line << ", " << token.position.column << ": " << token.type->desc << " <" << std::string(token.text, token.length) << ">" << std::endl;
		return token;
	}
	const Token& GetToken() {
		return token;
	}
	int GetTokenType() {
		return GetToken().type->id;
	}
private:
	Scanner scanner;
	Token token;
};

#endif // ndef PARSER_H
