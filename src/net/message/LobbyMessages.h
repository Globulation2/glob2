// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <memory>
#include <vector>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGGameInfo.h"
#include "YOGMessage.h"
#include "YOGPlayerSessionInfo.h"

/// Updates the user's pre-joining game list. Sends only the differences between
/// the user's known list and the server's current list (additions, removals,
/// changed entries) and lets the receiver reassemble the full list. Both sides
/// must hold matching state for this to work.
class NetUpdateGameList : public NetMessage
{
public:
	NetUpdateGameList();

	/// Computes and stores the differences between two YOGGameInfo containers.
	/// Container needs ::const_iterator, begin(), end(); typical std containers fit.
	template<typename container> void updateDifferences(const container& original, const container& updated);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	/// Applies the recorded differences to the given container.
	/// Container needs erase(iter), begin(), end(), insert(iter, object).
	template<typename container> void applyDifferences(container& original) const;
private:
	std::vector<Uint16> removedGames;
	std::vector<YOGGameInfo> updatedGames;
};

/// Same diff-update mechanism as NetUpdateGameList, but for the connected
/// players list (YOGPlayerSessionInfo).
class NetUpdatePlayerList : public NetMessage
{
public:
	NetUpdatePlayerList();

	template<typename container> void updateDifferences(const container& original, const container& updated);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	template<typename container> void applyDifferences(container& original) const;
private:
	std::vector<YOGPlayerID> removedPlayers;
	std::vector<YOGPlayerSessionInfo> updatedPlayers;
};

/// Carries a chat message for a YOG channel (lobby chat, in-game chat, etc.).
class NetSendYOGMessage : public NetMessage
{
public:
	NetSendYOGMessage(Uint32 channel, std::shared_ptr<YOGMessage> message);
	NetSendYOGMessage();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint32 getChannel() const;
	std::shared_ptr<YOGMessage> getMessage() const;
private:
	Uint32 channel;
	std::shared_ptr<YOGMessage> message;
};

/// Tells the client that their username has been banned by an administrator.
class NetPlayerIsBanned : public NetMessage
{
public:
	NetPlayerIsBanned();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

/// Tells the client that their IP address has been banned by an administrator.
class NetIPIsBanned : public NetMessage
{
public:
	NetIPIsBanned();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

// ---------------------------------------------------------------------------
// Template definitions
// ---------------------------------------------------------------------------

template<typename container> void NetUpdateGameList::updateDifferences(const container& original, const container& updated)
{
	removedGames.clear();
	updatedGames.clear();
	// Find all removed games
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
	// Find changed games
	for(typename container::const_iterator i = original.begin(); i!=original.end(); ++i)
	{
		for(typename container::const_iterator j = updated.begin(); j!=updated.end(); ++j)
		{
			// Same ID but a different property — game has changed and needs to be sent.
			if((i->getGameID() == j->getGameID()) && ((*i) != (*j)))
			{
				updatedGames.push_back(*j);
				break;
			}
		}
	}
	// Find added games
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
	// Remove the removed games
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

	// Change the changed games and add the rest
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
	// Find removed players
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

	// Find added or changed players
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
	// Remove removed players
	for(std::vector<YOGPlayerID>::const_iterator i = removedPlayers.begin(); i!=removedPlayers.end(); ++i)
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

	// Change and/or add the updated players
	for(std::vector<YOGPlayerSessionInfo>::const_iterator i=updatedPlayers.begin(); i!=updatedPlayers.end(); ++i)
	{
		bool found=false;
		for(typename container::iterator j=original.begin(); j!=original.end(); ++j)
		{
			// Same player ID — this player has changed somehow.
			if(i->getPlayerID() == j->getPlayerID())
			{
				(*j) = (*i);
				found = true;
			}
		}
		// Not found — this is a new player.
		if(!found)
		{
			original.insert(original.end(), (*i));
		}
	}
}
