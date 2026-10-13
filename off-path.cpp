#include "ComputeExecutor.h"
template void ComputeExecutor::worker<ComputeTelemetryMode::Off>(std::size_t);
template void ComputeExecutor::execute<ComputeTelemetryMode::Off>(const ComputeExecutor::Claim&, std::size_t);
template std::size_t ComputeExecutor::invoke<ComputeTelemetryMode::Off>(std::size_t);
template void ComputeExecutor::executePresentation<ComputeTelemetryMode::Off>(const ComputeExecutor::PresentationTicket&, std::size_t);
template void ComputeExecutor::executeCpuPresentation<ComputeTelemetryMode::Off>(std::unique_lock<std::mutex>&, std::size_t, bool);
