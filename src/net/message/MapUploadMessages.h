// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"
#include "YOGDownloadableMapInfo.h"

/// Client -> map-database server: request to upload a new map (carries the
/// map's metadata so the server can check policy/duplicates).
class NetRequestMapUpload : public NetMessage
{
public:
	NetRequestMapUpload();
	NetRequestMapUpload(YOGDownloadableMapInfo mapInfo);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGDownloadableMapInfo getMapInfo() const;
private:
	YOGDownloadableMapInfo mapInfo;
};

/// Server -> uploader: upload accepted; here is the fileID to push chunks to.
class NetAcceptMapUpload : public NetMessage
{
public:
	NetAcceptMapUpload();
	NetAcceptMapUpload(Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getFileID() const;
private:
	Uint16 fileID;
};

/// Server -> uploader: upload refused, carrying the reason.
class NetRefuseMapUpload : public NetMessage
{
public:
	NetRefuseMapUpload();
	NetRefuseMapUpload(YOGMapUploadRefusalReason reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGMapUploadRefusalReason getReason() const;
private:
	YOGMapUploadRefusalReason reason;
};
