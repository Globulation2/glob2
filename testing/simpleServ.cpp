#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char *argv[])
{
	UDPsocket socket;
	socket=SDLNet_UDP_Open(7008);
	
	if (!socket)
		printf("failed to open sockat at port 7008.\n");
		
	if (socket)
	{
		bool wait=true;
		printf("waiting...\n");
		while (wait)
		{
			UDPpacket *packet=NULL;
			packet=SDLNet_AllocPacket(8);
			assert(packet);

			while (SDLNet_UDP_Recv(socket, packet)==1)
			{
				printf("Packet received.\n");
				printf("packet=%d\n", (int)packet);
				printf("packet->channel=%d\n", packet->channel);
				printf("packet->len=%d\n", packet->len);
				printf("packet->maxlen=%d\n", packet->maxlen);
				printf("packet->status=%d\n", packet->status);
				printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
				printf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));
				printf("addr=0x%8.8x:%d\n", packet->address.host);
				

				printf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);
				
				wait=false;
			}

			SDLNet_FreePacket(packet);
		}
	}
	
	return 0;
}
