// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientBackend.h"
#include <string>
#include <vector>
namespace gradient_kernel
{
enum class Availability
{
	Uninitialized,
	Initializing,
	Ready,
	Unavailable,
	Failed
};
struct AcceleratorStatus
{
	Backend backend = Backend::CPU;
	Availability availability = Availability::Uninitialized;
	std::string device, error;
	std::string driver, shaderIdentity;
	std::uint64_t fallbacks = 0;
	std::uint64_t fields = 0, dispatches = 0;
};
struct BackendDescriptor
{
	Backend id;
	std::string name;
	bool compiled = false, available = false;
	std::vector<Algorithm> algorithms;
	AcceleratorStatus status;
	// Discovery support and admission readiness are distinct. An uninitialized
	// supported device may be selected to request probing; an in-flight probe
	// cannot be selected again until it publishes Ready or a failure status.
	bool selectable() const
	{
		return compiled && available &&
			   (status.availability == Availability::Ready ||
				status.availability == Availability::Uninitialized);
	}
	bool supports(Operation operation) const
	{
		return operation == Operation::CompleteField ||
			   (operation == Operation::ResumableSearch && !isGPU(id));
	}
};
std::vector<BackendDescriptor> gradientBackends();
AcceleratorStatus acceleratorStatus(Backend);
bool initializeGradientAccelerator(Backend);
bool executeGradientAccelerator(std::span<const BackendRequest>, Plan);
void maintainGradientAccelerators() noexcept;
void shutdownGradientAccelerators() noexcept;
bool initializeVulkanGradient();
bool executeVulkanGradient(std::span<const BackendRequest>, Plan);
AcceleratorStatus vulkanGradientStatus();
void maintainVulkanGradient() noexcept;
void shutdownVulkanGradient() noexcept;
bool initializeWebGPUGradient();
bool executeWebGPUGradient(std::span<const BackendRequest>, Plan);
AcceleratorStatus webGPUGradientStatus();
void shutdownWebGPUGradient() noexcept;
void maintainWebGPUGradient() noexcept;
bool reserveGradientDeviceBytes(std::size_t) noexcept;
void releaseGradientDeviceBytes(std::size_t) noexcept;
void recordGradientFallback(Backend) noexcept;
} // namespace gradient_kernel
