#include "map/gradient/GradientPipeline.h"
#include "map/CellRules.h"
#include "sim/snapshot/SnapshotStorage.h"
#include "resource/ResourceRegistry.h"
#include "common/ThreadCpuClock.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <cstdlib>
#include <thread>
class GlobalContainer;
GlobalContainer* globalContainer=nullptr;
volatile unsigned long long auditSink=0;
template<class F> void phase(const char* label, int n,F f){
 auto begin=glob2::threadCpuNs();for(int i=0;i<n;++i) f(i);
 auto elapsed=glob2::threadCpuDeltaNs(begin,glob2::threadCpuNs());
 std::printf("%s %d %llu\n",label,n,(unsigned long long)elapsed);
}
int main(int argc,char**argv){
 if(argc>2) {std::thread([]{}).join();}
 const int n=argc>1?std::atoi(argv[1]):100000;
 auto terrains=TerrainRegistry::builtins();auto resources=ResourceRegistry::builtins();
 phase("empty",n,[&](int i){auditSink=i;});
 phase("rule_compile_builtin",std::max(1,n/1000),[&](int){CellRuleTable t(terrains,resources);auditSink=t.size();});
 phase("rule_compile_mixed",std::max(1,n/1000),[&](int){CellRuleTable t(terrains,resources);
  for(unsigned a=0;a<terrains->size();++a)for(unsigned b=a;b<terrains->size();++b)t.intern(TerrainType(a),TerrainType(a),TerrainType(b),TerrainType(b));auditSink=t.size();});
 CellRuleTable original(terrains,resources);
 phase("rule_clone",std::max(1,n/1000),[&](int){auto clone=original; auditSink=clone.size();});
 SimulationSnapshot::Handle h;
 h.terrain=std::make_shared<SimulationSnapshot::Terrain>();
 h.resources=std::make_shared<SimulationSnapshot::Resources>();
 h.occupancy=std::make_shared<SimulationSnapshot::Occupancy>();
 h.areas=std::make_shared<SimulationSnapshot::Areas>();
 const auto requirements=SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain)|SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)|SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy)|SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
 h.requirements=requirements;
 phase("snapshot_project_release",n,[&](int){auto p=h.project(requirements);auditSink=p.terrain.use_count();});
 phase("job_allocate_free",n,[&](int){auto j=std::make_unique<GradientPipeline::Job>();auditSink=reinterpret_cast<std::uintptr_t>(j.get());});
 phase("output_allocate_free_128",n,[&](int){auto p=std::unique_ptr<std::uint16_t[]>(new std::uint16_t[128*128]);auditSink=reinterpret_cast<std::uintptr_t>(p.get());});
 phase("output_allocate_free_512",std::max(1,n/16),[&](int){auto p=std::unique_ptr<std::uint16_t[]>(new std::uint16_t[512*512]);auditSink=reinterpret_cast<std::uintptr_t>(p.get());});
 std::shared_ptr<gradient_kernel::OpenCLRequiredControl> control;
 phase("control_atomic_clear_null",n,[&](int){std::atomic_store(&control,decltype(control){});});
 phase("control_owner_clear_null",n,[&](int){control.reset();std::atomic_signal_fence(std::memory_order_seq_cst);});
 phase("backend_read",n,[&](int){auditSink=unsigned(gradient_kernel::backend());});
 #ifndef AUDIT_BEFORE_BACKENDS
 phase("cpu_selection",n,[&](int){auditSink=unsigned(gradient_kernel::cpuBackend());});
 {gradient_kernel::CPUSelectionScope scope(gradient_kernel::Backend::CPU);
 phase("scoped_cpu_selection",n,[&](int){auditSink=unsigned(gradient_kernel::cpuBackend());});}
 #endif
 gradient_kernel::BackendSession session;
 session.setExternalInitialization(true);
 phase("backend_pending_disabled",n,[&](int){auditSink=session.pending();});
 phase("plan_choose_cpu",n,[&](int){
#ifdef AUDIT_BEFORE_BACKENDS
 auto choice=session.choose(gradient_kernel::Family::Generic,1,gradient_kernel::backend());
 auditSink=choice.generation ^ unsigned(choice.plan);
#else
 auto choice=session.choose(gradient_kernel::Family::Generic,1,gradient_kernel::configuration());
 auditSink=choice.generation ^ unsigned(choice.plan) ^ gradient_kernel::encodeConfiguration(choice.configuration);
#endif
 });
 phase("host_reserve_release",n,[&](int){if(gradient_kernel::reserveOpenCLHostBytes(64))gradient_kernel::releaseOpenCLHostBytes(64);});
 SimulationSnapshot::Storage storage;
 auto buffer=storage.terrain.acquire(storage.allocations);
 buffer->rules=std::make_shared<CellRuleTable>(terrains,resources);
 buffer->cellRules.resize(512*512);
 phase("snapshot_pool_reuse",n,[&](int){auto owner=storage.terrain.acquire(storage.allocations);auditSink=owner.use_count();});
 phase("snapshot_memory_accounting",std::max(1,n/10),[&](int){auditSink=storage.memoryMetrics().retainedBytes;});
 std::printf("job_bytes %zu handle_bytes %zu session_bytes %zu rule_bytes %zu\n",sizeof(GradientPipeline::Job),sizeof(h),sizeof(gradient_kernel::BackendSession),sizeof(CellRuleTable));
}
