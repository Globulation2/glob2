#ifndef SCANNER_H
#define SCANNER_H

#include "token.h"
#include <vector>

class Scanner {
public:
	template<typename TokenTypesIterator>
	Scanner(TokenTypesIterator tokenTypesBegin, TokenTypesIterator tokenTypesEnd, const char* text): tokenTypes(tokenTypesBegin, tokenTypesEnd), text(text), position(1, 1) {}
	template<typename TokenTypesContainer>
	Scanner(TokenTypesContainer tokenTypesContainer, const char* text): tokenTypes(container_traits<TokenTypesContainer>::beginof(tokenTypesContainer), container_traits<TokenTypesContainer>::endof(tokenTypesContainer)), text(text), position(1, 1) {}
	/*
	template<typename TokenTypesContainer>
	Scanner(TokenTypesContainer tokenTypesContainer, const char* text): tokenTypes(beginof(tokenTypesContainer), endof(tokenTypesContainer)), text(text), position(1, 0) {}
	*/
	Token Next();
private:
	const std::vector<Token::Type> tokenTypes;
	const char* text;
	Position position;
};

#endif // ndef SCANNER_H
