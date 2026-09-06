// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-trip test for Utilities::BitArray::serialize/deserialize, with a
// deliberately non-multiple-of-8 bit length. deserialize takes a BIT count
// while serialize emits getByteLength() bytes — this exercises both units
// on the same array so a bytes/bits mix-up at either end fails loudly.

#include <cppunit/extensions/HelperMacros.h>

#include "BitArray.h"

class BitArrayTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(BitArrayTest);
		CPPUNIT_TEST(testRoundTripOddBitLength);
		CPPUNIT_TEST(testByteLengthIsCeilOfBits);
	CPPUNIT_TEST_SUITE_END();

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

		CPPUNIT_ASSERT_EQUAL(bitCount, src.getBitLength());
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), src.getByteLength());

		unsigned char stream[2] = {0xFF, 0xFF};
		src.serialize(stream);

		Utilities::BitArray dst;
		dst.deserialize(stream, bitCount);

		CPPUNIT_ASSERT_EQUAL(bitCount, dst.getBitLength());
		CPPUNIT_ASSERT_EQUAL(src.getByteLength(), dst.getByteLength());
		for (size_t pos = 0; pos < bitCount; pos++)
			CPPUNIT_ASSERT_EQUAL(src.get(pos), dst.get(pos));
	}

	void testByteLengthIsCeilOfBits(void)
	{
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), Utilities::BitArray(0).getByteLength());
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), Utilities::BitArray(1).getByteLength());
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), Utilities::BitArray(8).getByteLength());
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), Utilities::BitArray(9).getByteLength());
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(BitArrayTest);
