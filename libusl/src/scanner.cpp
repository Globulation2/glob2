#include "scanner.h"

#include <iostream>
Token Scanner::Next() {
	const Token::Type* type = NULL;
	ssize_t length = -1;
	foreach(tokenTypeIterator, tokenTypes.begin(), tokenTypes.end()) {
		const Token::Type& newType = *tokenTypeIterator;
		ssize_t newLength = newType.Match(text);
		if(newLength > length) {
			type = &newType;
			length = newLength;
		}
	}
	if(length == -1) {
		length = 0;
	}
	Token token(position, type, text, length);
	position.Move(text, text + length);
	text += length;
	return token;
}

/*
Token next;

Token nextToken(Scanner& scanner) {
	next = scanner.nextToken();
	return next;
}
#include <fstream>
int main() {
	//std::ifstream file("toto.usl");
	Scanner scanner(std::cin);
	while(nextToken(scanner).type != Token::END) {
		std::cout << next.pos.line << ',' << next.pos.column << ": ";
		std::cout << next.type;
		if(next.value.size() > 0)
			std::cout << " (" << next.value << ')';
		std::cout << std::endl;
	}
}
*/
