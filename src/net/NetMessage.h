// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <memory>
#include <string>

#include "Stream.h"
#include "NetMessageType.h"

/// A message carried by the net engine. A message has a type tag (from
/// NetMessageType) and a body. The base class provides a static factory that
/// reads a message from a stream and returns a shared_ptr to the appropriate
/// derived class; callers can dispatch using getMessageType() + dynamic_cast.
class NetMessage
{
public:
	virtual ~NetMessage() {}

	/// Returns the message's NetMessageType tag.
	virtual Uint8 getMessageType() const = 0;

	/// Reads a message off the stream and returns the appropriate derived class.
	static std::shared_ptr<NetMessage> getNetMessage(GAGCore::InputStream* stream);

	/// Encodes this message's body to the stream in its serialized form.
	virtual void encodeData(GAGCore::OutputStream* stream) const = 0;

	/// Decodes this message's body from the stream. The leading type byte has
	/// already been consumed by getNetMessage and can be ignored here.
	virtual void decodeData(GAGCore::InputStream* stream) = 0;

	/// Human-readable representation, for debugging and logging.
	virtual std::string format() const = 0;

	/// Compares two messages. Derived classes must check that rhs casts to
	/// their concrete type before comparing internal data.
	virtual bool operator==(const NetMessage& rhs) const = 0;

	/// Provided for convenience; derived classes may override for efficiency.
	virtual bool operator!=(const NetMessage& rhs) const;
};
