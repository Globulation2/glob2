#include "MusicLibrary.h"
#include <zlib.h>
#include <algorithm>
#include <iostream>
namespace
{
void zip32(std::vector<unsigned char> &bytes, size_t at, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		bytes[at + i] = value >> (i * 8);
}
// One structurally valid stored entry; validation then rejects the incomplete set.
std::vector<unsigned char> singleEntryZip()
{
	const std::string name = "a1.opus";
	std::vector<unsigned char> bytes(113);
	zip32(bytes, 0, 0x04034b50);
	zip32(bytes, 14, crc32(0, reinterpret_cast<const Bytef *>("x"), 1));
	zip32(bytes, 18, 1);
	zip32(bytes, 22, 1);
	bytes[26] = name.size();
	std::copy(name.begin(), name.end(), bytes.begin() + 30);
	bytes[37] = 'x';
	zip32(bytes, 38, 0x02014b50);
	zip32(bytes, 38 + 16, crc32(0, reinterpret_cast<const Bytef *>("x"), 1));
	zip32(bytes, 38 + 20, 1);
	zip32(bytes, 38 + 24, 1);
	bytes[38 + 28] = name.size();
	std::copy(name.begin(), name.end(), bytes.begin() + 38 + 46);
	zip32(bytes, 91, 0x06054b50);
	bytes[91 + 8] = bytes[91 + 10] = 1;
	zip32(bytes, 91 + 12, 53);
	zip32(bytes, 91 + 16, 38);
	return bytes;
}
} // namespace
int main() {
 static_assert(sizeof(size_t) == 4);
 Music::Library library("/tmp/music-bounds");
 unsigned rejected=0;
 for(size_t field : {size_t(80),size_t(58),size_t(103),size_t(107)})
  for(std::uint32_t value : {0xffffffffu,0xfffffffeu,0xfffffff0u}) {
   auto archive=singleEntryZip(); zip32(archive,field,value);
   try { library.importZip(archive); return 1; } catch(const std::exception&) {++rejected;}
  }
 std::cout << rejected << " malformed ZIP offsets/sizes rejected with 32-bit size_t\n";
 return rejected == 12 && library.list().empty() ? 0 : 1;
}
