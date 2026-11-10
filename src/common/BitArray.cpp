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

	void BitArray::reportInvalidPos(size_t pos) const
	{
		std::cerr << "BitArray::assertPos(" << pos << ") : index out of bounds. Max size is " << bitLength << std::endl;
		assert(false);
	}

	void BitArray::resize(size_t size, bool defaultValue)
	{
		bitLength = size;
		// Every byte becomes 1 or 0 (not 0xFF), discarding old contents, as
		// std::valarray::resize did; masks on the wire depend on it.
		values.assign(bitToByte(size), defaultValue ? 1 : 0);
	}
	
	size_t BitArray::bitToByte(size_t v) const
	{
		if (v % BITS_PER_BYTE)
			return (v / BITS_PER_BYTE) + 1;
		else
			return v / BITS_PER_BYTE;
	}

	void BitArray::serialize(unsigned char *stream) const
	{
		const size_t l = values.size();
		if (l == 0)
			return;
		std::copy(values.begin(), values.end(), stream);
	}
	
	void BitArray::deserialize(const unsigned char *stream, size_t bitCount)
	{
		bitLength = bitCount;
		values.assign(bitToByte(bitCount), 0);
		std::copy(stream, stream+values.size(), values.begin());
	}
}
