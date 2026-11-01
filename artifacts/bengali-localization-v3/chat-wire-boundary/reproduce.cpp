#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
using Uint8=uint8_t; using Uint32=uint32_t;
Uint32 SDL_Swap32BE(Uint32 x) { return __builtin_bswap32(x); }
constexpr int ORDER_TEXT_MESSAGE_MAX_LEN=256;
class MessageOrder { public:
 MessageOrder()=default;
 MessageOrder(Uint32,Uint32,const char*); ~MessageOrder();
 Uint8* getData(); bool setData(const Uint8*,int,Uint32);
 Uint32 recipientsMask=0,messageOrderType=0; Uint8* data=nullptr; int length=0;
};
inline void addUint32(const Uint8 *data, Uint32 val, int pos)
{
	const Uint32 wire=SDL_Swap32BE(static_cast<Uint32>(val));
	memcpy(const_cast<Uint8 *>(data)+pos, &wire, sizeof(wire));
}

inline Uint32 getUint32(const Uint8 *data, int pos)
{
	Uint32 wire;
	memcpy(&wire, data+pos, sizeof(wire));
	return static_cast<Uint32>(SDL_Swap32BE(wire));
}

inline void addUint8(const Uint8 *data, Uint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Uint8 getUint8(const Uint8 *data, int pos)
{
	return *(((Uint8 *)data)+pos);
}

namespace Utilities {
int strmlen(const char *s, int max)
	{
		for (int i=0; i<max; i++)
			if (*(s+i)==0)
				return i+1;
		return max;
	}
}

MessageOrder::MessageOrder(Uint32 recipientsMask, Uint32 messageOrderType, const char * text)
{
	length=Utilities::strmlen(text, ORDER_TEXT_MESSAGE_MAX_LEN)+9;
	data=(Uint8 *)malloc(length);
	memcpy(data+9, text, length-9);
	data[length-1]=0;
	addUint32(data, recipientsMask, 0);
	addUint32(data, messageOrderType, 4);
	addUint8(data, (Uint8)(length-9), 8);
	this->recipientsMask=recipientsMask;
	this->messageOrderType=messageOrderType;
}

MessageOrder::~MessageOrder()
{
	if (data)
		free(data);
}

Uint8 *MessageOrder::getData(void)
{
	return data;
}

bool MessageOrder::setData(const Uint8 *data, int dataLength, Uint32 versionMinor)
{
	// Header and at least the text's terminator (the constructor always writes one).
	// With only the header, the text check below would read past the buffer.
	if (dataLength<10)
		return false;
	this->length=dataLength;
	this->recipientsMask=getUint32(data, 0);
	this->messageOrderType=getUint32(data, 4);
	Uint8 textLength=getUint8(data, 8);
	if (this->data!=NULL)
		free(this->data);
	this->data=(Uint8 *)malloc(dataLength);
	memcpy(this->data, data, dataLength);
	if (this->data[dataLength-1]!=0)
		return false;
	if (textLength!=Utilities::strmlen((const char *)(this->data+9), ORDER_TEXT_MESSAGE_MAX_LEN))
		return false;
	if (textLength!=dataLength-9)
		return false;
	return true;
}
int main() {
 std::string bn84; for(int i=0;i<84;++i) bn84 += "ক";
 for (const auto& c : {std::pair<std::string,std::string>{"ascii254",std::string(254,'a')},{"ascii255",std::string(255,'a')},{"bengali254",bn84+"ab"},{"bengali255",bn84+"ক"}}) {
  MessageOrder sent(1,1,c.second.c_str()); MessageOrder received;
  bool accepted=received.setData(sent.getData(),sent.length,0);
  std::cout << c.first << " input_bytes=" << c.second.size() << " wire_text_including_NUL=" << sent.length-9 << " uint8_length=" << unsigned(sent.getData()[8]) << " decoder_accepts=" << accepted << " exact_text_roundtrip=" << (accepted && c.second==std::string(reinterpret_cast<const char*>(received.getData()+9))) << "\n";
  if (accepted != (c.second.size()<=254)) return 1;
 }
}
