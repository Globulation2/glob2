// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <BinaryStream.h>
#include <DeferredStream.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

// Lossless, bounded blocks. Callers supply the checked logical element count.
// All arithmetic is unsigned at the wire width, including wraparound deltas.
// Each block has a one-byte tag and a big-endian uint32 payload length:
//   0: raw big-endian words; 1: one repeated word; 2: first word plus deltas.
// Delta tokens are canonical unsigned base-128 varints of width-specific zigzag
// differences. Token zero is reserved for a positive uint32 repeat count.
// The logical element count is supplied by the containing schema, not the block.
// These rules are the format-128 wire contract; changing them needs a version gate.
namespace GAGCore::PackedArray
{
constexpr size_t blockSize = 4096;
enum Encoding : Uint8
{
	Raw = 0,
	Constant = 1,
	Delta = 2
};
inline void fail()
{
	throw std::ios_base::failure("Invalid packed array");
}
inline bool binary(OutputStream *s)
{
	return dynamic_cast<BinaryOutputStream *>(s) != nullptr;
}
inline bool binary(InputStream *s)
{
	return dynamic_cast<BinaryInputStream *>(s) != nullptr;
}

template <class U> void word(std::vector<Uint8> &out, U v)
{
	for (size_t b = sizeof(U); b; --b)
		out.push_back(Uint8(v >> ((b - 1) * 8)));
}
template <class U> U readWord(const std::vector<Uint8> &in, size_t &p)
{
	if (in.size() - p < sizeof(U))
		fail();
	U v = 0;
	for (size_t b = 0; b < sizeof(U); ++b)
		v = U((v << 8) | in[p++]);
	return v;
}
template <class U> void varint(std::vector<Uint8> &out, U v)
{
	while (v >= 128)
	{
		out.push_back(Uint8(v) | 128);
		v >>= 7;
	}
	out.push_back(Uint8(v));
}
template <class U> U readVarint(const std::vector<Uint8> &in, size_t &p)
{
	U v = 0;
	constexpr unsigned bits = sizeof(U) * 8;
	for (unsigned shift = 0; shift < bits; shift += 7)
	{
		if (p == in.size())
			fail();
		const Uint8 b = in[p++];
		if (shift + 7 > bits && (b & 127) >= (1u << (bits - shift)))
			fail();
		v |= U(U(b & 127) << shift);
		if (!(b & 128))
		{
			if (shift && b == 0)
				fail();
			return v;
		}
	}
	fail();
	return 0;
}

template <class U, class Get> void writeImmediate(OutputStream *s, size_t count, Get get)
{
	static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool>);
	std::array<U, blockSize> values;
	std::vector<Uint8> delta, payload;
	delta.reserve(std::min(count, blockSize) * (sizeof(U) + 1));
	payload.reserve(std::min(count, blockSize) * sizeof(U));
	for (size_t off = 0; off < count; off += blockSize)
	{
		const size_t n = std::min(blockSize, count - off);
		for (size_t i = 0; i < n; ++i)
			values[i] = U(get(off + i));
		const bool constant = std::all_of(values.begin() + 1, values.begin() + n,
										  [&](U v) { return v == values[0]; });
		Uint8 mode = Raw;
		payload.clear();
		if (constant && n > 1)
		{
			mode = Constant;
			word(payload, values[0]);
		}
		else
		{
			delta.clear();
			word(delta, values[0]);
			for (size_t i = 1; i < n;)
			{
				const U difference = U(values[i] - values[i - 1]);
				if (!difference)
				{
					size_t end = i + 1;
					while (end < n && values[end] == values[i - 1])
						++end;
					delta.push_back(0);
					varint(delta, Uint32(end - i));
					i = end;
				}
				else
				{
					// Treat the high bit as the sign without signed overflow or shifts.
					const U zigzag =
						U(U(difference << 1) ^ U(U(0) - U(difference >> (sizeof(U) * 8 - 1))));
					varint(delta, zigzag);
					++i;
				}
			}
			if (delta.size() < n * sizeof(U))
			{
				mode = Delta;
				payload.assign(delta.begin(), delta.end());
			}
			else
				for (size_t i = 0; i < n; ++i)
					word(payload, values[i]);
		}
		s->writeUint8(mode, "encoding");
		s->writeUint32(Uint32(payload.size()), "bytes");
		s->write(payload.data(), payload.size(), "payload");
	}
}

