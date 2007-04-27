/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "SDL_net.h"
#include <string>


///This is the enum of message types
enum NetMessageType
{
	MNetSendOrder,
};



///This is bassically a message in the Net Engine. A Message has two parts,
///a type and a body. The NetMessage base class also has a static function
///that will read data in, and create the appropriette derived class. 
class NetMessage
{
public:
	///Returns the message type
	virtual Uint8 getMessageType() = 0;

	///Reads the data, and returns an Order containing the data.
	///The Order may be casted to its particular subclass, using
	///the getMessageType function
	static NetMessage* getNetMessage(const Uint8 *netData, int dataLength);

	///Encodes the data into its shrunken, serialized form. It is important that
	///the first byte be the type returned from getMessageType. All
	///derived classes must follow this rule.
	virtual Uint8 *encodeData(void)=0;

	///Returns the length of the data that was encoded with the above function.
	///Derived classes must follow account for the messageType being the first
	///byte.
	virtual Uint32 getDataLength(void)=0;

	///Decodes data from the serialized form. Returns true on success, false otherwise.
	///The first byte is the type from getMessageType, and can be safely ignored by
	///derived classes, as it is handled by getNetMessage
	virtual bool decodeData(const Uint8 *data, int dataLength)=0;

	///This causes the message to be formated to a string, for debugging and/or logging
	///purposes
	virtual std::string format()=0;

};



///This message bassically wraps the Order class, meant to deliver an Order across a network.
class NetSendOrder : public NetMessage
{
public:
	///Creates a NetSendOrder message with a NULL Order.
	NetSendOrder();
	
	///Creates a NetSendOrder message with the provided Order.
	///This will assume ownership of the Order.
	NetSendOrder(Order* newOrder);
	
	///Changes the Order that NetSendOrder holds. This will
	///delete an Order that was already present.
	void changeOrder(Order* newOrder);
	
	///Returns the Order that NetSendOrder holds.
	Order* getOrder();

	///Returns MNetSendOrder
	Uint8 getMessageType();

	///Encodes the data, wraps the encoding of the Order
	Uint8 *encodeData(void);

	///Returns the data length
	Uint32 getDataLength(void);

	///Decodes the data, and reconstructs the Order.
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetSendOrder message with a small amount
	///of information.
	std::string format();

private:
	Order* order;
};
