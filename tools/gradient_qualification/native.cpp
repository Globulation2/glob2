// SPDX-License-Identifier: GPL-3.0-or-later
// Offline, single-field, unprofiled dispatch. No dependency on OpenCL headers.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

using Handle = void *;
using UInt = std::uint32_t;
using Clock = std::chrono::steady_clock;
extern "C" {
int clSetKernelArg(Handle, UInt, std::size_t, const void *);
int clEnqueueWriteBuffer(Handle, Handle, UInt, std::size_t, std::size_t, const void *, UInt, const Handle *, Handle *);
int clEnqueueReadBuffer(Handle, Handle, UInt, std::size_t, std::size_t, void *, UInt, const Handle *, Handle *);
int clEnqueueFillBuffer(Handle, Handle, const void *, std::size_t, std::size_t, std::size_t, UInt, const Handle *, Handle *);
int clEnqueueNDRangeKernel(Handle, Handle, UInt, const std::size_t *, const std::size_t *, const std::size_t *, UInt, const Handle *, Handle *);
}
#define CHECK(call) do { const int error = (call); if (error) return error; } while (false)

extern "C" int run_native(Handle queue, Handle kernel, Handle *buffers,
    const std::uint16_t *seeds, const UInt *descriptors, std::uint16_t *output,
    UInt total, UInt width, UInt height, UInt threads, UInt bounded, double *stats)
{
    const auto start = Clock::now();
    const UInt pitch = (width + 15) / 16, rows = (height + 15) / 16;
    const UInt stride = pitch * rows, zero = 0, one = 1;
    const std::size_t global[]{std::size_t(pitch) * threads, rows, 1};
    const std::size_t local[]{threads, 1, 1};
    Handle a = buffers[0], b = buffers[1], active = buffers[12], next = buffers[13];
    for (UInt i = 2; i < 12; ++i)
        CHECK(clSetKernelArg(kernel, i, sizeof(Handle), &buffers[i]));
    CHECK(clSetKernelArg(kernel, 14, sizeof(UInt), &stride));
    CHECK(clSetKernelArg(kernel, 15, sizeof(UInt), &pitch));
    CHECK(clEnqueueWriteBuffer(queue, a, 1, 0, total * 2, seeds, 0, nullptr, nullptr));
    CHECK(clEnqueueWriteBuffer(queue, buffers[11], 1, 0, 32, descriptors, 0, nullptr, nullptr));
    CHECK(clEnqueueFillBuffer(queue, active, &one, 4, 0, stride * 4, 0, nullptr, nullptr));
    UInt rounds = 0, flag = 0;
    const UInt checks = bounded ? 1 : 8;
    do {
        for (UInt i = 0; i < checks; ++i) {
            if (i + 1 == checks)
                CHECK(clEnqueueFillBuffer(queue, buffers[10], &zero, 4, 0, 4, 0, nullptr, nullptr));
            CHECK(clEnqueueFillBuffer(queue, next, &zero, 4, 0, stride * 4, 0, nullptr, nullptr));
            CHECK(clSetKernelArg(kernel, 0, sizeof(Handle), &a));
            CHECK(clSetKernelArg(kernel, 1, sizeof(Handle), &b));
            CHECK(clSetKernelArg(kernel, 12, sizeof(Handle), &active));
            CHECK(clSetKernelArg(kernel, 13, sizeof(Handle), &next));
            CHECK(clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, local, 0, nullptr, nullptr));
            std::swap(a, b);
            std::swap(active, next);
            ++rounds;
        }
        // The selected dependency cone covers floor(cap/minStep) path edges.
        // Eligibility is checked before submission. No convergence read is needed.
        if (bounded) break;
        CHECK(clEnqueueReadBuffer(queue, buffers[10], 1, 0, 4, &flag, 0, nullptr, nullptr));
        if (flag && rounds >= 65536) return -999;
    } while (flag);
    CHECK(clEnqueueReadBuffer(queue, a, 1, 0, total * 2, output, 0, nullptr, nullptr));
    stats[0] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    stats[2] = rounds;
    return 0;
}

