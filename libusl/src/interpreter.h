#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "parser.h"
#include "context.h"

class Interpreter: Parser {
public:
	Interpreter(const char* text): Parser(text) {}
	const std::string Run(Context* context);
};

#endif // ndef INTERPRETER_H
