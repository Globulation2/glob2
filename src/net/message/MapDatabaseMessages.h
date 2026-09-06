// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>
#include <vector>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "MapThumbnail.h"
#include "YOGDownloadableMapInfo.h"

/// Server -> client: list of downloadable maps (metadata only).
class NetDownloadableMapInfos : public NetMessage
{
public:
	NetDownloadableMapInfos();
	NetDownloadableMapInfos(std::vector<YOGDownloadableMapInfo> maps);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	std::vector<YOGDownloadableMapInfo> getMaps() const;
private:
	std::vector<YOGDownloadableMapInfo> maps;
};

/// Client -> server: please send the current downloadable-maps list.
class NetRequestDownloadableMapList : public NetMessage
{
public:
	NetRequestDownloadableMapList();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

/// Client -> server: send the thumbnail for the given map.
class NetRequestMapThumbnail : public NetMessage
{
public:
	NetRequestMapThumbnail();
	NetRequestMapThumbnail(Uint16 mapID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getMapID() const;
private:
	Uint16 mapID;
};

/// Server -> client: thumbnail bitmap for the previously-requested map.
class NetSendMapThumbnail : public NetMessage
{
public:
	NetSendMapThumbnail();
	NetSendMapThumbnail(Uint16 mapID, MapThumbnail thumbnail);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getMapID() const;
	MapThumbnail getThumbnail() const;
private:
	Uint16 mapID;
	MapThumbnail thumbnail;
};

/// Client -> server: submit the user's rating (1-5) for a map.
class NetSubmitRatingOnMap : public NetMessage
{
public:
	NetSubmitRatingOnMap();
	NetSubmitRatingOnMap(Uint16 mapID, Uint8 rating);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getMapID() const;
	Uint8 getRating() const;
private:
	Uint16 mapID;
	Uint8 rating;
};
