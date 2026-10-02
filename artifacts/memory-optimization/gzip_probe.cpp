#include <zlib.h>
#include <vector>
#include <cstdio>
#include <cstdint>
std::vector<unsigned char> compress(size_t n,size_t chunk) {
 std::vector<unsigned char> in(n);uint32_t rng=19;for(auto& c:in){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;c=rng;}
 z_stream s{};deflateInit2(&s,0,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY);gz_header h{};h.os=255;deflateSetHeader(&s,&h);
 std::vector<unsigned char> out; std::vector<unsigned char> buf(chunk?chunk:deflateBound(&s,n));s.next_in=in.data();s.avail_in=n;int r;
 do{s.next_out=buf.data();s.avail_out=(chunk && s.total_out)?chunk-6:buf.size();size_t avail=s.avail_out;r=deflate(&s,Z_FINISH);out.insert(out.end(),buf.begin(),buf.begin()+avail-s.avail_out);}while(r==Z_OK);deflateEnd(&s);return out;
}
int main(){for(auto n:{262143,262144,262145,1048576}){auto a=compress(n,0),b=compress(n,262170);printf("n %d old %zu new %zu\n",n,a.size(),b.size());for(auto* p:{&a,&b}){size_t off=10;while(off+5<p->size()-8){auto len=(*p)[off+1]+256*(*p)[off+2];printf("%zu:%d:%u ",off,(*p)[off],len);off+=5+len;}puts("");}}}
