// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <StreamBackend.h>
#include <iostream>
#include "SDL_net.h"

namespace GAGCore
{

	MemoryStreamBackend::MemoryStreamBackend(const void *data, const size_t size)
	{
		index = 0;
		if (size)
			write(data, size);
	}
	
	void MemoryStreamBackend::write(const void *data, const size_t size)
	{
		const char *_data = static_cast<const char *>(data);
		if ((index + size) > buffer.size())
			buffer.resize(index + size);
		std::copy(_data, _data+size, buffer.begin()+index);
		index += size;
	}
	
	void MemoryStreamBackend::read(void *data, size_t size)
	{
		char *_data = static_cast<char *>(data);
		if (index+size > buffer.size())
		{
			// overread, read 0
			std::fill(_data, _data+size, 0);
		}
		else
		{
			std::copy(buffer.data() + index, buffer.data() + index + size, _data);
			index += size;
		}
	}
	
	void MemoryStreamBackend::putc(int c)
	{
		Uint8 ch = c;
		write(&ch, 1);
	}
	
	//TODO: Why is the Uint8 ch returned as int?
	int MemoryStreamBackend::getChar(void)
	{
		Uint8 ch;
		read(&ch, 1);
		return ch;
	}
	
	void MemoryStreamBackend::seekFromStart(int displacement)
	{
		index = std::min(static_cast<size_t>(displacement), buffer.size());
	}
	
	void MemoryStreamBackend::seekFromEnd(int displacement)
	{
		index = static_cast<size_t>(std::max(0, static_cast<int>(buffer.size()) - displacement));
	}
	
	void MemoryStreamBackend::seekRelative(int displacement)
	{
		int newIndex = static_cast<int>(index) + displacement;
		newIndex = std::max(newIndex, 0);
		newIndex = std::min(newIndex, static_cast<int>(buffer.size()));
		index = static_cast<size_t>(newIndex);
	}
	
	size_t MemoryStreamBackend::getPosition(void)
	{
		return index;
	}
	
	bool MemoryStreamBackend::isEndOfStream(void)
	{
		return index >= buffer.size();
	}
}
