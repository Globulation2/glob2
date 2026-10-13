// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLGradient.h"
#include "OpenCLRequiredLifetimePool.h"
#include "OpenCLTransferProof.h"
#include "OpenCLLaneRegistry.h"
#include "GradientBackend.h"
#include "ThreadCpuClock.h"
#include "GradientDiagnostics.h"
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
MemoryBudget globalDeviceBudget{OpenCLDeviceBudget};
struct BudgetExceeded : std::runtime_error { BudgetExceeded():std::runtime_error("OpenCL payload budget exceeded") {} };
struct RequiredCanceled : std::exception {const char* what() const noexcept override{return "Required OpenCL execution canceled";}};
} // namespace
namespace {
class BudgetLease {
public:
    MemoryBudget* budget;
    std::size_t bytes=0;
    explicit BudgetLease(MemoryBudget& budget):budget(&budget) {}
    BudgetLease(const BudgetLease&)=delete;
    BudgetLease& operator=(const BudgetLease&)=delete;
    ~BudgetLease(){budget->release(bytes);}
    void transferTo(BudgetLease& destination) noexcept {
        if(destination.bytes || budget!=destination.budget)std::terminate();
        destination.bytes=bytes;bytes=0;
    }
    void splitTo(BudgetLease& destination,std::size_t count) noexcept {
        if(destination.bytes || count>bytes || budget!=destination.budget)std::terminate();
        destination.bytes=count;bytes-=count;
    }
    void resize(std::size_t next) {
        if(next>bytes && !budget->reserve(next-bytes))throw BudgetExceeded();
        if(next<bytes)budget->release(bytes-next);
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
    CL_FUNCTION(Flush, Int, Handle);
    CL_FUNCTION(GetEventInfo, Int, Handle, UInt, std::size_t, void*, std::size_t*);
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
        LOAD(Flush);
        LOAD(GetEventInfo);
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
#define CORE_X 16
#define CORE_Y 16
#define STEPS 8
#define WG 128
#define FROZEN_HALO 1

#define HALO (FROZEN_HALO?1:STEPS)
#define UPDATE_X (FROZEN_HALO?CORE_X:SIDE_X)
#define UPDATE_Y (FROZEN_HALO?CORE_Y:SIDE_Y)
#define FIRST_CELL (FROZEN_HALO?1:0)
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
 if(!active[tile]){
  // Keep ping-pong buffers coherent. Only propagation/local-memory work is skipped.
  for(uint o=lid;o<CORE_X*CORE_Y;o+=WG){uint x=gx*CORE_X+o%CORE_X,y=gy*CORE_Y+o/CORE_X;
  if(x<w&&y<h){uint i=base+y*w+x;b[i]=a[i];}}return;
 }
 __local uint c[LOCAL_PATCH],flags[WG];
 __local ushort v[LOCAL_PATCH];
 __local uint firstSweepChanged;
 for(uint j=lid;j<PATCH;j+=WG){
  int sx=wrap(gx*CORE_X+(int)(j%SIDE_X)-HALO,w),sy=wrap(gy*CORE_Y+(int)(j/SIDE_X)-HALO,h);
  uint localIndex=(j/SIDE_X)*LOCAL_X+j%SIDE_X;
  v[localIndex]=a[base+sy*w+sx];if(!uniform)c[localIndex]=costs[sy*w+sx];
 }
 barrier(CLK_LOCAL_MEM_FENCE);
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
    atomic_or(nextActive+f*stride+ny*pitch+nx,1u);
   }
  }
 }
}
)CL";
// Programs are shared by the device; each lane owns distinct kernel handles.
// The only GPU descriptor is Frozen8/check8; this is not a plan portfolio.
struct KernelVariant : ExecutionPlan
{
    Handle program = nullptr, kernel = nullptr;
};
static_assert(PLANS[1].id==Plan::Frozen8 && PLANS[1].steps==8 &&
    PLANS[1].threads==128 && PLANS[1].tileWidth==16 && PLANS[1].tileHeight==16 &&
    PLANS[1].colored && PLANS[1].frozen);
