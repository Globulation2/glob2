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

#ifndef __NetMessage_h
#define __NetMessage_h

#include "SDL_net.h"
#include <string>
#include <boost/shared_ptr.hpp>
#include "YOGConsts.h"

using namespace boost;

///This is the enum of message types
enum NetMessageType
{
	MNetSendOrder,
	MNetSendClientInformation,
	MNetSendServerInformation,
	MNetAttemptLogin,
	MNetLoginSuccessful,
	MNetRefuseLogin,
	MNetUpdateGameList,
	MNetDisconnect,
	MNetAttemptRegistrationUser,
	MNetAcceptRegistration,
	MNetRefuseRegistration,
	MNetUpdatePlayerList,
	MNetCreateGame,
	MNetAttemptJoinGame,
	MNetGameJoinAccepted,
	MNetGameJoinRefused,
	//type_append_marker
};


///This is bassically a message in the Net Engine. A Message has two parts,
///a type and a body. The NetMessage base class also has a static function
///that will read data in, and create the appropriette derived class. 
class NetMessage
{
public:
	///Virtual destructor for derived classes
	virtual ~NetMessage() {}

	///Returns the message type
	virtual Uint8 getMessageType() const = 0;

	///Reads the data, and returns an Order containing the data.
	///The Order may be casted to its particular subclass, using
	///the getMessageType function
	static sharded_ptr<NetMessage> getNetMessage(const Uint8 *netData, int dataLength);

	///Encodes the data into its shrunken, serialized form. It is important that
	///the first byte be the type returned from getMessageType. All
	///derived classes must follow this rule.
	virtual Uint8 *encodeData() const = 0;

	///Returns the length of the data that was encoded with the above function.
	///Derived classes must follow account for the messageType being the first
	///byte. The length should not exceed 64 kilobytes.
	virtual Uint16 getDataLength() const = 0;

	///Decodes data from the serialized form. Returns true on success, false otherwise.
	///The first byte is the type from getMessageType, and can be safely ignored by
	///derived classes, as it is handled by getNetMessage
	virtual bool decodeData(const Uint8 *data, int dataLength) = 0;

	///This causes the message to be formated to a string, for debugging and/or logging
	///purposes
	virtual std::string format() const = 0 ;

	///Compares two NetMessages. All derived Messages must implement this by
	///first testing to see if NetMessage casts to the derived class, and then
	///comparing internal data.
	virtual bool operator==(const NetMessage& rhs) = 0 const;
	///This does not need to be overloaded, but can be for efficiency purposes.
	virtual bool operator!=(const NetMessage& rhs) const;
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
	Uint8 getMessageType() const;

	///Encodes the data, wraps the encoding of the Order
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data, and reconstructs the Order.
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetSendOrder message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendOrder
	bool operator==(const NetMessage& rhs) const;
private:
	Order* order;
};



///This message sends local version information to the server
class NetSendClientInformation : public NetMessage
{
public:
	///Creates a NetSendClientInformation message
	NetSendClientInformation();

	///Returns MNetSendClientInformation
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetSendClientInformation message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendClientInformation
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the version minor
	Uint63 getVersionMinor() const;
private:
	Uint16 versionMinor;
};



///This message sends server information to the client. This includes
///login and game policies (for example anonymous / password required login)
class NetSendServerInformation : public NetMessage
{
public:
	///Creates a NetSendServerInformation message with the provided server information
	NetSendServerInformation(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy);
	
	///Creates an empty NetSendServerInformation message
	NetSendServerInformation();

	///Returns MNetSendServerInformation
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetSendServerInformation message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendServerInformation
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the login policy
	YOGLoginPolicy getLoginPolicy() const;
	
	///Returns the game policy
	YOGGamePolicy getGamePolicy() const;
	
private:
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
};



