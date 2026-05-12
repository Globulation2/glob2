// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdlib.h>

#include "Marshaling.h"
#include "Order.h"
#include "Utilities.h"

// MiscOrder's code

MiscOrder::MiscOrder()
:Order()
{
}

// NullOrder's code

NullOrder::NullOrder()
:MiscOrder()
{
}

// MessageOrder's code

MessageOrder::MessageOrder()
:MiscOrder()
{
	data = NULL;
	length = 0;
}

std::optional<std::shared_ptr<MessageOrder>> MessageOrder::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint32 recepientsMask || Uint32 messageOrderType || Uint8
	// textLength || char text[textLength] || NUL terminator.
	if (dataLength<9)
		return std::nullopt;

	std::shared_ptr<MessageOrder> order = std::make_shared<MessageOrder>();
	// Initialize data to NULL so the destructor is safe even on partial failure.
	order->data = NULL;
	order->length=dataLength;
	order->recepientsMask=getUint32(data, 0);
	order->messageOrderType=getUint32(data, 4);
	Uint8 textLength=getUint8(data, 8);
	order->data=(Uint8 *)malloc(dataLength);
	memcpy(order->data, data, dataLength);
	if (order->data[dataLength-1]!=0)
		return std::nullopt;
	if (textLength!=Utilities::strmlen((const char *)(order->data+9), ORDER_TEXT_MESSAGE_MAX_LEN))
		return std::nullopt;
	if (textLength!=dataLength-9)
		return std::nullopt;
	return order;
}

MessageOrder::MessageOrder(Uint32 recepientsMask, Uint32 messageOrderType, const char * text)
{
	length=Utilities::strmlen(text, ORDER_TEXT_MESSAGE_MAX_LEN)+9;
	data=(Uint8 *)malloc(length);
	memcpy(data+9, text, length-9);
	data[length-1]=0;
	addUint32(data, recepientsMask, 0);
	addUint32(data, messageOrderType, 4);
	addUint8(data, (Uint8)(length-9), 8);
	this->recepientsMask=recepientsMask;
	this->messageOrderType=messageOrderType;
}

MessageOrder::~MessageOrder()
{
	assert(data);
	free(data);
}

Uint8 *MessageOrder::getData(void)
{
	return data;
}

bool MessageOrder::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if (dataLength<9)
		return false;
	this->length=dataLength;
	this->recepientsMask=getUint32(data, 0);
	this->messageOrderType=getUint32(data, 4);
	Uint8 textLength=getUint8(data, 8);
	if (this->data!=NULL)
		free(this->data);
	this->data=(Uint8 *)malloc(dataLength);
	memcpy(this->data, data, dataLength);
	if (this->data[dataLength-1]!=0)
		return false;
	if (textLength!=Utilities::strmlen((const char *)(this->data+9), ORDER_TEXT_MESSAGE_MAX_LEN))
		return false;
	if (textLength!=dataLength-9)
		return false;
	return true;
}

// OrderVoiceData's code

OrderVoiceData::OrderVoiceData()
:MiscOrder()
{
	data = NULL;
	framesDatasLength = 0;
	frameCount = 0;
}

std::optional<std::shared_ptr<OrderVoiceData>> OrderVoiceData::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint32 recepientsMask || Uint8 frameCount || Uint8
	// frames[frameCount].
	if (dataLength < 5)
		return std::nullopt;

	std::shared_ptr<OrderVoiceData> order = std::make_shared<OrderVoiceData>();
	order->data = NULL;
	order->framesDatasLength = (size_t)dataLength - 5;
	order->recepientsMask = getUint32(data, 0);
	order->frameCount = getUint8(data, 4);

	order->data = (Uint8 *)malloc(dataLength);
	memcpy(order->data, data, dataLength);
	return order;
}

OrderVoiceData::OrderVoiceData(Uint32 recepientsMask, size_t framesDatasLength, Uint8 frameCount, const Uint8 *framesDatas)
{
	this->recepientsMask = recepientsMask;
	this->framesDatasLength = framesDatasLength;
	this->frameCount = frameCount;

	data = (Uint8 *)malloc(framesDatasLength+5);
	if (framesDatas)
		memcpy(data+5, framesDatas, framesDatasLength);
}

OrderVoiceData::~OrderVoiceData()
{
	assert(data);
	free(data);
}

Uint8 *OrderVoiceData::getData(void)
{
	addUint32(data, recepientsMask, 0);
	addUint8(data, frameCount, 4);
	return data;
}

bool OrderVoiceData::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	assert(dataLength >= 5);
	if (dataLength<5)
		return false;

	this->framesDatasLength = (size_t)dataLength - 5;
	this->recepientsMask = getUint32(data, 0);
	this->frameCount = getUint8(data, 4);

	if (this->data != NULL)
		free(this->data);
	this->data = (Uint8 *)malloc(dataLength);
	memcpy(this->data, data, dataLength);
	return true;
}

// SetAllianceOrder's code

SetAllianceOrder::SetAllianceOrder()
:MiscOrder()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<SetAllianceOrder>> SetAllianceOrder::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint32 teamNumber || Uint32 alliedMask || Uint32 enemyMask ||
	// Uint32 visionExchangeMask || Uint32 visionFoodMask || Uint32 visionOtherMask.
	if(dataLength!=24)
		return std::nullopt;

	std::shared_ptr<SetAllianceOrder> order = std::make_shared<SetAllianceOrder>();
	order->teamNumber=getUint32(data, 0);
	order->alliedMask=getUint32(data, 4);
	order->enemyMask=getUint32(data, 8);
	order->visionExchangeMask=getUint32(data, 12);
	order->visionFoodMask=getUint32(data, 16);
	order->visionOtherMask=getUint32(data, 20);
	memcpy(order->data, data, dataLength);
	return order;
}

