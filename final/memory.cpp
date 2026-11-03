#include <ComputeExecutor.h>
#include <iostream>
int main() {
 using namespace PerformanceTelemetry;
 const auto banks=5*sizeof(ComputeExecutor::PerformanceShard::Metrics);
 const auto executors=2*sizeof(ComputeExecutor)+banks;
 const auto collectors=4*sizeof(Collector);
 constexpr std::size_t presentation=384*1024;
 std::cout<<"{\"history_bytes\":"<<sizeof(History)<<",\"history_limit_bytes\":"<<History::ByteLimit<<",\"history_capacity\":"<<History::Capacity<<",\"collectors_bytes\":"<<collectors<<",\"two_executors_and_five_banks_bytes\":"<<executors<<",\"presentation_gpu_reserve_bytes\":"<<presentation<<",\"nonhistory_bytes\":"<<collectors+executors+presentation<<",\"total_bytes\":"<<sizeof(History)+collectors+executors+presentation<<",\"limit_bytes\":16777216}\n";
}
