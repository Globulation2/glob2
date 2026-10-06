// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-trip test for Utilities::BitArray::serialize/deserialize, with a
// deliberately non-multiple-of-8 bit length. deserialize takes a BIT count
// while serialize emits getByteLength() bytes — this exercises both units
// on the same array so a bytes/bits mix-up at either end fails loudly.

#include "Glob2Test.h"

#include "BitArray.h"

class BitArrayTest
{
public:

protected:
	void testRoundTripOddBitLength(void)
	{
		const size_t bitCount = 13; // spans 2 bytes, 3 bits used in the last
		Utilities::BitArray src(bitCount);
		src.set(0, true);
		src.set(5, true);
		src.set(7, true);  // last bit of byte 0
		src.set(8, true);  // first bit of byte 1
		src.set(12, true); // last valid bit

		CHECK_EQ(bitCount, src.getBitLength());
		CHECK_EQ(static_cast<size_t>(2), src.getByteLength());

		unsigned char stream[2] = {0xFF, 0xFF};
		src.serialize(stream);
		CHECK_EQ(stream[0], 0xA1);
		CHECK_EQ(stream[1], 0x11);

		Utilities::BitArray dst;
		dst.deserialize(stream, bitCount);

		CHECK_EQ(bitCount, dst.getBitLength());
		CHECK_EQ(src.getByteLength(), dst.getByteLength());
		for (size_t pos = 0; pos < bitCount; pos++)
			CHECK_EQ(src.get(pos), dst.get(pos));
	}

	void testByteLengthIsCeilOfBits(void)
	{
		CHECK_EQ(static_cast<size_t>(0), Utilities::BitArray(0).getByteLength());
		CHECK_EQ(static_cast<size_t>(1), Utilities::BitArray(1).getByteLength());
		CHECK_EQ(static_cast<size_t>(1), Utilities::BitArray(8).getByteLength());
		CHECK_EQ(static_cast<size_t>(2), Utilities::BitArray(9).getByteLength());
	}
};
TEST_SUITE("BitArray")
{
	TEST_CASE_FIXTURE(BitArrayTest, "RoundTripOddBitLength") { testRoundTripOddBitLength(); }
	TEST_CASE_FIXTURE(BitArrayTest, "ByteLengthIsCeilOfBits") { testByteLengthIsCeilOfBits(); }

	TEST_CASE("empty bit arrays serialize and reset without a buffer")
	{
		Utilities::BitArray empty(0);
		empty.serialize(nullptr);
		empty.deserialize(nullptr, 0);
		CHECK_EQ(empty.getByteLength(), 0);
		CHECK_EQ(empty.getBitLength(), 0);

		Utilities::BitArray populated(9);
		populated.set(8, true);
		populated.deserialize(nullptr, 0);
		CHECK_EQ(populated.getByteLength(), 0);
		CHECK_EQ(populated.getBitLength(), 0);
		populated.serialize(nullptr);
	}
}
