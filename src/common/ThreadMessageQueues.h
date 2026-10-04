// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <queue>

/// Bidirectional message-queue plumbing shared by worker-thread classes.
/// The subclass owns `incoming` (main thread writes via sendMessage, worker drains).
/// `outgoing` is a reference to a queue owned by the main thread (worker writes via
/// sendToMainThread, main thread drains).
template <typename MessageT>
class ThreadMessageQueues
{
public:
	using MessagePtr = std::shared_ptr<MessageT>;
	using MessageQueue = std::queue<MessagePtr>;

	ThreadMessageQueues(MessageQueue& outgoing, std::recursive_mutex& outgoingMutex)
		: outgoing(outgoing), outgoingMutex(outgoingMutex), hasExited(false)
	{
	}

	void sendMessage(MessagePtr message)
	{
		std::lock_guard<std::recursive_mutex> lock(incomingMutex);
		incoming.push(message);
	}

	bool hasThreadExited() const
	{
		return hasExited;
	}

protected:
	void sendToMainThread(MessagePtr message)
	{
		std::lock_guard<std::recursive_mutex> lock(outgoingMutex);
		outgoing.push(message);
	}

	MessageQueue incoming;
	MessageQueue& outgoing;
	std::recursive_mutex incomingMutex;
	std::recursive_mutex& outgoingMutex;
	bool hasExited;
};