SetAllianceOrder::SetAllianceOrder(Uint32 teamNumber, Uint32 alliedMask, Uint32 enemyMask, Uint32 visionExchangeMask, Uint32 visionFoodMask, Uint32 visionOtherMask)
{
	this->teamNumber=teamNumber;
	this->alliedMask=alliedMask;
	this->enemyMask=enemyMask;
	this->visionExchangeMask=visionExchangeMask;
	this->visionFoodMask=visionFoodMask;
	this->visionOtherMask=visionOtherMask;
}

Uint8 *SetAllianceOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint32(data, this->teamNumber, 0);
	addUint32(data, this->alliedMask, 4);
	addUint32(data, this->enemyMask, 8);
	addUint32(data, this->visionExchangeMask, 12);
	addUint32(data, this->visionFoodMask, 16);
	addUint32(data, this->visionOtherMask, 20);
	return data;
}

bool SetAllianceOrder::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(dataLength!=getDataLength())
		return false;
	this->teamNumber=getUint32(data, 0);
	this->alliedMask=getUint32(data, 4);
	this->enemyMask=getUint32(data, 8);
	this->visionExchangeMask=getUint32(data, 12);
	this->visionFoodMask=getUint32(data, 16);
	this->visionOtherMask=getUint32(data, 20);
	memcpy(this->data, data, dataLength);
	return true;
}

// MapMarkOrder's code

MapMarkOrder::MapMarkOrder()
:MiscOrder()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<MapMarkOrder>> MapMarkOrder::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint32 teamNumber || Sint32 x || Sint32 y.
	if(dataLength!=12)
		return std::nullopt;

	std::shared_ptr<MapMarkOrder> order = std::make_shared<MapMarkOrder>();
	order->teamNumber=getUint32(data, 0);
	order->x=getSint32(data, 4);
	order->y=getSint32(data, 8);
	memcpy(order->data, data, dataLength);
	return order;
}

MapMarkOrder::MapMarkOrder(Uint32 teamNumber, Sint32 x, Sint32 y)
{
	this->teamNumber=teamNumber;
	this->x=x;
	this->y=y;
}

Uint8 *MapMarkOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint32(data, this->teamNumber, 0);
	addSint32(data, this->x, 4);
	addSint32(data, this->y, 8);
	return data;
}

bool MapMarkOrder::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(dataLength!=getDataLength())
		return false;

	this->teamNumber=getUint32(data, 0);
	this->x=getSint32(data, 4);
	this->y=getSint32(data, 8);

	memcpy(this->data, data, dataLength);

	return true;
}

// PauseGameOrder's code

PauseGameOrder::PauseGameOrder()
:MiscOrder()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<PauseGameOrder>> PauseGameOrder::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint8 pause.
	if(dataLength!=1)
		return std::nullopt;

	std::shared_ptr<PauseGameOrder> order = std::make_shared<PauseGameOrder>();
	order->pause=(bool)data[0];
	memcpy(order->data, data, dataLength);
	return order;
}

PauseGameOrder::PauseGameOrder(bool pause)
{
	this->pause=pause;
}

Uint8 *PauseGameOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	data[0]=(Uint8)pause;
	return data;
}

bool PauseGameOrder::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(dataLength!=getDataLength())
		return false;
	pause=(bool)data[0];
	memcpy(this->data, data, dataLength);
	return true;
}

// PlayerQuitsGameOrder code

PlayerQuitsGameOrder::PlayerQuitsGameOrder()
:MiscOrder()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<PlayerQuitsGameOrder>> PlayerQuitsGameOrder::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint32 player.
	if(dataLength!=4)
		return std::nullopt;

	std::shared_ptr<PlayerQuitsGameOrder> order = std::make_shared<PlayerQuitsGameOrder>();
	order->player=getUint32(data, 0);
	memcpy(order->data, data, dataLength);
	return order;
}

PlayerQuitsGameOrder::PlayerQuitsGameOrder(Sint32 player)
{
	this->player=player;
}

Uint8 *PlayerQuitsGameOrder::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint32(data, this->player, 0);
	return data;
}

bool PlayerQuitsGameOrder::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(dataLength!=getDataLength())
		return false;

	this->player=getUint32(data, 0);

	memcpy(this->data, data, dataLength);

	return true;
}

// AdjustLatency code

AdjustLatency::AdjustLatency()
:MiscOrder()
{
	memset(data, 0, sizeof(data));
}

std::optional<std::shared_ptr<AdjustLatency>> AdjustLatency::deserialize(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Wire encoding: Uint16 latencyAdjustment.
	if(dataLength!=2)
		return std::nullopt;

	std::shared_ptr<AdjustLatency> order = std::make_shared<AdjustLatency>();
	order->latencyAdjustment=getUint16(data, 0);
	memcpy(order->data, data, dataLength);
	return order;
}

AdjustLatency::AdjustLatency(Uint16 latencyAdjustment)
{
	this->latencyAdjustment=latencyAdjustment;
}

Uint8 *AdjustLatency::getData(void)
{
	assert(sizeof(data) == getDataLength());
	addUint16(data, this->latencyAdjustment, 0);
	return data;
}

bool AdjustLatency::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	if(dataLength!=getDataLength())
		return false;

	this->latencyAdjustment=getUint16(data, 0);

	memcpy(this->data, data, dataLength);

	return true;
}
