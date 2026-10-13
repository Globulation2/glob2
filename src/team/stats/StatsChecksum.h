// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <BinaryStream.h>
#include <stdexcept>

// Hash canonical stream bytes without allocating or hashing object padding.
class StatsChecksumBackend final : public GAGCore::StreamBackend
{
  public:
	Uint32 value = 2166136261u;
	size_t position = 0;
	void write(const void *data, size_t size) override
	{
		const auto *bytes = static_cast<const unsigned char *>(data);
		for (size_t i = 0; i < size; ++i)
			value = (value ^ bytes[i]) * 16777619u;
		position += size;
	}
	void flush() override {}
	void read(void *, size_t) override { throw std::logic_error("Checksum is write-only"); }
	void putc(int c) override
	{
		const Uint8 b = c;
		write(&b, 1);
	}
	int getChar() override { throw std::logic_error("Checksum is write-only"); }
	void seekFromStart(int) override { throw std::logic_error("Checksum cannot seek"); }
	void seekFromEnd(int) override { throw std::logic_error("Checksum cannot seek"); }
	void seekRelative(int) override { throw std::logic_error("Checksum cannot seek"); }
	size_t getPosition() override { return position; }
	bool isEndOfStream() override { return false; }
	bool isValid() override { return true; }
};
