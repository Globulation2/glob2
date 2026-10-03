// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Length-prefixed frames over an ordered byte stream: a big-endian u16 payload
// length, then the payload. The one implementation behind every such stream:
// NetConnection (NetMessage), LanLink (LAN room and turn messages), the
// client's RelayTransport and the relay's connections (turn messages; the
// frames are carried inside WebSocket messages, so turn traffic is framed the
// same way over TCP and WebSocket). Header-only: the relay links it without the
// game's network code.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace NetFrame
{
	/// The longest payload a frame can carry.
	constexpr std::size_t MAX_PAYLOAD = 0xFFFF;
	/// Bytes of the length prefix.
	constexpr std::size_t HEADER = 2;

	/// Appends one frame carrying `size` bytes at `data`. False (and nothing
	/// appended) when the payload is longer than MAX_PAYLOAD.
	inline bool append(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t size)
	{
		if (size > MAX_PAYLOAD)
			return false;
		out.reserve(out.size() + HEADER + size);
		out.push_back(static_cast<std::uint8_t>(size >> 8));
		out.push_back(static_cast<std::uint8_t>(size & 0xFF));
		out.insert(out.end(), data, data + size);
		return true;
	}

	/// One frame carrying `payload`; empty when the payload is too long.
	inline std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& payload)
	{
		std::vector<std::uint8_t> frame;
		if (!append(frame, payload.data(), payload.size()))
			frame.clear();
		return frame;
	}

	/// Reassembles frames from received chunks of the stream. A zero-length
	/// frame is returned like any other (with an empty payload); streams that
	/// never carry one treat it as a protocol error.
	class Reader
	{
	public:
		/// Adds received bytes. False, adding nothing, when the unread bytes would
		/// then exceed `limit` (the stream is overflowing).
		bool append(const std::uint8_t* data, std::size_t size,
		            std::size_t limit = std::numeric_limits<std::size_t>::max())
		{
			if (size > limit - std::min(buffered(), limit))
				return false;
			if (offset > 0 && offset == buffer.size())
				clear();
			buffer.insert(buffer.end(), data, data + size);
			return true;
		}

		/// The next whole frame, without copying: `data` and `size` stay valid
		/// until the next call to any member.
		bool next(const std::uint8_t*& data, std::size_t& size)
		{
			compact();
			if (buffer.size() - offset < HEADER)
				return false;
			const std::size_t length = (std::size_t(buffer[offset]) << 8) | buffer[offset + 1];
			if (buffer.size() - offset - HEADER < length)
				return false;
			data = buffer.data() + offset + HEADER;
			size = length;
			offset += HEADER + length;
			return true;
		}

		/// The next whole frame's payload.
		bool next(std::vector<std::uint8_t>& payload)
		{
			const std::uint8_t* data = nullptr;
			std::size_t size = 0;
			if (!next(data, size))
				return false;
			payload.assign(data, data + size);
			return true;
		}

		/// Unread bytes (a partial frame, or frames not taken yet).
		std::size_t buffered() const { return buffer.size() - offset; }
		void clear()
		{
			buffer.clear();
			offset = 0;
		}

	private:
		// Drops the consumed prefix once it dominates, so the buffer stays
		// proportional to what is unread without moving bytes on every frame.
		void compact()
		{
			if (offset > 0 && offset == buffer.size())
				clear();
			else if (offset > 65536 && offset * 2 > buffer.size())
			{
				buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
				offset = 0;
			}
		}

		std::vector<std::uint8_t> buffer;
		std::size_t offset = 0;
	};
}
