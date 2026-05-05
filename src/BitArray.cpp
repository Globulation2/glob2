// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BitArray.h"
#include <assert.h>
#include <algorithm>
#include <iostream>

namespace Utilities
{
	BitArray::BitArray(size_t size, bool defaultValue)
	{
		resize(size, defaultValue);
	}
	
	void BitArray::assertPos(size_t pos) const
	{
		size_t wordPos = pos / 8;
		if (wordPos >= values.size())
		{
			std::cerr << "BitArray::assertPos(" << pos << ") : index out of bounds. Max size is " << bitLength << std::endl;
			assert(false);
		}
	}
	
	void BitArray::resize(size_t size, bool defaultValue)
	{
		bitLength = size;
		if (defaultValue)
			values.resize(bitToByte(size), 1);
		else
			values.resize(bitToByte(size), 0);
	}
	
	size_t BitArray::bitToByte(size_t v) const
	{
		if (v&0x7)
			return (v>>3)+1;
		else
			return v>>3;
	}
	
	void BitArray::set(size_t pos, bool value)
	{
		assertPos(pos);
		
		size_t wordPos = pos / 8;
		size_t bitPos = pos % 8;
		
		if (value)
			values[wordPos] |= (1<<bitPos);
		else
			values[wordPos] &= ~(1<<bitPos);
	}
	
	bool BitArray::get(size_t pos) const
	{
		assertPos(pos);
		
		size_t wordPos = pos / 8;
		size_t bitPos = pos % 8;

		return (values[wordPos] & (1<<bitPos)) != 0;
	}
	
	void BitArray::serialize(unsigned char *stream) const
	{
		size_t l = values.size();
		std::copy(&values[0], l + &values[0], stream);
	}
	
	void BitArray::deserialize(const unsigned char *stream, size_t size)
	{
		bitLength = size;
		values.resize(bitToByte(size));
		std::copy(stream, stream+values.size(), &values[0]);
	}
}
