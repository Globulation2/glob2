#include "Marshaling.h"
#include <cassert>
int main(){for(int offset=0;offset<8;++offset){Uint8 b[32]={};addUint32(b,0x12345678u,offset);assert(b[offset]==0x12&&b[offset+3]==0x78);assert(getUint32(b,offset)==0x12345678u);addSint32(b,-1234567,offset);assert(getSint32(b,offset)==-1234567);addUint16(b,0xabcd,offset);assert(b[offset]==0xab&&b[offset+1]==0xcd);assert(getUint16(b,offset)==0xabcd);addSint16(b,-1234,offset);assert(getSint16(b,offset)==-1234);} }
