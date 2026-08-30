// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <memory>
#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"

/// Client -> server: request a file (typically a map) by its fileID.
class NetRequestFile : public NetMessage
{
public:
	NetRequestFile();
	NetRequestFile(Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getFileID();
private:
	Uint16 fileID;
};

/// Server -> client: announces an upcoming file transfer (size + fileID).
class NetSendFileInformation : public NetMessage
{
public:
	NetSendFileInformation();
	NetSendFileInformation(Uint32 filesize, Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint32 getFileSize() const;
	Uint16 getFileID() const;
private:
	Uint32 size;
	Uint16 fileID;
};

/// One chunk of a streamed file transfer. Each message holds up to 4096 bytes
/// drained from the supplied input stream.
class NetSendFileChunk : public NetMessage
{
public:
	NetSendFileChunk();

	/// Reads from the stream until it ends or the chunk size is reached.
	NetSendFileChunk(std::shared_ptr<GAGCore::InputStream> stream, Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	const Uint8* getBuffer() const;
	Uint32 getChunkSize() const;
	Uint16 getFileID() const;
private:
	Uint32 size;
	Uint8 data[4096];
	Uint16 fileID;
};

/// Sender -> receiver: aborts an in-flight outbound file transfer.
class NetCancelSendingFile : public NetMessage
{
public:
	NetCancelSendingFile();
	NetCancelSendingFile(Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getFileID() const;
private:
	Uint16 fileID;
};

/// Receiver -> sender: aborts an in-flight inbound file transfer.
class NetCancelRecievingFile : public NetMessage
{
public:
	NetCancelRecievingFile();
	NetCancelRecievingFile(Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getFileID() const;
private:
	Uint16 fileID;
};
