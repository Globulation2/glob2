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

#include "Order.h"
#include "Stream.h"
#include "YOGConsts.h"
#include "YOGGameInfo.h"
#include "YOGPlayerInfo.h"
#include "YOGMessage.h"
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include "MapHeader.h"
#include "GameHeader.h"


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
	MNetAttemptRegistration,
	MNetAcceptRegistration,
	MNetRefuseRegistration,
	MNetUpdatePlayerList,
	MNetCreateGame,
	MNetAttemptJoinGame,
	MNetGameJoinAccepted,
	MNetGameJoinRefused,
	MNetSendYOGMessage,
	MNetSendMapHeader,
	MNetCreateGameAccepted,
	MNetCreateGameRefused,
	MNetUpdateGameHeaderPlayers,
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

	///Reads the data, and returns a message containing the data.
	///The Message may be casted to its particular subclass, using
	///the getMessageType function and dynamic_cast
	static shared_ptr<NetMessage> getNetMessage(GAGCore::InputStream* stream);

	///Encodes the data into its shrunken, serialized form.
	virtual void encodeData(GAGCore::OutputStream* stream) const = 0;

	///Decodes data from the serialized form. Returns true on success, false otherwise.
	///The first byte is the type from getMessageType, and can be safely ignored by
	///derived classes, as it is handled by getNetMessage
	virtual void decodeData(GAGCore::InputStream* stream) = 0;

	///This causes the message to be formated to a string, for debugging and/or logging
	///purposes
	virtual std::string format() const = 0 ;

	///Compares two NetMessages. All derived Messages must implement this by
	///first testing to see if NetMessage casts to the derived class, and then
	///comparing internal data.
	virtual bool operator==(const NetMessage& rhs) const = 0;
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
	NetSendOrder(boost::shared_ptr<Order> newOrder);
	
	///Changes the Order that NetSendOrder holds. This will
	///delete an Order that was already present.
	void changeOrder(boost::shared_ptr<Order> newOrder);
	
	///Returns the Order that NetSendOrder holds.
	boost::shared_ptr<Order> getOrder();

	///Returns MNetSendOrder
	Uint8 getMessageType() const;

	///Encodes the data, wraps the encoding of the Order
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data, and reconstructs the Order.
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetSendOrder message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendOrder
	bool operator==(const NetMessage& rhs) const;
private:
	boost::shared_ptr<Order> order;
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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetSendClientInformation message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendClientInformation
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the version minor
	Uint16 getVersionMinor() const;
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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data.
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data.
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetDisconnect message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetDisconnect
	bool operator==(const NetMessage& rhs) const;
};




///NetAttemptRegistration attempts to register the user
class NetAttemptRegistration : public NetMessage
{
public:
	///Creates a NetAttemptRegistration message
	NetAttemptRegistration();
	
	///Creates a NetAttemptRegistration message
	NetAttemptRegistration(const std::string& username, const std::string& password);

	///Returns MNetAttemptRegistration
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetAttemptRegistration message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetAttemptRegistration
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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	///so long as they store YOGPlayerInfo
	template<typename container> void updateDifferences(const container& original, const container& updated);

	///Returns MNetUpdatePlayerList
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetUpdatePlayerList message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetUpdatePlayerList
	bool operator==(const NetMessage& rhs) const;
	
	///This will apply the recorded differences to the given container
	template<typename container> void applyDifferences(container& original) const;

private:
	std::vector<Uint16> removedPlayers;
	std::vector<YOGPlayerInfo> updatedPlayers;
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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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




///NetAttemptJoinGame tries to join a game. In the future, games may be password private and require a password,
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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

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




///NetSendYOGMessage
class NetSendYOGMessage : public NetMessage
{
public:
	///Creates a NetSendYOGMessage message
	NetSendYOGMessage(boost::shared_ptr<YOGMessage> message);

	///Creates a NetSendYOGMessage message
	NetSendYOGMessage();

	///Returns MNetSendYOGMessage
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetSendYOGMessage message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendYOGMessage
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the YOG message
	boost::shared_ptr<YOGMessage> getMessage() const;
private:
	boost::shared_ptr<YOGMessage> message;
};




///NetSendMapHeader sends a map header to the server
class NetSendMapHeader : public NetMessage
{
public:
	///Creates a NetSendMapHeader message
	NetSendMapHeader();

	///Creates a NetSendMapHeader message
	NetSendMapHeader(const MapHeader& mapHeader);

	///Returns MNetSendMapHeader
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetSendMapHeader message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetSendMapHeader
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the map header
	const MapHeader& getMapHeader() const;
private:
	mutable MapHeader mapHeader;
};




///NetCreateGameAccepted
class NetCreateGameAccepted : public NetMessage
{
public:
	///Creates a NetCreateGameAccepted message
	NetCreateGameAccepted();

	///Returns MNetCreateGameAccepted
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetCreateGameAccepted message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetCreateGameAccepted
	bool operator==(const NetMessage& rhs) const;
};




///NetCreateGameRefused
class NetCreateGameRefused : public NetMessage
{
public:
	///Creates a NetCreateGameRefused message
	NetCreateGameRefused();

	///Creates a NetCreateGameRefused message
	NetCreateGameRefused(YOGGameCreateRefusalReason reason);

