#ifndef POSITION_H
#define POSITION_H

#include <string>
#include <ostream>

struct Position
{
	std::string filename;
	size_t line;
	size_t column;
	
	Position(): filename(), line(0), column(0) {}
	Position(const std::string& filename, size_t line, size_t column): filename(filename), line(line), column(column) {}
	
	bool operator<(const Position& that) const;
	
	void operator+=(char c);
	void move(const char *text, size_t length);
};

inline std::ostream& operator<<(std::ostream& stream, const Position& position)
{
	return stream << position.filename << ":" << position.line << ":" << position.column;
}

#endif // ndef POSITION_H
