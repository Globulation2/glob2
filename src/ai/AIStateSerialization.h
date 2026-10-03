// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <BinaryStream.h>
#include <TextStream.h>
#include <charconv>
#include <stdexcept>

namespace AIStateSerialization
{
// Legacy text numeric reads accept partial values and leave missing values
// uninitialized. Continuation fields must reject either case explicitly.
inline Sint32 readSint32(GAGCore::InputStream *stream, const char *name)
{
	if (dynamic_cast<GAGCore::TextInputStream *>(stream))
	{
		const auto encoded = stream->readText(name);
		Sint32 value;
		const auto result = std::from_chars(encoded.data(), encoded.data() + encoded.size(), value);
		if (result.ec != std::errc{} || result.ptr != encoded.data() + encoded.size())
			throw std::runtime_error(std::string("Invalid AI continuation field: ") + name);
		return value;
	}
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	return stream->readSint32(name);
}
}
