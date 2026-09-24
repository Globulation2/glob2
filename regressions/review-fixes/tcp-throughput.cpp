#include "NetTransport.h"
#include <SDL.h>
#include <chrono>
#include <iostream>
#include <thread>
int main() {
 SDL_Init(0); SDLNet_Init(); IPaddress addr{};
 SDLNet_ResolveHost(&addr, nullptr, 19879);
 TCPsocket listener=SDLNet_TCP_Open(&addr);
 auto sender=makeNetTransport(); sender->open("127.0.0.1",19879);
 TCPsocket peer=nullptr;
 while (!(peer=SDLNet_TCP_Accept(listener))) SDL_Delay(1);
 while(sender->state()!=NetTransport::State::Connected) SDL_Delay(1);
 auto start=std::chrono::steady_clock::now();
 for(int i=0;i<300;i++) sender->send(std::vector<uint8_t>(16,42));
 int total=0; char buffer[8192];
 while(total<4800) {int n=SDLNet_TCP_Recv(peer,buffer,sizeof buffer); if(n<=0)break;total+=n;}
 std::cout<<total<<" bytes in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds\n";
 sender->close(); SDLNet_TCP_Close(peer); SDLNet_TCP_Close(listener);
 SDLNet_Quit(); SDL_Quit();
}
