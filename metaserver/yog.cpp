/*
 *  Ysagoon Online Gaming
 *  Meta Server with chat for Ysagoon game (first is glob2)
 *  (c) 2001 Stephane Magnenat <nct@ysagoon.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <SDL.h>
#include <SDL_net.h>
#include <set>
#include <map>
#include <string.h>
#include <signal.h>

const int GAME_INFO_MAX_SIZE = 1024;

//! A game that is running
struct Game
{
	char info[GAME_INFO_MAX_SIZE];
	Uint32 creationTick;

	Game(char *info) { strcpy(this->info, info); creationTick=SDL_GetTicks(); }
};

//! An active connection with a player
struct Connection
{
	TCPsocket socket;
	bool receiveChat;

	Connection(TCPsocket socket) { this->socket=socket; this->receiveChat=false; }
};


//! The main object for our meta server
class GlobalStuff
{
private:
	//! The list of games
	map<Uint32, Game *> games;
	//! The list of connections
	set<Connection *> connections;
	//! The port on which we seten
	Uint16 port;
	//! The server socket
	TCPsocket serverSocket;
	//! This is a set of socket to wait for
	SDLNet_SocketSet allSocket;
	//! This is the maximum size of the socket set
	int maxSocket;

public:
	//! this is static var so that signal will shut it down
	static int isRunning;

public:
	//! Constructor
	GlobalStuff();
	//! Destructor
	~GlobalStuff();
	//! Welcome message, should be called first
	void welcomeMsg(void);
	//! Parse command line arguments
	void parseCmd(int argc, char *argv[]);
	//! This inits all subsystems, should be called afet parse cmd
	void init(void);
	//! This is where the main loop is
	void run(void);

private:
	//! This quits all subsystems, called by destructor
	void quit(void);
	//! This handle incoming datas on a socket
	void handleSockets(void);
	//! This handle new connection
	void handleNewConnection(void);
	//! This handle datas from a collection
	void handleConnection(Connection *connection);
	//! this handle conenction when client has closed
	void handleCloseConnection(Connection *connection);
	//! This get a string from a TCP socket, retrun false if socket has been closed
	bool getString(TCPsocket socket, char data[GAME_INFO_MAX_SIZE]);
	//! This send a string from a TCP socket, retrun false if socket has been closed
	bool sendString(TCPsocket socket, char *data);
};


void sigHandler(int sig)
{
	GlobalStuff::isRunning=false;
}

int GlobalStuff::isRunning;

GlobalStuff::GlobalStuff(void)
{
	port=3000;
	maxSocket=200;
	serverSocket=NULL;
	allSocket=NULL;
	isRunning=true;
}

void GlobalStuff::welcomeMsg(void)
{
	printf("INFO : YOG - Ysagoon Online Gaming\n");
	printf("INFO : (c) 2001 Stephane Magnenat\n");
	printf("INFO : This program is GPL\n");
}

void GlobalStuff::parseCmd(int argc, char *argv[])
{
	if (argc==3)
	{
		port=atoi(argv[1]);
		maxSocket=atoi(argv[2]);
		printf("INFO : port set to %d and maxSocket set to %d\n", port, maxSocket);
	}
	else
	{
		printf("INFO : server will use default port (%d) and maxScoket (%d)\n", port, maxSocket);
	}
}

void GlobalStuff::init(void)
{
	printf("INFO : Server is starting\n");

	// init sdl & sdl net
	//SDL_Init(SDL_INIT_EVENTTHREAD);
	SDLNet_Init();

	// create the socket set (we add one for server socket)
	allSocket=SDLNet_AllocSocketSet(maxSocket+1);
	if (allSocket==NULL)
	{
		fprintf(stderr, "ERROR : can't allocate %d max connections\n", maxSocket);
		exit(1);
	}

	// create the server ip
	IPaddress myIp;
	myIp.host=INADDR_NONE;
	myIp.port=SDL_SwapBE16(port);
	serverSocket=SDLNet_TCP_Open(&myIp);
	if (serverSocket==NULL)
	{
		fprintf(stderr, "ERROR : can't open server socket\n");
		exit(2);
	}

	// add it to the set
	SDLNet_TCP_AddSocket(allSocket, serverSocket);

	// activate signal handling
	signal(SIGQUIT, sigHandler);
	signal(SIGINT, sigHandler);
	signal(SIGTERM, sigHandler);
	signal(SIGALRM, sigHandler);
	signal(SIGPIPE, sigHandler);

	printf("INFO : Server is working\n");
}

GlobalStuff::~GlobalStuff()
{
	quit();
}

void GlobalStuff::quit(void)
{
	printf("INFO : Server will shutdown NOW\n");

	// remove any remaining connections
	for (set<Connection *>::iterator connectionIt=connections.begin(); connectionIt!=connections.end(); ++connectionIt)
		handleCloseConnection(*connectionIt);

	// remove server socket from the set
	SDLNet_TCP_DelSocket(allSocket, serverSocket);

	// close the server ip
	if (serverSocket)
		SDLNet_TCP_Close(serverSocket);

	// deallocate the set
	if (allSocket)
		SDLNet_FreeSocketSet(allSocket);

	// close sdl & sdl net
	SDLNet_Quit();
	//SDL_Quit();

	printf("INFO : Server shutdown completed\n");
}

void GlobalStuff::handleCloseConnection(Connection *connection)
{
	connections.erase(connection);
	SDLNet_TCP_DelSocket(allSocket, connection->socket);
	printf("INFO : Connection has been closed, there is %u connections\n", connections.size());
	SDLNet_TCP_Close(connection->socket);
	delete connection;
}

void GlobalStuff::handleConnection(Connection *connection)
{
	char data[GAME_INFO_MAX_SIZE];
	if (getString(connection->socket, data)==false)
	{
		handleCloseConnection(connection);
		return;
	}
	char *token=strtok(data, "\r\n \t");
	if (token)
	{
		if (strcasecmp(token, "say")==0)
		{
			token=strtok(NULL, "\r\n");
			if (token)
			{
				char data2[GAME_INFO_MAX_SIZE];
				printf("INFO : client said : %s\n", token);
				for (set<Connection *>::iterator connectionIt=connections.begin(); connectionIt!=connections.end(); ++connectionIt)
				{
					if ((*connectionIt)->receiveChat)
					{
						if (sendString((*connectionIt)->socket, token)==false)
							handleCloseConnection(*connectionIt);
					}
				}
			}
		}
		else if (strcasecmp(token, "newgame")==0)
		{
			token=strtok(NULL, "\r\n");
			if (token)
			{
				IPaddress *ip=SDLNet_TCP_GetPeerAddress(connection->socket);
				map<Uint32, Game *>::iterator it=games.find(ip->host);
				if (it==games.end())
				{
					games[ip->host]=new Game(token);
					printf("INFO : Game has been created from IP 0x%x, there is %u games\n", ip->host, games.size());
				}
			}
		}
		else if (strcasecmp(token, "deletegame")==0)
		{
			IPaddress *ip=SDLNet_TCP_GetPeerAddress(connection->socket);
			map<Uint32, Game *>::iterator it=games.find(ip->host);
			if (it!=games.end())
			{
				delete ((*it).second);
				games.erase(it);
				IPaddress *ip=SDLNet_TCP_GetPeerAddress(connection->socket);
				printf("INFO : Game has been closed from IP 0x%x, there is %u games\n", ip->host, games.size());
			}
		}
		else if (strcasecmp(token, "updategame")==0)
		{
			token=strtok(NULL, "\r\n");
			if (token)
			{
				IPaddress *ip=SDLNet_TCP_GetPeerAddress(connection->socket);
				map<Uint32, Game *>::iterator it=games.find(ip->host);
				if (it!=games.end())
				{
					strcpy(((*it).second)->info, token);
					IPaddress *ip=SDLNet_TCP_GetPeerAddress(connection->socket);
					printf("INFO : Game has been updated from IP 0x%x, there is %u games\n", ip->host, games.size());
				}
			}
		}
		else if (strcasecmp(token, "listgames")==0)
		{
			for (map<Uint32, Game *>::iterator it=games.begin(); it!=games.end(); ++it)
			{
				snprintf(data, GAME_INFO_MAX_SIZE, "0x%x:%s\n", (*it).first, (*it).second->info);
				if (sendString(connection->socket, data)==false)
				{
					handleCloseConnection(connection);
					return;
				}
			}
			snprintf(data, GAME_INFO_MAX_SIZE, "end\n");
			if (sendString(connection->socket, data)==false)
				handleCloseConnection(connection);
		}
		else if (strcasecmp(token, "listenon")==0)
		{
			connection->receiveChat=true;
		}
		else if (strcasecmp(token, "listenoff")==0)
		{
			connection->receiveChat=false;
		}
		else if (strcasecmp(token, "quit")==0)
		{
			handleCloseConnection(connection);
		}
		else if (strcasecmp(token, "help")==0)
		{
			char *helpMessage[]={ 	{ "\n" },
									{ "YOG - Ysagoon Online Gaming" },
									{ "(c) 2001 Stephane Magnenat" },
									{ "this program is GPL" },
									{ "this version is alpha" },
									{ ""},
									{ "commands :" },
									{ "newgame [game args]"},
									{ "deletegame"},
									{ "updategame [game args]"},
									{ "listgames"},
									{ "listenon"},
									{ "listenoff"},
									{ "quit"},
									{ "help"},
									{ "\n"},
									0 };
			char *thisText;
			int i=0;
			while ( (thisText=helpMessage[i])!=NULL)
			{
				snprintf(data, GAME_INFO_MAX_SIZE, "%s\n", thisText);
				if (sendString(connection->socket, data)==false)
				{
					handleCloseConnection(connection);
					return;
				}
				i++;
			}
		}
		else
		{
			printf("WARNING, command \"%s\" not found\n", token);
		}
	}
}

void GlobalStuff::handleNewConnection(void)
{
	TCPsocket newSocket=SDLNet_TCP_Accept(serverSocket);
	if (connections.size()<maxSocket)
	{
		SDLNet_TCP_AddSocket(allSocket, newSocket);
		Connection *connection=new Connection(newSocket);
		connections.insert(connection);
		IPaddress *ip=SDLNet_TCP_GetPeerAddress(connection->socket);
		printf("INFO : Connection has been created from IP 0x%x, there is %u connections\n", ip->host, connections.size());
	}
	else
	{
		SDLNet_TCP_Close(newSocket);
		printf("WARNING : Maximum connection reached (%d), connection rejected\n", newSocket);
	}
}

void GlobalStuff::handleSockets(void)
{
	if (SDLNet_SocketReady(serverSocket))
		handleNewConnection();

	for (set<Connection *>::iterator connectionIt=connections.begin(); connectionIt!=connections.end(); ++connectionIt)
	{
		if (SDLNet_SocketReady((*connectionIt)->socket))
			handleConnection(*connectionIt);
	}
}

void GlobalStuff::run(void)
{
	while (isRunning)
	{
		// perform network operations
		int checkResult=SDLNet_CheckSockets(allSocket, 100);
		if (checkResult==-1)
		{
			fprintf(stderr, "ERROR : system error while in select\n");
			exit(3);
		}
		else if (checkResult>0)
		{
			handleSockets();
		}

	}
}

bool GlobalStuff::getString(TCPsocket socket, char data[GAME_INFO_MAX_SIZE])
{
	int i;
	int value;
	char c;

	i=0;
	while ( (  (value=SDLNet_TCP_Recv(socket, &c, 1)) >0) && (i<GAME_INFO_MAX_SIZE-1))
	{
		if ((c==0) || (c=='\n') || (c=='\r'))
		{
			break;
		}
		else
		{
			data[i]=c;
		}
		i++;
	}
	data[i]=0;
	if (value<=0)
		return false;
	else
		return true;
}

bool GlobalStuff::sendString(TCPsocket socket, char *data)
{
	int len=strlen(data)+1; // add one for the terminating NULL
	int result=SDLNet_TCP_Send(socket, data, len);
	return (result==len);
}


int main(int argc, char *argv[])
{
	GlobalStuff global;
	global.welcomeMsg();
	global.parseCmd(argc, argv);
	global.init();
	global.run();
	return 0;
}
