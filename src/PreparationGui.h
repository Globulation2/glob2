/*
 * Globulation 2 preparation gui
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __PREPARATIONGUI_H
#define __PREPARATIONGUI_H

#include "GAG.h"
#include "Race.h"
#include "Session.h"
#include "SDL_net.h"

class MainMenuScreen:public Screen
{
public:
	enum
	{
		CAMPAIN = 1,
		CUSTOM = 2,
		MULTIPLAYERS = 3, 
		GAME_SETUP = 4,
		QUIT = 5
	};
private:
	Sprite *arch;
public:
	MainMenuScreen();
	virtual ~MainMenuScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
};

class MultiplayersOfferScreen:public Screen
{
public:
	enum
	{
		HOST = 1,
		JOIN = 4,
		QUIT = 5
	};
private:
	Sprite *arch;
	Font *font;
public:
	MultiplayersOfferScreen();
	virtual ~MultiplayersOfferScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
};

class TextInput;

class SessionScreen:public Screen
{
protected:
	enum {MAX_PACKET_SIZE=4000};
public:
	SessionScreen();
	virtual ~SessionScreen();
protected:
	bool validSessionInfo;
	Font *font;
	int crossPacketRecieved[32];
	int startGameTimeCounter;
protected:
	void paintSessionInfo(int state);
	enum {hostiphost=0};
	enum {hostipport=0};
public:

	SessionInfo sessionInfo;
	Sint32 myPlayerNumber;
	UDPsocket socket;
	bool destroyNet;
	int channel;

};

class MultiplayersChooseMapScreen:public SessionScreen
{
public:
	enum
	{
		LOAD = 1,
		SHARE = 4,
		CANCEL = 5
	};

private:
	Sprite *arch;
	TextInput *mapName;
	Button *load, *share, *cancel;

public:
	MultiplayersChooseMapScreen();
	virtual ~MultiplayersChooseMapScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
};


class MultiplayersCrossConnectableScreen:public SessionScreen
{
public:
	virtual ~MultiplayersCrossConnectableScreen(){};
	void tryCrossConnections(void);
public:
	IPaddress serverIP;
};

class MultiplayersHostScreen:public MultiplayersCrossConnectableScreen
{
public:
	enum
	{
		START = 1,
		CANCEL = 5,

		STARTED=11
	};

	enum HostGlobalState
	{
		HGS_BAD=0,
		HGS_SHARING_SESSION_INFO=1,
		HGS_WAITING_CROSS_CONNECTIONS=2,
		HGS_ALL_PLAYERS_CROSS_CONNECTED=3,
		HGS_GAME_START_SENDED=4,
		HGS_PLAYING_COUNTER=5 // the counter 5-4-3-2-1-0 is playing
	};

	enum
	{
		SECONDS_BEFORE_START_GAME=5
	};
private:
	Sprite *arch;
	TextInput *mapName;

	HostGlobalState hostGlobalState;

public:
	MultiplayersHostScreen(SessionInfo *sessionInfo);
	virtual ~MultiplayersHostScreen();
	void initHostGlobalState(void);
	void stepHostGlobalState(void);
	void removePlayer(int p);
	void removePlayer(char *data, int size, IPaddress ip);
	void newPlayer(char *data, int size, IPaddress ip);
	void newHostPlayer(void);
	void confirmPlayer(char *data, int size, IPaddress ip);
	void confirmStartCrossConnection(char *data, int size, IPaddress ip);
	void confirmStillCrossConnecting(char *data, int size, IPaddress ip);
	void confirmCrossConnectionAchieved(char *data, int size, IPaddress ip);
	void confirmPlayerStartGame(char *data, int size, IPaddress ip);
	void treatData(char *data, int size, IPaddress ip);
	void onTimer(Uint32 tick);
	void sendingTime();
	bool send(const int v);
	bool send(const int u, const int v);
	void stopHosting(void);
	void startGame(void);
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);

};

class MultiplayersJoinScreen:public MultiplayersCrossConnectableScreen
{
public:
	enum
	{
		CONNECT = 1,
		QUIT = 5,
		
		STARTED=11
	};
	enum WaitingState
	{
		WS_BAD=0,
		WS_TYPING_SERVER_NAME,
		WS_WAITING_FOR_SESSION_INFO,
		WS_WAITING_FOR_CHECKSUM_CONFIRMATION,
		WS_OK,
		
		WS_CROSS_CONNECTING,
		WS_CROSS_CONNECTING_START_CONFIRMED,
		WS_CROSS_CONNECTING_ACHIEVED,
		
		WS_CROSS_CONNECTING_SERVER_HEARD,
		
		WS_SERVER_START_GAME
	};
private:
	Sprite *arch;
	TextInput *serverName;
	TextInput *playerName;
	
	WaitingState waitingState;
	int waitingTimeout;
	int waitingTimeoutSize;
	int waitingTOTL;
	
public:
	MultiplayersJoinScreen();
	virtual ~MultiplayersJoinScreen();
	void dataSessionInfoRecieved(char *data, int size, IPaddress ip);
	void checkSumConfirmationRecieved(char *data, int size, IPaddress ip);
	void unCrossConnectSessionInfo(void);
	void tryCrossConnections(void);
	void startCrossConnections(void);
	void crossConnectionFirstMessage(char *data, int size, IPaddress ip);
	void checkAllCrossConnected(void);
	void crossConnectionSecondMessage(char *data, int size, IPaddress ip);
	void stillCrossConnectingConfirmation(IPaddress ip);
	void crossConnectionsAchievedConfirmation(IPaddress ip);
	void serverAskForBeginning(char *data, int size, IPaddress ip);
	void confirmPlayerStartGame(IPaddress ip);
	void treatData(char *data, int size, IPaddress ip);
	void onTimer(Uint32 tick);
	void sendingTime();
	void onSDLEvent(SDL_Event *event);
	bool sendSessionInfoRequest();
	bool sendSessionInfoConfirmation();
	bool send(const int v);
	bool send(const int u, const int v);
	bool tryConnection();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);

};


void raceMenu(Race *race);


#endif 
