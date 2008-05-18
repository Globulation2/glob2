#ifndef POSITION_H
#define POSITION_H

#include <string>

struct Position
{
	size_t line;
	size_t column;
	
	Position(): line(0), column(0) {}
	Position(size_t line, size_t column): line(line), column(column) {}
	
	bool operator<(const Position& that) const;
	
	void operator+=(char c);
	void move(const char *text, size_t length);
};

struct FilePosition
{
	std::string file;
	Position position;
	
	FilePosition(const std::string& file, const Position& position):
		file(file),
		position(position)
	{}
};

#endif // ndef POSITION_H
