#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char *argv[])
{
	bool success=true;
	
	printf("Openning a (server)socket at port 7009...\n");
	UDPsocket serverSocket;
	serverSocket=SDLNet_UDP_Open(7009);
	
	if (!serverSocket)
	{
		printf("Failed to open the (server)socket!\n");
		return 1;
	}
	else
		printf("The (server)socket is open.\n");
	
	printf("Opening (another)socket at any port...\n");
	UDPsocket socket;
	IPaddress ip;
	int channel;
	socket=SDLNet_UDP_Open(0);
	
	if (!socket)
	{
		printf("Failed to open the (another)socket!\n");
		return 1;
	}
	else
		printf("The (another)socket is open.\n");
	
	ip.host=INADDR_BROADCAST;
	ip.port=SDL_SwapBE16(7009);
	printf("broadcasting adress=%d.%d.%d.%d\n", (ip.host>>24)&0xFF, (ip.host>>16)&0xFF, (ip.host>>8)&0xFF, (ip.host>>0)&0xFF);
	
	printf("Creating data...\n");
	int size=4;
	char data[size];
	data[0]=0;
	data[1]=9;
	data[2]=8;
	data[3]=0;
	printf("Data [%d, %d, %d, %d] created\n", data[0], data[1], data[2], data[3]);
	
	printf("Alocating packet...\n");
	UDPpacket *packet=SDLNet_AllocPacket(size);
	if (packet==NULL)
	{
		printf("Failed to blocat packet!\n");
		return 1;
	}
	else
		printf("Packet alocated.\n");
	
	printf("Filling packet.\n");
	packet->len=size;
	memcpy((char *)packet->data, data, size);
	packet->address=ip;
	packet->channel=channel;
	
	printf("Sending packet with the (another)socket and explicit address ...\n");
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
	{
		printf("Failed to send the packet!\n");
		return 1;
	}
	else
		printf("Succeded to send the packet.\n");
	
	
	printf("Binding the (another)socket to the broadcasting address and port 7009...\n");
	channel=SDLNet_UDP_Bind(socket, 1, &ip);
	
	if (channel==-1)
	{
		printf("Failed to bind the (another)socket!\n");
		return 1;
	}
	else
		printf("The (another)socket binded with SDL channel = %d\n", channel);
	
	printf("Sending packet with the (another)socket and SDL channel ...\n");
	packet->address.host=0;
	//packet->address.port=0;
	//packet->channel=-1;
	rv=SDLNet_UDP_Send(socket, channel, packet);
	if (rv!=1)
	{
		printf("Failed to send the packet!\n");
		return 1;
	}
	else
		printf("Succeded to send the packet.\n");
	
	int waitedPackets=1;
	printf("We wait for at least one packet on (server)socket...\n");
	while (waitedPackets>0)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(8);
		assert(packet);
		while (SDLNet_UDP_Recv(serverSocket, packet)==1)
		{
			printf("Packet received.\n");
			printf("packet=%d\n", (int)packet);
			printf("packet->channel=%d\n", packet->channel);
			printf("packet->len=%d\n", packet->len);
			printf("packet->maxlen=%d\n", packet->maxlen);
			printf("packet->status=%d\n", packet->status);
			printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
			printf("Data [%d, %d, %d, %d] received\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
			for (int i=0; i<4; i++)
				if ( packet->data[i]!=data[i])
					success=false;
			printf("host=%s\n", SDLNet_ResolveIP(&packet->address));
			waitedPackets--;
		}

		SDLNet_FreePacket(packet);
	}
	
	printf("Closing...\n");
	SDLNet_FreePacket(packet);
	SDLNet_UDP_Close(socket);
	SDLNet_UDP_Close(serverSocket);
	
	printf("Closed.\n");
	
	if (success)
		printf("Test is sucessfull.\n");
	return 0;
}