constexpr auto kernelVariants()
{
    std::array<KernelVariant, 1> variants{};
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
    OpenCLRequiredLifetimePool requiredLifetime;
    bool requiredQueryFailureDiagnostics=false;
    MemoryBudget& deviceBudget=globalDeviceBudget;
    Handle device = nullptr, context = nullptr;
    OpenCLStatus status;
    std::array<KernelVariant, 1> variants = kernelVariants();
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
            std::span<const std::uint8_t> mask,bool scalar) {
            BudgetLease reserved{hostBudget};
            reserved.resize(estimatedBytes(scalar ? 1 : request.grid.cells(),mask.size(),
                retainedCostOwnerBytes(request.identity)));
            return std::make_shared<Plane>(api,request,mask,scalar,reserved);
        }
        template<class Request>
        Plane(API& api,const Request& r,std::span<const std::uint8_t> mask,bool scalar,
            BudgetLease& admission):api(&api),identity(r.identity),width(r.grid.width()),height(r.grid.height()) {
            admission.transferTo(hostLease);
            const auto count=scalar ? std::size_t(1) : r.grid.cells();
            data.resize(count);
            blocked.assign(mask.begin(),mask.end());
            hostLease.resize(estimatedBytes(data.capacity(),blocked.capacity(),retainedCostOwnerBytes(identity)));
        }

    };
    std::array<std::shared_ptr<Plane>, 8> planes;
    std::array<std::uint64_t, 8> used{};
    std::uint64_t age = 0;
    static constexpr std::size_t LaneCapacity=64;
    static_assert(LaneCapacity==OpenCLRequiredLifetimePool::MaxSlots);
    OpenCLLaneRegistry<Runtime,LaneCapacity> lanes;
    OpenCLStatus retired;
    std::uint64_t retiredSequence = 0;
    std::atomic<std::uint64_t> sequence{0}, active{0}, maximumActive{0};
    std::atomic<std::uint64_t> noopFields{0}, threadCPUNs{0},threadCPUInvalidMeasurements{0},laneBudgetDeclines{0};
    std::atomic<std::uint64_t> requiredQuarantines{0},requiredProofFailures{0};
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
            requiredQueryFailureDiagnostics=numericOverride("GLOB2_TEST_OPENCL_REQUIRED_QUERY_FAILURE",0,1)!=0;
            api.load();
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
            // is diagnostic only; it never changes execution or admission.
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
            // Unsupported/error results leave this diagnostic identity empty.
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
                constexpr const char* options="-cl-std=CL1.2";
                const auto built =
                    api.BuildProgram(variant.program, 1, &device, options, nullptr, nullptr);
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
std::weak_ptr<Device> existingDevice;
std::mutex existingDeviceMutex;
std::shared_ptr<Device> makeDevice() {
    BudgetLease reserved{hostBudget};
    reserved.resize(sizeof(Device)+256+Device::LaneCapacity*256+65536);
    auto result=std::make_shared<Device>(reserved);
    {std::lock_guard lock(existingDeviceMutex);existingDevice=result;}
    return result;
}
struct Runtime : std::enable_shared_from_this<Runtime>
{
    std::shared_ptr<Device> shared;
    API& api;
    bool execution;
    // Declared before payload so leases outlive all lane storage, including
    // status strings retained by a concurrent snapshot's shared lane handle.
    BudgetLease objectLease{hostBudget};
    std::uint64_t statusSequence = 0;
    Runtime(std::shared_ptr<Device> state = makeDevice(), bool lane = false)
        : shared(std::move(state)), api(shared->api), execution(lane) {}
    std::mutex mutex;
    OpenCLStatus status;
    bool probed = false;
    Handle device = nullptr, context = nullptr, queue = nullptr;
    std::array<KernelVariant, 1> variants = kernelVariants();
    std::size_t selectedVariant = 0;
    Handle kernel = nullptr;
    Handle first = nullptr, second = nullptr, changed = nullptr, descriptors = nullptr;
    std::size_t capacity = 0, tileCapacity = 0;
    Handle tilesFirst = nullptr, tilesSecond = nullptr;
    // Each lane retains its accounting through concurrent status snapshots.
    BudgetLease valuesLease{hostBudget}, blockedLease{hostBudget};
    BudgetLease deviceLease{shared->deviceBudget};
    std::vector<std::uint16_t> values;
    std::vector<std::uint8_t> blocked;
    struct RequiredTransaction {
        struct Admission {
            std::shared_ptr<Device> parent;
            OpenCLRequiredLifetimePool::Slot* slot=nullptr;
            bool owns=false;
            ~Admission(){if(owns)slot->release();}
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
        RequiredTransaction(std::shared_ptr<Runtime> owner,BudgetLease& reserved,OpenCLRequiredLifetimePool::Slot& slot):lane(std::move(owner)) {
            admission.parent=lane->shared;admission.slot=&slot;reserved.transferTo(objectLease);
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
        out.fields+=in.fields;out.cpuSelections+=in.cpuSelections;
        out.batches+=in.batches;out.maxBatchFields=std::max(out.maxBatchFields,in.maxBatchFields);
        out.costUploads+=in.costUploads;out.costCacheHits+=in.costCacheHits;out.dispatches+=in.dispatches;
        out.hostChecks+=in.hostChecks;out.costIdentityHits+=in.costIdentityHits;out.retiredFields+=in.retiredFields;
        out.schedulerBatches+=in.schedulerBatches;
        out.budgetDeclines+=in.budgetDeclines;out.threadCPUNs+=in.threadCPUNs;
        out.threadCPUAvailable=out.threadCPUAvailable||in.threadCPUAvailable;
        out.tileMaskInitializations+=in.tileMaskInitializations;out.tileMaskClears+=in.tileMaskClears;
        out.kernelArgumentUpdates+=in.kernelArgumentUpdates;
        out.deviceObservedFields+=in.deviceObservedFields;out.committedFields+=in.committedFields;
        out.seedCopiedBytes+=in.seedCopiedBytes;out.seedUploadedBytes+=in.seedUploadedBytes;
        out.outputCopiedBytes+=in.outputCopiedBytes;
        out.requiredCancellations+=in.requiredCancellations;
    }
    static std::shared_ptr<Runtime> registeredLane(const std::shared_ptr<Device>& device,
        BudgetLease& objectAdmission) {
        // Separate payload/control credits survive until the last weak
        // registration expires, without retaining the destroyed Runtime.
        struct ControlCredit {BudgetLease host{hostBudget};};
        auto credit=std::make_shared<ControlCredit>();
        objectAdmission.splitTo(credit->host,256);
        auto result=std::shared_ptr<Runtime>(new Runtime(device,true),[credit](Runtime* value){delete value;});
        objectAdmission.transferTo(result->objectLease);
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
        out.requiredQuarantines=shared->requiredQuarantines.load();
        out.requiredProofFailures=shared->requiredProofFailures.load();
        out.requiredQuarantinePending=shared->requiredLifetime.quarantined();
        out.hostBytes=hostBudget.current.load();out.peakHostBytes=hostBudget.peak.load();
        out.deviceBytes=shared->deviceBudget.current.load();out.peakDeviceBytes=shared->deviceBudget.peak.load();
        out.noopFields=shared->noopFields.load();out.threadCPUNs=shared->threadCPUNs.load();
        out.threadCPUInvalidMeasurements+=shared->threadCPUInvalidMeasurements.load();
        out.threadCPUAvailable=out.threadCPUAvailable || shared->threadCPUAvailable.load();
        out.executionLanes=std::count_if(live.begin(),live.end(),[](const auto& lane){return bool(lane);});out.maxConcurrentBatches=shared->maximumActive.load();
        return out;
    }
    void buffers()
    {
        GradientPhaseScope phase(GradientPhase::Retirement);
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
		Int error = 0;
		queue = api.CreateCommandQueue(context, device, 0, &error);
		check(error);
		for (std::size_t i = 0; i < variants.size(); ++i)
			if (shared->variants[i].kernel)
			{
				variants[i].program = shared->variants[i].program;
				variants[i].kernel = api.CreateKernel(variants[i].program, "propagate", &error);
				check(error);
			}
		selectedVariant = shared->selectedVariant;
		selectVariant(selectedVariant);
		status.available = true;
		probed = true;
	}
	template <class... Args> Int fill(Args... args)
	{
		GradientPhaseScope phase(GradientPhase::Recording);
		const auto result = api.EnqueueFillBuffer(args...);
		if (!result) countGradientWork(GradientCounter::Enqueues);
		if (required)
			required->proof.enqueueUnobserved(result);
		return result;
	}
	template <class... Args> Int enqueueKernel(Args... args)
	{
		GradientPhaseScope phase(GradientPhase::Recording);
		const auto result = api.EnqueueNDRangeKernel(args...);
		if (!result) { countGradientWork(GradientCounter::Enqueues); countGradientWork(GradientCounter::Dispatches); }
		if (required)
			required->proof.enqueueUnobserved(result);
		return result;
	}
	template <class T> void argumentTo(Handle target, UInt index, T value)
	{
		GradientPhaseScope phase(GradientPhase::Recording);
		check(api.SetKernelArg(target, index, sizeof(T), &value));
		++status.kernelArgumentUpdates;
	}
	template <class T> void argument(UInt index, T value) { argumentTo(kernel, index, value); }
	struct Event
	{
		API &api;
		Handle value = nullptr;
		bool owns = true;
		explicit Event(API &api) : api(api) {}
		~Event()
		{
			if (value && owns)
				api.ReleaseEvent(value);
		}
		Event(const Event &) = delete;
	};
	void await(Event &event)
	{
		GradientPhaseScope phase(GradientPhase::Waiting);
		if (!event.value || (!required && !shared->status.pollMicros))
			return;
		{ GradientPhaseScope submission(GradientPhase::Submission);
		check(api.Flush(queue)); countGradientWork(GradientCounter::Submissions); }
		for (;;)
		{
			if (required && required->canceled())
				throw RequiredCanceled();
			Int state = 1;
			if (required && required->diagnosticQueryFailures)
			{
				--required->diagnosticQueryFailures;
				throw std::runtime_error("Injected required event query failure");
			}
			{ GradientPhaseScope convergence(GradientPhase::Convergence);
			check(api.GetEventInfo(event.value, 0x11D3, sizeof state, &state, nullptr)); }
			if(state<0)check(state);
            if(!state) {if(required)required->proof.eventCompleted();return;}
            // Polling remains interruptible by required publication/save/stop.
            const auto delay=std::chrono::microseconds(shared->status.pollMicros ? shared->status.pollMicros : 100);
            if(required && required->controls[0])required->controls[0]->waitFor(delay);
            else std::this_thread::sleep_for(delay);
		}
		// Failure unwinds only into batch's nonblocking proof/quarantine guard.
        // All transfer endpoints and retained events already belong to it.
	}
	void read(Handle buffer,void* output,std::size_t bytes,bool finalOutput) {
        GradientPhaseScope phase(finalOutput ? GradientPhase::Readback : GradientPhase::Convergence);
        if(required && required->canceled())throw RequiredCanceled();
        Event event(api);
        const bool asynchronous=required || shared->status.pollMicros!=0;
        const bool capture=asynchronous;
        const auto result=api.EnqueueReadBuffer(queue,buffer,!asynchronous,0,bytes,output,0,nullptr,capture ? &event.value : nullptr);
        if(!result) { countGradientWork(GradientCounter::Enqueues); countGradientWork(GradientCounter::ReadbackBytes,bytes); }
        requiredEnqueue(result,event.value,!asynchronous);
        if(required && event.value)event.owns=false;
        check(result);
        if(asynchronous && !event.value)throw std::runtime_error("OpenCL omitted transfer event");
        await(event);
    }
    void write(Handle buffer, const void *data, std::size_t bytes)
    {
        GradientPhaseScope phase(GradientPhase::Upload);
        if(required && required->canceled())throw RequiredCanceled();
        Event event(api);
        const bool asynchronous=required || shared->status.pollMicros!=0;
        const bool capture=asynchronous;
        const auto result=api.EnqueueWriteBuffer(queue,buffer,!asynchronous,0,bytes,data,0,nullptr,capture ? &event.value : nullptr);
        if(!result) { countGradientWork(GradientCounter::Enqueues); countGradientWork(GradientCounter::UploadBytes,bytes); }
        requiredEnqueue(result,event.value,!asynchronous);
        if(required && event.value)event.owns=false;
        check(result);
        if(asynchronous && !event.value)throw std::runtime_error("OpenCL omitted transfer event");
        await(event);
    }
    void awaitPlane(const std::shared_ptr<Device::Plane>& p) {
        GradientPhaseScope phase(GradientPhase::Waiting);
        while(p->ready.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready) {
            if(required && required->canceled())throw RequiredCanceled();
            if(shared->failed.load()) throw std::runtime_error("OpenCL cost preparation failed on another lane");
        }
        p->ready.get();
    }
    std::shared_ptr<Device::Plane> costPlane(const BackendRequest& r) {
        GradientPhaseScope phase(GradientPhase::InputConversion);
        const auto packed=r.identity.packedUniformCost;
        const bool knownUniform=r.identity.owner && r.identity.allCells &&
            (packed&65535u) && (packed>>16);
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
            if(knownUniform)p->data[0]=packed;
            else for(std::size_t i=0;i<n;++i) {
                if(!(i%64) && required && required->canceled())throw RequiredCanceled();
                const auto step=!r.identity.allCells&&blocked[i]?LAND_STEPS:r.costAt(r.context,i);
                if(!step.cardinal||!step.diagonal||step.cardinal>65535||step.diagonal>65535)
                    throw std::runtime_error("Invalid OpenCL gradient edge cost");
                p->data[i]=step.cardinal|(step.diagonal<<16);
                if (i && p->data[i] != p->data[0]) p->uniform = false;
            }
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
            Int error=0;
            { GradientPhaseScope allocation(GradientPhase::Allocation);
                p->buffer=p->storage->buffer=api.CreateBuffer(context,1,bytes,nullptr,&error);check(error);
                countGradientWork(GradientCounter::BufferAllocations); }
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
    void computeBatch(std::span<const BackendRequest *const> requests,std::span<std::uint16_t> staging)
    {
        GradientPhaseScope phase(GradientPhase::Bookkeeping);
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
            GradientPhaseScope allocation(GradientPhase::Allocation);
            buffers();
            try {deviceLease.resize(total*2*sizeof(std::uint16_t)+288);}
            catch(const BudgetExceeded&) {shared->evictUnusedPlanes();deviceLease.resize(total*2*sizeof(std::uint16_t)+288);}
            Int error = 0;
            for (auto *buffer : {&first, &second})
            {
                *buffer = api.CreateBuffer(context, 1, total * sizeof(std::uint16_t), nullptr, &error);
                check(error); countGradientWork(GradientCounter::BufferAllocations);
            }
            changed = api.CreateBuffer(context, 1, 8 * sizeof(UInt), nullptr, &error);
            check(error); countGradientWork(GradientCounter::BufferAllocations);
            descriptors = api.CreateBuffer(context, 1, 64 * sizeof(UInt), nullptr, &error);
            check(error); countGradientWork(GradientCounter::BufferAllocations);
            capacity = total;
        }
        // Polling failure may outlive this stack frame. Every borrowed endpoint
        // belongs to the pre-admitted transaction, never to these locals.
        phase.setPhase(GradientPhase::InputConversion);
        auto& desc=required->descriptors;desc.fill(0);
        auto& held=required->costs;held.fill({});
        // Even an errored blocking call has ambiguous acceptance. Never borrow
        // caller seeds: CPU recovery may start before the driver stops reading.
        std::size_t offset=0, field=0;
        for(auto* r:requests) {
            const auto n=r->grid.cells();
            std::copy(r->gradient,r->gradient+n,staging.begin()+offset);
            status.seedCopiedBytes+=n*sizeof(std::uint16_t);
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
        write(first,staging.data(),seedBytes);
        status.seedUploadedBytes+=seedBytes;
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
            GradientPhaseScope allocation(GradientPhase::Allocation);
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
                check(error); countGradientWork(GradientCounter::BufferAllocations);
            }
            tileCapacity = tileCount;
        }
        phase.setPhase(GradientPhase::Recording);
        UInt one = 1;
        check(fill(queue, tilesFirst, &one, sizeof one, 0, tileCount * sizeof(UInt), 0,
                                    nullptr, nullptr));
        ++status.tileMaskInitializations;
        Handle active = tilesFirst, nextActive = tilesSecond;
        const std::size_t global[]{std::size_t(pitch) * variant.threads, std::size_t(rows),
                                   requests.size()},
            local[]{variant.threads, 1, 1};
        std::array<UInt,8> zero{};
        auto& flags=required->flags;flags.fill(0);
        ++status.batches;
        status.maxBatchFields = std::max(status.maxBatchFields, std::uint64_t(requests.size()));
        // Each lane owns one Frozen8 kernel and binds this immutable batch.
        kernel=variants[selectedVariant].kernel;
        for(unsigned i=0;i<8;++i)argument(2+i,held[i]?held[i]->buffer:held[0]->buffer);
        argument(10,changed);argument(11,descriptors);
        argument(14,UInt(stride));argument(15,pitch);
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
            for (unsigned dispatch = 0; dispatch < count; ++dispatch)
            {
                phase.setPhase(GradientPhase::Recording);
                // Only the last dispatch's changes are read by the host. The
                // in-order queue clears earlier accumulated flags before it.
                if (dispatch+1 == count)
                    check(fill(queue, changed, zero.data(), sizeof(UInt), 0,
                                                requests.size() * sizeof(UInt), 0, nullptr, nullptr));
                check(fill(queue,nextActive,zero.data(),sizeof(UInt),0,
                           tileCount*sizeof(UInt),0,nullptr,nullptr));
                ++status.tileMaskClears;
                argument(0,a);argument(1,b);
                argument(12,active);argument(13,nextActive);
                check(
                    enqueueKernel(queue,kernel,3,nullptr,global,local,0,nullptr,
                        nullptr));
                std::swap(a, b);
                std::swap(active, nextActive);
                ++status.dispatches;
            }
            round+=count;
            phase.setPhase(GradientPhase::Convergence);
            read(changed,flags.data(),requests.size()*sizeof(UInt),false);
            if(!deviceObserved) {
                // This successful in-order read proves completion of every
                // preceding kernel for this chunk. Submission alone cannot.
                for(const auto* request:requests)
                    if(request->deviceExecutionObserved)*request->deviceExecutionObserved=true;
                status.deviceObservedFields+=requests.size();deviceObserved=true;
                countGradientWork(GradientCounter::ExecutedFields,requests.size());
            }
            ++status.hostChecks;
            countGradientWork(GradientCounter::ConvergenceChecks);
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
                phase.setPhase(GradientPhase::Readback);
                read(a,staging.data(),total*sizeof(std::uint16_t),true);
                countGradientWork(GradientCounter::ConvergedFields,requests.size());
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
        const bool diagnostics=input.empty() ? gradientProfilingRequested()
            : (input.front().diagnostics ? *input.front().diagnostics : gradientProfilingRequested());
        GradientProfileSession profile(Backend::OpenCL,diagnostics);
        GradientPhaseScope phase(GradientPhase::Bookkeeping);
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
        if(plan!=Plan::Frozen8 || unsigned(plan)>=PLANS.size() ||
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
        profile.count(GradientCounter::AttemptedFields,input.size());
        std::lock_guard lock(mutex);
        struct Active {
            Device& d;
            Active(Device& d):d(d) {const auto n=++d.active;auto old=d.maximumActive.load();while(old<n&&!d.maximumActive.compare_exchange_weak(old,n)){} }
            ~Active(){--d.active;}
        } activeLane(*shared);
        statusSequence=++shared->sequence;
        auto* slot=shared->requiredLifetime.tryReserve();
        if(!slot){++status.budgetDeclines;return false;}
        struct SlotGuard {OpenCLRequiredLifetimePool::Slot* slot;bool transferred=false;
            ~SlotGuard(){if(!transferred)slot->release();}} slotGuard{slot};
        std::shared_ptr<RequiredTransaction> transaction;
        struct CurrentGuard {Runtime& lane;~CurrentGuard(){lane.required=nullptr;}} currentGuard{*this};
        try {
            phase.setPhase(GradientPhase::Allocation);
            BudgetLease reserved{hostBudget};reserved.resize(sizeof(RequiredTransaction)+256);
            transaction=std::make_shared<RequiredTransaction>(shared_from_this(),reserved,*slot);
            transaction->admission.owns=true;slotGuard.transferred=true;required=transaction.get();
            for(std::size_t i=0;i<std::min<std::size_t>(8,input.size());++i)transaction->controls[i]=input[i].requiredControl;
            if(transaction->canceled())throw RequiredCanceled();
            probe(); // Lane buffers/queue only. Compilation must already be complete.
            if(!variants[unsigned(plan)-1].kernel) return false;
            selectVariant(unsigned(plan)-1);
            // Stage ALL output before commit; even a failure in the last chunk
            // leaves every caller's original seeds intact for exact CPU recovery.
            std::size_t total=0;
            for(const auto& request:input) {
                if(request.grid.cells()>std::numeric_limits<std::size_t>::max()-total) throw BudgetExceeded();
                total+=request.grid.cells();
            }
            if(total>OpenCLHostBudget/sizeof(std::uint16_t)) throw BudgetExceeded();
            try {resizeStaging(values,total,valuesLease);}
            catch(const BudgetExceeded&) {shared->evictUnusedPlanes();resizeStaging(values,total,valuesLease);}
            phase.setPhase(GradientPhase::Bookkeeping);
            std::size_t outputOffset=0;
            for(std::size_t begin=0;begin<input.size();begin+=8) {
                const auto count=std::min<std::size_t>(8,input.size()-begin);
                std::array<const BackendRequest*,8> pointers{};transaction->controls.fill({});
                for(std::size_t i=0;i<count;++i){pointers[i]=&input[begin+i];transaction->controls[i]=input[begin+i].requiredControl;}
                if(transaction->canceled())throw RequiredCanceled();
                std::size_t chunkCells=0;for(std::size_t i=0;i<count;++i) chunkCells+=pointers[i]->grid.cells();
                computeBatch(std::span(pointers.data(),count),std::span(values.data()+outputOffset,chunkCells));
                outputOffset+=chunkCells;
                status.fields+=count;
            }
            if(std::any_of(input.begin(),input.end(),[](const auto& r){return r.requiredControl && r.requiredControl->shouldCancel(monotonicNs());}))throw RequiredCanceled();
            if(shared->failed.load() || std::any_of(input.begin(),input.end(),[](const auto& r){return r.session.failed.load();}))
                throw std::runtime_error("OpenCL device or session failed on another lane");
            phase.setPhase(GradientPhase::OutputConversion);
            outputOffset=0;
            for(const auto& r:input) {
                std::copy_n(values.data()+outputOffset,r.grid.cells(),r.gradient);
                status.outputCopiedBytes+=r.grid.cells()*sizeof(std::uint16_t);
                if(r.executedOnDevice) *r.executedOnDevice=true;
                ++status.committedFields;
                countGradientWork(GradientCounter::CommittedFields);
                outputOffset+=r.grid.cells();
            }
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
        GradientPhaseScope phase(GradientPhase::Retirement);
        // A covered positive event is proof. Failure/absence is never permission
        // to block on Finish or release borrowed endpoints during CPU recovery.
        if(RequiredTransaction::terminalProof(transaction.get()))return;
        ++shared->requiredProofFailures;++shared->requiredQuarantines;
        shared->failed.store(true,std::memory_order_release);readyPlans.store(0,std::memory_order_release);
        if(!transaction->admission.slot->quarantine(transaction))std::terminate();
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
    GradientProfileSession profile(Backend::OpenCL,gradientProfilingRequested());
    GradientPhaseScope phase(GradientPhase::Initialization);
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    auto& device=*runtime().shared;
    device.initialize();
    unsigned mask=0;
    if(device.status.available && !device.failed.load())
        for(std::size_t i=0;i<device.variants.size();++i) if(device.variants[i].kernel) mask|=1u<<unsigned(device.variants[i].id);
    readyPlans.fetch_and(~(1u<<unsigned(Plan::Frozen8)),std::memory_order_release);
    readyPlans.fetch_or(mask,std::memory_order_release);
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
bool reserveGradientDeviceBytes(std::size_t bytes) noexcept {return globalDeviceBudget.reserve(bytes);}
void releaseGradientDeviceBytes(std::size_t bytes) noexcept {globalDeviceBudget.release(bytes);}
bool reserveOpenCLHostBytes(std::size_t bytes) noexcept {return hostBudget.reserve(bytes);}
void releaseOpenCLHostBytes(std::size_t bytes) noexcept {hostBudget.release(bytes);}
std::size_t openCLHostBytes() noexcept {return hostBudget.current.load(std::memory_order_relaxed);}
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
std::shared_ptr<const void> retainOpenCLLifetime() {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    return runtime().shared;
#else
    return {};
#endif
}
bool requiredOpenCLQuarantinePending() noexcept {
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    std::shared_ptr<Device> device;
    {std::lock_guard lock(existingDeviceMutex);device=existingDevice.lock();}
    return device && device->requiredLifetime.quarantined();
#else
    return false;
#endif
}
bool reapOpenCLRequiredQuarantine() noexcept {
    GradientProfileSession profile(Backend::OpenCL,gradientProfilingRequested());
    GradientPhaseScope phase(GradientPhase::Retirement);
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
    try {prepare();} catch(...) {readyPlans.fetch_and(~(1u<<unsigned(Plan::Frozen8)),std::memory_order_release);}
    initializationState.store(2,std::memory_order_release);
    return (readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(Plan::Frozen8)))!=0;
}
bool executeOpenCLDevice(std::span<const BackendRequest> requests,Plan plan) {
    return executeBatch(requests,plan);
}
OpenCLStatus openCLStatus()
{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    try {return runtime().snapshot();}
    catch(...) {OpenCLStatus unavailable;unavailable.hostBytes=hostBudget.current.load();unavailable.peakHostBytes=hostBudget.peak.load();
        return unavailable;}
#else
    auto status=runtime().status;
    status.hostBytes=hostBudget.current.load();status.peakHostBytes=hostBudget.peak.load();
    status.deviceBytes=globalDeviceBudget.current.load();status.peakDeviceBytes=globalDeviceBudget.peak.load();
    return status;
#endif
}
} // namespace gradient_kernel
