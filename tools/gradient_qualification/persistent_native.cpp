// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only ABI. One complete field, one resident work-group, immutable
// original ushort upload, staged uint output. Incomplete output is never read.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
using Handle=void*;using UInt=std::uint32_t;
extern "C" {
int clFinish(Handle);
int clSetKernelArg(Handle,UInt,std::size_t,const void*);
int clEnqueueWriteBuffer(Handle,Handle,UInt,std::size_t,std::size_t,const void*,UInt,const Handle*,Handle*);
int clEnqueueReadBuffer(Handle,Handle,UInt,std::size_t,std::size_t,void*,UInt,const Handle*,Handle*);
int clEnqueueNDRangeKernel(Handle,Handle,UInt,const std::size_t*,const std::size_t*,const std::size_t*,UInt,const Handle*,Handle*);
}
#define CHECK(call) do {const int error=(call);if(error){clFinish(queue);return error;}}while(false)
extern "C" int run_persistent(Handle queue,Handle kernel,Handle* buffers,const std::uint16_t* seeds,UInt* output,
 UInt width,UInt height,UInt cap,UInt threads,UInt popLimit,UInt epochLimit,std::uint64_t* metrics)
{
 std::fill_n(metrics,16,0);
 if(!width || !height || width>std::numeric_limits<UInt>::max()/height || cap>UInt(std::numeric_limits<int>::max()) ||
    (threads!=64 && threads!=128 && threads!=256) || !popLimit || popLimit>(1u<<24) ||
    !epochLimit || epochLimit>65536)return -997;
 const UInt cells=width*height;
 if(std::uint64_t(cells)*22+32>128ull*1024*1024)return -997;
 for(UInt i=0;i<7;++i)CHECK(clSetKernelArg(kernel,i,sizeof(Handle),buffers+i));
 const UInt arguments[]{width,height,cap,popLimit,epochLimit};
 for(UInt i=0;i<5;++i)CHECK(clSetKernelArg(kernel,7+i,sizeof(UInt),arguments+i));
 metrics[10]=12;metrics[11]=std::uint64_t(cells)*22+32;
 CHECK(clEnqueueWriteBuffer(queue,buffers[0],1,0,std::size_t(cells)*2,seeds,0,nullptr,nullptr));
 metrics[12]=std::uint64_t(cells)*2;
 const std::size_t global=threads,local=threads;
 CHECK(clEnqueueNDRangeKernel(queue,kernel,1,nullptr,&global,&local,0,nullptr,nullptr));
 metrics[8]=1; // submitted; device completion remains unknown until read succeeds.
 std::array<UInt,8> metadata{};
 CHECK(clEnqueueReadBuffer(queue,buffers[6],1,0,sizeof(metadata),metadata.data(),0,nullptr,nullptr));
 // A failed blocking read is not completion proof. CHECK drains inside
 // this stack frame before its metadata storage can expire on any API error.
 std::copy(metadata.begin(),metadata.end(),metrics);metrics[9]=1;metrics[13]=sizeof(metadata);metrics[15]=1;
 if(metadata[7] || metadata[0]>1 || metadata[2]>popLimit || metadata[1]>epochLimit ||
    metadata[3]>cells || metadata[5]>cells || metadata[6]>cells)return -998;
 if(metadata[0])return 1; // exact CPU recovery consumes ORIGINAL seeds.
 CHECK(clEnqueueReadBuffer(queue,buffers[1],1,0,std::size_t(cells)*4,output,0,nullptr,nullptr));
 metrics[14]=std::uint64_t(cells)*4;return 0;
}