// A cheap fixed snapshot representation limits capture memory without running
// the on-disk codec's predictor/size search on the simulation thread.
// This is private in-memory storage, not a second save format. Getters must be
// pure: the raw fallback reads them again before capture returns.
template <class U> struct Capture
{
	std::vector<Uint8> bytes;
	size_t count = 0;
	bool raw = false;
	template <class Get> Capture(size_t n, Get get) : count(n)
	{
		bytes.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			const U value = U(get(i));
			const U z = U(U(value << 1) ^ U(U(0) - U(value >> (sizeof(U) * 8 - 1))));
			varint(bytes, z);
		}
		if (bytes.size() > n * sizeof(U))
		{
			raw = true;
			bytes.clear();
			for (size_t i = 0; i < n; ++i)
				word(bytes, U(get(i)));
		}
		bytes.shrink_to_fit();
	}
	CooperativeTask restoreTask(std::vector<U> &values) const
	{
		values.resize(count);
		size_t p = 0;
		for (size_t i = 0; i < count; ++i)
		{
			if (raw)
				values[i] = readWord<U>(bytes, p);
			else
			{
				const U z = readVarint<U>(bytes, p);
				values[i] = U(U(z >> 1) ^ U(U(0) - U(z & 1)));
			}
			if (i % 16384 == 16383)
				co_await CooperativeTask::checkpoint("Restoring snapshot batch");
		}
		co_return true;
	}
	std::vector<U> restore() const
	{
		std::vector<U> values(count);
		size_t p = 0;
		for (auto &value : values)
		{
			if (raw)
				value = readWord<U>(bytes, p);
			else
			{
				const U z = readVarint<U>(bytes, p);
				value = U(U(z >> 1) ^ U(U(0) - U(z & 1)));
			}
		}
		return values;
	}
};

template <class U, class Get> void write(OutputStream *s, size_t count, Get get)
{
	if (auto *deferred = dynamic_cast<DeferredStream *>(s))
	{
		for (size_t off = 0; off < count; off += blockSize)
		{
			const size_t n = std::min(blockSize, count - off);
			Capture<U> copy(n, [&](size_t i) { return get(off + i); });
			deferred->defer(
				[copy = std::move(copy)](OutputStream *out)
				{
					const auto values = copy.restore();
					writeImmediate<U>(out, values.size(), [&](size_t i) { return values[i]; });
				});
		}
		return;
	}
	writeImmediate<U>(s, count, get);
}

template <class U, class Set> void read(InputStream *s, size_t count, Set set)
{
	static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool>);
	BinaryInputStream::CheckedReads checked(s);
	std::vector<Uint8> payload;
	for (size_t off = 0; off < count; off += blockSize)
	{
		const size_t n = std::min(blockSize, count - off);
		const auto mode = s->readUint8("encoding");
		const auto bytes = s->readUint32("bytes");
		if (mode > Delta || bytes < sizeof(U) || bytes > n * sizeof(U) ||
			(mode == Raw && bytes != n * sizeof(U)) || (mode == Constant && bytes != sizeof(U)))
			fail();
		payload.resize(bytes);
		s->read(payload.data(), bytes, "payload");
		size_t p = 0;
		U previous = readWord<U>(payload, p);
		set(off, previous);
		for (size_t i = 1; i < n;)
		{
			if (mode == Raw)
			{
				previous = readWord<U>(payload, p);
				set(off + i++, previous);
			}
			else if (mode == Constant)
				set(off + i++, previous);
			else
			{
				const U z = readVarint<U>(payload, p);
				if (!z)
				{
					const auto run = readVarint<Uint32>(payload, p);
					if (!run || run > n - i)
						fail();
					for (size_t end = i + run; i < end; ++i)
						set(off + i, previous);
				}
				else
				{
					const U difference = U(U(z >> 1) ^ U(U(0) - U(z & 1)));
					previous = U(previous + difference);
					set(off + i++, previous);
				}
			}
		}
		if (p != payload.size())
			fail();
	}
}
} // namespace GAGCore::PackedArray
