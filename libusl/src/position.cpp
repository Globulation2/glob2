#include "position.h"

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

void Position::move(const char *text, size_t length)
{
	for (size_t i = 0; i < length; i++)
		*this += text[i];
}