extern "C" int run_frontier(Handle queue, Handle kernel, Handle values, Handle costs,
    Handle active, Handle next, Handle changed, const UInt *seeds, UInt *output,
    UInt width, UInt height, UInt cap, double *stats)
{
    const auto start = Clock::now();
    const UInt cells = width * height, zero = 0, one = 1;
    const std::size_t global = ((std::size_t(cells) + 127) / 128) * 128, local = 128;
    CHECK(clEnqueueWriteBuffer(queue, values, 1, 0, cells * 4, seeds, 0, nullptr, nullptr));
    CHECK(clEnqueueFillBuffer(queue, active, &one, 4, 0, cells * 4, 0, nullptr, nullptr));
    CHECK(clSetKernelArg(kernel, 0, sizeof(Handle), &values));
    CHECK(clSetKernelArg(kernel, 1, sizeof(Handle), &costs));
    CHECK(clSetKernelArg(kernel, 4, sizeof(Handle), &changed));
    CHECK(clSetKernelArg(kernel, 5, 4, &width));
    CHECK(clSetKernelArg(kernel, 6, 4, &height));
    CHECK(clSetKernelArg(kernel, 7, 4, &cap));
    UInt rounds = 0, flag = 0;
    do {
        for (UInt i = 0; i < 8; ++i) {
            CHECK(clEnqueueFillBuffer(queue, next, &zero, 4, 0, cells * 4, 0, nullptr, nullptr));
            if (i == 7)
                CHECK(clEnqueueFillBuffer(queue, changed, &zero, 4, 0, 4, 0, nullptr, nullptr));
            CHECK(clSetKernelArg(kernel, 2, sizeof(Handle), &active));
            CHECK(clSetKernelArg(kernel, 3, sizeof(Handle), &next));
            CHECK(clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr));
            std::swap(active, next);
            ++rounds;
        }
        CHECK(clEnqueueReadBuffer(queue, changed, 1, 0, 4, &flag, 0, nullptr, nullptr));
        if (flag && rounds >= 65536) return -999;
    } while (flag);
    CHECK(clEnqueueReadBuffer(queue, values, 1, 0, cells * 4, output, 0, nullptr, nullptr));
    stats[0] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    stats[2] = rounds;
    return 0;
}

extern "C" int run_compact(Handle queue,Handle kernel,Handle* buffers,
 const UInt* seeds,const UInt* initial,UInt* output,UInt seedCount,
 UInt width,UInt height,UInt cap,double* stats)
{
    const auto started=Clock::now();
    const UInt cells=width*height,zero=0;
    if(!seedCount) {
        std::copy_n(seeds,cells,output);stats[0]=0;stats[2]=0;return 0;
    }
    CHECK(clEnqueueWriteBuffer(queue,buffers[0],1,0,cells*4,seeds,0,nullptr,nullptr));
    CHECK(clEnqueueWriteBuffer(queue,buffers[2],1,0,seedCount*4,initial,0,nullptr,nullptr));
    CHECK(clEnqueueFillBuffer(queue,buffers[4],&zero,4,0,cells*4,0,nullptr,nullptr));
    for(UInt i:{0u,1u,4u,5u})CHECK(clSetKernelArg(kernel,i,sizeof(Handle),buffers+i));
    CHECK(clSetKernelArg(kernel,8,sizeof(UInt),&width));
    CHECK(clSetKernelArg(kernel,9,sizeof(UInt),&height));
    CHECK(clSetKernelArg(kernel,10,sizeof(UInt),&cap));
    Handle active=buffers[2],next=buffers[3];UInt count=seedCount,rounds=0;
    while(count) {
        if(rounds==65536)return -999;
        const UInt epoch=++rounds;
        CHECK(clEnqueueFillBuffer(queue,buffers[5],&zero,4,0,4,0,nullptr,nullptr));
        CHECK(clSetKernelArg(kernel,2,sizeof(Handle),&active));
        CHECK(clSetKernelArg(kernel,3,sizeof(Handle),&next));
        CHECK(clSetKernelArg(kernel,6,sizeof(UInt),&count));
        CHECK(clSetKernelArg(kernel,7,sizeof(UInt),&epoch));
        const std::size_t global=((std::size_t(count)+127)/128)*128,local=128;
        CHECK(clEnqueueNDRangeKernel(queue,kernel,1,nullptr,&global,&local,0,nullptr,nullptr));
        CHECK(clEnqueueReadBuffer(queue,buffers[5],1,0,4,&count,0,nullptr,nullptr));
        if(count>cells)return -998; // retain overflow as a qualification failure
        std::swap(active,next);
    }
    CHECK(clEnqueueReadBuffer(queue,buffers[0],1,0,cells*4,output,0,nullptr,nullptr));
    stats[0]=std::chrono::duration<double,std::milli>(Clock::now()-started).count();stats[2]=rounds;
    return 0;
}

