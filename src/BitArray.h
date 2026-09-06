// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <valarray>

namespace Utilities
{
	//! A packed array of bits, stored 8 to a byte.
	//!
	//! Bit `pos` lives in byte `pos/8` at bit `pos%8`, counting from the
	//! least significant bit of each byte. This ordering is part of the wire
	//! format — OrderAlterateArea transmits a BitArray mask verbatim via
	//! serialize()/deserialize(), so both ends must agree on it. Do not
	//! reorder to MSB-first: it would silently corrupt masks between a
	//! patched and an unpatched peer rather than fail loudly.
	class BitArray
	{
	private:
		static constexpr size_t BITS_PER_BYTE = 8;

		std::valarray<unsigned char> values;
		size_t bitLength;

		//! Number of whole bytes needed to hold `v` bits, i.e. ceil(v/8).
		size_t bitToByte(size_t v) const;
		void assertPos(size_t pos) const;

	public:
		BitArray() { bitLength = 0; }
		BitArray(size_t size, bool defaultValue = false);
		void resize(size_t size, bool defaultValue = false);
		size_t getBitLength(void) const { return bitLength; }
		size_t getByteLength(void) const { return values.size(); }
		void set(size_t pos, bool value);
		bool get(size_t pos) const;
		//! Writes getByteLength() == ceil(getBitLength()/8) BYTES to `stream`.
		//! The bit length itself is not written — the caller must transmit it
		//! out-of-band and pass it back to deserialize() as `bitCount`.
		void serialize(unsigned char *stream) const;
		//! Copies ceil(bitCount/8) bytes from `stream` into the internal
		//! buffer and sets the bit length to `bitCount`. `bitCount` is a BIT
		//! count, not a byte count — do not pass getByteLength() here.
		//! Performs no bound check on `stream` — the caller must guarantee
		//! that at least ceil(bitCount/8) bytes are readable. See BH-195.
		void deserialize(const unsigned char *stream, size_t bitCount);
	};
}
