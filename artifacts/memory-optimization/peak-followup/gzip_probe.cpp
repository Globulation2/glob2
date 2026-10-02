#include <ChunkedStreamBackend.h>
#include <FileManager.h>
#include <GzipUtil.h>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <cstdlib>
using namespace GAGCore;
bool FileManager::isAbsolutePath(const std::string& s) { return !s.empty() && s[0]=='/'; }
StreamBackend* FileManager::openInputStreamBackend(const std::string s) { return new FileStreamBackend(fopen(s.c_str(),"rb")); }
std::string read(const std::string& p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
int main() {
 for (size_t n : {size_t(0),size_t(1048576),size_t(1048577),size_t(3145745)}) {
  std::string bytes(n,'x'); uint32_t r=19; for(char& c:bytes) { r^=r<<13;r^=r>>17;r^=r<<5;c=char(r); }
  ChunkedBuffer chunks; chunks.writeAt(0,bytes.data(),bytes.size());
  for(int level : {0,1,6,9,-1}) {
   std::string expected; if(!gzipCompress(bytes,level,expected)) return 1;
   const std::string path="artifacts/memory-optimization/peak-followup/probe.gz";
   if(!writeGzipAtomicToPath(path,chunks,level) || read(path)!=expected) { std::cerr<<"compression mismatch "<<n<<" "<<level<<"\n";return 2; }
   std::unique_ptr<StreamBackend> stream(openInflatingFileStreamBackend(path));
   std::string actual(n,'\0'); if(!stream->isValid() || !stream->readExact(actual.data(),n) || actual!=bytes) return 3;
   auto* b=dynamic_cast<ChunkedStreamBackend*>(stream.get()); if(!b || b->contents().allocatedCapacity()>n+ChunkedBuffer::blockSize) return 4;
  }
 }
 std::cout<<"PASS exact gzip bytes at five levels across empty/full/partial/multiple blocks; direct inflate and capacity bounds\n";
}
