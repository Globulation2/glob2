// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientAccelerators.h"
#include "OpenCLGradient.h"
#include <SDL3/SDL_platform_defines.h>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace gradient_kernel
{
namespace
{
std::array<std::atomic<std::uint64_t>, unsigned(Backend::Count)> fallbacks{}, verified{},
	verificationNs{};
struct Register
{
	Register()
	{
		prepareSelectedAccelerator = &initializeGradientAccelerator;
		validateAcceleratedResult = &verifyGradientResult;
		reserveVerificationBytes = &reserveOpenCLHostBytes;
		releaseVerificationBytes = &releaseOpenCLHostBytes;
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
	result.verified = verified[unsigned(backend)].load();
	result.verificationNs = verificationNs[unsigned(backend)].load();
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
void verifyGradientResult(const BackendRequest &request, std::span<const std::uint16_t> original)
{
	if (!request.gradient || !request.costAt || original.size() != request.grid.cells())
		throw std::invalid_argument("Gradient verification requires matching original inputs");
	const auto verificationStart = monotonicNs();
	const auto b =
		normalizedConfiguration(request.capturedConfiguration.value_or(configuration())).backend;
	const auto bytes = original.size_bytes();
	if (!reserveOpenCLHostBytes(bytes))
		throw std::runtime_error("Gradient verification host budget exceeded");
	struct Lease
	{
		std::size_t bytes;
		~Lease() { releaseOpenCLHostBytes(bytes); }
	} lease{bytes};
	std::vector<std::uint16_t> expected(original.begin(), original.end());
	if (!request.cpu && !request.cpuBatch)
		throw std::runtime_error("Missing gradient verification CPU reference");
	CPUSelectionScope scalar(Backend::Scalar);
	if (request.cpu)
		request.cpu(request.context, expected.data());
	else
	{
		// Batch-only callers still own a complete CPU recovery operation. Verify
		// one immutable field through that operation without inventing a callback.
		const std::array<const BackendRequest *, 1> requests{&request};
		const std::array<std::uint16_t *, 1> outputs{expected.data()};
		request.cpuBatch(requests, outputs);
	}
	const auto mismatch = std::mismatch(expected.begin(), expected.end(), request.gradient);
	if (mismatch.first == expected.end())
	{
		verified[unsigned(b)].fetch_add(1);
		verificationNs[unsigned(b)].fetch_add(monotonicNs() - verificationStart);
		return;
	}
	const auto cell = std::size_t(mismatch.first - expected.begin());
	const char *directory = std::getenv("GLOB2_TEST_ARTIFACTS");
	const auto path = std::filesystem::path(directory ? directory : "artifacts") /
					  (std::string("gradient-mismatch-") + backendName(b) + "-" +
					   std::to_string(monotonicNs()) + ".bin");
	std::string evidence;
	try
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary);
		const auto writeLE = [&](std::uint32_t value, unsigned size)
		{
			for (unsigned i = 0; i < size; ++i)
				output.put(char((value >> (8 * i)) & 255));
		};
		for (const auto value :
			 {0x47524144u, unsigned(request.grid.width()), unsigned(request.grid.height()),
			  unsigned(request.limit), unsigned(request.family)})
			writeLE(value, 4);
		for (const auto value : original)
			writeLE(value, 2);
		for (const auto value : expected)
			writeLE(value, 2);
		for (std::size_t i = 0; i < original.size(); ++i)
			writeLE(request.gradient[i], 2);
		for (std::size_t i = 0; i < original.size(); ++i)
		{
			const auto costs = original[i] || request.identity.allCells
								   ? request.costAt(request.context, i)
								   : LAND_STEPS;
			writeLE(costs.cardinal | (costs.diagonal << 16), 4);
		}
		output.flush();
		evidence =
			output ? "; fixture " + path.string() : "; unable to preserve fixture " + path.string();
	}
	catch (const std::exception &error)
	{
		// The proven mismatch remains the primary error even when diagnostics
		// cannot be written (permissions, exhausted storage or invalid directory).
		evidence = "; unable to preserve fixture: " + std::string(error.what());
	}
	std::ostringstream message;
	message << "Gradient backend mismatch at cell " << cell << ": expected " << expected[cell]
			<< ", actual " << request.gradient[cell] << evidence;
	throw std::runtime_error(message.str());
}
} // namespace gradient_kernel
