// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLGradient.h"
#include "GradientBackend.h"
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_platform_defines.h>
#include <array>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace gradient_kernel
{
namespace
{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
// Private subset of the stable OpenCL 1.2 C ABI. Load the installed driver with
// SDL, so neither an SDK nor an OpenCL library is a required build/runtime input.
// Opaque handles, 32-bit cl_uint/cl_int and 64-bit cl_bitfield match Khronos' ABI.
#if defined(_WIN32)
#define CL_CALL __stdcall
#else
#define CL_CALL
#endif
using Handle = void *;
using UInt = std::uint32_t;
using Int = std::int32_t;
using Bits = std::uint64_t;
struct API
{
    SDL_SharedObject *library = nullptr;
#define CL_FUNCTION(name, result, ...) result(CL_CALL *name)(__VA_ARGS__) = nullptr
    CL_FUNCTION(GetPlatformIDs, Int, UInt, Handle *, UInt *);
    CL_FUNCTION(GetDeviceIDs, Int, Handle, Bits, UInt, Handle *, UInt *);
    CL_FUNCTION(GetDeviceInfo, Int, Handle, UInt, std::size_t, void *, std::size_t *);
    CL_FUNCTION(CreateContext, Handle, const std::intptr_t *, UInt, const Handle *,
                void(CL_CALL *)(const char *, const void *, std::size_t, void *), void *, Int *);
    CL_FUNCTION(CreateCommandQueue, Handle, Handle, Handle, Bits, Int *);
    CL_FUNCTION(CreateProgramWithSource, Handle, Handle, UInt, const char **, const std::size_t *, Int *);
    CL_FUNCTION(BuildProgram, Int, Handle, UInt, const Handle *, const char *,
                void(CL_CALL *)(Handle, void *), void *);
    CL_FUNCTION(GetProgramBuildInfo, Int, Handle, Handle, UInt, std::size_t, void *, std::size_t *);
    CL_FUNCTION(CreateKernel, Handle, Handle, const char *, Int *);
    CL_FUNCTION(GetKernelWorkGroupInfo, Int, Handle, Handle, UInt, std::size_t, void *, std::size_t *);
    CL_FUNCTION(CreateBuffer, Handle, Handle, Bits, std::size_t, void *, Int *);
    CL_FUNCTION(SetKernelArg, Int, Handle, UInt, std::size_t, const void *);
    CL_FUNCTION(EnqueueWriteBuffer, Int, Handle, Handle, UInt, std::size_t, std::size_t, const void *, UInt,
                const Handle *, Handle *);
    CL_FUNCTION(EnqueueFillBuffer, Int, Handle, Handle, const void *, std::size_t, std::size_t, std::size_t,
                UInt, const Handle *, Handle *);
    CL_FUNCTION(EnqueueReadBuffer, Int, Handle, Handle, UInt, std::size_t, std::size_t, void *, UInt,
                const Handle *, Handle *);
    CL_FUNCTION(EnqueueNDRangeKernel, Int, Handle, Handle, UInt, const std::size_t *, const std::size_t *,
                const std::size_t *, UInt, const Handle *, Handle *);
    CL_FUNCTION(Finish, Int, Handle);
    CL_FUNCTION(ReleaseMemObject, Int, Handle);
    CL_FUNCTION(ReleaseKernel, Int, Handle);
    CL_FUNCTION(ReleaseProgram, Int, Handle);
    CL_FUNCTION(ReleaseCommandQueue, Int, Handle);
    CL_FUNCTION(ReleaseContext, Int, Handle);
#undef CL_FUNCTION
    ~API()
    {
        if (library)
            SDL_UnloadObject(library);
    }
    void load()
    {
#if defined(_WIN32)
        library = SDL_LoadObject("OpenCL.dll");
#elif defined(__APPLE__)
        library = SDL_LoadObject("/System/Library/Frameworks/OpenCL.framework/OpenCL");
#else
        library = SDL_LoadObject("libOpenCL.so.1");
#endif
        if (!library)
            throw std::runtime_error("OpenCL driver library unavailable");
#define LOAD(name)                                                                                           \
    name = reinterpret_cast<decltype(name)>(SDL_LoadFunction(library, "cl" #name));                          \
    if (!name)                                                                                               \
    throw std::runtime_error("Missing OpenCL function: cl" #name)
        LOAD(GetPlatformIDs);
        LOAD(GetDeviceIDs);
        LOAD(GetDeviceInfo);
        LOAD(CreateContext);
        LOAD(CreateCommandQueue);
        LOAD(CreateProgramWithSource);
        LOAD(BuildProgram);
        LOAD(GetProgramBuildInfo);
        LOAD(CreateKernel);
        LOAD(GetKernelWorkGroupInfo);
        LOAD(CreateBuffer);
        LOAD(SetKernelArg);
        LOAD(EnqueueWriteBuffer);
        LOAD(EnqueueFillBuffer);
        LOAD(EnqueueReadBuffer);
        LOAD(EnqueueNDRangeKernel);
        LOAD(Finish);
        LOAD(ReleaseMemObject);
        LOAD(ReleaseKernel);
        LOAD(ReleaseProgram);
        LOAD(ReleaseCommandQueue);
        LOAD(ReleaseContext);
#undef LOAD
    }
};
void check(Int result)
{
    if (result != 0)
        throw std::runtime_error("OpenCL error " + std::to_string(result));
}
constexpr const char *source = R"CL(
#define HALO (FROZEN_HALO?1:STEPS)
#define UPDATE_X (FROZEN_HALO?CORE_X:SIDE_X)
#define UPDATE_Y (FROZEN_HALO?CORE_Y:SIDE_Y)
#define FIRST_CELL (FROZEN_HALO?1:0)
#if FROZEN_HALO && !COLOR_RELAXATION
#error Frozen halos require colored relaxation
#endif
#define SIDE_X (CORE_X+2*HALO)
#define SIDE_Y (CORE_Y+2*HALO)
#define PATCH (SIDE_X*SIDE_Y)
inline int wrap(int x,uint size){if((size&(size-1))==0)return x&(size-1);x%=(int)size;return x<0?x+size:x;}
__kernel void propagate(__global const ushort *a,__global ushort *b,
 __global const uint *c0,__global const uint *c1,__global const uint *c2,__global const uint *c3,
 __global const uint *c4,__global const uint *c5,__global const uint *c6,__global const uint *c7,
 __global uint *changed,__global const uint *desc,
 __global const uint *active,__global uint *nextActive,uint stride,uint pitch)
{
 uint f=get_group_id(2),d=f*8,w=desc[d],h=desc[d+1],base=desc[d+2],cb=desc[d+3],cap=desc[d+4];
 __global const uint *costs=cb==0?c0:cb==1?c1:cb==2?c2:cb==3?c3:cb==4?c4:cb==5?c5:cb==6?c6:c7;
 // A uniform plane needs no per-cell local cost loads. Bit zero is the
 // existing active-field flag; bit one describes the immutable cost plane.
 uint uniform=desc[d+5]&2,packed=costs[0],cardinal=packed&65535,diagonal=packed>>16;
 // Both acceptance tests reduce to a single lower bound on the candidate.
 // Source and edge values fit ushort, so their signed difference cannot overflow.
 int threshold=max(2,65535-(int)cap);
 uint gx=get_group_id(0),gy=get_group_id(1),tile=f*stride+gy*pitch+gx;

 uint lid=get_local_id(0);
 if(gx>=desc[d+6]||gy>=desc[d+7]||!desc[d+5])return; // Entire workgroup/retired field.
 if(!active[tile]){
  // Keep ping-pong buffers coherent. Only propagation/local-memory work is skipped.
  for(uint o=lid;o<CORE_X*CORE_Y;o+=WG){uint x=gx*CORE_X+o%CORE_X,y=gy*CORE_Y+o/CORE_X;
  if(x<w&&y<h){uint i=base+y*w+x;b[i]=a[i];}}return;
 }
 __local uint c[PATCH],flags[WG];
#if COLOR_RELAXATION
 __local ushort v[PATCH];
 __local uint firstSweepChanged;
#else
 __local ushort storage[2][PATCH];
 __local ushort *v=storage[0],*next=storage[1];
#endif
 for(uint j=lid;j<PATCH;j+=WG){
  int sx=wrap(gx*CORE_X+(int)(j%SIDE_X)-HALO,w),sy=wrap(gy*CORE_Y+(int)(j/SIDE_X)-HALO,h);
  v[j]=a[base+sy*w+sx];if(!uniform)c[j]=costs[sy*w+sx];
 }
 barrier(CLK_LOCAL_MEM_FENCE);
#if COLOR_RELAXATION
 // Four parity classes have no adjacent cells within a class, so each
 // in-place sweep is race-free. Every update extends a valid path and is
 // monotone. Repeated tile exchanges reach the same exact fixed point;
 // a sweep need not equal a fixed number of Jacobi rounds.
 for(uint round=0;round<STEPS;round++){
 uint localChanged=0;
 for(uint color=0;color<4;color++){
  for(uint q=lid;q<UPDATE_X*UPDATE_Y/4;q+=WG){int sx=FIRST_CELL+(q%(UPDATE_X/2))*2+(color&1),sy=FIRST_CELL+(q/(UPDATE_X/2))*2+(color>>1);uint j=sy*SIDE_X+sx;uint val=v[j];
   int best=0; if(val)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    if((dx||dy)&&(FROZEN_HALO||(sx+dx>=0&&sx+dx<SIDE_X&&sy+dy>=0&&sy+dy<SIDE_Y))){
     uint k=j+dy*SIDE_X+dx,src=v[k],step=(dx&&dy)?(uniform?diagonal:c[k]>>16):(uniform?cardinal:c[k]&65535);
     best=max(best,(int)src-(int)step);
    }
   }
   if(best>=threshold)val=max(val,(uint)best);
   localChanged|=(v[j]!=val);v[j]=val;
  }
  barrier(CLK_LOCAL_MEM_FENCE);

 }
 // An unchanged complete sweep is a local fixed point. Expanding halos
 // check the first sweep; frozen one-cell halos check every sweep and update
 // only the core. Both extend valid paths monotonically toward the same fixed
 // point. Keep this flag separate from the final output reduction in flags[].
 if(FROZEN_HALO||round==0){
  if(lid==0)firstSweepChanged=0;
  barrier(CLK_LOCAL_MEM_FENCE);
  if(localChanged)atomic_or(&firstSweepChanged,1u);
  barrier(CLK_LOCAL_MEM_FENCE);
  if(!firstSweepChanged)break;
 }
 }
#else
 // The halo covers the complete dependency cone: core results equal STEPS
 // global Jacobi rounds, including wrapped seams and deferred/capped seeds.
 for(uint round=0;round<STEPS;round++){
  for(uint j=lid;j<PATCH;j+=WG){
   int sx=j%SIDE_X,sy=j/SIDE_X;
   // Only this shrinking cone can influence the final core. Its neighbors
   // were all written by the preceding round before the shared barrier.
   if(sx<(int)round+1||sy<(int)round+1||sx>=SIDE_X-(int)round-1||sy>=SIDE_Y-(int)round-1)continue;
   uint val=v[j];
   int best=0; if(val)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    if((dx||dy)&&(FROZEN_HALO||(sx+dx>=0&&sx+dx<SIDE_X&&sy+dy>=0&&sy+dy<SIDE_Y))){
     uint k=j+dy*SIDE_X+dx,src=v[k],step=(dx&&dy)?(uniform?diagonal:c[k]>>16):(uniform?cardinal:c[k]&65535);
     best=max(best,(int)src-(int)step);
    }
   }
   if(best>=threshold)val=max(val,(uint)best);
   next[j]=val;
  }
  barrier(CLK_LOCAL_MEM_FENCE);
  __local ushort *tmp=v;v=next;next=tmp;
 }
#endif
 flags[lid]=0;
 for(uint o=lid;o<CORE_X*CORE_Y;o+=WG){uint x=gx*CORE_X+o%CORE_X,y=gy*CORE_Y+o/CORE_X;
 uint j=(o/CORE_X+HALO)*SIDE_X+o%CORE_X+HALO;
 if(x<w&&y<h){uint i=base+y*w+x;b[i]=v[j];flags[lid]|=(a[i]!=v[j]);}}
 barrier(CLK_LOCAL_MEM_FENCE);
 if(lid<8){uint v=flags[lid];for(uint z=8;z<WG;z+=8)v|=flags[lid+z];flags[lid]=v;}barrier(CLK_LOCAL_MEM_FENCE);
 // Every publishing work-item reads the same eight completed partial reductions.
 // flags[0..7] remain read-only after the preceding barrier.
 if(lid<25){
  uint tileChanged=flags[0];for(uint z=1;z<8;z++)tileChanged|=flags[z];
  if(tileChanged){
   if(lid==0)atomic_or(changed+f,1u);
   // A partial edge tile can reach two tiles across a wrapped seam.
   // Distribute the same 3x3, 3x5, 5x3 or 5x5 neighborhood across work-items.
   int rx=(w%CORE_X&&w>CORE_X)?2:1,ry=(h%CORE_Y&&h>CORE_Y)?2:1;
   if(lid<(uint)((2*rx+1)*(2*ry+1))){
    int dx,dy;if(rx==1){dx=(int)lid%3-1;dy=(int)lid/3-ry;}else{dx=(int)lid%5-2;dy=(int)lid/5-ry;}
    uint nx=wrap((int)gx+dx,desc[d+6]),ny=wrap((int)gy+dy,desc[d+7]);
    atomic_or(nextActive+f*stride+ny*pitch+nx,1u);
   }
  }
 }
}
)CL";
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
struct Runtime;
struct Device
{
    API api;
    std::mutex initialization, cacheMutex, lanesMutex, failureMutex;
    bool probed = false;
    std::atomic<bool> failed{false};
    Handle device = nullptr, context = nullptr;
    OpenCLStatus status;
    struct Variant
    {
        UInt x, y, steps, threads;
        bool colored = false, frozen = false;
        Handle program = nullptr, kernel = nullptr;
    };
    // Keep a Jacobi fallback, three expanding-halo variants, and two local
    // fixed-point variants. Workgroup size is independent of the output tile.
    std::array<Variant, 6> variants{{{16, 16, 4, 256}, {16, 16, 2, 128, true},
                                     {16, 16, 4, 128, true}, {16, 16, 8, 256, true},
                                     {16, 16, 8, 128, true, true}, {16, 16, 16, 64, true, true}}};
    std::size_t selectedVariant = 0;
    Handle kernel = nullptr;
    struct Plane
    {
        API* api;
        Handle buffer = nullptr;
        struct Storage {
            API* api;
            Handle buffer=nullptr;
            explicit Storage(API& api):api(&api) {}
            ~Storage(){if(buffer)api->ReleaseMemObject(buffer);}
        };
        std::shared_ptr<Storage> storage;
        CostIdentity identity;
        int width, height;
        std::vector<UInt> data;
        bool uniform = true;
        std::vector<std::uint8_t> blocked;
        std::promise<void> completed;
        std::shared_future<void> ready = completed.get_future().share();
        std::atomic<bool> published{false};
        Plane(API& api, const BackendRequest& r, std::vector<std::uint8_t> mask)
            : api(&api), identity(r.identity), width(r.grid.width()), height(r.grid.height()), blocked(std::move(mask)) {}

    };
    std::array<std::shared_ptr<Plane>, 8> planes;
    std::array<std::uint64_t, 8> used{};
    std::uint64_t age = 0;
    std::vector<std::weak_ptr<Runtime>> lanes;
    OpenCLStatus retired;
    std::uint64_t retiredSequence = 0;
    std::atomic<std::uint64_t> sequence{0}, active{0}, maximumActive{0};
    ~Device() {
        for(auto& v:variants) {
            if(v.kernel) api.ReleaseKernel(v.kernel);
            if(v.program) api.ReleaseProgram(v.program);
        }
        if(context) api.ReleaseContext(context);
    }
    void initialize() { std::scoped_lock lock(initialization, failureMutex); probe(); }
    void probe()
    {
        if (probed)
            return;
        probed = true;
        try
        {
            api.load();
            UInt count = 0;
            check(api.GetPlatformIDs(0, nullptr, &count));
            std::vector<Handle> platforms(count);
            check(api.GetPlatformIDs(count, platforms.data(), nullptr));
            for (auto platform : platforms)
            {
                UInt found = 0;
                if (api.GetDeviceIDs(platform, 4 /* GPU */, 0, nullptr, &found) != 0)
                    continue;
                std::vector<Handle> devices(found);
                check(api.GetDeviceIDs(platform, 4, found, devices.data(), nullptr));
                for (auto candidate : devices)
                {
                    UInt available = 0, compiler = 0;
                    std::size_t group = 0;
                    check(api.GetDeviceInfo(candidate, 0x1027, sizeof available, &available, nullptr));
                    check(api.GetDeviceInfo(candidate, 0x1028, sizeof compiler, &compiler, nullptr));
                    check(api.GetDeviceInfo(candidate, 0x1004, sizeof group, &group, nullptr));
                    if (available && compiler && group >= 256)
                    {
                        device = candidate;
                        break;
                    }
                }
                if (device)
                    break;
            }
            if (!device)
                throw std::runtime_error("No supported OpenCL GPU");
            std::size_t bytes = 0;
            check(api.GetDeviceInfo(device, 0x102B, 0, nullptr, &bytes));
            std::vector<char> name(bytes);
            check(api.GetDeviceInfo(device, 0x102B, bytes, name.data(), nullptr));
            status.device = name.data();
            Int error = 0;
            context = api.CreateContext(nullptr, 1, &device, nullptr, nullptr, &error);
            check(error);
            UInt dimensions = 0;
            check(api.GetDeviceInfo(device, 0x1003, sizeof dimensions, &dimensions, nullptr));
            if (dimensions < 3)
                throw std::runtime_error("OpenCL device needs three work-item dimensions");
            std::vector<std::size_t> axes(dimensions);
            check(api.GetDeviceInfo(device, 0x1005, axes.size() * sizeof(std::size_t), axes.data(), nullptr));
            std::string unsupported;
            for (auto &variant : variants)
            {
                if (variant.threads > axes[0] || axes[1] < 1 || axes[2] < 1)
                    continue;
                const char *text = source;
                variant.program = api.CreateProgramWithSource(context, 1, &text, nullptr, &error);
                check(error);
                const auto options = "-cl-std=CL1.2 -DCORE_X=" + std::to_string(variant.x) +
                                     " -DCORE_Y=" + std::to_string(variant.y) +
                                     " -DSTEPS=" + std::to_string(variant.steps) +
                                     " -DCOLOR_RELAXATION=" + std::to_string(variant.colored) +
                                     " -DWG=" + std::to_string(variant.threads) +
                                     " -DFROZEN_HALO=" + std::to_string(variant.frozen);
                const auto built =
                    api.BuildProgram(variant.program, 1, &device, options.c_str(), nullptr, nullptr);
                if (built != 0)
                {
                    if (built != -11)
                        check(built); // Driver errors, rather than a rejected candidate build.
                    std::size_t size = 0;
                    api.GetProgramBuildInfo(variant.program, device, 0x1183, 0, nullptr, &size);
                    std::vector<char> log(size + 1);
                    api.GetProgramBuildInfo(variant.program, device, 0x1183, size, log.data(), nullptr);
                    unsupported = "OpenCL build failed: " + std::string(log.data());
                    api.ReleaseProgram(variant.program);
                    variant.program = nullptr;
                    continue;
                }
                variant.kernel = api.CreateKernel(variant.program, "propagate", &error);
                check(error);
                std::size_t group = 0;
                check(api.GetKernelWorkGroupInfo(variant.kernel, device, 0x11B0, sizeof group, &group,
                                                 nullptr));
                if (group < variant.threads)
                {
                    unsupported = "OpenCL kernel workgroup exceeds the supported size";
                    api.ReleaseKernel(variant.kernel);
                    api.ReleaseProgram(variant.program);
                    variant.kernel = variant.program = nullptr;
                }
            }
            if (!variants[selectedVariant].kernel)
            {
                auto candidate = std::find_if(variants.begin(), variants.end(),
                                              [](const auto &v) { return v.kernel != nullptr; });
                if (candidate == variants.end())
                    throw std::runtime_error(unsupported.empty() ? "No supported OpenCL tile configuration"
                                                                 : unsupported);
                selectedVariant = std::size_t(candidate - variants.begin());
            }
            kernel = variants[selectedVariant].kernel;
            status.tileWidth = variants[selectedVariant].x;
            status.tileHeight = variants[selectedVariant].y;
            status.localSteps = variants[selectedVariant].steps;
            status.available = true;
        }
        catch (const std::exception &e)
        {
            status.error = e.what();
            status.available = false;
        }
    }
    void touch(const std::shared_ptr<Plane>& plane) {
        std::lock_guard lock(cacheMutex);
        for(std::size_t i=0;i<planes.size();++i)
            if(planes[i]==plane) {used[i]=++age;break;}
    }
    void fail(std::string_view message) noexcept {
        failed.store(true);
        try { std::lock_guard lock(failureMutex); if(status.error.empty()) status.error.assign(message); } catch(...) {}
    }
};
struct Runtime
{
    std::shared_ptr<Device> shared;
    API& api;
    bool execution;
    std::uint64_t statusSequence = 0;
    Runtime(std::shared_ptr<Device> state = std::make_shared<Device>(), bool lane = false)
        : shared(std::move(state)), api(shared->api), execution(lane) {}
    std::mutex mutex;
    OpenCLStatus status;
    bool probed = false;
    Handle device = nullptr, context = nullptr, queue = nullptr;
    struct Variant
    {
        UInt x, y, steps, threads;
        bool colored = false, frozen = false;
        Handle program = nullptr, kernel = nullptr;
    };
    // Keep a Jacobi fallback, three expanding-halo variants, and two local
    // fixed-point variants. Workgroup size is independent of the output tile.
    std::array<Variant, 6> variants{{{16, 16, 4, 256}, {16, 16, 2, 128, true},
                                     {16, 16, 4, 128, true}, {16, 16, 8, 256, true},
                                     {16, 16, 8, 128, true, true}, {16, 16, 16, 64, true, true}}};
    std::size_t selectedVariant = 0;
    Handle kernel = nullptr;
    Handle first = nullptr, second = nullptr, changed = nullptr, descriptors = nullptr;
    std::size_t capacity = 0, tileCapacity = 0;
    Handle tilesFirst = nullptr, tilesSecond = nullptr;
    std::vector<std::uint16_t> values;
    std::vector<UInt> entries;
    std::vector<std::uint8_t> blocked;
    ~Runtime()
    {
        buffers();
        for (auto &variant : variants)
        {
            if (variant.kernel)
                api.ReleaseKernel(variant.kernel);

        }
        if (queue)
            api.ReleaseCommandQueue(queue);
        if(execution) {
            std::lock_guard lock(shared->lanesMutex);
            accumulate(shared->retired, status);
            if(statusSequence >= shared->retiredSequence) {
                parameters(shared->retired, status);
                shared->retiredSequence = statusSequence;
            }
        }
    }
    static void parameters(OpenCLStatus& out,const OpenCLStatus& in) {
        out.tileWidth=in.tileWidth;out.tileHeight=in.tileHeight;out.localSteps=in.localSteps;out.colored=in.colored;
    }
    static void accumulate(OpenCLStatus& out,const OpenCLStatus& in) {
        out.fields+=in.fields;out.calibrations+=in.calibrations;out.cpuSelections+=in.cpuSelections;
        out.batches+=in.batches;out.maxBatchFields=std::max(out.maxBatchFields,in.maxBatchFields);
        out.costUploads+=in.costUploads;out.costCacheHits+=in.costCacheHits;out.dispatches+=in.dispatches;
        out.hostChecks+=in.hostChecks;out.costIdentityHits+=in.costIdentityHits;out.retiredFields+=in.retiredFields;
        out.schedulerBatches+=in.schedulerBatches;out.tunings+=in.tunings;
    }
    Runtime& lane() {
        thread_local std::shared_ptr<Runtime> current;
        if(!current) {
            current=std::make_shared<Runtime>(shared,true);
            std::lock_guard lock(shared->lanesMutex);
            shared->lanes.erase(std::remove_if(shared->lanes.begin(),shared->lanes.end(),[](const auto& v){return v.expired();}),shared->lanes.end());
            shared->lanes.push_back(current);
        }
        return *current;
    }
    OpenCLStatus snapshot() {
        shared->initialize();
        OpenCLStatus out;
        { std::lock_guard lock(shared->failureMutex); out=shared->status; }
        std::vector<std::shared_ptr<Runtime>> live;
        std::uint64_t last;
        { std::lock_guard lock(shared->lanesMutex);
            accumulate(out,shared->retired);parameters(out,shared->retired);last=shared->retiredSequence;
            for(auto& v:shared->lanes) if(auto l=v.lock()) live.push_back(std::move(l));
        }
        for(auto& l:live) { std::lock_guard lock(l->mutex);accumulate(out,l->status);
            if(l->statusSequence>=last) {parameters(out,l->status);last=l->statusSequence;}
        }
        out.available=out.available&&!shared->failed.load();
        out.executionLanes=live.size();out.maxConcurrentBatches=shared->maximumActive.load();
        return out;
    }
    void buffers()
    {
        for (auto *buffer : {&first, &second, &changed, &descriptors, &tilesFirst, &tilesSecond})
            if (*buffer)
            {
                api.ReleaseMemObject(*buffer);
                *buffer = nullptr;
            }
        capacity = tileCapacity = 0;
    }
    void probe() {
        if(probed) return;
        shared->initialize();
        if(!shared->status.available || shared->failed.load()) throw std::runtime_error("OpenCL device unavailable");
        context=shared->context;device=shared->device;
        Int error=0;
        queue=api.CreateCommandQueue(context,device,0,&error);check(error);
        for(std::size_t i=0;i<variants.size();++i) if(shared->variants[i].kernel) {
            variants[i].program=shared->variants[i].program;
            variants[i].kernel=api.CreateKernel(variants[i].program,"propagate",&error);check(error);
        }
        selectedVariant=shared->selectedVariant;selectVariant(selectedVariant);
        status.available=true;probed=true;
    }
    template <class T> void argument(UInt index, T value)
    {
        check(api.SetKernelArg(kernel, index, sizeof(T), &value));
    }
    void write(Handle buffer, const void *data, std::size_t bytes)
    {
        // Blocking uploads keep borrowed storage valid even after an error.
        check(api.EnqueueWriteBuffer(queue, buffer, 1, 0, bytes, data, 0, nullptr, nullptr));
    }
    void awaitPlane(const std::shared_ptr<Device::Plane>& p) {
        while(p->ready.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)
            if(shared->failed.load()) throw std::runtime_error("OpenCL cost preparation failed on another lane");
        p->ready.get();
    }
    std::shared_ptr<Device::Plane> costPlane(const BackendRequest& r) {
        auto sameIdentity=[&](const auto& p) {
            return p&&r.identity.owner&&p->identity.owner==r.identity.owner&&p->identity.variant==r.identity.variant&&
                p->identity.revision==r.identity.revision&&p->identity.allCells==r.identity.allCells&&
                p->width==r.grid.width()&&p->height==r.grid.height()&&(r.identity.allCells||p->blocked==blocked);
        };
        // Immutable snapshots allow expensive mask/content comparison outside
        // the cache lock. Only publication and replacement need that lock.
        std::array<std::shared_ptr<Device::Plane>,8> candidates;
        {std::lock_guard lock(shared->cacheMutex);candidates=shared->planes;}
        for(auto& p:candidates) if(sameIdentity(p)) {
            awaitPlane(p);shared->touch(p);++status.costIdentityHits;return p;
        }
        auto p=std::make_shared<Device::Plane>(api,r,blocked);
        bool reserved=false;
        if(r.identity.owner) {
            // Mask comparison is outside the lock; pointer equality detects a
            // concurrent publication and retries from a fresh immutable snapshot.
            for(;;) {
                {std::lock_guard lock(shared->cacheMutex);candidates=shared->planes;}
                for(auto& other:candidates) if(sameIdentity(other)) {
                    awaitPlane(other);shared->touch(other);++status.costIdentityHits;return other;
                }
                std::lock_guard lock(shared->cacheMutex);
                if(candidates!=shared->planes) continue;
                const auto slot=std::min_element(shared->used.begin(),shared->used.end())-shared->used.begin();
                shared->planes[slot]=p;shared->used[slot]=++shared->age;reserved=true;break;
            }
        }
        try {
            const auto n=r.grid.cells();p->data.resize(n);
            for(std::size_t i=0;i<n;++i) {
                const auto step=!r.identity.allCells&&blocked[i]?LAND_STEPS:r.costAt(r.context,i);
                if(!step.cardinal||!step.diagonal||step.cardinal>65535||step.diagonal>65535)
                    throw std::runtime_error("Invalid OpenCL gradient edge cost");
                p->data[i]=step.cardinal|(step.diagonal<<16);
                if (i && p->data[i] != p->data[0]) p->uniform = false;
            }
            {std::lock_guard lock(shared->cacheMutex);candidates=shared->planes;}
            for(auto& other:candidates) if(other&&other!=p&&other->published.load()&&other->data==p->data) {
                ++status.costCacheHits;
                // A reservation must still complete for concurrent identity
                // waiters. Share the buffer through an immutable retained alias.
                p->storage=other->storage;p->buffer=other->buffer;
                p->published.store(true);p->completed.set_value();
                if(!reserved) {std::lock_guard lock(shared->cacheMutex);
                    const auto slot=std::min_element(shared->used.begin(),shared->used.end())-shared->used.begin();
                    shared->planes[slot]=p;shared->used[slot]=++shared->age;
                }
                return p;
            }
            p->storage=std::make_shared<Device::Plane::Storage>(api);
            Int error=0;p->buffer=p->storage->buffer=api.CreateBuffer(context,1,n*sizeof(UInt),nullptr,&error);check(error);
            write(p->buffer,p->data.data(),n*sizeof(UInt));++status.costUploads;
            p->published.store(true);p->completed.set_value();
            if(!reserved) {std::lock_guard lock(shared->cacheMutex);
                const auto slot=std::min_element(shared->used.begin(),shared->used.end())-shared->used.begin();
                shared->planes[slot]=p;shared->used[slot]=++shared->age;
            }
            return p;
        } catch(...) {
            try {p->completed.set_exception(std::current_exception());} catch(...) {}
            throw;
        }
    }
    void computeBatch(std::span<const BackendRequest *const> requests)
    {
        if(shared->failed.load()) throw std::runtime_error("OpenCL device failed on another lane");
        std::size_t largest = 0, total = 0;
        UInt width = 0, height = 0;
        for (auto *r : requests)
        {
            largest = std::max(largest, r->grid.cells());
            total += r->grid.cells();
            width = std::max(width, UInt(r->grid.width()));
            height = std::max(height, UInt(r->grid.height()));
        }
        if (largest > std::numeric_limits<UInt>::max() / 8 ||
            largest > std::numeric_limits<std::size_t>::max() / (8 * sizeof(UInt)))
            throw std::runtime_error("Gradient batch exceeds OpenCL index range");
        if (largest > capacity)
        {
            buffers();
            Int error = 0;
            for (auto *buffer : {&first, &second})
            {
                *buffer = api.CreateBuffer(context, 1, largest * 8 * sizeof(std::uint16_t), nullptr, &error);
                check(error);
            }
            changed = api.CreateBuffer(context, 1, 8 * sizeof(UInt), nullptr, &error);
            check(error);
            descriptors = api.CreateBuffer(context, 1, 64 * sizeof(UInt), nullptr, &error);
            check(error);
            capacity = largest;
        }
        values.resize(total);
        std::vector<UInt> desc;
        std::array<std::shared_ptr<Device::Plane>,8> held;
        std::size_t offset=0, field=0;
        for(auto* r:requests) {
            const auto n=r->grid.cells();
            std::copy(r->gradient,r->gradient+n,values.begin()+offset);
            blocked.clear();
            if(!r->identity.allCells) {
                blocked.resize(n);
                for(std::size_t i=0;i<n;++i) blocked[i]=r->gradient[i]==0;
            }
            held[field]=costPlane(*r);
            desc.insert(desc.end(),{UInt(r->grid.width()),UInt(r->grid.height()),UInt(offset),UInt(field),
                UInt(r->limit),held[field]->uniform ? 3u : 1u,UInt((r->grid.width()+variants[selectedVariant].x-1)/variants[selectedVariant].x),
                UInt((r->grid.height()+variants[selectedVariant].y-1)/variants[selectedVariant].y)});
            offset+=n;++field;
        }
        write(first, values.data(), total * sizeof(std::uint16_t));
        write(descriptors, desc.data(), desc.size() * sizeof(UInt));
        Handle a = first, b = second;
        const auto &variant = variants[selectedVariant];
        const UInt pitch = (width + variant.x - 1) / variant.x, rows = (height + variant.y - 1) / variant.y;
        const std::size_t stride = std::size_t(pitch) * rows, tileCount = stride * requests.size();
        if (tileCount > std::numeric_limits<UInt>::max() ||
            tileCount > std::numeric_limits<std::size_t>::max() / sizeof(UInt))
            throw std::runtime_error("OpenCL tile mask exceeds index range");
        if (tileCount > tileCapacity)
        {
            for (auto *mask : {&tilesFirst, &tilesSecond})
            {
                if (*mask)
                    api.ReleaseMemObject(*mask);
                *mask = nullptr;
                Int error = 0;
                *mask = api.CreateBuffer(context, 1, tileCount * sizeof(UInt), nullptr, &error);
                check(error);
            }
            tileCapacity = tileCount;
        }
        UInt one = 1;
        check(api.EnqueueFillBuffer(queue, tilesFirst, &one, sizeof one, 0, tileCount * sizeof(UInt), 0,
                                    nullptr, nullptr));
        Handle active = tilesFirst, nextActive = tilesSecond;
        const std::size_t global[]{std::size_t(pitch) * variant.threads, std::size_t(rows),
                                   requests.size()},
            local[]{variant.threads, 1, 1};
        std::array<UInt, 8> zero{}, flags{};
        ++status.batches;
        status.maxBatchFields = std::max(status.maxBatchFields, std::uint64_t(requests.size()));
        // Each lane owns its kernel instance. These arguments are immutable
        // throughout this computation; only ping-pong buffers change per round.
        for(unsigned i=0;i<8;++i) argument(2+i,held[i]?held[i]->buffer:held[0]->buffer);
        argument(10, changed);
        argument(11, descriptors);
        argument(14, UInt(stride));
        argument(15, pitch);
        // Every dispatch extends at least one global path edge. Frozen halos
        // exchange only one cell even when they perform many local sweeps, so
        // the ushort convergence bound counts dispatches, not local sweeps.
        for (UInt round = 0; round < 65536; round += 8)
        {
            if(shared->failed.load()) throw std::runtime_error("OpenCL device failed on another lane");
            for (unsigned dispatch = 0; dispatch < 8; ++dispatch)
            {
                // Only the last dispatch's changes are read by the host. The
                // in-order queue clears earlier accumulated flags before it.
                if (dispatch == 7)
                    check(api.EnqueueFillBuffer(queue, changed, zero.data(), sizeof(UInt), 0,
                                                requests.size() * sizeof(UInt), 0, nullptr, nullptr));
                check(api.EnqueueFillBuffer(queue, nextActive, zero.data(), sizeof(UInt), 0,
                                            tileCount * sizeof(UInt), 0, nullptr, nullptr));
                argument(0, a);
                argument(1, b);
                argument(12, active);
                argument(13, nextActive);
                check(
                    api.EnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, local, 0, nullptr, nullptr));
                std::swap(a, b);
                std::swap(active, nextActive);
                ++status.dispatches;
            }
            check(api.EnqueueReadBuffer(queue, changed, 1, 0, requests.size() * sizeof(UInt), flags.data(), 0,
                                        nullptr, nullptr));
            ++status.hostChecks;
            bool retired = false;
            for (std::size_t f = 0; f < requests.size(); ++f)
                if (desc[f * 8 + 5] && !flags[f])
                {
                    desc[f * 8 + 5] = 0;
                    retired = true;
                    ++status.retiredFields;
                }
            if (std::none_of(flags.begin(), flags.begin() + requests.size(), [](UInt v) { return v != 0; }))
            {
                check(api.EnqueueReadBuffer(queue, a, 1, 0, total * sizeof(std::uint16_t), values.data(), 0, nullptr,
                                            nullptr));
                return;
            }
            // No later kernel consumes descriptors once every field is done.
            // Mixed batches still retire finished fields before dispatching again.
            if (retired)
                write(descriptors, desc.data(), desc.size() * sizeof(UInt));
        }
        throw std::runtime_error("OpenCL gradient failed to converge");
    }
    void compute(const BackendRequest &r)
    {
        const std::array requests{&r};
        computeBatch(requests);
    }
    void selectVariant(std::size_t index)
    {
        selectedVariant = index;
        kernel = variants[index].kernel;
        status.tileWidth = variants[index].x;
        status.tileHeight = variants[index].y;
        status.localSteps = variants[index].steps;
        status.colored = variants[index].colored;
    }
    void tune(std::span<const BackendRequest* const> requests,
              std::span<std::uint16_t* const> expected, bool warmed = false)
    {
        ++status.tunings;
        if (!warmed) computeBatch(requests);
        auto sample = [&] {
            const auto start = Clock::now();
            computeBatch(requests);
            const auto ms = elapsed(start);
            std::size_t offset = 0;
            for (std::size_t f = 0; f < requests.size(); ++f)
            {
                const auto n = requests[f]->grid.cells();
                if (!std::equal(expected[f], expected[f] + n, values.begin() + offset))
                    throw std::runtime_error("OpenCL tile variant disagrees with CPU");
                offset += n;
            }
            return ms;
        };
        // Screen all supported variants once, then spend repeat measurements on
        // the two finalists. Every sampled result still receives an exact check.
        std::vector<std::pair<double, std::size_t>> screened;
        for (std::size_t i = 0; i < variants.size(); ++i)
        {
            if (!variants[i].kernel) continue;
            selectVariant(i);
            screened.emplace_back(sample(), i);
        }
        std::sort(screened.begin(), screened.end());
        double best = std::numeric_limits<double>::max();
        std::size_t winner = selectedVariant;
        for (std::size_t i = 0; i < std::min<std::size_t>(2, screened.size()); ++i)
        {
            selectVariant(screened[i].second);
            std::array samples{screened[i].first, sample(), sample()};
            std::sort(samples.begin(), samples.end());
            if (samples[1] < best) { best = samples[1]; winner = selectedVariant; }
        }
        selectVariant(winner);
    }
    static bool alreadyFixed(const BackendRequest& request)
    {
        const auto* begin = request.gradient;
        const auto* end = begin + request.grid.cells();
        const auto* source = std::find_if(begin, end, [](auto value) { return value > 1; });
        // Without a source no legal path can start.
        if (source == end) return true;
        if (*source != 65535) return false;
        // With only forbidden cells and maximal goals, no cell can improve,
        // regardless of positive costs.
        return std::none_of(begin, end, [](auto value) { return value != 0 && value != 65535; });
    }
    // A map normally keeps the same dimensions and cap, but generic callers can
    // share a session across unlike fields. Do not reuse their old placement.
    static std::uint64_t workload(std::span<const BackendRequest* const> requests)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (auto request : requests)
            for (auto value : {unsigned(request->grid.width()), unsigned(request->grid.height()),
                               unsigned(request->limit)})
                hash = (hash ^ value) * 1099511628211ull;
        return hash ? hash : 1;
    }
    static std::uint64_t movementMask(std::span<const BackendRequest* const> requests)
    {
        std::uint64_t mask = 0;
        for (auto request : requests)
            if (request->identity.variant < 64) mask |= std::uint64_t(1) << request->identity.variant;
        return mask;
    }
    static void reconsider(const BackendRequest& request, std::size_t count,
                           std::uint64_t signature, std::uint64_t movements)
    {
        auto& session = request.session;
        auto& selection = session.selection(request.family, count);
        if (selection.load(std::memory_order_relaxed) == Backend::Automatic) return;
        auto& timing = session.timing(request.family, count);
        const auto prior = timing.workload.load(std::memory_order_relaxed);
        // A caller may explicitly install a choice without any timing record.
        if (!prior) return;
        const bool newMovement = (timing.movements.fetch_or(movements, std::memory_order_relaxed) & movements)
                                 != movements;
        const auto calls = timing.calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (prior == signature && !newMovement && calls < timing.recheckAfter.load(std::memory_order_relaxed)) return;
        std::lock_guard lock(session.classMutex(request.family, count));
        if (newMovement || timing.workload.load(std::memory_order_relaxed) != signature ||
            timing.calls.load(std::memory_order_relaxed) >= timing.recheckAfter.load(std::memory_order_relaxed))
            selection.store(Backend::Automatic, std::memory_order_relaxed);
    }
    bool batch(std::span<const BackendRequest> input, Backend choice,
               std::span<std::uint8_t> handled = {}, double schedulingMs = 0)
    {
        const bool workerCalls = !handled.empty();
        std::fill(handled.begin(), handled.end(), 0);
        if (input.empty()) return true;
        if(shared->failed.load()) {
            for(const auto& r:input) r.session.failed.store(true);
            return false;
        }
        if (choice == Backend::CPU || std::any_of(input.begin(), input.end(),
            [](const auto& r) { return r.session.failed.load(); })) return false;
        if (std::all_of(input.begin(), input.end(), alreadyFixed)) {
            std::fill(handled.begin(), handled.end(), 1);
            return true;
        }
        if(!execution) {
            try { return lane().batch(input,choice,handled,schedulingMs); }
            catch(...) {shared->fail("Unable to allocate OpenCL execution lane");for(const auto& r:input)r.session.failed.store(true);return false;}
        }
        std::lock_guard lock(mutex);
        struct Active {
            Device& d;
            Active(Device& d):d(d) {const auto n=++d.active;auto old=d.maximumActive.load();while(old<n&&!d.maximumActive.compare_exchange_weak(old,n)){} }
            ~Active(){--d.active;}
        } activeLane(*shared);
        statusSequence=++shared->sequence;
        try
        {
            std::vector<bool> processed(input.size(), false);
            for (std::size_t i = 0; i < input.size(); ++i)
                if (alreadyFixed(input[i])) {
                    processed[i] = true;
                    if (workerCalls) handled[i] = 1;
                }
            probe();
            if (!status.available) throw std::runtime_error(status.error);
            // Homogeneous family/session groups get their own actual batch size.
            // Stage every result before committing so a late GPU error preserves
            // all original seeds for permanent CPU recovery.
            std::vector<std::vector<std::uint16_t>> output(input.size());
            for (std::size_t first = 0; first < input.size(); ++first)
            {
                if (processed[first]) continue;
                const auto& representative = input[first];
                std::array<const BackendRequest*, 8> pointers{};
                std::array<std::size_t, 8> indices{};
                std::size_t count = 0;
                for (std::size_t i = first; i < input.size() && count < 8; ++i)
                    if (!processed[i] && input[i].family == representative.family &&
                        (choice == Backend::OpenCL || &input[i].session == &representative.session))
                    {
                        processed[i] = true;
                        indices[count] = i;
                        pointers[count++] = &input[i];
                    }
                const std::span requests(pointers.data(), count);
                auto& selection = representative.session.selection(representative.family, count);
                const auto signature = workload(requests);
                if (choice == Backend::Automatic) reconsider(representative, count, signature, movementMask(requests));
                double groupSchedulingMs = schedulingMs;
                for (auto request : requests)
                    groupSchedulingMs = std::max(groupSchedulingMs, schedulingMs + request->schedulingMs);
                std::unique_lock classLock(representative.session.classMutex(representative.family,count),std::defer_lock);
                const bool large=std::any_of(requests.begin(),requests.end(),[](auto r){return r->grid.cells()>=65536;});
                // Rechecks can invalidate a previous winner while another lane
                // enters. Read the decision under its lock before calibrating.
                if(choice==Backend::Automatic ||
                   (large&&!representative.session.tileSelection(representative.family,count).load())) classLock.lock();
                auto selected = selection.load(std::memory_order_relaxed);
                std::array<std::uint16_t*, 8> destinations{};
                auto prepareCPU = [&] {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        auto& out = output[indices[i]];
                        const auto& r = *pointers[i];
                        out.assign(r.gradient, r.gradient + r.grid.cells());
                        destinations[i] = out.data();
                    }
                };
                auto executeCPU = [&] {
                    if (representative.cpuBatch && std::all_of(requests.begin(), requests.end(),
                        [&](auto r) { return r->cpuBatch == representative.cpuBatch; }))
                        representative.cpuBatch(requests, std::span(destinations.data(), count));
                    else
                        for (std::size_t i = 0; i < count; ++i)
                            pointers[i]->cpu(pointers[i]->context, destinations[i]);
                };
                if (choice == Backend::Automatic && selected == Backend::CPU)
                {
                    if (classLock.owns_lock()) classLock.unlock();
                    if (!workerCalls) { prepareCPU(); executeCPU(); }
                    continue; // The singleton CPU caller resumes in its own scratch.
                }
                auto& tileChoice = representative.session.tileSelection(representative.family, count);
                if (const auto stored = tileChoice.load(std::memory_order_relaxed))
                    selectVariant(stored - 1);
                else if (choice == Backend::OpenCL && large)
                {
                    prepareCPU(); executeCPU();
                    tune(requests, std::span(destinations.data(), count));
                    tileChoice.store(unsigned(selectedVariant + 1), std::memory_order_relaxed);
                }
                else {
                    // Losing classes try another candidate at their next budgeted
                    // check, so one poor default kernel cannot exclude the GPU forever.
                    const auto probes = representative.session.timing(representative.family, count)
                                            .variantProbes.load(std::memory_order_relaxed);
                    for (std::size_t attempt = 0; attempt < variants.size(); ++attempt) {
                        const auto index = (1 + probes + attempt) % variants.size();
                        if (variants[index].kernel) { selectVariant(index); break; }
                    }
                }
                if (choice == Backend::Automatic && selected == Backend::Automatic)
                {
                    ++status.calibrations;
                    const auto calibrationStart = Clock::now();
                    std::array<double, 3> cpuTimes{}, gpuTimes{};
                    // The ordinary CPU path operates in place. Private benchmark
                    // seed resets are excluded for singleton and explicit batches;
                    // executor dispatch/join stays inside the CPU timer.
                    for (auto& sample : cpuTimes)
                    {
                        prepareCPU();
                        const auto start = Clock::now();
                        executeCPU();
                        sample = elapsed(start);
                    }
                    std::vector<std::vector<std::uint16_t>> measured(count), committed(count);
                    for (std::size_t i = 0; i < count; ++i) committed[i].resize(pointers[i]->grid.cells());
                    auto measureGPU = [&] {
                    for (auto& sample : gpuTimes)
                    {
                        const auto start = Clock::now();
                        computeBatch(requests);
                        std::size_t offset = 0;
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            const auto size = pointers[i]->grid.cells();
                            measured[i].assign(values.begin() + offset, values.begin() + offset + size);
                            std::copy(measured[i].begin(), measured[i].end(), committed[i].begin());
                            offset += size;
                        }
                        sample = elapsed(start) + groupSchedulingMs;
                        // Verify outside the timed region, after output conversion.
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            if (output[indices[i]] != measured[i])
                                throw std::runtime_error("OpenCL workload class disagrees with CPU");
                        }
                    }
                    std::sort(gpuTimes.begin(), gpuTimes.end());
                    };
                    std::sort(cpuTimes.begin(), cpuTimes.end());
                    measureGPU();
                    // Median absolute deviation tolerates one cold or interrupted
                    // sample without turning that outlier into a permanent CPU bias.
                    auto uncertainty = [&] {
                        const auto cpuNoise = std::min(cpuTimes[1] - cpuTimes[0], cpuTimes[2] - cpuTimes[1]);
                        const auto gpuNoise = std::min(gpuTimes[1] - gpuTimes[0], gpuTimes[2] - gpuTimes[1]);
                        return std::max(cpuTimes[1] * 0.05, 2 * (cpuNoise + gpuNoise));
                    };
                    auto& timing = representative.session.timing(representative.family, count);
                    if (!tileChoice.load(std::memory_order_relaxed)) {
                        timing.variantProbes.fetch_add(1, std::memory_order_relaxed);
                        // Pay for variant tuning only after this actual workload
                        // demonstrates plausible GPU throughput. Reuse the CPU oracle.
                        if (gpuTimes[1] <= cpuTimes[1] + uncertainty()) {
                            tune(requests, std::span(destinations.data(), count), true);
                            tileChoice.store(unsigned(selectedVariant + 1), std::memory_order_relaxed);
                            measureGPU();
                        }
                    }
                    selected = gpuTimes[1] + uncertainty() < cpuTimes[1] ? Backend::OpenCL : Backend::CPU;
                    timing.cpuMs.store(cpuTimes[1], std::memory_order_relaxed);
                    timing.workload.store(signature, std::memory_order_relaxed);
                    timing.movements.fetch_or(movementMask(requests), std::memory_order_relaxed);
                    timing.calls.store(0, std::memory_order_relaxed);
                    timing.slowSamples.store(0, std::memory_order_relaxed);
                    // Explore untuned variants sooner, but make the interval
                    // proportional to probe cost and useful CPU work. A slow losing
                    // GPU must not be sampled every few cheap CPU calls.
                    const bool exploring = !tileChoice.load(std::memory_order_relaxed) &&
                        timing.variantProbes.load(std::memory_order_relaxed) < variants.size();
                    const double winnerMs = std::max(0.001, std::min(cpuTimes[1], gpuTimes[1]));
                    const double interval = elapsed(calibrationStart) / (winnerMs * (exploring ? 0.05 : 0.01));
                    timing.recheckAfter.store(unsigned(std::clamp(interval, exploring ? 32.0 : 256.0, 65536.0)),
                                              std::memory_order_relaxed);
                    selection.store(selected, std::memory_order_relaxed);
                    status.cpuSelections += selected == Backend::CPU;
                    // CPU reference is already a verified completed result.
                    continue;
                }
                if(classLock.owns_lock()) classLock.unlock();
                const auto executionStart = Clock::now();
                computeBatch(requests);
                std::size_t offset = 0;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto size = pointers[i]->grid.cells();
                    output[indices[i]].assign(values.begin() + offset, values.begin() + offset + size);
                    offset += size;
                }
                if (choice == Backend::Automatic) {
                    auto& timing = representative.session.timing(representative.family, count);
                    const auto baseline = timing.cpuMs.load(std::memory_order_relaxed);
                    if (baseline > 0 && elapsed(executionStart) + groupSchedulingMs > baseline * 1.1) {
                        if (timing.slowSamples.fetch_add(1, std::memory_order_relaxed) + 1 >= 8)
                            timing.calls.store(timing.recheckAfter.load(std::memory_order_relaxed),
                                               std::memory_order_relaxed);
                    } else timing.slowSamples.store(0, std::memory_order_relaxed);
                }
                status.fields += count;
            }
            if(shared->failed.load() || std::any_of(input.begin(),input.end(),[](const auto& r){return r.session.failed.load();}))
                throw std::runtime_error("OpenCL device or session failed on another lane");
            for (std::size_t i = 0; i < input.size(); ++i)
                if (!output[i].empty()) {
                    std::copy(output[i].begin(), output[i].end(), input[i].gradient);
                    if (workerCalls) handled[i] = 1;
                }
            ++status.schedulerBatches;
            return true;
        }
        catch (...)
        {
            status.available = false;
            for (const auto& r : input)
            {
                r.session.failed.store(true);
                ++status.cpuSelections;
            }
            // Diagnostics must never let an allocation failure strand waiting
            // workers. The failure flags and untouched seeds are authoritative.
            try {
                try { throw; }
                catch (const std::exception& e) { status.error = e.what(); }
                catch (...) { status.error = "OpenCL workload failed with an unknown exception"; }
            } catch (...) { status.error.clear(); }
            shared->fail(status.error);
            if (queue) api.Finish(queue);
            return false;
        }
    }
    bool run(const BackendRequest& r, Backend choice)
    {
        const auto schedulingStart = Clock::now();
        if(shared->failed.load()) {r.session.failed.store(true);return false;}
        if (r.session.failed.load()) return false;
        if (choice == Backend::CPU) return false;
        // Known singleton CPU work does not allocate, scan seeds, or rendezvous
        // with GPU callers. Explicit batches still measure their own sizes.
        if (choice == Backend::Automatic &&
            r.session.selection(r.family, 1).load(std::memory_order_relaxed) == Backend::CPU) {
            const BackendRequest* pointer = &r;
            reconsider(r, 1, workload(std::span(&pointer, 1)), movementMask(std::span(&pointer, 1)));
            if (r.session.selection(r.family, 1).load(std::memory_order_relaxed) == Backend::CPU)
                return false;
        }
        if (alreadyFixed(r))
            return true;
        // Jobs that already own a worker run immediately. Reliable batching
        // belongs to callers that have an explicit ready group, rather than a
        // rendezvous that suspends workers hoping more jobs will arrive.
        auto request = r;
        request.cpuBatch = nullptr; // No nested dispatch for a singleton's CPU reference.
        std::uint8_t handled = 0;
        return batch(std::span(&request, 1), choice, std::span(&handled, 1), elapsed(schedulingStart)) && handled;
    }

};
#else
struct Runtime
{
    std::mutex mutex;
    OpenCLStatus status{false, {}, "OpenCL backend unavailable on this platform", 0, 0, 0};
    void probe() {}
    bool batch(std::span<const BackendRequest> requests, Backend)
    {
        for (const auto &r : requests)
            r.session.failed.store(true);
        return false;
    }
    bool run(const BackendRequest &r, Backend)
    {
        r.session.failed.store(true);
        return false;
    }
};
#endif
Runtime &runtime()
{
    static Runtime value;
    return value;
}
bool execute(const BackendRequest &r, Backend choice) { return runtime().run(r, choice); }
bool executeBatch(std::span<const BackendRequest> requests, Backend choice)
{
    return runtime().batch(requests, choice);
}
struct Register
{
    Register()
    {
        accelerator = &execute;
        batchAccelerator = &executeBatch;
    }
} registration;
} // namespace
OpenCLStatus openCLStatus()
{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    return runtime().snapshot();
#else
    return runtime().status;
#endif
}
} // namespace gradient_kernel
