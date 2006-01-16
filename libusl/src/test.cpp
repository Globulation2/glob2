#include "interpreter.h"
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

int main(int argc, char* argv[]) {
	const char* filename;
	if(argc >= 2) {
		filename = argv[1];
	}
	else {
		filename = "toto.usl";
	}
	const char* source = LoadFile(filename);
	Interpreter interpreter(source);
	try {
		std::cout << interpreter.Run(new Context());
	}
	catch(Parser::Error e) {
		std::cerr << "Parser error at line " << e.position.line << ", column " << e.position.column << ": " << e.what() << std::endl;
	}
	delete[] source;
	return 0;
}
