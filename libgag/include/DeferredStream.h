// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <BinaryStream.h>
#include <ChunkedStreamBackend.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <map>
#include <CooperativeTask.h>

namespace GAGCore
{
// The capture owns all inputs. Encoding callbacks must capture values, never live
// engine objects. Placeholder positions are relocated after worker finalization.
class DeferredStream : public BinaryOutputStream
{
  public:
	struct Task
	{
		size_t offset;
		std::function<CooperativeTask(OutputStream *)> encode;
	};
	struct Snapshot
	{
		ChunkedBuffer literals;
		std::vector<Task> tasks;
		std::map<size_t, size_t> pointers;
		std::vector<std::pair<size_t, size_t>> positions;
		// Valid after finishTask: each boundary maps a captured byte position
		// to its final position. Literal spans retain their relative offsets.
		size_t relocate(size_t old) const
		{
			auto it =
				std::upper_bound(positions.begin(), positions.end(), old,
								 [](size_t value, const auto &item) { return value < item.first; });
			if (it == positions.begin())
				return old;
			--it;
			return it->second + (old - it->first);
		}
		CooperativeTask finishTask(ChunkedBuffer &result)
		{
			auto *backend = new ChunkedStreamBackend;
			BinaryOutputStream out(backend);
			size_t start = 0;
			positions = {{0, 0}};
			for (auto &task : tasks)
			{
				while (start < task.offset)
				{
					const size_t n = std::min<size_t>(65536, task.offset - start);
					literals.forEachRange(start, n, [&](const auto *p, size_t len)
										  { out.write(p, len, "literal"); });
					start += n;
					co_await CooperativeTask::checkpoint("Copying save");
				}
				positions.emplace_back(task.offset, out.getPosition());
				if (!(co_await task.encode(&out)))
					co_return false;
				// The coroutine lambda borrows its closure until the await
				// completes. Only now may its captured input be released.
				task.encode = {};
				co_await CooperativeTask::checkpoint("Encoding save");
				start = task.offset + 1;
				positions.emplace_back(start, out.getPosition());
			}
			while (start < literals.size())
			{
				const size_t n = std::min<size_t>(65536, literals.size() - start);
				literals.forEachRange(start, n, [&](const auto *p, size_t len)
									  { out.write(p, len, "literal"); });
				start += n;
				co_await CooperativeTask::checkpoint("Copying save");
			}
			for (const auto &[offset, target] : pointers)
			{
				const size_t position = relocate(offset);
				const size_t destination = relocate(target);
				if (position > size_t(std::numeric_limits<int>::max()) ||
					destination > std::numeric_limits<Uint32>::max())
					throw std::length_error("Deferred stream offset exceeds wire range");
				out.seekFromStart(static_cast<int>(position));
				out.writeUint32(static_cast<Uint32>(destination), "offset");
			}
			literals = ChunkedBuffer{};
			result = backend->takeContents();
			co_return true;
		}
		ChunkedBuffer finish()
		{
			ChunkedBuffer result;
			if (!finishTask(result).run())
				throw std::runtime_error("Snapshot encoding failed");
			return result;
		}
	};
	DeferredStream() : BinaryOutputStream(new ChunkedStreamBackend) {}
	void defer(std::function<void(OutputStream *)> encode)
	{
		deferTask(
			[encode = std::move(encode)](OutputStream *out) -> CooperativeTask
			{
				encode(out);
				co_return true;
			});
	}
	// Deferred fields are appended in stream order. Seeks are reserved for
	// fixed-size literal backpatches; an encoder must not reference live state.
	void deferTask(std::function<CooperativeTask(OutputStream *)> encode)
	{
		tasks.push_back({getPosition(), std::move(encode)});
		writeUint8(0, "placeholder");
	}
	// Backpatches may rewrite the same offset. Keep its latest captured target;
	// ordinary integers, even ones with the same field name, are not relocated.
	void writeOffset32(Uint32 value, const std::string name) override
	{
		pointers[getPosition()] = value;
		BinaryOutputStream::writeUint32(value, name);
	}
	Snapshot takeSnapshot()
	{
		return {static_cast<ChunkedStreamBackend *>(backend)->takeContents(),
				std::move(tasks),
				std::move(pointers),
				{}};
	}

  private:
	std::vector<Task> tasks;
	std::map<size_t, size_t> pointers;
};
} // namespace GAGCore
