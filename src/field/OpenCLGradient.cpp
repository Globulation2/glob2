// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLGradient.h"
#include "GradientBackend.h"
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_platform_defines.h>
#include <array>
#include <chrono>
#include <condition_variable>
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
#define SIDE_X (CORE_X+2*STEPS)
#define SIDE_Y (CORE_Y+2*STEPS)
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
 uint gx=get_group_id(0),gy=get_group_id(1),tile=f*stride+gy*pitch+gx;
 uint x=gx*CORE_X+get_local_id(0),y=gy*CORE_Y+get_local_id(1);
 uint lid=get_local_id(1)*CORE_X+get_local_id(0);
 if(gx>=desc[d+6]||gy>=desc[d+7]||!desc[d+5])return; // Entire workgroup/retired field.
 if(!active[tile]){
  // Keep ping-pong buffers coherent. Only propagation/local-memory work is skipped.
  if(x<w&&y<h){uint i=base+y*w+x;b[i]=a[i];}return;
 }
 __local uint c[PATCH],flags[256];
#if COLOR_RELAXATION
 __local ushort v[PATCH];
#else
 __local ushort storage[2][PATCH];
 __local ushort *v=storage[0],*next=storage[1];
#endif
 for(uint j=lid;j<PATCH;j+=256){
  int sx=wrap(gx*CORE_X+(int)(j%SIDE_X)-STEPS,w),sy=wrap(gy*CORE_Y+(int)(j/SIDE_X)-STEPS,h);
  v[j]=a[base+sy*w+sx];c[j]=costs[sy*w+sx];
 }
 barrier(CLK_LOCAL_MEM_FENCE);
#if COLOR_RELAXATION
 // Four parity classes have no adjacent cells within a class, so each
 // in-place sweep is race-free. Every update extends a valid path and is
 // monotone. Repeated tile exchanges reach the same exact fixed point;
 // a sweep need not equal a fixed number of Jacobi rounds.
 for(uint round=0;round<STEPS;round++)for(uint color=0;color<4;color++){
  for(uint q=lid;q<PATCH/4;q+=256){int sx=(q%(SIDE_X/2))*2+(color&1),sy=(q/(SIDE_X/2))*2+(color>>1);uint j=sy*SIDE_X+sx;uint val=v[j];
   if(val)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    if((dx||dy)&&sx+dx>=0&&sx+dx<SIDE_X&&sy+dy>=0&&sy+dy<SIDE_Y){
     uint k=j+dy*SIDE_X+dx,src=v[k],step=(dx&&dy)?c[k]>>16:c[k]&65535;
     if(src>step+1&&65535-src+step<=cap)val=max(val,src-step);
    }
   }
   v[j]=val;
  }
  barrier(CLK_LOCAL_MEM_FENCE);

 }
#else
 // The halo covers the complete dependency cone: core results equal STEPS
 // global Jacobi rounds, including wrapped seams and deferred/capped seeds.
 for(uint round=0;round<STEPS;round++){
  for(uint j=lid;j<PATCH;j+=256){
   int sx=j%SIDE_X,sy=j/SIDE_X;
   // Only this shrinking cone can influence the final core. Its neighbors
   // were all written by the preceding round before the shared barrier.
   if(sx<(int)round+1||sy<(int)round+1||sx>=SIDE_X-(int)round-1||sy>=SIDE_Y-(int)round-1)continue;
   uint val=v[j];
   if(val)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    if((dx||dy)&&sx+dx>=0&&sx+dx<SIDE_X&&sy+dy>=0&&sy+dy<SIDE_Y){
     uint k=j+dy*SIDE_X+dx,src=v[k],step=(dx&&dy)?c[k]>>16:c[k]&65535;
     if(src>step+1&&65535-src+step<=cap)val=max(val,src-step);
    }
   }
   next[j]=val;
  }
  barrier(CLK_LOCAL_MEM_FENCE);
  __local ushort *tmp=v;v=next;next=tmp;
 }
