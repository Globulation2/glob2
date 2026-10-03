// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <PackedArray.h>
#include <string>

namespace GAGCore::PackedRecords
{
// Fixed-width records made of network-order 32-bit words. This keeps the
// existing field serializers/validators authoritative and bounds transpose
// scratch space to 256 records, independent of the length of the history.
constexpr size_t batchSize = 256;
// Largest supported record: telemetry's tick/availability words plus 4,096 fields,
// each containing 64-bit data, a 32-bit update tick and a 32-bit validity flag.
// Bound scratch allocation even for bad input.
constexpr size_t maxRowBytes = 8 + 16 * 4096;
template <class WriteOne>
void writeImmediate(OutputStream *stream, size_t count, size_t rowBytes, WriteOne writeOne)
{
	if (!rowBytes || rowBytes % 4 || rowBytes > maxRowBytes)
		PackedArray::fail();
	for (size_t off = 0; off < count; off += batchSize)
	{
		const size_t n = std::min(batchSize, count - off);
		auto *memory = new MemoryStreamBackend;
		BinaryOutputStream rows(memory);
		for (size_t i = 0; i < n; ++i)
			writeOne(&rows, off + i);
		const auto bytes = memory->takeContents();
		if (bytes.size() != n * rowBytes)
			PackedArray::fail();
		for (size_t c = 0; c < rowBytes; c += 4)
			PackedArray::write<Uint32>(stream, n,
									   [&](size_t i)
									   {
										   const auto *p = reinterpret_cast<const Uint8 *>(
											   bytes.data() + i * rowBytes + c);
										   return Uint32(p[0]) << 24 | Uint32(p[1]) << 16 |
												  Uint32(p[2]) << 8 | Uint32(p[3]);
									   });
	}
}
template <class WriteOne>
void write(OutputStream *stream, size_t count, size_t rowBytes, WriteOne writeOne)
{
	if (!rowBytes || rowBytes % 4 || rowBytes > maxRowBytes || count > SIZE_MAX / rowBytes)
		PackedArray::fail();
	if (auto *deferred = dynamic_cast<DeferredStream *>(stream))
	{
		// Copy one bounded batch; both transpose and packing run after capture.
		for (size_t off = 0; off < count; off += batchSize)
		{
			const size_t n = std::min(batchSize, count - off);
			auto *memory = new MemoryStreamBackend;
			BinaryOutputStream rows(memory);
			for (size_t i = 0; i < n; ++i)
				writeOne(&rows, off + i);
			const auto bytes = memory->takeContents();
			if (bytes.size() != n * rowBytes)
				PackedArray::fail();
			PackedArray::Capture<Uint32> copy(bytes.size() / 4,
											  [&](size_t i)
											  {
												  const auto *p = reinterpret_cast<const Uint8 *>(
													  bytes.data() + i * 4);
												  return Uint32(p[0]) << 24 | Uint32(p[1]) << 16 |
														 Uint32(p[2]) << 8 | Uint32(p[3]);
											  });
			deferred->deferTask(
				[copy = std::move(copy), n, rowBytes](OutputStream *out) -> CooperativeTask
				{
					std::vector<Uint32> values;
					if (!(co_await copy.restoreTask(values)))
						co_return false;
					for (size_t c = 0; c < rowBytes / 4; ++c)
					{
						PackedArray::writeImmediate<Uint32>(
							out, n, [&](size_t i) { return values[i * rowBytes / 4 + c]; });
						if (c % 32 == 31)
							co_await CooperativeTask::checkpoint("Encoding history columns");
					}
					co_return true;
				});
		}
		return;
	}
	writeImmediate(stream, count, rowBytes, writeOne);
}
template <class ReadOne>
void read(InputStream *stream, size_t count, size_t rowBytes, ReadOne readOne)
{
	if (!rowBytes || rowBytes % 4 || rowBytes > maxRowBytes)
		PackedArray::fail();
	for (size_t off = 0; off < count; off += batchSize)
	{
		const size_t n = std::min(batchSize, count - off);
		std::string bytes(n * rowBytes, '\0');
		for (size_t c = 0; c < rowBytes; c += 4)
			PackedArray::read<Uint32>(stream, n,
									  [&](size_t i, Uint32 v)
									  {
										  for (unsigned b = 0; b < 4; ++b)
											  bytes[i * rowBytes + c + b] = char(v >> (24 - b * 8));
									  });
		BinaryInputStream rows(new MemoryStreamBackend(std::move(bytes)));
		rows.seekFromStart(0);
		BinaryInputStream::CheckedReads checked(&rows);
		for (size_t i = 0; i < n; ++i)
			readOne(&rows, off + i);
		if (rows.getPosition() != n * rowBytes)
			PackedArray::fail();
	}
}
} // namespace GAGCore::PackedRecords