///This message sends login information (username and password) to the server.
class NetAttemptLogin : public NetMessage
{
public:
	///Creates a NetAttemptLogin message with the given username and password
	NetAttemptLogin(const std::string& username, const std::string& password);
	
	///Creates an empty NetAttemptLogin message
	NetAttemptLogin();

	///Returns MNetAttemptLogin
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetAttemptLogin message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetAttemptLogin
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the username
	const std::string& getUsername() const;
	
	///Returns the password
	const std::string& getPassword() const;
	
private:
	std::string username;
	std::string password;
};



///This message informs the client its login was successfull
class NetLoginSuccessful : public NetMessage
{
public:
	///Creates a NetLoginSuccessful message
	NetLoginSuccessful();

	///Returns MNetLoginSuccessful
	Uint8 getMessageType() const;

	///Encodes the data, however, this message has no data, it must be atleast one byte.
	Uint8 *encodeData() const;

	///Returns the data length of 1
	Uint16 getDataLength() const;

	///Decodes the data.
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetLoginSuccessful message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetLoginSuccessful
	bool operator==(const NetMessage& rhs) const;
};



///This message informs the client its login was refused. It carries with it the reason why.
class NetRefuseLogin : public NetMessage
{
public:
	///Creates an empty NetRefuseLogin message
	NetRefuseLogin();

	///Creates a NetRefuseLogin message with the given reason
	NetRefuseLogin(YOGLoginState reason);

	///Returns MNetRefuseLogin
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data.
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetRefuseLogin message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetRefuseLogin
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the reason why this login was refused
	YOGLoginState getRefusalReason() const;
private:
	YOGLoginState reason;
};


///This message updates the users pre-joining game list. Bassically, it takes what the user already has
///(the server should have a copy), and the new server game list, and sends a message with the differences
///between the two, and reassembles the completed list at the other end. This both reduces bandwidth,
///and eliminates the need for seperate GameAdded, GameRemoved, and GameChanged messages just to keep
///a connected user updated. For this to work, the server and the client should have synced versions
///of what the list is, and this message will just pass updates.
class NetUpdateGameList : public NetMessage
{
public:
	///Creates an empty NetUpdateGameList message.
	NetUpdateGameList();

	///Computes and stores the differences between the two provided lists of YOGGameInfo objects.
	///The container can be any container with a ::const_iterator, a begin(), and an end(), for
	///iterating over the ranges. std containers are most common. For this to work, the original
	///list has to be the same as the one on the client (while they don't have to be the same
	///type of container), they must be in sync.
	template<typename container> void updateDifferences(const container& original, const container& updated);

	///Returns MNetUpdateGameList
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetUpdateGameList message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetUpdateGameList
	bool operator==(const NetMessage& rhs) const;
	
	///Applies the differences that this message has been given to the provided container.
	///The container must have the methods erase(iter), begin(), end(), and insert(iter, object)
	template<typename container> void applyDifferences(container& original) const;
private:
	std::vector<Uint16> removedGames;
	std::vector<YOGGameInfo> updatedGames;
};


///NetDisconnect informs the server and/or client that the other is disconnecting
class NetDisconnect : public NetMessage
{
public:
	///Creates a NetDisconnect message
	NetDisconnect();

	///Returns MNetDisconnect
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetDisconnect message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetDisconnect
	bool operator==(const NetMessage& rhs) const;
};




///NetAttemptRegistrationUser attempts to register the user
class NetAttemptRegistrationUser : public NetMessage
{
public:
	///Creates a NetAttemptRegistrationUser message
	NetAttemptRegistrationUser();
	
	///Creates a NetAttemptRegistrationUser message
	NetAttemptRegistrationUser(const std::string& username, const std::string& password);

	///Returns MNetAttemptRegistrationUser
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetAttemptRegistrationUser message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetAttemptRegistrationUser
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the username
	std::string getUsername() const;
	
	///Returns the password
	std::string getPassword() const;
private:
	std::string username;
	std::string password;
};




