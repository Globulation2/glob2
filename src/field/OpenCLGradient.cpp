// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLGradient.h"
#include "OpenCLCpuAttribution.h"
#include "OpenCLBorrowedLifetime.h"
#include "OpenCLTransferProof.h"
#include "OpenCLLaneRegistry.h"
#include "GradientBackend.h"
#include "ThreadCpuClock.h"
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
#include <charconv>
#include <thread>

namespace gradient_kernel
{
namespace
{
struct MemoryBudget {
    const std::size_t limit;
    std::atomic<std::size_t> current{0}, peak{0};
    bool reserve(std::size_t bytes) noexcept {
        auto old=current.load(std::memory_order_relaxed);
        do { if(bytes>limit || old>limit-bytes) return false; }
        while(!current.compare_exchange_weak(old,old+bytes,std::memory_order_relaxed));
        auto maximum=peak.load(std::memory_order_relaxed);
        while(maximum<old+bytes && !peak.compare_exchange_weak(maximum,old+bytes,std::memory_order_relaxed)) {}
        return true;
    }
    void release(std::size_t bytes) noexcept {
        auto old=current.load(std::memory_order_relaxed);
        do {if(bytes>old) std::terminate();}
        while(!current.compare_exchange_weak(old,old-bytes,std::memory_order_relaxed));
    }
};
MemoryBudget hostBudget{OpenCLHostBudget};
MemoryBudget probeBudget{OpenCLProbeBudget};
struct BudgetExceeded : std::runtime_error { BudgetExceeded():std::runtime_error("OpenCL payload budget exceeded") {} };
struct RequiredCanceled : std::exception {const char* what() const noexcept override{return "Required OpenCL execution canceled";}};
struct BudgetLease {
    MemoryBudget& budget;
    std::size_t bytes=0;
    explicit BudgetLease(MemoryBudget& budget):budget(budget) {}
    BudgetLease(const BudgetLease&)=delete;
    BudgetLease& operator=(const BudgetLease&)=delete;
    ~BudgetLease(){budget.release(bytes);}
    void transferTo(BudgetLease& destination) noexcept {
        if(&budget!=&destination.budget || destination.bytes)std::terminate();
        destination.bytes=bytes;bytes=0;
    }
    void splitTo(BudgetLease& destination,std::size_t count) noexcept {
        if(&budget!=&destination.budget || destination.bytes || count>bytes)std::terminate();
        destination.bytes=count;bytes-=count;
    }
    void resize(std::size_t next) {
        if(next>bytes && !budget.reserve(next-bytes)) throw BudgetExceeded();
        if(next<bytes) budget.release(bytes-next);
        bytes=next;
    }
};
template<class T> void resizeStaging(std::vector<T>& values,std::size_t count,BudgetLease& lease) {
    if(count>values.capacity()) {
        // Reserve the transient old+new allocation, not just their difference.
        const auto previous=lease.bytes;
        lease.resize(previous+count*sizeof(T));
        try { std::vector<T> replacement(count); values.swap(replacement); }
        catch(...) {lease.resize(previous);throw;}
        lease.resize(values.capacity()*sizeof(T));
    } else values.resize(count);
}
std::uint64_t threadCPUClock() noexcept {
    return glob2::threadCpuNs();
}
thread_local unsigned backendCPUDepth=0;
unsigned numericOverride(const char* name,unsigned fallback,unsigned maximum) {
    const auto* value=std::getenv(name);
    if(!value) return fallback;
    unsigned parsed=0;
    const auto* end=value+std::strlen(value);
    const auto result=std::from_chars(value,end,parsed);
    if(result.ec!=std::errc{} || result.ptr!=end || parsed>maximum)
        throw std::runtime_error(std::string("Invalid ")+name);
    return parsed;
}
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
    CL_FUNCTION(GetPlatformInfo, Int, Handle, UInt, std::size_t, void *, std::size_t *);
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
    CL_FUNCTION(Flush, Int, Handle);
    CL_FUNCTION(GetEventInfo, Int, Handle, UInt, std::size_t, void*, std::size_t*);
    CL_FUNCTION(GetEventProfilingInfo, Int, Handle, UInt, std::size_t, void*, std::size_t*);
    CL_FUNCTION(ReleaseEvent, Int, Handle);
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
        LOAD(GetPlatformInfo);
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
        LOAD(Flush);
        LOAD(GetEventInfo);
        LOAD(GetEventProfilingInfo);
        LOAD(ReleaseEvent);
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
// Storage padding only: logical geometry and lane assignment are unchanged.
#define LOCAL_X (FROZEN_HALO?(CORE_X+8):SIDE_X)
#define LOCAL_PATCH (LOCAL_X*SIDE_Y)
inline int wrap(int x,uint size){if((size&(size-1))==0)return x&(size-1);x%=(int)size;return x<0?x+size:x;}
__kernel void propagate(__global const ushort *a,__global ushort *b,
 __global const uint *c0,__global const uint *c1,__global const uint *c2,__global const uint *c3,
 __global const uint *c4,__global const uint *c5,__global const uint *c6,__global const uint *c7,
 __global uint *changed,__global const uint *desc,
 __global const uint *active,__global uint *nextActive,uint stride,uint pitch
#if ACTIVE_EPOCH
 ,uint activeEpoch
#endif
 )
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
#if ACTIVE_EPOCH
 if(active[tile]!=activeEpoch){
#else
 if(!active[tile]){
#endif
  // Keep ping-pong buffers coherent. Only propagation/local-memory work is skipped.
  for(uint o=lid;o<CORE_X*CORE_Y;o+=WG){uint x=gx*CORE_X+o%CORE_X,y=gy*CORE_Y+o/CORE_X;
  if(x<w&&y<h){uint i=base+y*w+x;b[i]=a[i];}}return;
 }
 __local uint c[LOCAL_PATCH],flags[WG];
#if COLOR_RELAXATION
 __local ushort v[LOCAL_PATCH];
 __local uint firstSweepChanged;
#else
 __local ushort storage[2][LOCAL_PATCH];
 __local ushort *v=storage[0],*next=storage[1];
#endif
 for(uint j=lid;j<PATCH;j+=WG){
  int sx=wrap(gx*CORE_X+(int)(j%SIDE_X)-HALO,w),sy=wrap(gy*CORE_Y+(int)(j/SIDE_X)-HALO,h);
  uint localIndex=(j/SIDE_X)*LOCAL_X+j%SIDE_X;
  v[localIndex]=a[base+sy*w+sx];if(!uniform)c[localIndex]=costs[sy*w+sx];
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
  for(uint q=lid;q<UPDATE_X*UPDATE_Y/4;q+=WG){int sx=FIRST_CELL+(q%(UPDATE_X/2))*2+(color&1),sy=FIRST_CELL+(q/(UPDATE_X/2))*2+(color>>1);uint j=sy*LOCAL_X+sx;uint val=v[j];
   int best=0; if(val)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    if((dx||dy)&&(FROZEN_HALO||(sx+dx>=0&&sx+dx<SIDE_X&&sy+dy>=0&&sy+dy<SIDE_Y))){
     uint k=j+dy*LOCAL_X+dx,src=v[k],step=(dx&&dy)?(uniform?diagonal:c[k]>>16):(uniform?cardinal:c[k]&65535);
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
     uint k=j+dy*LOCAL_X+dx,src=v[k],step=(dx&&dy)?(uniform?diagonal:c[k]>>16):(uniform?cardinal:c[k]&65535);
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
 uint j=(o/CORE_X+HALO)*LOCAL_X+o%CORE_X+HALO;
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
#if ACTIVE_EPOCH
    // All publishers in this dispatch store the same next epoch. Kernel
    // boundaries publish it globally before the following tile exchange.
    atomic_xchg(nextActive+f*stride+ny*pitch+nx,activeEpoch+1u);
#else
    atomic_or(nextActive+f*stride+ny*pitch+nx,1u);
#endif
   }
  }
 }
}
)CL";
// Programs are shared by the device; each lane owns distinct kernel handles.
// Both use the complete shared plan descriptor, so advertised parameters,
// compilation options and dispatch geometry cannot drift apart.
struct KernelVariant : ExecutionPlan
{
    Handle program = nullptr, kernel = nullptr, reverseKernel = nullptr;
};
constexpr auto kernelVariants()
{
    std::array<KernelVariant, PLANS.size() - 1> variants{};
    for (std::size_t i = 0; i < variants.size(); ++i)
        static_cast<ExecutionPlan&>(variants[i]) = PLANS[i + 1];
    return variants;
}

struct Runtime;
struct Device
{
    // Pre-reserved object, retained strings and weak control metadata; released
    // after all payload and registrations, including quarantined aliases.
    BudgetLease metadataLease{hostBudget};
    explicit Device(BudgetLease& reserved){reserved.transferTo(metadataLease);}
    API api;
    std::mutex initialization, cacheMutex, lanesMutex, failureMutex;
    bool probed = false;
    std::atomic<bool> failed{false};
    OpenCLBorrowedLifetime optionalLifetime,requiredLifetime;
    bool requiredQueryFailureDiagnostics=false;
    MemoryBudget deviceBudget{OpenCLDeviceBudget};
    Handle device = nullptr, context = nullptr;
    OpenCLStatus status;
    std::array<KernelVariant, PLANS.size() - 1> variants = kernelVariants();
    std::size_t selectedVariant = 0;
    Handle kernel = nullptr;
    struct Plane
    {
        // This lease precedes every retained owner, allocation and future state.
        BudgetLease hostLease{hostBudget};
        API* api;
        Handle buffer = nullptr;
        struct Storage {
            BudgetLease hostLease{hostBudget};
            API* api;
            Handle buffer=nullptr;
            BudgetLease deviceLease;
            Storage(API& api,MemoryBudget& budget,BudgetLease& admission):api(&api),deviceLease(budget) {
                admission.transferTo(hostLease);
            }
            static constexpr std::size_t fixedBytes(){return sizeof(Storage)+128;}
            static std::shared_ptr<Storage> create(API& api,MemoryBudget& budget) {
                BudgetLease reserved{hostBudget};reserved.resize(fixedBytes());
                return std::make_shared<Storage>(api,budget,reserved);
            }
            ~Storage(){if(buffer)api->ReleaseMemObject(buffer);}
        };
        std::shared_ptr<Storage> storage;
        CostIdentity identity;
        int width, height;
        std::vector<UInt> data;
        bool uniform = true;
        std::vector<std::uint8_t> blocked;
        // A fixed preallocated failure cannot retain an arbitrary callback's
        // exception graph in a published promise or quarantined cost plane.
        std::exception_ptr preparationFailure=std::make_exception_ptr(std::runtime_error("OpenCL cost preparation failed"));
        std::promise<void> completed;
        std::shared_future<void> ready = completed.get_future().share();
        std::atomic<bool> published{false};
        static constexpr std::size_t fixedBytes(){return sizeof(Plane)+1024;}
        static std::size_t estimatedBytes(std::size_t count,std::size_t mask,std::size_t retained) {
            if(mask>OpenCLHostBudget-fixedBytes() || retained>OpenCLHostBudget-fixedBytes()-mask ||
                count>(OpenCLHostBudget-fixedBytes()-mask-retained)/sizeof(UInt))throw BudgetExceeded();
            return fixedBytes()+count*sizeof(UInt)+mask+retained;
        }
        template<class Request>
        static std::shared_ptr<Plane> create(API& api,const Request& request,
            std::span<const std::uint8_t> mask,bool scalar,bool reserveOnly=false) {
            BudgetLease reserved{hostBudget};
            reserved.resize(estimatedBytes(scalar ? 1 : request.grid.cells(),mask.size(),
                request.identity.owner ? request.identity.retainedBytes : 0));
            return std::make_shared<Plane>(api,request,mask,scalar,reserveOnly,reserved);
        }
        template<class Request>
        Plane(API& api,const Request& r,std::span<const std::uint8_t> mask,bool scalar,bool reserveOnly,
            BudgetLease& admission):api(&api),identity(r.identity),width(r.grid.width()),height(r.grid.height()) {
            admission.transferTo(hostLease);
            const auto count=scalar ? std::size_t(1) : r.grid.cells();
            if(reserveOnly)data.reserve(count);else data.resize(count);
            blocked.assign(mask.begin(),mask.end());
            hostLease.resize(estimatedBytes(data.capacity(),blocked.capacity(),identity.owner ? identity.retainedBytes : 0));
        }
        void compactUniform() {
            if(data.size()==1)return;
            const auto previous=hostLease.bytes;
            hostLease.resize(previous+sizeof(UInt));
            try {{std::vector<UInt> scalar(1,data[0]);data.swap(scalar);}}
            catch(...){hostLease.resize(previous);throw;}
            hostLease.resize(estimatedBytes(data.capacity(),blocked.capacity(),identity.owner ? identity.retainedBytes : 0));
        }
    };
    std::array<std::shared_ptr<Plane>, 8> planes;
    std::array<std::uint64_t, 8> used{};
    std::uint64_t age = 0;
    static constexpr std::size_t LaneCapacity=64;
    OpenCLLaneRegistry<Runtime,LaneCapacity> lanes;
    OpenCLStatus retired;
    std::uint64_t retiredSequence = 0;
    std::atomic<std::uint64_t> sequence{0}, active{0}, maximumActive{0};
    std::atomic<std::uint64_t> noopFields{0}, threadCPUNs{0},threadCPUInvalidMeasurements{0},laneBudgetDeclines{0};
    std::atomic<std::uint64_t> optionalQuarantines{0},requiredQuarantines{0},requiredProofFailures{0};
    std::atomic<bool> threadCPUAvailable{false};
    void reapExpiredLanes() noexcept {std::lock_guard lock(lanesMutex);lanes.clearExpired();}
    void evictUnusedPlanes() {
        std::lock_guard lock(cacheMutex);
        for(auto& p:planes) if(p && p.use_count()==1) p.reset();
    }
    ~Device() {
        for(auto& v:variants) {
            if(v.kernel) api.ReleaseKernel(v.kernel);
            if(v.program) api.ReleaseProgram(v.program);
        }
        if(context) api.ReleaseContext(context);
    }
    void initialize() {
        std::scoped_lock lock(initialization, failureMutex);
        if(probed) return;
        const auto started=threadCPUClock();probe();
        const auto end=threadCPUClock();
        if(started && end>=started){status.threadCPUAvailable=true;status.initializationThreadCPUNs+=end-started;}
        else ++status.threadCPUInvalidMeasurements;
    }
    void probe()
    {
        if (probed)
            return;
        probed = true;
        try
        {
            status.directSeedUploadRequested=numericOverride("GLOB2_OPENCL_DIRECT_SEED_UPLOAD",0,1)!=0;
            requiredQueryFailureDiagnostics=numericOverride("GLOB2_TEST_OPENCL_REQUIRED_QUERY_FAILURE",0,1)!=0;
            status.apiCpuMode=numericOverride("GLOB2_OPENCL_API_CPU",0,2);
            api.load();
            status.checkInterval=numericOverride("GLOB2_OPENCL_CHECK_INTERVAL",8,32);
            if(!status.checkInterval) throw std::runtime_error("Invalid GLOB2_OPENCL_CHECK_INTERVAL");
            status.pollMicros=numericOverride("GLOB2_OPENCL_POLL_US",0,1000);
            status.deviceProfiling=numericOverride("GLOB2_OPENCL_PROFILE",0,1)!=0;
            status.uniformMetadata=numericOverride("GLOB2_OPENCL_UNIFORM_METADATA",0,1)!=0;
            status.probeColdCosts=numericOverride("GLOB2_OPENCL_PROBE_COLD_COSTS",0,1)!=0;
            status.activeEpoch=numericOverride("GLOB2_OPENCL_ACTIVE_EPOCH",0,1)!=0;
            status.parityBound=numericOverride("GLOB2_OPENCL_PARITY_BOUND",0,1)!=0;
            const auto requestedDevice=numericOverride("GLOB2_OPENCL_DEVICE",0,std::numeric_limits<unsigned>::max());
            const bool explicitDevice=std::getenv("GLOB2_OPENCL_DEVICE")!=nullptr;
            unsigned ordinal=0;
            UInt count = 0;
            check(api.GetPlatformIDs(0, nullptr, &count));
            if(!count || count>64)throw std::runtime_error("OpenCL platform count exceeds bounded metadata");
            std::array<Handle,64> platforms{};
            check(api.GetPlatformIDs(count, platforms.data(), nullptr));
            for (auto platform : std::span(platforms.data(),count))
            {
                UInt found = 0;
                if (api.GetDeviceIDs(platform, 4 /* GPU */, 0, nullptr, &found) != 0)
                    continue;
                if(!found)continue;
                if(found>64)throw std::runtime_error("OpenCL device count exceeds bounded metadata");
                std::array<Handle,64> devices{};
                check(api.GetDeviceIDs(platform, 4, found, devices.data(), nullptr));
                for (auto candidate : std::span(devices.data(),found))
                {
                    const auto candidateOrdinal=ordinal++;
                    if(explicitDevice && candidateOrdinal!=requestedDevice) continue;
                    UInt available = 0, compiler = 0;
                    std::size_t group = 0;
                    check(api.GetDeviceInfo(candidate, 0x1027, sizeof available, &available, nullptr));
                    check(api.GetDeviceInfo(candidate, 0x1028, sizeof compiler, &compiler, nullptr));
                    check(api.GetDeviceInfo(candidate, 0x1004, sizeof group, &group, nullptr));
                    if (available && compiler && group >= 256)
                    {
                        device = candidate;
                        status.deviceOrdinal=candidateOrdinal;status.deviceOrdinalKnown=true;
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
            if(!bytes || bytes>2048)throw std::runtime_error("OpenCL device name exceeds bounded metadata");
            std::array<char,2048> name{};
            check(api.GetDeviceInfo(device, 0x102B, bytes, name.data(), nullptr));
            if(name[bytes-1]!=0)throw std::runtime_error("Unterminated OpenCL device name");
            status.device.assign(name.data(),bytes-1);
            // Fingerprints are captured once on the initialization coordinator.
            // A malformed/unsupported optional query leaves metadata empty and
            // therefore cannot qualify a matching offline batch manifest.
            const auto infoString=[](auto query,Handle object,UInt key) {
                std::size_t size=0;
                if(query(object,key,0,nullptr,&size) || !size || size>2048)return std::string{};
                std::array<char,2048> value{};
                if(query(object,key,size,value.data(),nullptr) || value[size-1]!=0)return std::string{};
                return std::string(value.data(),size-1);
            };
            Handle platform=nullptr;
            if(!api.GetDeviceInfo(device,0x1031 /* CL_DEVICE_PLATFORM */,sizeof platform,&platform,nullptr) && platform) {
                status.platform=infoString(api.GetPlatformInfo,platform,0x0902);
                status.platformVendor=infoString(api.GetPlatformInfo,platform,0x0903);
                status.platformVersion=infoString(api.GetPlatformInfo,platform,0x0901);
            }
            status.deviceVendor=infoString(api.GetDeviceInfo,device,0x102C);
            status.driverVersion=infoString(api.GetDeviceInfo,device,0x102D);
            status.deviceVersion=infoString(api.GetDeviceInfo,device,0x102F);
            status.openCLCVersion=infoString(api.GetDeviceInfo,device,0x103D);
            // cl_khr_device_uuid defines a 16-byte CL_DEVICE_UUID_KHR query.
            // Unsupported/error results only disable matching offline evidence.
            std::array<unsigned char,16> uuid{};std::size_t uuidBytes=0;
            if(!api.GetDeviceInfo(device,0x106A,sizeof uuid,uuid.data(),&uuidBytes) && uuidBytes==uuid.size() &&
                std::any_of(uuid.begin(),uuid.end(),[](auto byte){return byte!=0;})) {
                constexpr char hex[]="0123456789abcdef";
                status.deviceUUIDHex.reserve(uuid.size()*2);
                for(const auto byte:uuid){status.deviceUUIDHex.push_back(hex[byte>>4]);status.deviceUUIDHex.push_back(hex[byte&15]);}
            }
            Int error = 0;
            context = api.CreateContext(nullptr, 1, &device, nullptr, nullptr, &error);
            check(error);
            UInt dimensions = 0;
            check(api.GetDeviceInfo(device, 0x1003, sizeof dimensions, &dimensions, nullptr));
            if (dimensions < 3 || dimensions>64)
                throw std::runtime_error("OpenCL device needs three work-item dimensions");
            std::array<std::size_t,64> axes{};
            check(api.GetDeviceInfo(device, 0x1005, dimensions * sizeof(std::size_t), axes.data(), nullptr));
            std::string unsupported;
            for (auto &variant : variants)
            {
                if (variant.threads > axes[0] || axes[1] < 1 || axes[2] < 1)
                    continue;
                const char *text = source;
                variant.program = api.CreateProgramWithSource(context, 1, &text, nullptr, &error);
                check(error);
                const auto options = "-cl-std=CL1.2 -DCORE_X=" + std::to_string(variant.tileWidth) +
                                     " -DCORE_Y=" + std::to_string(variant.tileHeight) +
                                     " -DSTEPS=" + std::to_string(variant.steps) +
                                     " -DCOLOR_RELAXATION=" + std::to_string(variant.colored) +
                                     " -DWG=" + std::to_string(variant.threads) +
                                     " -DFROZEN_HALO=" + std::to_string(variant.frozen) +
                                     " -DACTIVE_EPOCH=" + std::to_string(status.activeEpoch);
                const auto built =
                    api.BuildProgram(variant.program, 1, &device, options.c_str(), nullptr, nullptr);
                if (built != 0)
                {
                    if (built != -11)
                        check(built); // Driver errors, rather than a rejected candidate build.
                    std::size_t size = 0;
                    api.GetProgramBuildInfo(variant.program, device, 0x1183, 0, nullptr, &size);
                    std::array<char,4096> log{};
                    if(size && size<=log.size() &&
                        !api.GetProgramBuildInfo(variant.program,device,0x1183,size,log.data(),nullptr) && log[size-1]==0)
                        unsupported="OpenCL build failed: "+std::string(log.data(),size-1);
                    else unsupported="OpenCL build failed (diagnostic unavailable or exceeds bounded metadata)";
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
            status.tileWidth = variants[selectedVariant].tileWidth;
            status.tileHeight = variants[selectedVariant].tileHeight;
            status.localSteps = variants[selectedVariant].steps;
            status.available = true;
            status.directSeedUpload=status.directSeedUploadRequested;
            status.apiCpuConfigured=true;
        }
        catch (const std::exception &e)
        {
            status.error.assign(e.what(),std::min<std::size_t>(std::strlen(e.what()),4096));
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
        try { std::lock_guard lock(failureMutex); if(status.error.empty()) status.error.assign(message.substr(0,4096)); } catch(...) {}
    }
};
std::shared_ptr<Device> makeDevice() {
    BudgetLease reserved{hostBudget};
    reserved.resize(sizeof(Device)+256+Device::LaneCapacity*256+65536);
    return std::make_shared<Device>(reserved);
}
struct Runtime : std::enable_shared_from_this<Runtime>
{
    std::shared_ptr<Device> shared;
    API& api;
    bool execution;
    // Declared before payload so leases outlive all lane storage, including
    // status strings retained by a concurrent snapshot's shared lane handle.
    BudgetLease optionalObjectLease{hostBudget},optionalSubsetLease{probeBudget};
    std::uint64_t statusSequence = 0;
    Runtime(std::shared_ptr<Device> state = makeDevice(), bool lane = false)
        : shared(std::move(state)), api(shared->api), execution(lane) {}
    std::mutex mutex;
    OpenCLStatus status;
    OpenCLCpuAttribution cpuAttribution{status.apiCpu};
    bool probed = false;
    Handle device = nullptr, context = nullptr, queue = nullptr;
    std::array<KernelVariant, PLANS.size() - 1> variants = kernelVariants();
    std::size_t selectedVariant = 0;
    Handle kernel = nullptr;
    Handle first = nullptr, second = nullptr, changed = nullptr, descriptors = nullptr;
    std::size_t capacity = 0, tileCapacity = 0;
    Handle tilesFirst = nullptr, tilesSecond = nullptr;
    // Optional lanes retain their own accounting even when a concurrent status
    // snapshot temporarily holds the lane after its probe owner is destroyed.
    BudgetLease valuesLease{hostBudget}, blockedLease{hostBudget};
    BudgetLease deviceLease{shared->deviceBudget};
    std::vector<std::uint16_t> values;
    std::vector<std::uint8_t> blocked;
    struct RequiredTransaction {
        struct Admission {
            std::shared_ptr<Device> parent;
            bool owns=false;
            ~Admission(){if(owns)parent->requiredLifetime.release();}
        } admission; // Pins parent and releases admission after all payload.
        BudgetLease objectLease{hostBudget};
        std::shared_ptr<Runtime> lane;
        std::array<UInt,64> descriptors{};
        std::array<UInt,8> flags{};
        std::array<std::shared_ptr<Device::Plane>,8> costs{};
        std::array<std::shared_ptr<OpenCLRequiredControl>,8> controls{};
        std::shared_ptr<Device::Plane> preparing;
        Handle event=nullptr;
        OpenCLTransferProof proof;
        unsigned diagnosticQueryFailures=0;
        RequiredTransaction(std::shared_ptr<Runtime> owner,BudgetLease& reserved):lane(std::move(owner)) {
            admission.parent=lane->shared;reserved.transferTo(objectLease);
            diagnosticQueryFailures=lane->shared->requiredQueryFailureDiagnostics ? 2 : 0;
        }
        ~RequiredTransaction(){if(event)lane->api.ReleaseEvent(event);}
        void remember(Handle next) noexcept {
            if(!next)return;
            if(event)lane->api.ReleaseEvent(event);
            event=next;
        }
        bool canceled() const noexcept {
            const auto now=monotonicNs();
            for(const auto& control:controls)if(control && control->shouldCancel(now))return true;
            return false;
        }
        static bool terminalProof(void* value) noexcept {
            auto& transaction=*static_cast<RequiredTransaction*>(value);
            if(!transaction.event || !transaction.proof.canPollEvent())return false;
            if(transaction.diagnosticQueryFailures){--transaction.diagnosticQueryFailures;return false;}
            Int state=1;
            if(transaction.lane->api.GetEventInfo(transaction.event,0x11D3,sizeof state,&state,nullptr) || state!=0)return false;
            transaction.proof.eventCompleted();return !transaction.proof.needsProof();
        }
    };
    RequiredTransaction* required=nullptr; // Borrowed only inside synchronous batch.
    void requiredEnqueue(Int result,Handle event=nullptr,bool blocking=false) noexcept {
        if(!required)return;
        required->remember(event);required->proof.enqueue(result,event!=nullptr,blocking);
    }
    ~Runtime()
    {
        buffers();
        for (auto &variant : variants)
        {
            if (variant.kernel)
                api.ReleaseKernel(variant.kernel);
            if (variant.reverseKernel)
                api.ReleaseKernel(variant.reverseKernel);

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
        out.budgetDeclines+=in.budgetDeclines;out.threadCPUNs+=in.threadCPUNs;
        out.threadCPUAvailable=out.threadCPUAvailable||in.threadCPUAvailable;
        out.preparationNs+=in.preparationNs;out.uploadNs+=in.uploadNs;
        out.dispatchWaitNs+=in.dispatchWaitNs;out.readbackNs+=in.readbackNs;
        out.deviceUploadNs+=in.deviceUploadNs;out.deviceKernelNs+=in.deviceKernelNs;
        out.deviceReadbackNs+=in.deviceReadbackNs;out.deviceCheckReadNs+=in.deviceCheckReadNs;
        out.profilingErrors+=in.profilingErrors;
        out.uniformMetadataHits+=in.uniformMetadataHits;
        out.tileMaskInitializations+=in.tileMaskInitializations;out.tileMaskClears+=in.tileMaskClears;
        out.kernelArgumentUpdates+=in.kernelArgumentUpdates;
        out.deviceObservedFields+=in.deviceObservedFields;out.committedFields+=in.committedFields;
        out.directSeedUploads+=in.directSeedUploads;out.seedCopiedBytes+=in.seedCopiedBytes;
        out.seedUploadedBytes+=in.seedUploadedBytes;out.directSeedUploadedBytes+=in.directSeedUploadedBytes;
        out.outputCopiedBytes+=in.outputCopiedBytes;
        out.directSeedUploadSafetyCopies+=in.directSeedUploadSafetyCopies;
        out.requiredCancellations+=in.requiredCancellations;
        for(unsigned i=0;i<unsigned(OpenCLCpuCategory::Count);++i){out.apiCpu.ns[i]+=in.apiCpu.ns[i];out.apiCpu.calls[i]+=in.apiCpu.calls[i];}
        out.apiCpu.invalidScopes+=in.apiCpu.invalidScopes;out.apiCpu.reconciliationErrors+=in.apiCpu.reconciliationErrors;
        out.apiCpu.controlBracketNs+=in.apiCpu.controlBracketNs;out.apiCpu.clockReads+=in.apiCpu.clockReads;
        out.apiCpu.coveredNs+=in.apiCpu.coveredNs;
    }
    static std::shared_ptr<Runtime> registeredLane(const std::shared_ptr<Device>& device,
        BudgetLease& objectAdmission,BudgetLease* subsetAdmission=nullptr) {
        // Separate payload/control allocations: expired weak registration must
        // never retain a full Runtime after its payload credit is released.
        struct ControlCredit {BudgetLease host{hostBudget},subset{probeBudget};};
        // Existing admission pays for both this credit holder and the Runtime
        // control block before either allocation. Registry retains the credit
        // until the expired weak control is actually erased.
        auto credit=std::make_shared<ControlCredit>();
        objectAdmission.splitTo(credit->host,256);
        if(subsetAdmission)subsetAdmission->splitTo(credit->subset,256);
        auto result=std::shared_ptr<Runtime>(new Runtime(device,true),[credit](Runtime* value){delete value;});
        objectAdmission.transferTo(result->optionalObjectLease);
        if(subsetAdmission)subsetAdmission->transferTo(result->optionalSubsetLease);
        std::lock_guard lock(device->lanesMutex);
        if(!device->lanes.insert(result,credit))throw BudgetExceeded();
        return result;
    }
    Runtime& lane() {
        thread_local std::shared_ptr<Runtime> current;
        if(!current) {
            BudgetLease reserved{hostBudget};reserved.resize(sizeof(Runtime)+256+8192);
            current=registeredLane(shared,reserved);
        }
        return *current;
    }
    OpenCLStatus snapshot() {
        OpenCLStatus out;
        { std::lock_guard lock(shared->failureMutex); out=shared->status; }
        std::array<std::shared_ptr<Runtime>,Device::LaneCapacity> live{};
        std::uint64_t last;
        { std::lock_guard lock(shared->lanesMutex);
            accumulate(out,shared->retired);parameters(out,shared->retired);last=shared->retiredSequence;
            live=shared->lanes.snapshot();
        }
        for(auto& l:live) if(l) { std::lock_guard lock(l->mutex);accumulate(out,l->status);
            if(l->statusSequence>=last) {parameters(out,l->status);last=l->statusSequence;}
        }
        out.available=out.available&&!shared->failed.load();
        out.budgetDeclines+=shared->laneBudgetDeclines.load();
        out.optionalQuarantines=shared->optionalQuarantines.load();
        out.optionalQuarantinePending=shared->optionalLifetime.quarantined();
        out.requiredQuarantines=shared->requiredQuarantines.load();
        out.requiredProofFailures=shared->requiredProofFailures.load();
        out.requiredQuarantinePending=shared->requiredLifetime.quarantined();
        out.hostBytes=hostBudget.current.load();out.peakHostBytes=hostBudget.peak.load();
        out.probeBytes=probeBudget.current.load();out.peakProbeBytes=probeBudget.peak.load();
        out.deviceBytes=shared->deviceBudget.current.load();out.peakDeviceBytes=shared->deviceBudget.peak.load();
        out.noopFields=shared->noopFields.load();out.threadCPUNs=shared->threadCPUNs.load();
        out.threadCPUInvalidMeasurements+=shared->threadCPUInvalidMeasurements.load();
        out.threadCPUAvailable=out.threadCPUAvailable || shared->threadCPUAvailable.load();
        out.executionLanes=std::count_if(live.begin(),live.end(),[](const auto& lane){return bool(lane);});out.maxConcurrentBatches=shared->maximumActive.load();
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
        deviceLease.resize(0);
    }
    void probe() {
        if(probed) return;
        if(!readyPlans.load(std::memory_order_acquire)) throw std::runtime_error("OpenCL plans not ready");
        if(!shared->status.available || shared->failed.load()) throw std::runtime_error("OpenCL device unavailable");
        context=shared->context;device=shared->device;
        Int error=0;
        queue=api.CreateCommandQueue(context,device,shared->status.deviceProfiling ? 2 : 0,&error);check(error);
        for(std::size_t i=0;i<variants.size();++i) if(shared->variants[i].kernel) {
            variants[i].program=shared->variants[i].program;
            variants[i].kernel=api.CreateKernel(variants[i].program,"propagate",&error);check(error);
            if(shared->status.parityBound) {
                variants[i].reverseKernel=api.CreateKernel(variants[i].program,"propagate",&error);check(error);
            }
        }
        selectedVariant=shared->selectedVariant;selectVariant(selectedVariant);
        status.available=true;probed=true;
    }
    template<class Function> auto timedCpu(OpenCLCpuCategory category,Function&& function) {
        OpenCLCpuAttribution::Scope scope(cpuAttribution,category);return function();
    }
    template<class... Args> Int fill(Args... args) {
        const auto result=timedCpu(OpenCLCpuCategory::Fill,[&]{return api.EnqueueFillBuffer(args...);});
        if(required)required->proof.enqueueUnobserved(result);return result;
    }
    template<class... Args> Int enqueueKernel(Args... args) {
        const auto result=timedCpu(OpenCLCpuCategory::KernelEnqueue,[&]{return api.EnqueueNDRangeKernel(args...);});
        if(required)required->proof.enqueueUnobserved(result);return result;
    }
    template <class T> void argumentTo(Handle target,UInt index,T value)
    {
        check(timedCpu(OpenCLCpuCategory::Arguments,[&]{return api.SetKernelArg(target,index,sizeof(T),&value);}));++status.kernelArgumentUpdates;
    }
    template <class T> void argument(UInt index,T value) {argumentTo(kernel,index,value);}
    struct Event {
        API& api;Handle value=nullptr;bool owns=true;
        explicit Event(API& api):api(api) {}
        ~Event(){if(value && owns) api.ReleaseEvent(value);}
        Event(const Event&)=delete;
    };
    void await(Event& event) {
        if(!event.value || (!required && !shared->status.pollMicros))return;
        check(timedCpu(OpenCLCpuCategory::Other,[&]{return api.Flush(queue);}));
        for(;;) {
            if(required && required->canceled())throw RequiredCanceled();
            Int state=1;
            if(required && required->diagnosticQueryFailures){--required->diagnosticQueryFailures;throw std::runtime_error("Injected required event query failure");}
            check(timedCpu(OpenCLCpuCategory::Other,[&]{return api.GetEventInfo(event.value,0x11D3,sizeof state,&state,nullptr);}));
            if(state<0)check(state);
            if(!state) {if(required)required->proof.eventCompleted();return;}
            // Zero formerly selected a blocking transfer. Required execution
            // now uses a safety default; explicit nonzero overrides are honored.
            const auto delay=std::chrono::microseconds(shared->status.pollMicros ? shared->status.pollMicros : 100);
            if(required && required->controls[0])required->controls[0]->waitFor(delay);
            else std::this_thread::sleep_for(delay);
        }
        // Failure unwinds only into batch's nonblocking proof/quarantine guard.
        // All transfer endpoints and retained events already belong to it.
    }
    void profile(Handle event,std::uint64_t& total) {
        if(!event || !shared->status.deviceProfiling) return;
        Bits started=0,finished=0;
        if(timedCpu(OpenCLCpuCategory::Other,[&]{return api.GetEventProfilingInfo(event,0x1282,sizeof started,&started,nullptr);})!=0 ||
           timedCpu(OpenCLCpuCategory::Other,[&]{return api.GetEventProfilingInfo(event,0x1283,sizeof finished,&finished,nullptr);})!=0 || finished<started) {
            ++status.profilingErrors;return;
        }
        total+=finished-started;
    }
    void read(Handle buffer,void* output,std::size_t bytes,bool finalOutput) {
        if(required && required->canceled())throw RequiredCanceled();
        Event event(api);
        const bool asynchronous=required || shared->status.pollMicros!=0;
        const bool capture=asynchronous || shared->status.deviceProfiling;
        const auto result=timedCpu(finalOutput ? OpenCLCpuCategory::OutputRead : OpenCLCpuCategory::CheckRead,[&]{return api.EnqueueReadBuffer(queue,buffer,!asynchronous,0,bytes,output,0,nullptr,capture ? &event.value : nullptr);});
        requiredEnqueue(result,event.value,!asynchronous);
        if(required && event.value)event.owns=false;
        check(result);
        if(asynchronous && !event.value)throw std::runtime_error("OpenCL omitted transfer event");
        await(event);
        profile(event.value,finalOutput ? status.deviceReadbackNs : status.deviceCheckReadNs);
    }
    void write(Handle buffer, const void *data, std::size_t bytes)
    {
        if(required && required->canceled())throw RequiredCanceled();
        const auto start=activeStageTiming ? monotonicNs() : 0;
        Event event(api);
        const bool asynchronous=required || shared->status.pollMicros!=0;
        const bool capture=asynchronous || shared->status.deviceProfiling;
        const auto result=timedCpu(OpenCLCpuCategory::Upload,[&]{return api.EnqueueWriteBuffer(queue,buffer,!asynchronous,0,bytes,data,0,nullptr,capture ? &event.value : nullptr);});
        requiredEnqueue(result,event.value,!asynchronous);
        if(required && event.value)event.owns=false;
        check(result);
        if(asynchronous && !event.value)throw std::runtime_error("OpenCL omitted transfer event");
        await(event);profile(event.value,status.deviceUploadNs);
        if(activeStageTiming) activeStageTiming->uploadNs+=monotonicNs()-start;
    }
    void awaitPlane(const std::shared_ptr<Device::Plane>& p) {
        while(p->ready.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready) {
            if(required && required->canceled())throw RequiredCanceled();
            if(shared->failed.load()) throw std::runtime_error("OpenCL cost preparation failed on another lane");
        }
        p->ready.get();
    }
    std::shared_ptr<Device::Plane> costPlane(const BackendRequest& r) {
        const auto packed=r.identity.packedUniformCost;
        const bool knownUniform=shared->status.uniformMetadata && r.identity.owner && r.identity.allCells
            && (packed&65535u) && (packed>>16);
        auto sameIdentity=[&](const auto& p) {
            return p&&r.identity.owner&&p->identity.owner==r.identity.owner&&p->identity.variant==r.identity.variant&&
                p->identity.revision==r.identity.revision&&p->identity.allCells==r.identity.allCells&&
                p->identity.packedUniformCost==packed&&
                p->width==r.grid.width()&&p->height==r.grid.height()&&(r.identity.allCells||p->blocked==blocked);
        };
        // Immutable snapshots allow expensive mask/content comparison outside
        // the cache lock. Only publication and replacement need that lock.
        std::array<std::shared_ptr<Device::Plane>,8> candidates;
        {std::lock_guard lock(shared->cacheMutex);candidates=shared->planes;}
        for(auto& p:candidates) if(sameIdentity(p)) {
            awaitPlane(p);shared->touch(p);++status.costIdentityHits;return p;
        }
        candidates.fill({});
        std::shared_ptr<Device::Plane> p;
        try {p=Device::Plane::create(api,r,blocked,knownUniform);}
        catch(const BudgetExceeded&) {shared->evictUnusedPlanes();p=Device::Plane::create(api,r,blocked,knownUniform);}
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
            const auto n=r.grid.cells();
            if(knownUniform) {p->data[0]=packed;++status.uniformMetadataHits;}
            else for(std::size_t i=0;i<n;++i) {
                if(!(i%64) && required && required->canceled())throw RequiredCanceled();
                const auto step=!r.identity.allCells&&blocked[i]?LAND_STEPS:r.costAt(r.context,i);
                if(!step.cardinal||!step.diagonal||step.cardinal>65535||step.diagonal>65535)
                    throw std::runtime_error("Invalid OpenCL gradient edge cost");
                p->data[i]=step.cardinal|(step.diagonal<<16);
                if (i && p->data[i] != p->data[0]) p->uniform = false;
            }
            if(shared->status.uniformMetadata && p->uniform) p->compactUniform();
            {std::lock_guard lock(shared->cacheMutex);candidates=shared->planes;}
            for(auto& other:candidates) if(other&&other!=p&&other->published.load()&&other->uniform==p->uniform&&
                (p->uniform || (other->width==p->width&&other->height==p->height))&&other->data==p->data) {
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
            candidates.fill({});
            p->storage=Device::Plane::Storage::create(api,shared->deviceBudget);
            const auto bytes=p->data.size()*sizeof(UInt);
            try {p->storage->deviceLease.resize(bytes);}
            catch(const BudgetExceeded&) {shared->evictUnusedPlanes();p->storage->deviceLease.resize(bytes);}
            Int error=0;p->buffer=p->storage->buffer=api.CreateBuffer(context,1,bytes,nullptr,&error);check(error);
            if(required)required->preparing=p;
            write(p->buffer,p->data.data(),bytes);++status.costUploads;
            if(required)required->preparing.reset();
            p->published.store(true);p->completed.set_value();
            if(!reserved) {std::lock_guard lock(shared->cacheMutex);
                const auto slot=std::min_element(shared->used.begin(),shared->used.end())-shared->used.begin();
                shared->planes[slot]=p;shared->used[slot]=++shared->age;
            }
            return p;
        } catch(...) {
            try {p->completed.set_exception(p->preparationFailure);} catch(...) {}
            {std::lock_guard lock(shared->cacheMutex);for(auto& cached:shared->planes) if(cached==p) cached.reset();}
            throw;
        }
    }
    void computeBatch(std::span<const BackendRequest *const> requests,std::span<std::uint16_t> staging,
                      bool directSingletonSeed=false)
    {
        OpenCLCpuAttribution::Scope preparationCpuScope(cpuAttribution,OpenCLCpuCategory::Preparation);
        const auto preparationStart=activeStageTiming ? monotonicNs() : 0;
        if(shared->failed.load()) throw std::runtime_error("OpenCL device failed on another lane");
        std::size_t total = 0;
        UInt width = 0, height = 0;
        for (auto *r : requests)
        {
            total += r->grid.cells();
            width = std::max(width, UInt(r->grid.width()));
            height = std::max(height, UInt(r->grid.height()));
        }
        if (total > std::numeric_limits<UInt>::max() ||
            total > (std::numeric_limits<std::size_t>::max()-288) / (2 * sizeof(std::uint16_t)))
            throw std::runtime_error("Gradient batch exceeds OpenCL index range");
        if (total > capacity)
        {
            buffers();
            try {deviceLease.resize(total*2*sizeof(std::uint16_t)+288);}
            catch(const BudgetExceeded&) {shared->evictUnusedPlanes();deviceLease.resize(total*2*sizeof(std::uint16_t)+288);}
            Int error = 0;
            for (auto *buffer : {&first, &second})
            {
                *buffer = api.CreateBuffer(context, 1, total * sizeof(std::uint16_t), nullptr, &error);
                check(error);
            }
            changed = api.CreateBuffer(context, 1, 8 * sizeof(UInt), nullptr, &error);
            check(error);
            descriptors = api.CreateBuffer(context, 1, 64 * sizeof(UInt), nullptr, &error);
            check(error);
            capacity = total;
        }
        // Polling failure may outlive this stack frame. Every borrowed endpoint
        // belongs to the pre-admitted transaction, never to these locals.
        auto& desc=required->descriptors;desc.fill(0);
        auto& held=required->costs;held.fill({});
        // Even an errored blocking call has ambiguous acceptance. Never borrow
        // caller seeds: CPU recovery may start before the driver stops reading.
        if(directSingletonSeed)++status.directSeedUploadSafetyCopies;
        directSingletonSeed=false;
        std::size_t offset=0, field=0;
        for(auto* r:requests) {
            const auto n=r->grid.cells();
            if(!directSingletonSeed) {
                std::copy(r->gradient,r->gradient+n,staging.begin()+offset);
                status.seedCopiedBytes+=n*sizeof(std::uint16_t);
            }
            blocked.clear();
            if(!r->identity.allCells) {
                resizeStaging(blocked,n,blockedLease);
                for(std::size_t i=0;i<n;++i) {
                    if(!(i%64) && required->canceled())throw RequiredCanceled();
                    blocked[i]=r->gradient[i]==0;
                }
            }
            held[field]=costPlane(*r);
            const std::array descriptor{UInt(r->grid.width()),UInt(r->grid.height()),UInt(offset),UInt(field),
                UInt(r->limit),held[field]->uniform ? 3u : 1u,UInt((r->grid.width()+variants[selectedVariant].tileWidth-1)/variants[selectedVariant].tileWidth),
                UInt((r->grid.height()+variants[selectedVariant].tileHeight-1)/variants[selectedVariant].tileHeight)};
            std::copy(descriptor.begin(),descriptor.end(),desc.begin()+field*8);
            offset+=n;++field;
        }
        // Polling uploads always use retained staging. Original seeds stay
        // untouched and unborrowed until commit, including failed drain paths.
        const auto seedBytes=total*sizeof(std::uint16_t);
        write(first,directSingletonSeed ? requests.front()->gradient : staging.data(),seedBytes);
        status.seedUploadedBytes+=seedBytes;
        if(directSingletonSeed){++status.directSeedUploads;status.directSeedUploadedBytes+=seedBytes;}
        write(descriptors, desc.data(), requests.size()*8*sizeof(UInt));
        Handle a = first, b = second;
        const auto &variant = variants[selectedVariant];
        const UInt pitch = (width + variant.tileWidth - 1) / variant.tileWidth, rows = (height + variant.tileHeight - 1) / variant.tileHeight;
        const std::size_t stride = std::size_t(pitch) * rows, tileCount = stride * requests.size();
        if (tileCount > std::numeric_limits<UInt>::max() ||
            tileCount > std::numeric_limits<std::size_t>::max() / sizeof(UInt))
            throw std::runtime_error("OpenCL tile mask exceeds index range");
        if (tileCount > tileCapacity)
        {
            for(auto* mask:{&tilesFirst,&tilesSecond}) {if(*mask) api.ReleaseMemObject(*mask);*mask=nullptr;}
            deviceLease.resize(capacity*2*sizeof(std::uint16_t)+288);
            tileCapacity=0;
            deviceLease.resize(capacity*2*sizeof(std::uint16_t)+288+tileCount*2*sizeof(UInt));
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
        check(fill(queue, tilesFirst, &one, sizeof one, 0, tileCount * sizeof(UInt), 0,
                                    nullptr, nullptr));
        ++status.tileMaskInitializations;
        if(shared->status.activeEpoch) {
            const UInt zero=0;
            // Reset both masks on every new batch, including reused buffers.
            // Otherwise stale prior-request epochs could spuriously activate
            // a tile during this request's first exchanges.
            check(fill(queue,tilesSecond,&zero,sizeof zero,0,tileCount*sizeof(UInt),0,nullptr,nullptr));
            ++status.tileMaskInitializations;
        }
        Handle active = tilesFirst, nextActive = tilesSecond;
        const std::size_t global[]{std::size_t(pitch) * variant.threads, std::size_t(rows),
                                   requests.size()},
            local[]{variant.threads, 1, 1};
        std::array<UInt,8> zero{};
        auto& flags=required->flags;flags.fill(0);
        ++status.batches;
        status.maxBatchFields = std::max(status.maxBatchFields, std::uint64_t(requests.size()));
        // Each lane owns its kernel instances. Both parity instances share
        // immutable batch arguments but retain opposite pingpong bindings.
        // Required batches may change shape/cost/plan, so rebind both each time.
        const auto forward=variants[selectedVariant].kernel;
        const auto reverse=variants[selectedVariant].reverseKernel;
        kernel=forward;
        const auto bindCommon=[&](Handle target) {
            for(unsigned i=0;i<8;++i) argumentTo(target,2+i,held[i]?held[i]->buffer:held[0]->buffer);
            argumentTo(target,10,changed);argumentTo(target,11,descriptors);
            argumentTo(target,14,UInt(stride));argumentTo(target,15,pitch);
        };
        bindCommon(forward);
        if(shared->status.parityBound) {
            bindCommon(reverse);
            argumentTo(forward,0,first);argumentTo(forward,1,second);
            argumentTo(forward,12,tilesFirst);argumentTo(forward,13,tilesSecond);
            argumentTo(reverse,0,second);argumentTo(reverse,1,first);
            argumentTo(reverse,12,tilesSecond);argumentTo(reverse,13,tilesFirst);
        }
        preparationCpuScope.close();
        const auto dispatchStart=activeStageTiming ? monotonicNs() : 0;
        if(activeStageTiming) activeStageTiming->preparationNs+=dispatchStart-preparationStart;
        // Every dispatch extends at least one global path edge. Frozen halos
        // exchange only one cell even when they perform many local sweeps, so
        // the ushort convergence bound counts dispatches, not local sweeps.
        const auto checkInterval=shared->status.checkInterval;
        bool deviceObserved=false;
        for (UInt round = 0; round < 65536;)
        {
            if(shared->failed.load()) throw std::runtime_error("OpenCL device failed on another lane");
            if(required && required->canceled())throw RequiredCanceled();
            const auto count=std::min<UInt>(checkInterval,65536-round);
            struct Events {
                API& api;std::array<Handle,32> values{};
                ~Events(){for(auto event:values) if(event) api.ReleaseEvent(event);}
            } events{api};
            for (unsigned dispatch = 0; dispatch < count; ++dispatch)
            {
                if(shared->status.parityBound) kernel=a==first ? forward : reverse;
                // Only the last dispatch's changes are read by the host. The
                // in-order queue clears earlier accumulated flags before it.
                if (dispatch+1 == count)
                    check(fill(queue, changed, zero.data(), sizeof(UInt), 0,
                                                requests.size() * sizeof(UInt), 0, nullptr, nullptr));
                if(shared->status.activeEpoch)
                    // Epoch one is the initial mask. The convergence bound
                    // ends at epoch 65537, well before unsigned wraparound.
                    argument(16,UInt(round+dispatch+1));
                else {
                    check(fill(queue, nextActive, zero.data(), sizeof(UInt), 0,
                                                tileCount * sizeof(UInt), 0, nullptr, nullptr));
                    ++status.tileMaskClears;
                }
                if(!shared->status.parityBound) {
                    argument(0, a);argument(1, b);
                    argument(12, active);argument(13, nextActive);
                }
                check(
                    enqueueKernel(queue,kernel,3,nullptr,global,local,0,nullptr,
                        shared->status.deviceProfiling ? &events.values[dispatch] : nullptr));
                std::swap(a, b);
                std::swap(active, nextActive);
                ++status.dispatches;
            }
            round+=count;
            read(changed,flags.data(),requests.size()*sizeof(UInt),false);
            if(!deviceObserved) {
                // This successful in-order read proves completion of every
                // preceding kernel for this chunk. Submission alone cannot.
                for(const auto* request:requests)
                    if(request->deviceExecutionObserved)*request->deviceExecutionObserved=true;
                status.deviceObservedFields+=requests.size();deviceObserved=true;
            }
            for(auto event:events.values) profile(event,status.deviceKernelNs);
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
                const auto readStart=activeStageTiming ? monotonicNs() : 0;
                if(activeStageTiming) activeStageTiming->dispatchNs+=readStart-dispatchStart;
                read(a,staging.data(),total*sizeof(std::uint16_t),true);
                if(activeStageTiming) activeStageTiming->readbackNs+=monotonicNs()-readStart;
                return;
            }
            // No later kernel consumes descriptors once every field is done.
            // Mixed batches still retire finished fields before dispatching again.
            if (retired)
                write(descriptors, desc.data(), requests.size()*8*sizeof(UInt));
        }
        throw std::runtime_error("OpenCL gradient failed to converge");
    }
    void selectVariant(std::size_t index)
    {
        selectedVariant = index;
        kernel = variants[index].kernel;
        status.tileWidth = variants[index].tileWidth;
        status.tileHeight = variants[index].tileHeight;
        status.localSteps = variants[index].steps;
        status.colored = variants[index].colored;
    }
    static bool alreadyFixed(const BackendRequest& request)
    {
        return alreadyFixedGradient(std::span(request.gradient,request.grid.cells()));
    }
    bool batch(std::span<const BackendRequest> input, Plan plan)
    {
        struct CPUTimer {
            Device& device;bool outer;std::uint64_t start;
            explicit CPUTimer(Device& device):device(device),outer(backendCPUDepth++==0),start(outer ? threadCPUClock() : 0) {}
            ~CPUTimer(){
                --backendCPUDepth;
                if(outer){const auto end=threadCPUClock();
                    if(start && end>=start){device.threadCPUAvailable.store(true);device.threadCPUNs+=end-start;}
                    else ++device.threadCPUInvalidMeasurements;}
            }
        } cpuTimer{*shared};
        for(const auto& request:input) {
            if(request.executedOnDevice)*request.executedOnDevice=false;
            if(request.deviceExecutionObserved)*request.deviceExecutionObserved=false;
        }
        if(input.empty()) return true;
        if(plan==Plan::CPU || unsigned(plan)>=PLANS.size() ||
           !(readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(plan)))) return false;
        if(shared->failed.load()) {
            for(const auto& r:input) r.session.fail();
            return false;
        }
        if(std::any_of(input.begin(),input.end(),[](const auto& r) {
            return r.session.failed.load() || r.operation!=Operation::CompleteField || r.limit<0;
        })) return false;
        if(std::all_of(input.begin(),input.end(),alreadyFixed)) {shared->noopFields+=input.size();return true;}
        if(!execution) {
            try { return lane().batch(input,plan); }
            catch(const BudgetExceeded&) {++shared->laneBudgetDeclines;return false;}
            catch(...) { shared->fail("Unable to allocate OpenCL execution lane"); readyPlans.store(0);
                for(const auto& r:input) r.session.fail();
                return false; }
        }
        std::lock_guard lock(mutex);
        cpuAttribution.mode=shared->status.apiCpuMode;
        OpenCLCpuAttribution::Scope batchCpuScope(cpuAttribution,OpenCLCpuCategory::Other);
        struct Active {
            Device& d;
            Active(Device& d):d(d) {const auto n=++d.active;auto old=d.maximumActive.load();while(old<n&&!d.maximumActive.compare_exchange_weak(old,n)){} }
            ~Active(){--d.active;}
        } activeLane(*shared);
        statusSequence=++shared->sequence;
        StageTiming localStages;
        auto* stages=activeStageTiming ? activeStageTiming : accountingRequested() ? &localStages : nullptr;
        const auto before=stages ? *stages : StageTiming{};
        StageTimingScope timingScope(stages);
        struct StageAccumulator {
            OpenCLStatus& status;StageTiming* timing;StageTiming before;
            ~StageAccumulator() {
                if(!timing) return;
                status.preparationNs+=timing->preparationNs-before.preparationNs;
                status.uploadNs+=timing->uploadNs-before.uploadNs;
                status.dispatchWaitNs+=timing->dispatchNs-before.dispatchNs;
                status.readbackNs+=timing->readbackNs-before.readbackNs;
            }
        } stageAccumulator{status,stages,before};
        if(!shared->requiredLifetime.tryReserve()){++status.budgetDeclines;return false;}
        struct SlotGuard {Device& device;bool transferred=false;
            ~SlotGuard(){if(!transferred)device.requiredLifetime.release();}} slotGuard{*shared};
        std::shared_ptr<RequiredTransaction> transaction;
        struct CurrentGuard {Runtime& lane;~CurrentGuard(){lane.required=nullptr;}} currentGuard{*this};
        try {
            BudgetLease reserved{hostBudget};reserved.resize(sizeof(RequiredTransaction)+256);
            transaction=std::make_shared<RequiredTransaction>(shared_from_this(),reserved);
            transaction->admission.owns=true;slotGuard.transferred=true;required=transaction.get();
            for(std::size_t i=0;i<std::min<std::size_t>(8,input.size());++i)transaction->controls[i]=input[i].requiredControl;
            if(transaction->canceled())throw RequiredCanceled();
            probe(); // Lane buffers/queue only. Compilation must already be complete.
            if(!variants[unsigned(plan)-1].kernel) return false;
            selectVariant(unsigned(plan)-1);
            // Stage ALL output before commit; even a failure in the last chunk
            // leaves every caller's original seeds intact for exact CPU recovery.
            OpenCLCpuAttribution::Scope allocationCpuScope(cpuAttribution,OpenCLCpuCategory::Preparation);
            std::size_t total=0;
            for(const auto& request:input) {
                if(request.grid.cells()>std::numeric_limits<std::size_t>::max()-total) throw BudgetExceeded();
                total+=request.grid.cells();
            }
            if(total>OpenCLHostBudget/sizeof(std::uint16_t)) throw BudgetExceeded();
            try {resizeStaging(values,total,valuesLease);}
            catch(const BudgetExceeded&) {shared->evictUnusedPlanes();resizeStaging(values,total,valuesLease);}
            allocationCpuScope.close();
            std::size_t outputOffset=0;
            for(std::size_t begin=0;begin<input.size();begin+=8) {
                const auto count=std::min<std::size_t>(8,input.size()-begin);
                std::array<const BackendRequest*,8> pointers{};transaction->controls.fill({});
                for(std::size_t i=0;i<count;++i){pointers[i]=&input[begin+i];transaction->controls[i]=input[begin+i].requiredControl;}
                if(transaction->canceled())throw RequiredCanceled();
                std::size_t chunkCells=0;for(std::size_t i=0;i<count;++i) chunkCells+=pointers[i]->grid.cells();
                computeBatch(std::span(pointers.data(),count),std::span(values.data()+outputOffset,chunkCells),
                    shared->status.directSeedUpload && input.size()==1);
                outputOffset+=chunkCells;
                status.fields+=count;
            }
            if(std::any_of(input.begin(),input.end(),[](const auto& r){return r.requiredControl && r.requiredControl->shouldCancel(monotonicNs());}))throw RequiredCanceled();
            if(shared->failed.load() || std::any_of(input.begin(),input.end(),[](const auto& r){return r.session.failed.load();}))
                throw std::runtime_error("OpenCL device or session failed on another lane");
            outputOffset=0;
            OpenCLCpuAttribution::Scope outputCopyCpuScope(cpuAttribution,OpenCLCpuCategory::OutputCopy);
            for(const auto& r:input) {
                std::copy_n(values.data()+outputOffset,r.grid.cells(),r.gradient);
                status.outputCopiedBytes+=r.grid.cells()*sizeof(std::uint16_t);
                if(r.executedOnDevice) *r.executedOnDevice=true;
                ++status.committedFields;
                outputOffset+=r.grid.cells();
            }
            outputCopyCpuScope.close();
            ++status.schedulerBatches;
            return true;
        } catch(const RequiredCanceled&) {
            ++status.requiredCancellations;
            if(transaction && transaction->proof.needsProof())protectFailedTransaction(transaction);
            return false;
        } catch(const BudgetExceeded&) {
            ++status.budgetDeclines;
            if(transaction && transaction->proof.needsProof())protectFailedTransaction(transaction);
            return false;
        } catch(...) {
            status.available=false;
            for(const auto& r:input) { r.session.fail(); ++status.cpuSelections; }
            try {
                try { throw; }
                catch(const std::exception& e) { status.error.assign(e.what(),std::min<std::size_t>(std::strlen(e.what()),4096)); }
                catch(...) { status.error="OpenCL workload failed with an unknown exception"; }
            } catch(...) { status.error.clear(); }
            readyPlans.store(0,std::memory_order_release);
            shared->fail(status.error);
            if(transaction && transaction->proof.needsProof())protectFailedTransaction(transaction);
            return false;
        }
    }
    void protectFailedTransaction(const std::shared_ptr<RequiredTransaction>& transaction) noexcept {
        // A covered positive event is proof. Failure/absence is never permission
        // to block on Finish or release borrowed endpoints during CPU recovery.
        if(RequiredTransaction::terminalProof(transaction.get()))return;
        ++shared->requiredProofFailures;++shared->requiredQuarantines;
        shared->failed.store(true,std::memory_order_release);readyPlans.store(0,std::memory_order_release);
        if(!shared->requiredLifetime.quarantine(transaction))std::terminate();
    }
    bool run(const BackendRequest& r, Plan plan) { return batch(std::span(&r,1),plan); }

};
#else
struct Runtime
{
    std::mutex mutex;
    OpenCLStatus status{false, {}, "OpenCL backend unavailable on this platform", 0, 0, 0};
    void probe() {}
    bool batch(std::span<const BackendRequest> requests, Plan)
    {
        for (const auto &r : requests)
        {
            if(r.executedOnDevice) *r.executedOnDevice=false;
            if(r.deviceExecutionObserved) *r.deviceExecutionObserved=false;
            r.session.fail();
        }
        return false;
    }
    bool run(const BackendRequest &r, Plan)
    {
        if(r.executedOnDevice)*r.executedOnDevice=false;
        if(r.deviceExecutionObserved)*r.deviceExecutionObserved=false;
        r.session.fail();
        return false;
    }
};
#endif
Runtime &runtime()
{
    static Runtime value;
    return value;
}
bool execute(const BackendRequest &r, Plan choice) { return runtime().run(r, choice); }
bool executeBatch(std::span<const BackendRequest> requests, Plan choice)
{
    return runtime().batch(requests, choice);
}
void prepare()
{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    auto& device=*runtime().shared;
    device.initialize();
    unsigned mask=0;
    if(device.status.available && !device.failed.load())
        for(std::size_t i=0;i<device.variants.size();++i) if(device.variants[i].kernel) mask|=1u<<unsigned(device.variants[i].id);
    readyPlans.store(mask,std::memory_order_release);
#endif
}
struct Register
{
    Register()
    {
        prepareAccelerator = &prepare;
        accelerator = &execute;
        batchAccelerator = &executeBatch;
    }
} registration;
} // namespace
bool reserveOpenCLHostBytes(std::size_t bytes) noexcept {return hostBudget.reserve(bytes);}
void releaseOpenCLHostBytes(std::size_t bytes) noexcept {hostBudget.release(bytes);}
std::shared_ptr<OpenCLRequiredControl> makeOpenCLRequiredControl() noexcept {
    struct LeasedControl : OpenCLRequiredControl {
        BudgetLease lease{hostBudget};
        explicit LeasedControl(BudgetLease& reserved){reserved.transferTo(lease);}
    };
    try {
        BudgetLease reserved{hostBudget};reserved.resize(sizeof(LeasedControl)+128);
        return std::make_shared<LeasedControl>(reserved);
    }catch(...){return {};}
}
bool reserveOpenCLProbeBytes(std::size_t bytes) noexcept {
    if(!probeBudget.reserve(bytes)) return false;
    if(hostBudget.reserve(bytes)) return true;
    probeBudget.release(bytes);return false;
}
void releaseOpenCLProbeBytes(std::size_t bytes) noexcept {hostBudget.release(bytes);probeBudget.release(bytes);}
std::size_t openCLProbeBytes() noexcept {return probeBudget.current.load();}
std::shared_ptr<const void> retainOpenCLLifetime() {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    return runtime().shared;
#else
    return {};
#endif
}
std::shared_ptr<const void> reserveOpenCLDeviceBytesForDiagnostics(std::size_t bytes) {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    struct Lease {
        std::shared_ptr<Device> device;
        BudgetLease budget;
        Lease(std::shared_ptr<Device> device,std::size_t bytes):device(std::move(device)),budget(this->device->deviceBudget) {
            budget.resize(bytes);
        }
    };
    try {return std::make_shared<Lease>(runtime().shared,bytes);}catch(...) {return {};}
#else
    return {};
#endif
}
struct OpenCLProbe::Impl
{
    OpenCLProbeProgress progress=OpenCLProbeProgress::Pending;
    OpenCLProbe::Metrics measured;
    std::atomic<bool> cancelled{false};
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    using Phase=OpenCLProbe::Stage;
    struct AdmissionLease {
        std::shared_ptr<Device> parent;
        bool owns=false;
        ~AdmissionLease(){if(owns)parent->optionalLifetime.release();}
    } admission;
    Phase phase=Phase::Costs;
    OpenCLProbeInput request;
    Plan plan;
    BudgetLease objectLease{hostBudget};
    std::shared_ptr<const void> keepAlive;
    OpenCLProbeClock clock;
    std::shared_ptr<Runtime> lane;
    std::shared_ptr<Device::Plane> costs;
    std::uint64_t generation,lastCpuClock=0;
    std::size_t copied=0,tileCount=0,costCharge=0;
    UInt round=0,pitch=0,rows=0,flags=0,windowEnd=0;
    std::array<UInt,8> descriptor{};
    Handle a=nullptr,b=nullptr,active=nullptr,nextActive=nullptr,event=nullptr;
    bool privateScalar=false,privateCosts=false,source=false,onlyZerosAndMax=true,borrowedTransfers=false;
    bool completionUnknown=false,untrackedCommand=false;
    static constexpr std::size_t controlBytes=512+sizeof(OpenCLBorrowedLifetime);
    static constexpr std::size_t objectBytes() {return sizeof(OpenCLProbe)+sizeof(Impl)+sizeof(Runtime)+controlBytes;}
    Impl(const OpenCLProbeInput& r,Plan p,std::shared_ptr<const void> owner,OpenCLProbeClock clockValue,
         BudgetLease& objectAdmission,BudgetLease& laneAdmission,BudgetLease& subsetAdmission)
        :request(r),plan(p),keepAlive(std::move(owner)),clock(std::move(clockValue)),generation(r.session.currentGeneration()) {
        admission.parent=runtime().shared;
        objectAdmission.transferTo(objectLease);
        lane=Runtime::registeredLane(runtime().shared,laneAdmission,&subsetAdmission);
        measured.activeEpoch=lane->shared->status.activeEpoch;
        measured.checkInterval=measured.activeEpoch ? lane->shared->status.checkInterval : 1;

    }
    ~Impl() {
        // Only positive proof or command-free setup may reach destruction.
        // Ambiguous completion retains this state and its Device/API parent.
        if(event) lane->api.ReleaseEvent(event);
    }
    void quarantine(bool executionFailed=false) noexcept {
        measured.executionFailed=measured.executionFailed || executionFailed;
        if(!measured.quarantined)++lane->shared->optionalQuarantines;
        measured.quarantined=true;
        completionUnknown=true;cancelled=true;progress=OpenCLProbeProgress::Quarantined;
        lane->shared->failed.store(true,std::memory_order_release);
        readyPlans.store(0,std::memory_order_release);
    }
    // Every successfully accepted async command produces a trailing event.
    // Replacing the old event is safe on this in-order queue: positive terminal
    // proof for the new event also covers all preceding borrowed transfers.
    void remember(Handle next) {
        if(!next){untrackedCommand=true;quarantine(true);throw std::runtime_error("OpenCL omitted completion event");}
        if(event)lane->api.ReleaseEvent(event);
        event=next;
    }
    template<class Enqueue> void submitEvent(Enqueue enqueue) {
        const auto result=submitOpenCLTrackedCommand<Handle>(enqueue,
            [this](Handle next){remember(next);},
            [this]{untrackedCommand=true;quarantine(true);});
        check(result);
        if(untrackedCommand)throw std::runtime_error("OpenCL omitted completion event");
    }
    static bool terminalProof(void* value) noexcept {
        auto& state=*static_cast<Impl*>(value);
        if(state.untrackedCommand)return false;
        if(!state.event)return !state.borrowedTransfers && !state.completionUnknown;
        Int status=1;
        if(state.lane->api.GetEventInfo(state.event,0x11D3,sizeof status,&status,nullptr) || status!=0)return false;
        state.lane->api.ReleaseEvent(state.event);state.event=nullptr;
        state.borrowedTransfers=false;state.completionUnknown=false;
        state.progress=OpenCLProbeProgress::Declined;return true;
    }
    std::uint64_t clockNow() noexcept {
        const auto now=clock.read ? clock.read(clock.owner.get()) : threadCPUClock();
        if(!now || now<lastCpuClock) return 0;
        lastCpuClock=now;return now;
    }
    void invalidClock() noexcept {
        ++measured.invalidCpuClocks;++lane->shared->threadCPUInvalidMeasurements;cancelled=true;
    }
    bool readyEvent() {
        Int state=0;
        const auto result=lane->api.GetEventInfo(event,0x11D3,sizeof state,&state,nullptr);
        if(result || state<0){quarantine(true);return false;}
        if(state) return false;
        if(phase==Phase::KernelWait) lane->profile(event,lane->status.deviceKernelNs);
        if(phase==Phase::CheckWait) lane->profile(event,lane->status.deviceCheckReadNs);
        if(phase==Phase::OutputWait) lane->profile(event,lane->status.deviceReadbackNs);
        if(phase==Phase::CostsWait) lane->profile(event,lane->status.deviceUploadNs);
        lane->api.ReleaseEvent(event);event=nullptr;borrowedTransfers=false;return true;
    }
    bool privateCostPermission() const noexcept {return request.capturedPrivateCosts || lane->shared->status.probeColdCosts;}
    void flush() {check(lane->api.Flush(lane->queue));}
    OpenCLProbeProgress step(std::size_t copyCells,std::uint64_t started,std::uint64_t cpuBudget) {
        if(progress!=OpenCLProbeProgress::Pending) return progress;
        if(generation!=request.session.currentGeneration()) cancelled=true;
        if(lane->shared->failed.load() || request.session.failed.load()) cancelled=true;
        if(event && !readyEvent()) return progress;
        if(cancelled) {progress=OpenCLProbeProgress::Declined;return progress;}
        const auto n=request.grid.cells();
        auto& api=lane->api;
        switch(phase) {
        case Phase::Costs: {
            std::array<std::shared_ptr<Device::Plane>,8> candidates;
            {std::lock_guard lock(lane->shared->cacheMutex);candidates=lane->shared->planes;}
            for(auto& p:candidates) if(p && p->identity.owner==request.identity.owner &&
                p->identity.variant==request.identity.variant && p->identity.revision==request.identity.revision &&
                p->identity.allCells && p->identity.packedUniformCost==request.identity.packedUniformCost &&
                p->width==request.grid.width() && p->height==request.grid.height() && p->published.load() &&
                p->ready.wait_for(std::chrono::seconds(0))==std::future_status::ready) {costs=p;break;}
            if(!costs) {
                const auto packed=request.identity.packedUniformCost;
                if((!lane->shared->status.uniformMetadata && !request.capturedPrivateCosts) || !(packed&65535u) || !(packed>>16)) {
                    if(!privateCostPermission() || !request.identity.retainedBytes || !request.costAt) {
                        progress=OpenCLProbeProgress::Declined;return progress;
                    }
                    phase=Phase::CostsReserve;break;
                }
                // A private proven scalar needs no callback scan or wait for a
                // cache reservation being prepared by a different lane.
                const auto payload=Device::Plane::fixedBytes()+sizeof(UInt)+Device::Plane::Storage::fixedBytes();
                if(request.identity.retainedBytes>OpenCLProbeBudget-objectBytes()-payload) throw BudgetExceeded();
                const auto bytes=payload+request.identity.retainedBytes;
                lane->optionalSubsetLease.resize(objectBytes()-256+bytes);
                costs=Device::Plane::create(api,request,std::span<const std::uint8_t>{},true);
                costs->data[0]=packed;privateScalar=privateCosts=true;
                measured.costPreparedBytes=costs->data.capacity()*sizeof(UInt);
                measured.coldCostPlane=privateCostPermission();
            }
            costCharge=costs->hostLease.bytes+Device::Plane::Storage::fixedBytes();
            lane->optionalSubsetLease.resize(objectBytes()-256+costCharge);phase=Phase::Reserve;break;
        }
        case Phase::CostsReserve: {
            // No pending optional plane is ever inserted into the shared cache.
            // Required work uses its own lane/plane and never waits for this one.
            const auto retained=request.identity.retainedBytes;
            const auto available=OpenCLProbeBudget-objectBytes();
            const auto fixed=Device::Plane::fixedBytes()+Device::Plane::Storage::fixedBytes();
            if(retained>available || fixed>available-retained ||
                n>(available-retained-fixed)/sizeof(UInt))throw BudgetExceeded();
            const auto bytes=fixed+n*sizeof(UInt)+retained;
            lane->optionalSubsetLease.resize(objectBytes()-256+bytes);
            costs=Device::Plane::create(api,request,std::span<const std::uint8_t>{},false,true);
            costCharge=costs->hostLease.bytes+Device::Plane::Storage::fixedBytes();
            lane->optionalSubsetLease.resize(objectBytes()-256+costCharge);
            measured.coldCostPlane=true;privateCosts=true;
            measured.costPreparedBytes=costs->data.capacity()*sizeof(UInt);
            phase=Phase::CostsPack;break;
        }
        case Phase::CostsPack: {
            const auto stop=costs->data.size()+std::min(copyCells,n-costs->data.size());
            while(costs->data.size()<stop) {
                const auto end=costs->data.size()+std::min<std::size_t>(64,stop-costs->data.size());
                while(costs->data.size()<end) {
                    EntrySteps step;
                    try {step=request.costAt(request.context,costs->data.size());}
                    catch(...) {progress=OpenCLProbeProgress::Declined;return progress;}
                    if(!step.cardinal || !step.diagonal || step.cardinal>65535 || step.diagonal>65535) {
                        progress=OpenCLProbeProgress::Declined;return progress;
                    }
                    const UInt packed=step.cardinal|(step.diagonal<<16);
                    if(!costs->data.empty() && costs->data[0]!=packed) costs->uniform=false;
                    costs->data.push_back(packed);++measured.costCellsPrepared;
                }
                const auto now=clockNow();
                if(!now || now<started) {invalidClock();progress=OpenCLProbeProgress::Declined;return progress;}
                if(now-started>=cpuBudget) break;
            }
            if(costs->data.size()==n) phase=Phase::Reserve;
            break;
        }
        case Phase::CostsUpload:
            submitEvent([&](Handle* done){return api.EnqueueWriteBuffer(lane->queue,costs->buffer,0,0,costs->data.size()*sizeof(UInt),
                costs->data.data(),0,nullptr,done);});
            borrowedTransfers=true;++lane->status.costUploads;++measured.costUploads;
            if(privateScalar) ++lane->status.uniformMetadataHits;
            flush();phase=Phase::CostsWait;break;
        case Phase::CostsWait:
            // Completion is private. Do not publish a cache future or transfer
            // ownership; the probe lease covers this immutable plane to death.
            costs->published.store(true);costs->completed.set_value();
            phase=Phase::Upload;break;
        case Phase::Reserve:
            if(n>(OpenCLProbeBudget-objectBytes()-costCharge)/sizeof(std::uint16_t)) throw BudgetExceeded();
            lane->optionalSubsetLease.resize(objectBytes()-256+costCharge+n*sizeof(std::uint16_t));
            lane->valuesLease.resize(n*sizeof(std::uint16_t));
            lane->values.reserve(n); // No full-plane value initialization.
            lane->valuesLease.resize(lane->values.capacity()*sizeof(std::uint16_t));
            lane->optionalSubsetLease.resize(objectBytes()-256+costCharge+lane->valuesLease.bytes);
            phase=Phase::Copy;break;
        case Phase::Copy: {
            const auto stop=copied+std::min(copyCells,n-copied);
            while(copied<stop) {
                const auto end=copied+std::min<std::size_t>(64,stop-copied);
                lane->values.insert(lane->values.end(),request.gradient+copied,request.gradient+end);
                for(auto i=copied;i<end;++i) {
                    const auto value=request.gradient[i];source=source||value>1;
                    onlyZerosAndMax=onlyZerosAndMax&&(value==0||value==65535);
                }
                copied=end;
                const auto now=clockNow();
                if(!now || now<started) {invalidClock();progress=OpenCLProbeProgress::Declined;return progress;}
                if(now-started>=cpuBudget) break;
            }
            if(copied==n) {
                if(!source||onlyZerosAndMax) {++lane->shared->noopFields;progress=OpenCLProbeProgress::Complete;}
                else phase=Phase::Queue;
            }
            break;
        }
        case Phase::Queue:
            lane->probe();lane->selectVariant(unsigned(plan)-1);
            lane->statusSequence=++lane->shared->sequence;phase=Phase::Buffers;break;
        case Phase::Buffers: {
            const auto& variant=lane->variants[lane->selectedVariant];
            pitch=(UInt(request.grid.width())+variant.tileWidth-1)/variant.tileWidth;
            rows=(UInt(request.grid.height())+variant.tileHeight-1)/variant.tileHeight;
            tileCount=std::size_t(pitch)*rows;
            if(n>std::numeric_limits<UInt>::max() || tileCount>std::numeric_limits<UInt>::max()) throw BudgetExceeded();
            lane->deviceLease.resize(n*2*sizeof(std::uint16_t)+288+tileCount*2*sizeof(UInt));
            Int error=0;
            for(auto* buffer:{&lane->first,&lane->second}) {
                *buffer=api.CreateBuffer(lane->context,1,n*sizeof(std::uint16_t),nullptr,&error);check(error);
            }
            lane->changed=api.CreateBuffer(lane->context,1,8*sizeof(UInt),nullptr,&error);check(error);
            lane->descriptors=api.CreateBuffer(lane->context,1,64*sizeof(UInt),nullptr,&error);check(error);
            for(auto* buffer:{&lane->tilesFirst,&lane->tilesSecond}) {
                *buffer=api.CreateBuffer(lane->context,1,tileCount*sizeof(UInt),nullptr,&error);check(error);
            }
            lane->capacity=n;lane->tileCapacity=tileCount;
            if(privateCosts) {
                costs->storage=Device::Plane::Storage::create(api,lane->shared->deviceBudget);
                const auto bytes=costs->data.size()*sizeof(UInt);
                costs->storage->deviceLease.resize(bytes);
                costs->buffer=costs->storage->buffer=api.CreateBuffer(lane->context,1,bytes,nullptr,&error);check(error);
            }
            descriptor={UInt(request.grid.width()),UInt(request.grid.height()),0,0,UInt(request.limit),
                costs->uniform?3u:1u,pitch,rows};
            a=lane->first;b=lane->second;active=lane->tilesFirst;nextActive=lane->tilesSecond;
            ++lane->status.batches;lane->status.maxBatchFields=std::max<std::uint64_t>(lane->status.maxBatchFields,1);
            phase=privateCosts && privateCostPermission() ? Phase::CostsUpload : Phase::Upload;break;
        }
        case Phase::Upload: {
            // All borrowed sources persist until the trailing event completes.
            if(privateScalar && !privateCostPermission()) {
                submitEvent([&](Handle* done){return api.EnqueueWriteBuffer(lane->queue,costs->buffer,0,0,sizeof(UInt),costs->data.data(),0,nullptr,done);});
                borrowedTransfers=true;
                ++lane->status.costUploads;++lane->status.uniformMetadataHits;
            }
            submitEvent([&](Handle* done){return api.EnqueueWriteBuffer(lane->queue,a,0,0,n*sizeof(std::uint16_t),lane->values.data(),0,nullptr,done);});
            borrowedTransfers=true;
            submitEvent([&](Handle* done){return api.EnqueueWriteBuffer(lane->queue,lane->descriptors,0,0,sizeof descriptor,descriptor.data(),0,nullptr,done);});
            const UInt one=1;
            submitEvent([&](Handle* done){return api.EnqueueFillBuffer(lane->queue,active,&one,sizeof one,0,tileCount*sizeof(UInt),0,nullptr,done);});
            ++lane->status.tileMaskInitializations;++measured.tileMaskInitializations;
            if(measured.activeEpoch){
                const UInt zero=0;
                // Fresh A=1/B=0 across every probe, including cached costs and
                // differently shaped required work interleaved on its own lane.
                // The trailing event follows BOTH mask fills and all uploads.
                submitEvent([&](Handle* done){return api.EnqueueFillBuffer(lane->queue,nextActive,&zero,sizeof zero,0,tileCount*sizeof(UInt),0,nullptr,done);});
                ++lane->status.tileMaskInitializations;++measured.tileMaskInitializations;
            }
            flush();phase=Phase::InitialWait;break;
        }
        case Phase::InitialWait:
            if(privateScalar && !privateCostPermission()) {costs->published.store(true);costs->completed.set_value();}
            if(measured.activeEpoch){
                // Same immutable batch bindings as required epoch execution;
                // only pingpong/mask/epoch arguments change per dispatch. This
                // kernel is a lane-private clone of the same initialized program.
                const auto before=lane->status.kernelArgumentUpdates;
                for(unsigned i=0;i<8;++i)lane->argument(2+i,costs->buffer);
                lane->argument(10,lane->changed);lane->argument(11,lane->descriptors);
                lane->argument(14,UInt(tileCount));lane->argument(15,pitch);
                measured.kernelArgumentUpdates+=lane->status.kernelArgumentUpdates-before;
                windowEnd=std::min<UInt>(65536,measured.checkInterval);
            }
            phase=Phase::Dispatch;break;
        case Phase::Dispatch: {
            if(round==65536) throw std::runtime_error("OpenCL probe failed to converge");
            const UInt zero=0;
            if(!measured.activeEpoch || round+1==windowEnd)
                submitEvent([&](Handle* done){return api.EnqueueFillBuffer(lane->queue,lane->changed,&zero,sizeof zero,0,sizeof zero,0,nullptr,done);});
            if(!measured.activeEpoch){
                submitEvent([&](Handle* done){return api.EnqueueFillBuffer(lane->queue,nextActive,&zero,sizeof zero,0,tileCount*sizeof zero,0,nullptr,done);});
                ++lane->status.tileMaskClears;++measured.tileMaskClears;
            }
            const auto before=lane->status.kernelArgumentUpdates;
            lane->argument(0,a);lane->argument(1,b);
            if(!measured.activeEpoch){
                for(unsigned i=0;i<8;++i) lane->argument(2+i,costs->buffer);
                lane->argument(10,lane->changed);lane->argument(11,lane->descriptors);
                lane->argument(14,UInt(tileCount));lane->argument(15,pitch);
            }
            lane->argument(12,active);lane->argument(13,nextActive);
            if(measured.activeEpoch)lane->argument(16,round+1);
            measured.kernelArgumentUpdates+=lane->status.kernelArgumentUpdates-before;
            const auto threads=lane->variants[lane->selectedVariant].threads;
            const std::size_t global[]{std::size_t(pitch)*threads,rows,1},local[]{threads,1,1};
            submitEvent([&](Handle* done){return api.EnqueueNDRangeKernel(lane->queue,lane->kernel,3,nullptr,global,local,0,nullptr,done);});
            std::swap(a,b);std::swap(active,nextActive);++round;
            ++lane->status.dispatches;++measured.dispatches;
            flush();phase=Phase::KernelWait;break;
        }
        case Phase::KernelWait:
            // Yielded event polls add optional lifecycle cost, but kernel and
            // convergence-check counts mirror the required epoch check window.
            if(measured.activeEpoch && round<windowEnd){phase=Phase::Dispatch;break;}
            submitEvent([&](Handle* done){return api.EnqueueReadBuffer(lane->queue,lane->changed,0,0,sizeof flags,&flags,0,nullptr,done);});
            borrowedTransfers=true;
            flush();phase=Phase::CheckWait;break;
        case Phase::CheckWait:
            ++lane->status.hostChecks;++measured.hostChecks;
            if(flags){
                if(measured.activeEpoch)windowEnd=std::min<UInt>(65536,round+measured.checkInterval);
                phase=Phase::Dispatch;
            }
            else {
                submitEvent([&](Handle* done){return api.EnqueueReadBuffer(lane->queue,a,0,0,n*sizeof(std::uint16_t),lane->values.data(),0,nullptr,done);});
                borrowedTransfers=true;
                flush();phase=Phase::OutputWait;
            }
            break;
        case Phase::OutputWait:
            ++lane->status.fields;++lane->status.retiredFields;
            progress=OpenCLProbeProgress::Complete;break;
        }
        return progress;
    }
#else
    Impl(const BackendRequest&,Plan,std::shared_ptr<const void>,OpenCLProbeClock) {progress=OpenCLProbeProgress::Declined;}
#endif
};
OpenCLProbe::OpenCLProbe(std::shared_ptr<Impl> value):state(std::move(value)) {}
OpenCLProbe::~OpenCLProbe() {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    auto parent=state ? state->admission.parent : std::shared_ptr<Device>{};
    if(state && (state->event || state->borrowedTransfers || state->completionUnknown)) {
        state->quarantine();state->lane->shared->optionalLifetime.quarantine(state);
    }
    state.reset();
    if(parent)parent->reapExpiredLanes();
#endif
}
void OpenCLProbe::cancel() noexcept {state->cancelled=true;}
OpenCLProbe::Metrics OpenCLProbe::metrics() const noexcept {
    auto result=state->measured;
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    result.stage=state->phase;
#endif
    return result;
}
std::span<const std::uint16_t> OpenCLProbe::result() const noexcept {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    if(state->progress==OpenCLProbeProgress::Complete) return state->lane->values;
#endif
    return {};
}
OpenCLProbeProgress OpenCLProbe::advance(std::size_t copyCells,std::uint64_t cpuBudgetNs) noexcept {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    if(state->progress!=OpenCLProbeProgress::Pending) return state->progress;
    const auto started=state->clockNow();
    std::unique_lock lock(state->lane->mutex);
    auto& device=*state->lane->shared;
    const auto budget=std::max<std::uint64_t>(1,std::min<std::uint64_t>(cpuBudgetNs,500000));
    if(!started) state->invalidClock();
    try {state->step(std::max<std::size_t>(1,std::min<std::size_t>(copyCells,4096)),started,budget);}
    catch(const BudgetExceeded&) {++state->lane->status.budgetDeclines;state->cancelled=true;}
    catch(const std::bad_alloc&) {++state->lane->status.budgetDeclines;state->cancelled=true;}
    catch(...) {
        state->measured.executionFailed=true;
        if(state->event || state->borrowedTransfers || state->completionUnknown)state->quarantine(true);
        else {device.fail("OpenCL optional execution failed");readyPlans.store(0,std::memory_order_release);}
        state->cancelled=true;
    }
    // Retain ambiguous completion before sampling the endpoint. There is no
    // blocking drain on this coordinator, including failure/cancellation.
    if(state->progress==OpenCLProbeProgress::Quarantined)device.optionalLifetime.quarantine(state);
    else if(state->cancelled && !state->event) {
        if(state->borrowedTransfers) {
            state->quarantine();device.optionalLifetime.quarantine(state);
        }
        else state->progress=OpenCLProbeProgress::Declined;
    }
    lock.unlock();
    const auto ended=state->clockNow();
    if(!ended || (started && ended<started)) state->invalidClock();
    else if(started) {
        const auto elapsed=ended-started;
        device.threadCPUAvailable.store(true);device.threadCPUNs+=elapsed;
        state->measured.threadCpuNs+=elapsed;
        state->measured.maxAdvanceCpuNs=std::max(state->measured.maxAdvanceCpuNs,elapsed);
        if(elapsed>budget) ++state->measured.overshoots;
    }
    if(state->cancelled && state->progress!=OpenCLProbeProgress::Quarantined) {
        // Invalid clocks stop new optional commands. Pending events are reaped
        // on later coordinator advances before captured storage is released.
        if(state->event) return OpenCLProbeProgress::Pending;
        state->progress=OpenCLProbeProgress::Declined;
    }
    return state->progress;
#else
    return OpenCLProbeProgress::Declined;
#endif
}
std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const BackendRequest& request,Plan plan,std::shared_ptr<const void> keepAlive,OpenCLProbeClock clock) {
    return beginOpenCLProbe(OpenCLProbeInput{request.gradient,request.limit,request.grid,request.session,
        request.context,request.costAt,request.identity,request.operation},plan,std::move(keepAlive),std::move(clock));
}
std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const OpenCLProbeInput& request,Plan plan,std::shared_ptr<const void> keepAlive,OpenCLProbeClock clock) {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    auto& device=*runtime().shared;
    if(clock.read && !clock.owner) {++device.threadCPUInvalidMeasurements;return {};}
    const auto started=clock.read ? clock.read(clock.owner.get()) : threadCPUClock();
    if(!started) {++device.threadCPUInvalidMeasurements;return {};}
    OpenCLProbe::Impl* admitted=nullptr;
    struct CPU {
        Device& device;OpenCLProbe::Impl*& admitted;const OpenCLProbeClock& clock;
        std::uint64_t started;bool finished=false,valid=false;
        void finish() noexcept {
            if(finished) return;finished=true;
            const auto ended=clock.read ? clock.read(clock.owner.get()) : threadCPUClock();
            if(!ended || ended<started) {++device.threadCPUInvalidMeasurements;return;}
            valid=true;
            const auto elapsed=ended-started;device.threadCPUAvailable.store(true);device.threadCPUNs+=elapsed;
            if(admitted) {
                admitted->lastCpuClock=ended;
                admitted->measured.setupCpuNs+=elapsed;admitted->measured.threadCpuNs+=elapsed;
                if(elapsed>500000) ++admitted->measured.overshoots;
            }
        }
        ~CPU() {finish();}
    } timer{device,admitted,clock,started};
    // These required-only configurations have no matching yieldable probe yet.
    // It cannot provide live automatic-promotion evidence.
    if(!keepAlive || !request.gradient || !request.identity.owner || !request.identity.allCells ||
        request.grid.width()<=0 || request.grid.height()<=0 || request.operation!=Operation::CompleteField ||
        request.limit<0 || request.session.failed.load() ||
        plan==Plan::CPU || unsigned(plan)>=PLANS.size() || !(readyPlans.load()&(1u<<unsigned(plan))) ||
        device.status.parityBound || device.status.directSeedUpload || device.status.apiCpuMode) return {};
    // A caller-supplied readiness flag/mask is never permission to perform
    // device/compiler initialization inside an optional CPU reservation.
    {std::lock_guard lock(device.failureMutex);
        if(!device.probed || !device.status.available || device.failed.load())return {};}
    try {
        if(!device.optionalLifetime.tryReserve())return {};
        struct Admission {OpenCLBorrowedLifetime& slot;bool transferred=false;
            ~Admission(){if(!transferred)slot.release();}} admission{device.optionalLifetime};
        // Reserve fixed objects/control storage before any shared allocation or
        // constructor. Local guards retain admission if construction throws;
        // successful construction transfers the leases without changing totals.
        BudgetLease objectAdmission{hostBudget},laneAdmission{hostBudget},subsetAdmission{probeBudget};
        objectAdmission.resize(sizeof(OpenCLProbe)+sizeof(OpenCLProbe::Impl)+OpenCLProbe::Impl::controlBytes-256);
        laneAdmission.resize(sizeof(Runtime)+256);
        subsetAdmission.resize(OpenCLProbe::Impl::objectBytes());
        auto state=std::make_shared<OpenCLProbe::Impl>(request,plan,std::move(keepAlive),clock,
            objectAdmission,laneAdmission,subsetAdmission);
        state->admission.owns=true;admission.transferred=true;
        auto value=std::unique_ptr<OpenCLProbe>(new OpenCLProbe(std::move(state)));
        admitted=value->state.get();timer.finish();
        if(!timer.valid) {admitted=nullptr;return {};}
        return value;
    }
    catch(...) {device.reapExpiredLanes();return {};}
#else
    return {};
#endif
}
bool reapOpenCLProbeQuarantine() noexcept {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    try {auto device=runtime().shared;const bool reaped=device->optionalLifetime.tryReap(&OpenCLProbe::Impl::terminalProof);
        device->reapExpiredLanes();return reaped;}catch(...){return false;}
#else
    return false;
#endif
}
bool reapOpenCLRequiredQuarantine() noexcept {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    try {auto device=runtime().shared;const bool reaped=device->requiredLifetime.tryReap(&Runtime::RequiredTransaction::terminalProof);
        device->reapExpiredLanes();return reaped;}catch(...){return false;}
#else
    return false;
#endif
}
bool initializeOpenCL() {
    unsigned empty=0;
    initializationState.compare_exchange_strong(empty,1);
    try {prepare();} catch(...) {readyPlans.store(0,std::memory_order_release);}
    initializationState.store(2,std::memory_order_release);
    return readyPlans.load(std::memory_order_acquire)!=0;
}
bool executeOpenCLDevice(std::span<const BackendRequest> requests,Plan plan) {
    return executeBatch(requests,plan);
}
OpenCLStatus openCLStatus()
{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    try {return runtime().snapshot();}
    catch(...) {OpenCLStatus unavailable;unavailable.hostBytes=hostBudget.current.load();unavailable.peakHostBytes=hostBudget.peak.load();
        unavailable.probeBytes=probeBudget.current.load();unavailable.peakProbeBytes=probeBudget.peak.load();return unavailable;}
#else
    auto status=runtime().status;
    status.hostBytes=hostBudget.current.load();status.peakHostBytes=hostBudget.peak.load();
    status.probeBytes=probeBudget.current.load();status.peakProbeBytes=probeBudget.peak.load();
    return status;
#endif
}
} // namespace gradient_kernel
