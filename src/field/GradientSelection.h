// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#if (defined(__linux__) || defined(__ANDROID__)) && defined(__arm__) && defined(__ARM_NEON)
#include <sys/auxv.h>
#endif

namespace gradient_kernel
{
// Stable local preference IDs. They are never serialized in simulation state.
enum class Backend
{
	CPU,
	OpenCL,
	Scalar,
	SSE2,
	NEON,
	Vulkan,
	WebGPU,
	Count
};
enum class Algorithm
{
	Buckets,
	Frozen8,
	Jacobi
};
inline constexpr std::array<std::string_view, unsigned(Backend::Count)> backendNames{
	"cpu", "opencl", "scalar", "sse2", "neon", "vulkan", "webgpu"};
inline std::optional<Backend> parseBackend(std::string_view name)
{
	for (unsigned i = 0; i < backendNames.size(); ++i)
		if (name == backendNames[i])
			return Backend(i);
	return {};
}
inline const char *backendName(Backend value)
{
	return backendNames.at(unsigned(value)).data();
}
inline std::optional<Algorithm> parseAlgorithm(std::string_view name)
{
	if (name == "buckets")
		return Algorithm::Buckets;
	if (name == "frozen8")
		return Algorithm::Frozen8;
	if (name == "jacobi")
		return Algorithm::Jacobi;
	return {};
}
// Match command-line spelling and conventional environment booleans. Invalid
// environment values disable verification rather than changing simulation work.
inline std::optional<bool> parseVerification(std::string_view value)
{
	if (value == "1" || value == "on" || value == "true")
		return true;
	if (value == "0" || value == "off" || value == "false")
		return false;
	return {};
}
inline const char *algorithmName(Algorithm value)
{
	switch (value)
	{
	case Algorithm::Buckets:
		return "buckets";
	case Algorithm::Frozen8:
		return "frozen8";
	case Algorithm::Jacobi:
		return "jacobi";
	}
	return "invalid";
}
inline constexpr bool isGPU(Backend value)
{
	return value == Backend::OpenCL || value == Backend::Vulkan || value == Backend::WebGPU;
}
inline constexpr bool validBackend(Backend value)
{
	return unsigned(value) < unsigned(Backend::Count);
}
inline bool supportsAlgorithm(Backend b, Algorithm a)
{
	if (!validBackend(b))
		return false;
	return isGPU(b) ? (a == Algorithm::Frozen8 || (b != Backend::OpenCL && a == Algorithm::Jacobi))
					: a == Algorithm::Buckets;
}
struct Configuration
{
	Backend backend = Backend::CPU;
	Algorithm algorithm = Algorithm::Buckets;
	bool verify = false;
	friend bool operator==(const Configuration &, const Configuration &) = default;
};
inline unsigned encodeConfiguration(Configuration c)
{
	return unsigned(c.backend) | (unsigned(c.algorithm) << 8) | (unsigned(c.verify) << 16);
}
inline Configuration decodeConfiguration(unsigned c)
{
	return {Backend(c & 255), Algorithm((c >> 8) & 255), bool(c & (1u << 16))};
}
inline Configuration initialEnvironmentConfiguration()
{
	Configuration c;
	if (const auto *name = std::getenv("GLOB2_GRADIENT_BACKEND"))
		c.backend = parseBackend(name).value_or(Backend::CPU);
	c.algorithm = isGPU(c.backend) ? Algorithm::Frozen8 : Algorithm::Buckets;
	if (const auto *name = std::getenv("GLOB2_GRADIENT_ALGORITHM"))
		c.algorithm = parseAlgorithm(name).value_or(c.algorithm);
	if (!supportsAlgorithm(c.backend, c.algorithm))
		c.algorithm = isGPU(c.backend) ? Algorithm::Frozen8 : Algorithm::Buckets;
	if (const auto *verify = std::getenv("GLOB2_GRADIENT_VERIFY"))
		c.verify = parseVerification(verify).value_or(false);
	return c;
}
inline std::atomic<unsigned> requestedConfiguration{
	encodeConfiguration(initialEnvironmentConfiguration())},
	appliedConfiguration{encodeConfiguration(initialEnvironmentConfiguration())};
inline Configuration configuration()
{
	return decodeConfiguration(appliedConfiguration.load(std::memory_order_acquire));
}
inline Configuration requestedBackendConfiguration()
{
	return decodeConfiguration(requestedConfiguration.load(std::memory_order_acquire));
}
// Normalize local preferences at the boundary, before array-indexed dispatch.
inline Configuration normalizedConfiguration(Configuration c)
{
	if (!validBackend(c.backend))
		c.backend = Backend::CPU;
	if (!supportsAlgorithm(c.backend, c.algorithm))
		c.algorithm = isGPU(c.backend) ? Algorithm::Frozen8 : Algorithm::Buckets;
	return c;
}
inline void requestBackendConfiguration(Configuration c)
{
	c = normalizedConfiguration(c);
	requestedConfiguration.store(encodeConfiguration(c), std::memory_order_release);
}
inline void applyBackendConfiguration(Configuration c)
{
	appliedConfiguration.store(encodeConfiguration(normalizedConfiguration(c)),
							   std::memory_order_release);
}
// Compatibility for standalone harnesses. UI only requests; Map applies at a barrier.
inline Backend backend()
{
	return configuration().backend;
}
inline void setBackend(Backend b)
{
	auto c = configuration();
	c.backend = b;
	if (!supportsAlgorithm(b, c.algorithm))
		c.algorithm = isGPU(b) ? Algorithm::Frozen8 : Algorithm::Buckets;
	requestBackendConfiguration(c);
	applyBackendConfiguration(c);
}
inline bool cpuVariantAvailable(Backend b)
{
	if (b == Backend::CPU || b == Backend::Scalar)
		return true;
#if defined(__SSE2__) && !defined(GLOB2_GRADIENT_SCALAR)
	if (b == Backend::SSE2)
	{
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
		return __builtin_cpu_supports("sse2");
#else
		return true;
#endif
	}
#endif
#if defined(__ARM_NEON) && !defined(GLOB2_GRADIENT_SCALAR)
	if (b == Backend::NEON)
	{
#if (defined(__linux__) || defined(__ANDROID__)) && defined(__arm__)
		return (getauxval(AT_HWCAP) & (1ul << 12)) != 0;
#else
		return true;
#endif
	}
#endif
	return false;
}
inline Backend automaticCPU()
{
	if (cpuVariantAvailable(Backend::SSE2))
		return Backend::SSE2;
	if (cpuVariantAvailable(Backend::NEON))
		return Backend::NEON;
	return Backend::Scalar;
}
inline thread_local std::optional<Backend> cpuOverride;
inline Backend cpuBackend()
{
	const auto b = cpuOverride.value_or(backend());
	return (b == Backend::Scalar || b == Backend::SSE2 || b == Backend::NEON) &&
				   cpuVariantAvailable(b)
			   ? b
			   : automaticCPU();
}
struct CPUSelectionScope
{
	std::optional<Backend> previous = cpuOverride;
	explicit CPUSelectionScope(Backend b) { cpuOverride = b; }
	// A scope has exactly one restoration owner, even across nested dispatch.
	CPUSelectionScope(const CPUSelectionScope &) = delete;
	CPUSelectionScope &operator=(const CPUSelectionScope &) = delete;
	CPUSelectionScope(CPUSelectionScope &&) = delete;
	CPUSelectionScope &operator=(CPUSelectionScope &&) = delete;
	~CPUSelectionScope() { cpuOverride = previous; }
};
} // namespace gradient_kernel
