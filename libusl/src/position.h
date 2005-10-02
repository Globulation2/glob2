#ifndef POSITION_H
#define POSITION_H

#include <cstddef>

#define self (*this)
#define foreach(it, begin, end) for(typeof(begin) it = begin; it != end; ++it)

struct Position {
	Position() {}
	Position(size_t line, size_t column): line(line), column(column) {}
	size_t line;
	size_t column;
	Position& operator+=(char c) {
		if(c != '\n') {
			column++;
		}
		else {
			column = 1;
			line++;
		}
		return self;
	}
	template<typename CharIterator>
	Position& Move(CharIterator begin, CharIterator end) {
		foreach(it, begin, end) {
			self += *it;
		}
		return self;
	}
};

#endif // ndef POSITION_H
