#ifndef ERROR_H
#define ERROR_H

#include "position.h"

#include <stdexcept>

struct Exception: std::runtime_error
{
	Exception(const Position& position, const std::string& message): std::runtime_error(message), position(position) {}
	~Exception() throw() {}
	Position position;
};

#endif // ndef ERROR_H
