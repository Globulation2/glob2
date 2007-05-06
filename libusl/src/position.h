#ifndef POSITION_H
#define POSITION_H

#include <cstddef>

struct Position
{
	Position() {}
	Position(size_t line, size_t column): line(line), column(column) {}
	
	void operator+=(char c);
	void move(const char *text, size_t length);
	
	size_t line;
	size_t column;
};

#endif // ndef POSITION_H
