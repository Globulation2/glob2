from pathlib import Path
P=Path('artifacts/october-regression/coordination-redesign/full-design-comparison');H=P/'weighted-source/src/common/ComputeExecutor.h';t=H.read_text();t=t.replace('    enum class CostPolicy {','    friend struct ComputeWeightedTestAccess; // Artifact-only focused accounting contract tests.\n    enum class CostPolicy {',1);H.write_text(t)
p=P/'weighted-source/src/common/ComputeExecutorHarness.cpp';t=p.read_text();t+='''
// Experimental weighted accounting tests use no diagnostic counters or Trace.
struct ComputeWeightedTestAccess {
    static std::uint64_t outstanding(ComputeExecutor& e) {
        std::uint64_t result=0;
        for(auto& q:e.queues) result+=q->outstandingNs.load(std::memory_order_relaxed);
        return result;
    }
    static bool cold(const ComputeExecutor& e) {
        for(auto& sample:e.costSamples) if(sample.load(std::memory_order_relaxed)) return false;
        for(unsigned i=0;i<16;++i) if(e.estimateCost(i)!=e.fixedNs[i%8]) return false;
        return true;
    }
    static bool learned(const ComputeExecutor& e) {
        return e.costSamples[unsigned(ComputeExecutor::JobClass::AI)].load(std::memory_order_relaxed)>0;
    }
    static bool transferAndDebit(ComputeExecutor& e) {
        if(e.queues.size()<2 || outstanding(e)) return false;
        ComputeExecutor::Dispatch d;d.charged=0;d.reservedNs=12345;
        e.queues[0]->outstandingNs.fetch_add(d.reservedNs,std::memory_order_relaxed);
        e.transferReservation(d,1);
        const bool moved=d.charged==1 && e.queues[0]->outstandingNs.load()==0 && e.queues[1]->outstandingNs.load()==12345;
        e.releaseReservation(d);
        return moved && outstanding(e)==0;
    }
};
TEST_CASE("weighted cost classes cold reset and reservation transfer debit" * doctest::test_suite("ComputeExecutor")) {
    const auto* policy=std::getenv("GLOB2_WEIGHTED_POLICY");
    if(!policy || !*policy) return;
    std::array<unsigned,64> visits{}; // Context lifetime precedes executor.
    ComputeExecutor e;e.configureTelemetry(ComputeTelemetryMode::Off);e.configure(4);
    if(e.threadCount()!=4) { WARN("Weighted native accounting test requires actual4participants");return; }
    CHECK(ComputeWeightedTestAccess::cold(e));
    CHECK(ComputeWeightedTestAccess::transferAndDebit(e));
    const ComputeExecutor::Group g{visits.size(),{[](void* input,std::size_t index){
        auto& values=*static_cast<std::array<unsigned,64>*>(input);++values[index];
    },&visits},ComputeExecutor::NoLane,ComputeExecutor::JobClass::AI};
    auto batch=e.submit(std::span(&g,1));e.join(batch);
    for(auto value:visits) CHECK(value==1);
    CHECK(ComputeWeightedTestAccess::outstanding(e)==0);
    const bool learns=!std::strcmp(policy,"last") || !std::strcmp(policy,"ewma") || !std::strcmp(policy,"observe-only");
    CHECK(ComputeWeightedTestAccess::learned(e)==learns);
    e.configure(4); // Lifecycle resets model; no history from previous run.
    CHECK(ComputeWeightedTestAccess::cold(e));
    CHECK(ComputeWeightedTestAccess::outstanding(e)==0);
}
TEST_CASE("weighted reservations include active callbacks until their CPU slice returns" * doctest::test_suite("ComputeExecutor")) {
    const auto* policy=std::getenv("GLOB2_WEIGHTED_POLICY");
    if(!policy || !*policy) return;
    struct State { std::atomic<bool> entered{false},open{false}; } state;
    ComputeExecutor e;e.configureTelemetry(ComputeTelemetryMode::Off);e.configure(3);
    if(e.threadCount()!=3) { WARN("Weighted active reservation test requires actual3participants");return; }
    const ComputeExecutor::Group g{1,{[](void* input,std::size_t){
        auto& s=*static_cast<State*>(input);s.entered=true;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!s.open.load() && std::chrono::steady_clock::now()<end) std::this_thread::yield();
    },&state},ComputeExecutor::NoLane,ComputeExecutor::JobClass::Hiring};
    auto batch=e.submit(std::span(&g,1));
    struct Release { State& s;ComputeExecutor& e;ComputeExecutor::Batch& b;~Release(){s.open=true;try{e.join(b);}catch(...){}} } release{state,e,batch};
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!state.entered.load() && std::chrono::steady_clock::now()<end) std::this_thread::yield();
    CHECK(state.entered.load());
    const bool accounts=std::strcmp(policy,"bare-steal") && std::strcmp(policy,"clock-only");
    CHECK((ComputeWeightedTestAccess::outstanding(e)>0)==accounts);
    state.open=true;e.join(batch);
    CHECK(ComputeWeightedTestAccess::outstanding(e)==0);
}
''';p.write_text(t)
