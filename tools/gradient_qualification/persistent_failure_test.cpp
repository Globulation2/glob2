// SPDX-License-Identifier: GPL-3.0-or-later
// Controlled subprocess only. Compile the ACTUAL helper against deterministic
// OpenCL stubs; no driver or GPU is loaded. The fatal case must kill this process
// before its stack sentinel or outstanding borrowed metadata can unwind.
#include "persistent_native.cpp"
#include <cstring>
#include <iterator>

namespace {
bool failDrain=false;
unsigned readCalls=0,drainCalls=0,submissions=0;
void* borrowedMetadata=nullptr;
const std::uint16_t* observedSeeds=nullptr;
const UInt* observedOutput=nullptr;
const std::uint16_t original[]{65535,65001,0,1};
struct Unwind {
 const char* path=nullptr;
 ~Unwind(){if(path)if(auto* file=std::fopen(path,"w")){std::fputs("destructor unwound\n",file);std::fclose(file);}}
};
Unwind staticSentinel; // Also detects std::exit/global teardown instead of _Exit.
}
extern "C" int clSetKernelArg(Handle,UInt,std::size_t,const void*){return 0;}
extern "C" int clEnqueueWriteBuffer(Handle,Handle,UInt blocking,std::size_t,std::size_t bytes,const void* source,
 UInt,const Handle*,Handle*){
 if(!blocking || bytes!=sizeof(original) || std::memcmp(source,original,sizeof(original)))return -61;
 return 0;
}
extern "C" int clEnqueueNDRangeKernel(Handle,Handle,UInt dimensions,const std::size_t*,const std::size_t* global,
 const std::size_t* local,UInt,const Handle*,Handle*){
 if(dimensions!=1 || *global!=64 || *local!=64)return -54;
 ++submissions;return 0;
}
extern "C" int clEnqueueReadBuffer(Handle,Handle,UInt blocking,std::size_t,std::size_t bytes,void* destination,
 UInt,const Handle*,Handle*){
 ++readCalls;
 if(!blocking || bytes!=8*sizeof(UInt) || readCalls!=1)return -30;
 borrowedMetadata=destination;
 return -17; // A failed read deliberately leaves a borrowed pointer outstanding.
}
extern "C" int clFinish(Handle){
 ++drainCalls;
 if(!observedSeeds || !observedOutput || std::memcmp(observedSeeds,original,sizeof(original)))return -67;
 for(unsigned i=0;i<4;++i)if(observedOutput[i]!=0xDEADBEEFu)return -68;
 if(failDrain){std::fputs("STUB_FATAL_SENTINELS_INTACT\n",stderr);return -42;} // Outstanding metadata lifetime remains unknown.
 if(!borrowedMetadata)return -30;
 // The successful drain touches the actual native stack array before it is
 // released. This is a lifetime test, not just a returned-error code test.
 std::fill_n(static_cast<UInt*>(borrowedMetadata),8,0x12345678u);
 borrowedMetadata=nullptr;return 0;
}
int main(int argc,char** argv){
 if(argc!=4 || (std::strcmp(argv[1],"drained") && std::strcmp(argv[1],"fatal")))return 64;
 failDrain=std::strcmp(argv[1],"fatal")==0;
 staticSentinel.path=argv[3];
 Unwind sentinel{argv[3]};
 std::uint16_t seeds[4];std::copy(std::begin(original),std::end(original),seeds);
 UInt output[]{0xDEADBEEFu,0xDEADBEEFu,0xDEADBEEFu,0xDEADBEEFu};
 Handle buffers[7]{};std::uint64_t metrics[16]{};
 observedSeeds=seeds;observedOutput=output;
 const int result=run_persistent(nullptr,nullptr,buffers,seeds,output,2,2,700,64,65536,65536,metrics,argv[2]);
 if(failDrain){std::fputs("ERROR: fatal helper returned and would unwind borrowed storage\n",stderr);return 65;}
 if(result!=-17 || drainCalls!=1 || readCalls!=1 || submissions!=1 || borrowedMetadata)return 66;
 if(std::memcmp(seeds,original,sizeof(seeds)))return 67;
 for(const auto value:output)if(value!=0xDEADBEEFu)return 68;
 if(metrics[8]!=1 || metrics[9]!=0 || metrics[14]!=0 || metrics[15]!=0)return 69;
 std::puts("successful drain retained native stack metadata; seeds/output intact; completion unknown");
 return 0;
}