	///Returns MNetCreateGameRefused
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetCreateGameRefused message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetCreateGameRefused
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the reason why the player could not join the game.
	YOGGameCreateRefusalReason getRefusalReason() const;
private:
	YOGGameCreateRefusalReason reason;
};




///NetUpdateGameHeaderPlayers sends the BasePlayers portion of a game header
///across a network. It is meant to communicate from the server to the clients
///changes in who has joined or not joined a game. The host handles the other
///portions of the game header, the game options
class NetUpdateGameHeaderPlayers : public NetMessage
{
public:
	///Creates a NetUpdateGameHeaderPlayers message
	NetUpdateGameHeaderPlayers();
	
	///Creates a NetUpdateGameHeaderPlayers message
	NetUpdateGameHeaderPlayers(GameHeader& gameHeader);

	///Returns MNetUpdateGameHeaderPlayers
	Uint8 getMessageType() const;

	///Encodes the data
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes the data
	void decodeData(GAGCore::InputStream* stream);

	///Formats the NetUpdateGameHeaderPlayers message with a small amount
	///of information.
	std::string format() const;

	///Compares with another NetUpdateGameHeaderPlayers
	bool operator==(const NetMessage& rhs) const;
	
	///Returns the game header. Note that it may only be partially complete
	const GameHeader& getGameHeader();
private:
	GameHeader gameHeader;
};



//message_append_marker

#include <iostream>

template<typename container> void NetUpdateGameList::updateDifferences(const container& original, const container& updated)
{
	removedGames.clear();
	updatedGames.clear();
	///Find all removed games
	for(typename container::const_iterator i = original.begin(); i!=original.end(); ++i)
	{
		bool found=false;
		for(typename container::const_iterator j = updated.begin(); j!=updated.end(); ++j)
		{
			if(i->getGameID() == j->getGameID())
			{
				found=true;
				break;
			}
		}
		if(!found)
		{
			removedGames.push_back(i->getGameID());
		}
	}
	///Find changed games
	for(typename container::const_iterator i = original.begin(); i!=original.end(); ++i)
	{
		for(typename container::const_iterator j = updated.begin(); j!=updated.end(); ++j)
		{
			///If the ID's are the same but some other property isn't, then
			///the game has changed and needs to be updated
			if((i->getGameID() == j->getGameID()) && ((*i) != (*j)))
			{
				updatedGames.push_back(*j);
				break;
			}
		}
	}
	///Find added games
	for(typename container::const_iterator i = updated.begin(); i!=updated.end(); ++i)
	{
		bool found=false;
		for(typename container::const_iterator j = original.begin(); j!=original.end(); ++j)
		{
			if(i->getGameID() == j->getGameID())
			{
				found=true;
				break;
			}
		}
		if(!found)
		{
			updatedGames.push_back(*i);
		}
	}
}



template<typename container> void NetUpdateGameList::applyDifferences(container& original) const
{
	//Remove the removed games
	for(Uint16 i=0; i<removedGames.size(); ++i)
	{
		typename container::iterator game = original.end();
		for(typename container::iterator j=original.begin(); j!=original.end(); ++j)
		{
			if(j->getGameID() == removedGames[i])
			{
				game = j;
				break;
			}
		}
		original.erase(game);
	}
	
	
	//Change the changed games and add the rest
	for(Uint16 i=0; i<updatedGames.size(); ++i)
	{
		bool found=false;
		for(typename container::iterator j=original.begin(); j!=original.end(); ++j)
		{
			if(j->getGameID() == updatedGames[i].getGameID())
			{
				(*j) = updatedGames[i];
				found=true;
				break;
			}
		}
		if(!found)
		{
			original.insert(original.end(), updatedGames[i]);
		}
	}
}



template<typename container> void NetUpdatePlayerList::updateDifferences(const container& original, const container& updated)
{
	removedPlayers.clear();
	updatedPlayers.clear();
	//find removed players
	for(typename container::const_iterator i = original.begin(); i!=original.end(); ++i)
	{
		bool found=false;
		for(typename container::const_iterator j = updated.begin(); j!=updated.end(); ++j)
		{
			if(i->getPlayerID() == j->getPlayerID())
			{
				found=true;
				break;
			}
		}
		if(!found)
			removedPlayers.push_back(i->getPlayerID());
	}
	
	//Find added or changed players
	for(typename container::const_iterator i = updated.begin(); i!=updated.end(); ++i)
	{
		bool found=false;
		bool changed=false;
		for(typename container::const_iterator j = original.begin(); j!=original.end(); ++j)
		{
			if(i->getPlayerID() == j->getPlayerID())
			{
				found=true;
				if((*i) != (*j))
				{
					changed=true;
				}
				break;
			}
		}
		if(!found || changed)
			updatedPlayers.push_back(*i);
	}
}



template<typename container> void NetUpdatePlayerList::applyDifferences(container& original) const
{
	//Remove removed players
	for(std::vector<Uint16>::const_iterator i = removedPlayers.begin(); i!=removedPlayers.end(); ++i)
	{
		for(typename container::iterator j=original.begin(); j!=original.end(); ++j)
		{
			if(*i == j->getPlayerID())
			{
				original.erase(j);
				break;
			}
		}
	}
	
	//Change and/or add the players that are updated
	for(std::vector<YOGPlayerInfo>::const_iterator i=updatedPlayers.begin(); i!=updatedPlayers.end(); ++i)
	{
		bool found=false;
		for(typename container::iterator j=original.begin(); j!=original.end(); ++j)
		{
			//If the player id's are the same, then this player has somehow changed.
			if(i->getPlayerID() == j->getPlayerID())
			{
				(*j) = (*i);
				found = true;
			}
		}
		//Not found, meaning this player is a new one
		if(!found)
		{
			original.insert(original.end(), (*i));
		}
	}
}

#endif
