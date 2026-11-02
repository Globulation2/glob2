// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Stream.h>
#include <bit>
#include <array>
#include <stdexcept>

namespace GAGCore
{
// FNV-1a over exactly the bytes BinaryOutputStream writes. Hash scalar values
// directly, avoiding a backend allocation, endian scratch writes and the
// per-scalar SHA/backend dispatch. Sections and names have no binary payload.
class ChecksumStream final : public OutputStream
{
	size_t position = 0;
	void byte(Uint8 v) { value = (value ^ v) * 16777619u; }
public:
	Uint32 value = 2166136261u;
	// Schema visitors can feed integers without constructing ignored names.
	void addUint32(Uint32 v) { byte(v>>24); byte(v>>16); byte(v>>8); byte(v); position+=4; }
	void write(const void* data, size_t size, std::string_view) override {
		for (size_t i=0;i<size;++i) byte(static_cast<const Uint8*>(data)[i]);
		position += size;
	}
	void writeUint8(Uint8 v, std::string_view) override { byte(v); ++position; }
	void writeSint8(Sint8 v, std::string_view n) override { writeUint8(Uint8(v),n); }
	void writeUint16(Uint16 v, std::string_view) override { byte(v>>8); byte(v); position+=2; }
	void writeSint16(Sint16 v, std::string_view n) override { writeUint16(Uint16(v),n); }
	void writeUint32(Uint32 v, std::string_view) override {
		addUint32(v);
	}
	void writeSint32(Sint32 v, std::string_view n) override { writeUint32(Uint32(v),n); }
	void writeFloat(float v, std::string_view n) override { writeUint32(std::bit_cast<Uint32>(v),n); }
	void writeDouble(double v, std::string_view n) override {
		// Preserve the existing binary format's native 32-bit-word order.
		for (auto word:std::bit_cast<std::array<Uint32,2>>(v)) writeUint32(word,n);
	}
	void writeText(const std::string& v, std::string_view n) override {
		writeUint32(v.size(),n); write(v.data(),v.size(),n);
	}
	void flush() override {}
	void writeEnterSection(std::string_view) override {}
	void writeEnterSection(unsigned) override {}
	void writeLeaveSection(size_t=1) override {}
	bool canSeek() override { return false; }
	void seekFromStart(int) override { throw std::logic_error("Checksum cannot seek"); }
	void seekFromEnd(int) override { throw std::logic_error("Checksum cannot seek"); }
	void seekRelative(int) override { throw std::logic_error("Checksum cannot seek"); }
	size_t getPosition() override { return position; }
	bool isEndOfStream() override { return false; }
	bool isValid() override { return true; }
};
}
