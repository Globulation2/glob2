#include "position.h"

bool Position::operator<(const Position& that) const
{
	if (this->line < that.line)
		return true;
	else if (this->line > that.line)
		return false;
	else
		return this->column < that.column;
}

void Position::operator+=(char c)
{
	if(c != '\n')
	{
		column++;
	}
	else
	{
		column = 1;
		line++;
	};
}

void Position::move(const std::string text, size_t length)
{
	for (size_t i = 0; i < length; i++)
		*this += text[i];
}
