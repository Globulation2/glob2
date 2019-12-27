/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "NetConnectionThread.h"
#include "StreamBackend.h"
#include "BinaryStream.h"
#include "NetMessage.h"
#include "boost/lexical_cast.hpp"

using namespace GAGCore;

NetConnectionThread::NetConnectionThread(std::queue<boost::shared_ptr<NetConnectionThreadMessage> >& outgoing, boost::recursive_mutex& outgoingMutex)
	: outgoing(outgoing),  outgoingMutex(outgoingMutex)
{
	set=SDLNet_AllocSocketSet(1);
	connected=false;
	hasExited = false;
}



NetConnectionThread::~NetConnectionThread()
{
	SDLNet_FreeSocketSet(set);
}



void NetConnectionThread::operator()()
{
	while(true)
	{
		SDL_Delay(20);
		{
			//First parse incoming thread messages
			while(true)
			{
				boost::shared_ptr<NetConnectionThreadMessage> message;
				{
					boost::recursive_mutex::scoped_lock lock(incomingMutex);
					if(!incoming.empty())
					{
						message = incoming.front();
						incoming.pop();
					}
					else
					{
						break;
					}
				}
				Uint8 type = message->getMessageType();
				switch(type)
				{
					case NTMConnect:
					{
						boost::shared_ptr<NTConnect> info = static_pointer_cast<NTConnect>(message);
						if(!connected)
						{
							//Resolve the address
							if(SDLNet_ResolveHost(&address, info->getServer().c_str(), info->getPort()) == -1)
							{
								boost::shared_ptr<NTCouldNotConnect> error(new NTCouldNotConnect(SDLNet_GetError()));
								sendToMainThread(error);
							}
							else
							{
								//Open the connection
								socket=SDLNet_TCP_Open(&address);
								if(!socket)
								{
									boost::shared_ptr<NTCouldNotConnect> error(new NTCouldNotConnect(SDLNet_GetError()));
									sendToMainThread(error);
								}
								else
								{
									SDLNet_TCP_AddSocket(set, socket);
									connected=true;
									boost::shared_ptr<NTConnected> connected(new NTConnected(info->getServer()));
									sendToMainThread(connected);
								}
							}
						}
					}
					break;
					case NTMCloseConnection:
					{
						boost::shared_ptr<NTCloseConnection> info = static_pointer_cast<NTCloseConnection>(message);
						if(connected)
						{
							closeConnection();
						}
					}
					break;
					case NTMSendMessage:
					{
						boost::shared_ptr<NTSendMessage> info = static_pointer_cast<NTSendMessage>(message);
						if(connected)
						{
							boost::shared_ptr<NetMessage> message = info->getMessage();
							//std::cout<<"Sending: "<<message->format()<<std::endl;
							MemoryStreamBackend* msb = new MemoryStreamBackend;
							BinaryOutputStream* bos = new BinaryOutputStream(msb);
							bos->writeUint8(message->getMessageType(), "messageType");
							message->encodeData(bos);

							msb->seekFromEnd(0);
							Uint32 length = msb->getPosition();
							msb->seekFromStart(0);

							Uint8* newData = new Uint8[length+2];
							SDLNet_Write16(length, newData);
							msb->read(newData+2, length);

							Uint32 result=SDLNet_TCP_Send(socket, newData, length+2);
							if(result<(length+2))
							{
								boost::shared_ptr<NTLostConnection> error(new NTLostConnection(SDLNet_GetError()));
								sendToMainThread(error);
								closeConnection();
							}

							/*
							amount += length;
							if(amount >= 1024)
							{
								Uint32 newTime = SDL_GetTicks();
								std::cout<<"bandwidth usage: " << float(amount * 1000) / float(newTime - lastTime) <<std::endl;
								lastTime = newTime;
								amount = 0;
							}
							*/

							delete bos;
							delete[] newData;
						}
					}
					break;
					case NTMAcceptConnection:
					{
						boost::shared_ptr<NTAcceptConnection> info = static_pointer_cast<NTAcceptConnection>(message);
						if(!connected)
						{
							connected=true;
							socket=info->getSocket();
							address = *SDLNet_TCP_GetPeerAddress(socket);
							SDLNet_TCP_AddSocket(set, socket);
						}
					}
					break;
					case NTMExitThread:
					{
						if(connected)
						{
							closeConnection();
						}
						hasExited=true;
						return;
					}
					break;
				}
			}

			while (connected)
			{
				SDL_Delay(50);
				int numReady = SDLNet_CheckSockets(set, 0);
				//This checks if there are any active sockets.
				//SDLNet_CheckSockets is used because it is non-blocking
				if(numReady==-1)
				{
					boost::shared_ptr<NTLostConnection> error(new NTLostConnection(SDLNet_GetError()));
					sendToMainThread(error);
					perror("SDLNet_CheckSockets");
					if(connected)
						closeConnection();
					break;
				}
				else if(numReady)
				{
					//Read and interpret the length of the message
					Uint8* lengthData = new Uint8[2];
					int amount = SDLNet_TCP_Recv(socket, lengthData, 2);
					if(amount <= 0)
					{
						boost::shared_ptr<NTLostConnection> error(new NTLostConnection(SDLNet_GetError()));
						sendToMainThread(error);
						closeConnection();
					}
					else
					{
						Uint16 length = SDLNet_Read16(lengthData);
						//Read in the data.
						Uint8* data = new Uint8[length];

						for(int i=0; i<length; ++i)
						{
							amount = SDLNet_TCP_Recv(socket, data+i, 1);
							if(amount <= 0)
							{
								boost::shared_ptr<NTLostConnection> error(new NTLostConnection(SDLNet_GetError()));
								sendToMainThread(error);
								closeConnection();
							}
						}
						if(connected)
						{
						/*
							amount += length;
							if(amount >= 1024)
							{
								Uint32 newTime = SDL_GetTicks();
								std::cout<<"bandwidth usage: " << float(amount * 1000) / float(newTime - lastTime) <<std::endl;
								lastTime = newTime;
								amount = 0;
							}
						*/

							MemoryStreamBackend* msb = new MemoryStreamBackend(data, length);
							msb->seekFromStart(0);
							BinaryInputStream* bis = new BinaryInputStream(msb);

							//Now interpret the message from the data, and add it to the queue
							shared_ptr<NetMessage> message = NetMessage::getNetMessage(bis);
							boost::shared_ptr<NTRecievedMessage> recieved(new NTRecievedMessage(message));
							sendToMainThread(recieved);

							//std::cout<<"Recieved: "<<message->format()<<std::endl;

							delete bis;
						}
						delete[] data;
					}
					delete[] lengthData;
				}
				else
				{
					break;
				}
			}
		}
	}
}



void NetConnectionThread::sendMessage(boost::shared_ptr<NetConnectionThreadMessage> message)
{
	boost::recursive_mutex::scoped_lock lock(incomingMutex);
	incoming.push(message);
}



bool NetConnectionThread::hasThreadExited()
{
	return hasExited;
}



bool NetConnectionThread::isConnected()
{
	return connected;
}



void NetConnectionThread::closeConnection()
{
	SDLNet_TCP_DelSocket(set, socket);
	SDLNet_TCP_Close(socket);
	connected=false;
}



void NetConnectionThread::sendToMainThread(boost::shared_ptr<NetConnectionThreadMessage> message)
{
	boost::recursive_mutex::scoped_lock lock(outgoingMutex);
	outgoing.push(message);
}




