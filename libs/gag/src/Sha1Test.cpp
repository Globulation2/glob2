// SPDX-License-Identifier: GPL-3.0-or-later
//
// libgag's SHA-1 (save digests, BinaryOutputStream::enableSHA1) against the FIPS PUB
// 180-1 test vectors, fed both in one call and byte by byte.

#include "Glob2Test.h"

#include "Sha1.h"

#include <cstdio>
#include <string>

namespace
{
	std::string hex(const unsigned char digest[SHA_DIGEST_LENGTH])
	{
		std::string out;
		char byte[3];
		for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
		{
			std::snprintf(byte, sizeof(byte), "%02X", digest[i]);
			out += byte;
		}
		return out;
	}

	std::string digestOf(const std::string& message, bool byteByByte, int repeat = 1)
	{
		SHA1_CTX context;
		SHA1Init(&context);
		const unsigned char* data = reinterpret_cast<const unsigned char*>(message.data());
		for (int r = 0; r < repeat; r++)
		{
			if (byteByByte)
				for (size_t i = 0; i < message.size(); i++)
					SHA1Update(&context, data + i, 1);
			else
				SHA1Update(&context, data, static_cast<uint32_t>(message.size()));
		}
		unsigned char digest[SHA_DIGEST_LENGTH];
		SHA1Final(digest, &context);
		return hex(digest);
	}
}

TEST_SUITE("Sha1")
{
	TEST_CASE("FIPS 180-1 vectors")
	{
		for (bool byteByByte : {false, true})
		{
			CHECK(digestOf("abc", byteByByte) == "A9993E364706816ABA3E25717850C26C9CD0D89D");
			CHECK(digestOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", byteByByte)
				== "84983E441C3BD26EBAAE4AA1F95129E5E54670F1");
			CHECK(digestOf("", byteByByte) == "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
		}
		CHECK(digestOf(std::string(1000, 'a'), false, 1000) == "34AA973CD4C4DAA4F61EEB2BDBAD27316534016F");
	}
}