#endif
 uint j=(get_local_id(1)+STEPS)*SIDE_X+get_local_id(0)+STEPS;
 flags[lid]=0;
 if(x<w&&y<h){uint i=base+y*w+x;b[i]=v[j];flags[lid]=(a[i]!=v[j]);}
 barrier(CLK_LOCAL_MEM_FENCE);
 for(uint step=128;step;step>>=1){if(lid<step)flags[lid]|=flags[lid+step];barrier(CLK_LOCAL_MEM_FENCE);}
 if(lid==0&&flags[0]){
  atomic_or(changed+f,1u);
  // A changed tile reactivates its dependency neighbors. On general grids a
  // thin last tile can put the halo two tiles across a seam; game maps use one.
  int rx=(w%CORE_X&&w>CORE_X)?2:1,ry=(h%CORE_Y&&h>CORE_Y)?2:1;
  for(int dy=-ry;dy<=ry;dy++)for(int dx=-rx;dx<=rx;dx++){
   uint nx=wrap((int)gx+dx,desc[d+6]),ny=wrap((int)gy+dy,desc[d+7]);
   atomic_or(nextActive+f*stride+ny*pitch+nx,1u);
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
        UInt x, y, steps;
        bool colored = false;
        Handle program = nullptr, kernel = nullptr;
    };
    std::array<Variant, 6> variants{{{16, 16, 2}, {16, 16, 4}, {16, 16, 8},
                                     {16, 16, 2, true}, {16, 16, 4, true}, {16, 16, 8, true}}};
    std::size_t selectedVariant = 1;
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
                if (variant.x > axes[0] || variant.y > axes[1])
                    continue;
                const char *text = source;
                variant.program = api.CreateProgramWithSource(context, 1, &text, nullptr, &error);
                check(error);
                const auto options = "-cl-std=CL1.2 -DCORE_X=" + std::to_string(variant.x) +
                                     " -DCORE_Y=" + std::to_string(variant.y) +
                                     " -DSTEPS=" + std::to_string(variant.steps) +
                                     " -DCOLOR_RELAXATION=" + std::to_string(variant.colored);
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
                if (group < 256)
                {
                    unsupported = "OpenCL kernel requires a 256-work-item group";
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
        UInt x, y, steps;
        bool colored = false;
        Handle program = nullptr, kernel = nullptr;
    };
    std::array<Variant, 6> variants{{{16, 16, 2}, {16, 16, 4}, {16, 16, 8},
                                     {16, 16, 2, true}, {16, 16, 4, true}, {16, 16, 8, true}}};
    std::size_t selectedVariant = 1;
    Handle kernel = nullptr;
    Handle first = nullptr, second = nullptr, changed = nullptr, descriptors = nullptr;
    std::size_t capacity = 0, tileCapacity = 0;
    Handle tilesFirst = nullptr, tilesSecond = nullptr;
    std::vector<std::uint16_t> values;
    std::vector<UInt> entries;
    std::vector<std::uint8_t> blocked;
    struct Task
    {
        const BackendRequest &request;
        Backend choice;
        Runtime* owner;
        Task* leader = nullptr;
        bool done = false, result = false, assigned = false, cpuRequested = false, cpuDone = true;
        std::uint16_t* cpuOutput = nullptr;
        std::exception_ptr cpuError;
    };
    std::mutex pendingMutex;
    std::condition_variable pendingChanged;
    std::vector<Task *> pending;
    bool gathering = false;
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
                UInt(r->limit),1,UInt((r->grid.width()+variants[selectedVariant].x-1)/variants[selectedVariant].x),
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
        const std::size_t global[]{std::size_t(pitch) * variant.x, std::size_t(rows) * variant.y,
                                   requests.size()},
            local[]{variant.x, variant.y, 1};
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
        for (UInt round = 0; round < 65536 / variant.steps; round += 8)
        {
            if(shared->failed.load()) throw std::runtime_error("OpenCL device failed on another lane");
            for (unsigned dispatch = 0; dispatch < 8; ++dispatch)
            {
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
            if (retired)
                write(descriptors, desc.data(), desc.size() * sizeof(UInt));
            if (std::none_of(flags.begin(), flags.begin() + requests.size(), [](UInt v) { return v != 0; }))
            {
                check(api.EnqueueReadBuffer(queue, a, 1, 0, total * sizeof(std::uint16_t), values.data(), 0, nullptr,
                                            nullptr));
                return;
            }
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
              std::span<std::uint16_t* const> expected)
    {
        ++status.tunings;
        double best = std::numeric_limits<double>::max();
        std::size_t winner = selectedVariant;
        for (std::size_t i = 0; i < variants.size(); ++i)
        {
            if (!variants[i].kernel) continue;
            selectVariant(i);
            computeBatch(requests); // Warm buffers and the resident cost cache.
            std::array<double, 3> samples{};
            for (auto& sample : samples)
            {
                const auto start = Clock::now();
                computeBatch(requests);
                sample = elapsed(start);
                std::size_t offset = 0;
                for (std::size_t f = 0; f < requests.size(); ++f)
                {
                    const auto n = requests[f]->grid.cells();
                    if (!std::equal(expected[f], expected[f] + n, values.begin() + offset))
                        throw std::runtime_error("OpenCL tile variant disagrees with CPU");
                    offset += n;
                }
            }
            std::sort(samples.begin(), samples.end());
            if (samples[1] < best) { best = samples[1]; winner = i; }
        }
        selectVariant(winner);
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
        if (std::all_of(input.begin(), input.end(), [](const auto& r) {
            return std::none_of(r.gradient, r.gradient + r.grid.cells(), [](auto value) { return value > 1; });
        })) {
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
                if (std::none_of(input[i].gradient, input[i].gradient + input[i].grid.cells(),
                                 [](auto value) { return value > 1; })) {
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
                std::unique_lock classLock(representative.session.classMutex(representative.family,count),std::defer_lock);
                const bool large=std::any_of(requests.begin(),requests.end(),[](auto r){return r->grid.cells()>=65536;});
                if((choice==Backend::Automatic&&selection.load()==Backend::Automatic)||
                   (large&&!representative.session.tileSelection(representative.family,count).load())) classLock.lock();
                auto selected = selection.load(std::memory_order_relaxed);
                const bool active = std::any_of(requests.begin(), requests.end(), [](auto r) {
                    return std::any_of(r->gradient, r->gradient + r->grid.cells(), [](auto v) { return v > 1; });
                });
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
                if (choice == Backend::Automatic && (!active || selected == Backend::CPU))
                {
                    if (!workerCalls) { prepareCPU(); executeCPU(); }
                    continue; // CPU worker callers resume independently in their own scratch.
                }
                auto& tileChoice = representative.session.tileSelection(representative.family, count);
                if (const auto stored = tileChoice.load(std::memory_order_relaxed))
                    selectVariant(stored - 1);
                else if (std::any_of(requests.begin(), requests.end(), [](auto r) { return r->grid.cells() >= 65536; }))
                {
                    prepareCPU(); executeCPU();
                    tune(requests, std::span(destinations.data(), count));
                    tileChoice.store(unsigned(selectedVariant + 1), std::memory_order_relaxed);
                }
                else selectVariant(variants[1].kernel ? 1 : selectedVariant); // Small fields skip class tuning.
                if (choice == Backend::Automatic && selected == Backend::Automatic)
                {
                    ++status.calibrations;
                    prepareCPU(); executeCPU();
                    std::array<double, 3> cpuTimes{}, gpuTimes{};
                    // CPU workers operate in place. Resetting private benchmark
                    // seeds is outside their timer; GPU packing remains inside.
                    for (auto& sample : cpuTimes)
                    {
                        if (workerCalls) prepareCPU();
                        const auto start = Clock::now();
                        if (!workerCalls) prepareCPU();
                        executeCPU();
                        sample = elapsed(start) + (workerCalls ? schedulingMs : 0);
                    }
                    computeBatch(requests);
                    std::vector<std::vector<std::uint16_t>> measured(count), committed(count);
                    for (std::size_t i = 0; i < count; ++i) committed[i].resize(pointers[i]->grid.cells());
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
                        sample = elapsed(start) + schedulingMs;
                        // Verify outside the timed region, after output conversion.
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            if (output[indices[i]] != measured[i])
                                throw std::runtime_error("OpenCL workload class disagrees with CPU");
                        }
                    }
                    std::sort(cpuTimes.begin(), cpuTimes.end());
                    std::sort(gpuTimes.begin(), gpuTimes.end());
                    selected = gpuTimes[1] < cpuTimes[1] ? Backend::OpenCL : Backend::CPU;
                    selection.store(selected, std::memory_order_relaxed);
                    status.cpuSelections += selected == Backend::CPU;
                    // CPU reference is already a verified completed result.
                    continue;
                }
                if(classLock.owns_lock()) classLock.unlock();
                computeBatch(requests);
                std::size_t offset = 0;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto size = pointers[i]->grid.cells();
                    output[indices[i]].assign(values.begin() + offset, values.begin() + offset + size);
                    offset += size;
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
    void serviceCPU(Task& task, std::unique_lock<std::mutex>& lock)
    {
        auto* output = task.cpuOutput;
        task.cpuRequested = false;
        lock.unlock();
        std::exception_ptr error;
        try { task.request.cpu(task.request.context, output); }
        catch (...) { error = std::current_exception(); }
        lock.lock();
        task.cpuError = error;
        task.cpuDone = true;
        pendingChanged.notify_all();
    }
    // Waiting callers already own executor slots and independent scratch. Use
    // those very workers for the CPU comparison, without nesting executor.run
    // on the GPU leader (which would silently serialize the CPU batch).
    void parallelCPU(std::span<const BackendRequest* const> requests,
                     std::span<std::uint16_t* const> destinations, Task& leader)
    {
        std::unique_lock lock(pendingMutex);
        for (std::size_t i = 0; i < requests.size(); ++i) {
            auto& task = *static_cast<Task*>(requests[i]->context);
            task.cpuOutput = destinations[i];
            task.cpuError = {};
            task.cpuDone = false;
            task.cpuRequested = &task != &leader;
        }
        pendingChanged.notify_all();
        for (auto r : requests) {
            auto& task = *static_cast<Task*>(r->context);
            if (&task == &leader) serviceCPU(task, lock);
        }
        pendingChanged.wait(lock, [&] {
            return std::all_of(requests.begin(), requests.end(), [](auto r) {
                return static_cast<Task*>(r->context)->cpuDone;
            });
        });
        for (auto r : requests)
            if (auto error = static_cast<Task*>(r->context)->cpuError)
                std::rethrow_exception(error);
    }
    bool run(const BackendRequest& r, Backend choice)
    {
        if(shared->failed.load()) {r.session.failed.store(true);return false;}
        if (r.session.failed.load()) return false;
        if (std::none_of(r.gradient, r.gradient + r.grid.cells(), [](auto value) { return value > 1; }))
            return true;
        // Allocate before claiming any peer. Capacity stays bounded by the
        // native batch limit, and filtering/requeueing cannot allocate later.
        std::vector<BackendRequest> requests, retained;
        try { requests.reserve(8); retained.reserve(8); }
        catch (...) { r.session.failed.store(true); return false; }
        Task own{r, choice, this};
        std::unique_lock pendingLock(pendingMutex);
        try { pending.push_back(&own); }
        catch (...) { r.session.failed.store(true); return false; }
        pendingChanged.notify_all();
        while (!own.done)
        {
            if (own.assigned || gathering) {
                pendingChanged.wait(pendingLock, [&] {
                    return own.done || own.cpuRequested || (!own.assigned && !gathering);
                });
                if (own.cpuRequested) serviceCPU(own, pendingLock);
                continue;
            }
            gathering = true;
            const auto schedulingStart = Clock::now();
            pendingChanged.wait_for(pendingLock, std::chrono::microseconds(150), [&] {
                return std::count_if(pending.begin(), pending.end(), [&](auto task) {
                    return task->choice == choice;
                }) >= 8;
            });
            std::array<Task*, 8> gathered{};
            auto append = [&](Task* task) {
                gathered[requests.size()] = task;
                task->assigned = true;
                task->leader = &own;
                auto request = task->request;
                request.context = task;
                request.costAt = [](void* p, std::size_t cell) {
                    const auto& original = static_cast<Task*>(p)->request;
                    return original.costAt(original.context, cell);
                };
                request.cpu = [](void* p, std::uint16_t* out) {
                    const auto& original = static_cast<Task*>(p)->request;
                    original.cpu(original.context, out);
                };
                request.cpuBatch = [](auto group, auto out) {
                    auto& runtime = *static_cast<Task*>(group.front()->context)->owner;
                    runtime.parallelCPU(group, out, *static_cast<Task*>(group.front()->context)->leader);
                };
                requests.push_back(std::move(request));
            };
            // A leader must belong to its own group. Otherwise a second leader
            // could claim its pending job and await CPU calibration from a worker
            // that is itself blocked on the device mutex.
            const auto ownPosition = std::find(pending.begin(), pending.end(), &own);
            pending.erase(ownPosition);
            append(&own);
            for (auto it = pending.begin(); it != pending.end() && requests.size() < 8; ) {
                if ((*it)->choice != choice) { ++it; continue; }
                auto* task = *it;
                it = pending.erase(it);
                append(task);
            }
            const double schedulingMs = elapsed(schedulingStart);
            if (choice == Backend::Automatic) {
                std::array<bool, 8> grouped{}, cpu{};
                for (std::size_t first = 0; first < requests.size(); ++first) {
                    if (grouped[first]) continue;
                    const auto& representative = requests[first];
                    std::size_t count = 0;
                    for (const auto& request : requests)
                        count += request.family == representative.family &&
                                 &request.session == &representative.session;
                    const bool selectedCPU = representative.session.failed.load() ||
                        representative.session.selection(representative.family, count).load() == Backend::CPU;
                    for (std::size_t i = first; i < requests.size(); ++i)
                        if (requests[i].family == representative.family &&
                            &requests[i].session == &representative.session) {
                            grouped[i] = true;
                            cpu[i] = selectedCPU;
                        }
                }
                std::array<Task*, 8> retainedTasks{};
                for (std::size_t i = 0; i < requests.size(); ++i) {
                    if (cpu[i]) {
                        gathered[i]->result = false;
                        gathered[i]->done = true;
                    } else {
                        retainedTasks[retained.size()] = gathered[i];
                        retained.push_back(std::move(requests[i]));
                    }
                }
                requests = std::move(retained);
                gathered = retainedTasks;
                if (own.done) {
                    // The CPU caller cannot leave a dangling leader pointer.
                    // Hand the remaining jobs back to their original GPU workers.
                    for (std::size_t i = requests.size(); i-- > 0; ) {
                        gathered[i]->assigned = false;
                        gathered[i]->leader = nullptr;
                        pending.insert(pending.begin(), gathered[i]);
                    }
                }
            }
            // Membership selection is independent of the in-order device queue.
            // New CPU groups can gather and resume while a GPU group is busy.
            gathering = false;
            pendingChanged.notify_all();
            if (own.done) continue;
            pendingLock.unlock();
            std::array<std::uint8_t, 8> handled{};
            const bool result = batch(requests, choice,
                std::span(handled.data(), requests.size()), schedulingMs);
            pendingLock.lock();
            for (std::size_t i = 0; i < requests.size(); ++i) {
                gathered[i]->result = result && handled[i];
                gathered[i]->done = true;
            }
            pendingChanged.notify_all();
        }
        return own.result;
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