///NetAcceptRegistration informs the user that their registration information was accepted.
class NetAcceptRegistration : public NetMessage
{
public:
	///Creates a NetAcceptRegistration message
	NetAcceptRegistration();

	///Returns MNetAcceptRegistration
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetAcceptRegistration message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetAcceptRegistration
	bool operator==(const NetMessage& rhs) const;
};




///NetRefuseRegistration informs the user that their registration attemp was denied
class NetRefuseRegistration : public NetMessage
{
public:
	///Creates a NetRefuseRegistration message
	NetRefuseRegistration();

	///Creates a NetRefuseRegistration message with the given reason
	NetRefuseRegistration(YOGLoginState reason);
	
	///Returns MNetRefuseRegistration
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetRefuseRegistration message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetRefuseRegistration
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the reason why this registration was refused
	YOGLoginState getRefusalReason() const;
private:
	YOGLoginState reason;
};




///NetUpdatePlayerList
class NetUpdatePlayerList : public NetMessage
{
public:
	///Creates a NetUpdatePlayerList message
	NetUpdatePlayerList();

	///This computes the differences between the two lists of players. These can be of any container,
	///so long as they store std::string
	template<typename container> void updateDifferences(const container& original, const container& updated);

	///Returns MNetUpdatePlayerList
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetUpdatePlayerList message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetUpdatePlayerList
	bool operator==(const NetMessage& rhs) const;
	
	///This will apply the recorded differences to the given container
	template<typename container> void applyDifferences(container& original) const;

private:
	std::vector<Uint8> removedPlayers;
	std::vector<std::string> newPlayers;
};




///NetCreateGame creates a new game on the server.
class NetCreateGame : public NetMessage
{
public:
	///Creates a NetCreateGame message
	NetCreateGame(const std::string& gameName);

	///Creates a NetCreateGame message
	NetCreateGame();

	///Returns MNetCreateGame
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetCreateGame message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetCreateGame
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the game name
	const std::string& getGameName() const;
private:
	std::string gameName;
};




///NetAttemptJoinGame tries to join a game. Games may be password private and require a password,
///and so attempts to join a game may not always be successful
class NetAttemptJoinGame : public NetMessage
{
public:
	///Creates a NetAttemptJoinGame message
	NetAttemptJoinGame();

	///Creates a NetAttemptJoinGame message
	NetAttemptJoinGame(Uint16 gameID);

	///Returns MNetAttemptJoinGame
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetAttemptJoinGame message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetAttemptJoinGame
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the game ID
	Uint16 getGameID() const;
private:
	Uint16 gameID;
};




///NetGameJoinAccepted means that a NetAttemptJoinGame was accepted and the player is now
///joined in the game 
class NetGameJoinAccepted : public NetMessage
{
public:
	///Creates a NetGameJoinAccepted message
	NetGameJoinAccepted();

	///Returns MNetGameJoinAccepted
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetGameJoinAccepted message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetGameJoinAccepted
	bool operator==(const NetMessage& rhs) const;
};




///NetGameJoinRefused means that the attempt to join a game was denied.
class NetGameJoinRefused : public NetMessage
{
public:
	///Creates a NetGameJoinRefused message
	NetGameJoinRefused(YOGGameJoinRefusalReason reason);

	///Creates a NetGameJoinRefused message
	NetGameJoinRefused();

	///Returns MNetGameJoinRefused
	Uint8 getMessageType() const;

	///Encodes the data
	Uint8 *encodeData() const;

	///Returns the data length
	Uint16 getDataLength() const;

	///Decodes the data
	bool decodeData(const Uint8 *data, int dataLength);

	///Formats the NetGameJoinRefused message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetGameJoinRefused
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the reason why the player could not join the game.
	YOGGameJoinRefusalReason getRefusalReason() const;
private:
	YOGGameJoinRefusalReason reason;
};



//message_append_marker

#endif