// Independent development ABI. The original compact control above remains
// unchanged. Exactly eight ordered rounds per read, including empty rounds,
// reduce host synchronization while retaining global visibility boundaries.
extern "C" int run_compact_grouped(Handle queue,Handle kernel,Handle* buffers,
 const UInt* seeds,const UInt* initial,UInt* output,UInt seedCount,
 UInt width,UInt height,UInt cap,UInt groups,double* stats,std::uint64_t* metrics)
{
    const auto started=Clock::now();
    std::fill_n(metrics,14,0);
    if(!width || !height || !groups || groups>4096 || width>std::numeric_limits<UInt>::max()/height ||
       cap>65533 || std::size_t(groups)>std::numeric_limits<std::size_t>::max()/128)return -997;
    const UInt cells=width*height,zero=0;
    if(std::uint64_t(cells)*20+40>128ull*1024*1024)return -997;
    if(seedCount>cells)return -998;
    metrics[8]=std::uint64_t(cells)*20+40; // values,costs,two lists,stamps,metadata
    if(!seedCount){
        std::copy_n(seeds,cells,output);stats[2]=0;
        stats[0]=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        return 0;
    }
    CHECK(clEnqueueWriteBuffer(queue,buffers[0],1,0,std::size_t(cells)*4,seeds,0,nullptr,nullptr));
    metrics[4]=std::uint64_t(cells)*4;
    CHECK(clEnqueueWriteBuffer(queue,buffers[2],1,0,std::size_t(seedCount)*4,initial,0,nullptr,nullptr));
    metrics[5]=std::uint64_t(seedCount)*4;
    CHECK(clEnqueueFillBuffer(queue,buffers[4],&zero,4,0,std::size_t(cells)*4,0,nullptr,nullptr));
    metrics[12]=std::uint64_t(cells)*4;
    std::array<UInt,10> metadata{seedCount};
    CHECK(clEnqueueWriteBuffer(queue,buffers[5],1,0,sizeof(metadata),metadata.data(),0,nullptr,nullptr));
    for(UInt i:{0u,1u,4u,5u}){CHECK(clSetKernelArg(kernel,i,sizeof(Handle),buffers+i));++metrics[9];}
    CHECK(clSetKernelArg(kernel,8,sizeof(UInt),&width));++metrics[9];
    CHECK(clSetKernelArg(kernel,9,sizeof(UInt),&height));++metrics[9];
    CHECK(clSetKernelArg(kernel,10,sizeof(UInt),&cap));++metrics[9];
    Handle active=buffers[2],next=buffers[3];UInt slot=0,rounds=0,count=seedCount;
    const std::size_t global=std::size_t(groups)*128,local=128;
    while(count){
        if(rounds==65536)return -999;
        for(unsigned dispatch=0;dispatch<8;++dispatch){
            const UInt epoch=++rounds;
            CHECK(clEnqueueFillBuffer(queue,buffers[5],&zero,4,std::size_t(1u-slot)*4,4,0,nullptr,nullptr));
            ++metrics[10];
            CHECK(clSetKernelArg(kernel,2,sizeof(Handle),&active));++metrics[9];
            CHECK(clSetKernelArg(kernel,3,sizeof(Handle),&next));++metrics[9];
            CHECK(clSetKernelArg(kernel,6,sizeof(UInt),&slot));++metrics[9];
            CHECK(clSetKernelArg(kernel,7,sizeof(UInt),&epoch));++metrics[9];
            CHECK(clEnqueueNDRangeKernel(queue,kernel,1,nullptr,&global,&local,0,nullptr,nullptr));
            metrics[11]+=global;
            std::swap(active,next);slot=1u-slot;
        }
        CHECK(clEnqueueReadBuffer(queue,buffers[5],1,0,sizeof(metadata),metadata.data(),0,nullptr,nullptr));
        ++metrics[0];metrics[7]+=sizeof(metadata);
        metrics[1]=metadata[2]|(std::uint64_t(metadata[3])<<32);
        metrics[2]=metadata[4]|(std::uint64_t(metadata[5])<<32);
        metrics[3]=metadata[6]|(std::uint64_t(metadata[7])<<32);
        if(metadata[8] || metadata[9])return -998;
        count=metadata[slot];if(count>cells)return -998;
    }
    // No queued round can restart from an empty frontier. The successful
    // in-order metadata read proves the final empty fixed point and completion.
    CHECK(clEnqueueReadBuffer(queue,buffers[0],1,0,std::size_t(cells)*4,output,0,nullptr,nullptr));
    metrics[6]=std::uint64_t(cells)*4;metrics[13]=count;
    stats[0]=std::chrono::duration<double,std::milli>(Clock::now()-started).count();stats[2]=rounds;
    return 0;
}
