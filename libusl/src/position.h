#pragma once

#include <string>
#include <ostream>
#include <stdexcept>

struct Position
{
	std::string filename;
	size_t line;
	size_t column;

	Position(): filename(), line(0), column(0) {}
	Position(const std::string& filename, size_t line, size_t column): filename(filename), line(line), column(column) {}

	bool operator<(const Position& that) const;

	void operator+=(char c);
	void move(const std::string text, size_t length);
};

inline std::ostream& operator<<(std::ostream& stream, const Position& position)
{
	return stream << position.filename << ":" << position.line << ":" << position.column;
}

struct Exception: std::runtime_error
{
	Exception(const Position& position, const std::string& message): std::runtime_error(message), position(position) {}
	~Exception() throw() {}
	Position position;
};

