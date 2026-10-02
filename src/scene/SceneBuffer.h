// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Lock-free triple buffer handing whole values from one producer thread to one
// consumer thread. The producer fills back(), then publish() makes it the newest
// value; the consumer's acquire() takes the newest published value. Neither side
// ever waits for the other or sees a value the other is still using. Values are
// reused, so a producer can rebuild into the storage of an older value without
// reallocating. Exactly one producer thread and one consumer thread.
template <class T> class SceneBuffer
{
public:
	//! The value the producer is building. Stable until publish().
	T &back() { return slots[backIndex]; }

	//! Make back() the newest value and hand the producer an unused slot.
	void publish()
	{
		const std::uint8_t previous = middle.exchange(std::uint8_t(backIndex | Fresh), std::memory_order_acq_rel);
		backIndex = previous & Index;
	}

	//! True while the consumer has not taken the last published value yet.
	bool pending() const { return middle.load(std::memory_order_acquire) & Fresh; }

	//! Take the newest published value, if one arrived since the last call.
	//! Returns true when current() changed.
	bool acquire()
	{
		if (!(middle.load(std::memory_order_acquire) & Fresh))
			return false;
		const std::uint8_t previous = middle.exchange(frontIndex, std::memory_order_acq_rel);
		frontIndex = previous & Index;
		hasFront = true;
		return true;
	}

	//! The consumer's current value; valid once acquire() returned true.
	const T &current() const { return slots[frontIndex]; }
	//! False until the consumer acquired a first value.
	bool ready() const { return hasFront; }

private:
	static constexpr std::uint8_t Index = 0x3, Fresh = 0x4;
	std::array<T, 3> slots{};
	std::uint8_t backIndex = 0;              // producer only
	std::uint8_t frontIndex = 1;             // consumer only
	bool hasFront = false;                   // consumer only
	std::atomic<std::uint8_t> middle{2};     // shared: index plus Fresh flag
};
