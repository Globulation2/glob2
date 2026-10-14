// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientAccelerators.h"
#include "OpenCLGradient.h"
#include <SDL3/SDL_platform_defines.h>
namespace gradient_kernel
{
namespace
{
std::array<std::atomic<std::uint64_t>, unsigned(Backend::Count)> fallbacks{};
struct Register
{
	Register()
	{
		acceleratorImplementations[unsigned(Backend::OpenCL)] = {&initializeOpenCL,
																 &executeGradientAccelerator};
		acceleratorImplementations[unsigned(Backend::Vulkan)] = {&initializeVulkanGradient,
																 &executeGradientAccelerator};
		acceleratorImplementations[unsigned(Backend::WebGPU)] = {&initializeWebGPUGradient,
																 &executeGradientAccelerator};
	}
} registration;
} // namespace
AcceleratorStatus acceleratorStatus(Backend backend)
{
	if (!validBackend(backend))
		throw std::invalid_argument("Invalid gradient backend status ID");
	AcceleratorStatus result{backend};
	if (backend == Backend::Vulkan)
		result = vulkanGradientStatus();
	if (backend == Backend::WebGPU)
		result = webGPUGradientStatus();
	if (backend == Backend::OpenCL)
	{
		const auto s = openCLStatus();
		result.device = s.device;
		result.error = s.error;
		result.fields = s.committedFields;
		result.dispatches = s.dispatches;
		result.driver = s.driverVersion;
		result.availability = s.available       ? Availability::Ready
							  : s.error.empty() ? Availability::Uninitialized
												: Availability::Unavailable;
		if (!s.available && s.error.empty() &&
			initializationState.load(std::memory_order_acquire) == 1)
			result.availability = Availability::Initializing;
	}
	else if (!isGPU(backend))
		result.availability =
			cpuVariantAvailable(backend) ? Availability::Ready : Availability::Unavailable;
	result.fallbacks = fallbacks[unsigned(backend)].load();
	return result;
}
std::vector<BackendDescriptor> gradientBackends()
{
	std::vector<BackendDescriptor> result;
	for (const auto b : {Backend::CPU, Backend::Scalar, Backend::SSE2, Backend::NEON,
						 Backend::OpenCL, Backend::Vulkan, Backend::WebGPU})
	{
		bool compiled = cpuVariantAvailable(b);
#if !defined(__EMSCRIPTEN__)
		if (b == Backend::Vulkan)
			compiled = true;
#if !defined(__ANDROID__) && !defined(SDL_PLATFORM_IOS)
		if (b == Backend::OpenCL)
			compiled = true;
#endif
#elif defined(__EMSCRIPTEN_PTHREADS__)
		if (b == Backend::WebGPU)
			compiled = true;
#endif
		auto status = acceleratorStatus(b);
		const bool available = compiled && status.availability != Availability::Unavailable &&
							   status.availability != Availability::Failed;
		std::vector<Algorithm> algorithms{isGPU(b) ? Algorithm::Frozen8 : Algorithm::Buckets};
		if (b == Backend::Vulkan || b == Backend::WebGPU)
			algorithms.push_back(Algorithm::Jacobi);
		result.push_back(
			{b, backendName(b), compiled, available, std::move(algorithms), std::move(status)});
	}
	return result;
}
bool initializeGradientAccelerator(Backend b)
{
	if (b == Backend::OpenCL)
		return initializeOpenCL();
	if (b == Backend::Vulkan)
		return initializeVulkanGradient();
	if (b == Backend::WebGPU)
		return initializeWebGPUGradient();
	return false;
}
bool executeGradientAccelerator(std::span<const BackendRequest> requests, Plan plan)
{
	if (requests.size() > BATCH_CATEGORIES || unsigned(plan) >= PLANS.size())
		return false;
	for (const auto &request : requests)
		if (!request.gradient || !request.costAt || request.limit < 0 ||
			request.operation != Operation::CompleteField || !validFamily(request.family))
			return false;
	if (std::all_of(requests.begin(), requests.end(), [](const auto &r)
					{ return alreadyFixedGradient(std::span(r.gradient, r.grid.cells())); }))
		return true;
	switch (PLANS.at(unsigned(plan)).backend)
	{
	case Backend::OpenCL:
		return executeOpenCLDevice(requests, plan);
	case Backend::Vulkan:
		return executeVulkanGradient(requests, plan);
	case Backend::WebGPU:
		return executeWebGPUGradient(requests, plan);
	default:
		return false;
	}
}
void maintainGradientAccelerators() noexcept
{
	maintainVulkanGradient();
	maintainWebGPUGradient();
}
void shutdownGradientAccelerators() noexcept
{
	shutdownVulkanGradient();
	shutdownWebGPUGradient();
}
void recordGradientFallback(Backend b) noexcept
{
	// Invalid external jobs recover through CPU automatic; never index a
	// diagnostic array with an unchecked preference identifier.
	if (!validBackend(b))
		b = Backend::CPU;
	fallbacks[unsigned(b)].fetch_add(1, std::memory_order_relaxed);
}

} // namespace gradient_kernel
