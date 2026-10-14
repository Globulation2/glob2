// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientSelection.h"
#include "common/ThreadCpuClock.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ostream>

namespace gradient_kernel
{
// Diagnostic scopes partition host work. Device execution is deliberately not
// inferred from wall time, and unavailable CPU clocks never become wall clocks.
enum class GradientPhase : unsigned
{
	Initialization,
	SnapshotCapture, // Reserved schema slot; generic snapshot capture is not instrumented.
	Preparation,
	InputConversion,
	Allocation,
	Upload,
	Recording,
	Submission,
	Convergence,
	Waiting,
	Readback,
	Validation,
	OutputConversion,
	CpuSolving,
	Recovery,
	Retirement,
	Shutdown,
	ShadowVerification,
	Bookkeeping,
	Count
};
inline constexpr std::array<const char *, unsigned(GradientPhase::Count)> gradientPhaseNames{
	"initialization",    "shared_snapshot_capture",
	"cpu_preparation",   "input_conversion",
	"allocation",        "upload",
	"command_recording", "submission",
	"convergence_check", "waiting",
	"readback",          "validation",
	"output_conversion", "cpu_solving",
	"recovery",          "retirement",
	"shutdown",          "shadow_verification",
	"bookkeeping"};
enum class GradientCounter : unsigned
{
	BufferAllocations,
	CommandAllocations,
	BindingAllocations,
	Submissions,
	Enqueues,
	Dispatches,
	ConvergenceChecks,
	MappedControlBytesRead,
	UploadBytes,
	ReadbackBytes,
	AttemptedFields,
	// A completed dispatch, a converged readback, an independent fixed-point
	// proof and a committed transaction are four different milestones.
	ExecutedFields,
	ConvergedFields,
	ValidatedFields,
	CommittedFields,
	RecoveredFields,
	ResourceReuses,
	GpuValidationDispatches,
	GpuValidatedFields,
	ReferenceUploadBytes,
	CpuFixedPointChecks,
	CpuFixedPointChecksSkipped,
	Count
};
inline constexpr std::array<const char *, unsigned(GradientCounter::Count)> gradientCounterNames{
	"buffer_allocations",     "command_allocations",
	"binding_allocations",    "submissions",
	"command_enqueues",       "dispatches",
	"convergence_checks",     "mapped_control_bytes_read",
	"upload_bytes_submitted", "readback_bytes_submitted",
	"attempted_fields",       "executed_fields",
	"converged_fields",       "validated_fields",
	"committed_fields",       "recovered_fields",
	"resource_reuses", "gpu_validation_dispatches", "gpu_validated_fields",
    "reference_upload_bytes_submitted", "cpu_fixed_point_checks", "cpu_fixed_point_checks_skipped"};
struct GradientPhaseSample
{
	std::uint64_t samples = 0, cpuNs = 0, elapsedNs = 0, cpuValid = 0, cpuInvalid = 0;
};
struct GradientDiagnosticSnapshot
{
	bool collected = false;
	std::array<GradientPhaseSample, unsigned(GradientPhase::Count)> phases{};
	std::array<std::uint64_t, unsigned(GradientCounter::Count)> counters{};
};
struct GradientDiagnosticTotals
{
	struct Phase
	{
		std::atomic<std::uint64_t> samples{0}, cpuNs{0}, elapsedNs{0}, cpuValid{0}, cpuInvalid{0};
	};
	std::atomic<bool> collected{false};
	std::array<Phase, unsigned(GradientPhase::Count)> phases{};
	std::array<std::atomic<std::uint64_t>, unsigned(GradientCounter::Count)> counters{};
	GradientDiagnosticSnapshot snapshot() const noexcept
	{
		GradientDiagnosticSnapshot result;
		if (!(result.collected = collected.load(std::memory_order_relaxed)))
			return result;
		for (unsigned i = 0; i < phases.size(); ++i)
		{
			const auto &p = phases[i];
			result.phases[i] = {p.samples.load(), p.cpuNs.load(), p.elapsedNs.load(),
								p.cpuValid.load(), p.cpuInvalid.load()};
		}
		for (unsigned i = 0; i < counters.size(); ++i)
			result.counters[i] = counters[i].load();
		return result;
	}
};
inline std::array<GradientDiagnosticTotals, unsigned(Backend::Count)> gradientDiagnosticTotals;
inline bool gradientProfilingRequested() noexcept
{
	const auto *value = std::getenv("GLOB2_GRADIENT_DIAGNOSTICS");
	return value && std::strcmp(value, "1") == 0;
}
inline std::uint64_t gradientProfileElapsedNs() noexcept
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
inline std::uint64_t gradientProfileCpuNs() noexcept
{
#if defined(__EMSCRIPTEN__)
	// Browser timing APIs expose elapsed time, not worker/process CPU usage.
	return 0;
#else
	return glob2::threadCpuNs();
#endif
}
class GradientPhaseScope;
class GradientProfileSession;
inline thread_local GradientProfileSession *activeGradientProfile = nullptr;
class GradientProfileSession
{
	friend class GradientPhaseScope;
	GradientProfileSession *previous;
	GradientPhaseScope *active = nullptr;
	GradientDiagnosticTotals *totals;
	std::uint64_t (*elapsedClock)() noexcept;
	std::uint64_t (*cpuClock)() noexcept;

  public:
	// The enable decision is captured by the caller, not reread in inner scopes.
	explicit GradientProfileSession(Backend backend, bool enabled,
									std::uint64_t (*elapsed)() noexcept = gradientProfileElapsedNs,
									std::uint64_t (*cpu)() noexcept = gradientProfileCpuNs,
									GradientDiagnosticTotals *destination = nullptr) noexcept
		: previous(activeGradientProfile),
		  totals(enabled && validBackend(backend)
					 ? (destination ? destination : &gradientDiagnosticTotals[unsigned(backend)])
					 : nullptr),
		  elapsedClock(elapsed), cpuClock(cpu)
	{
		activeGradientProfile = totals ? this : nullptr;
		// Bridge adapter sessions to the surrounding job scope so their work is
		// subtracted once from the parent, even when totals belong to a different backend.
		active = previous ? previous->active : nullptr;
		if (totals)
			totals->collected.store(true, std::memory_order_relaxed);
	}
	~GradientProfileSession() { activeGradientProfile = previous; }
	GradientProfileSession(const GradientProfileSession &) = delete;
	GradientProfileSession &operator=(const GradientProfileSession &) = delete;
	void count(GradientCounter counter, std::uint64_t n = 1) noexcept
	{
		if (totals)
			totals->counters[unsigned(counter)].fetch_add(n, std::memory_order_relaxed);
	}
};
inline void countGradientWork(GradientCounter counter, std::uint64_t n = 1) noexcept
{
	if (activeGradientProfile)
		activeGradientProfile->count(counter, n);
}
class GradientPhaseScope
{
	GradientProfileSession *session = nullptr;
	GradientPhaseScope *parent = nullptr;
	GradientPhase phase;
	std::uint64_t elapsedStart = 0, cpuStart = 0, childElapsed = 0, childCpu = 0;
	bool childInvalid = false;

  public:
	explicit GradientPhaseScope(GradientPhase phase) noexcept : phase(phase)
	{
		if (activeGradientProfile && activeGradientProfile->totals)
		{
			session = activeGradientProfile;
			parent = session->active;
			session->active = this;
			elapsedStart = session->elapsedClock();
			cpuStart = session->cpuClock();
		}
	}
	~GradientPhaseScope() { finish(); }
	GradientPhaseScope(const GradientPhaseScope &) = delete;
	GradientPhaseScope &operator=(const GradientPhaseScope &) = delete;
	void finish() noexcept
	{
		if (!session)
			return;
		const auto cpuEnd = session->cpuClock(), elapsedEnd = session->elapsedClock();
		const auto elapsed = elapsedEnd >= elapsedStart ? elapsedEnd - elapsedStart : 0;
		const bool valid =
			cpuStart && cpuEnd >= cpuStart && !childInvalid && cpuEnd - cpuStart >= childCpu;
		const auto cpu = valid ? cpuEnd - cpuStart : 0;
		auto &p = session->totals->phases[unsigned(phase)];
		p.samples.fetch_add(1, std::memory_order_relaxed);
		p.elapsedNs.fetch_add(elapsed >= childElapsed ? elapsed - childElapsed : 0,
							  std::memory_order_relaxed);
		if (valid)
		{
			p.cpuNs.fetch_add(cpu - childCpu, std::memory_order_relaxed);
			p.cpuValid.fetch_add(1, std::memory_order_relaxed);
		}
		else
			p.cpuInvalid.fetch_add(1, std::memory_order_relaxed);
		// Propagate inclusive child time: each phase reports exclusive host work,
		// while its parent must also exclude the child's nested descendants.
		if (parent)
		{
			parent->childElapsed += elapsed;
			parent->childCpu += cpu;
			parent->childInvalid |= !valid;
		}
		session->active = parent;
		session = nullptr;
	}
	void setPhase(GradientPhase next) noexcept
	{
		finish();
		phase = next;
		childElapsed = childCpu = 0;
		childInvalid = false;
		if (activeGradientProfile && activeGradientProfile->totals)
		{
			session = activeGradientProfile;
			parent = session->active;
			session->active = this;
			elapsedStart = session->elapsedClock();
			cpuStart = session->cpuClock();
		}
	}
};
inline GradientDiagnosticSnapshot gradientDiagnosticSnapshot(Backend backend) noexcept
{
	return validBackend(backend) ? gradientDiagnosticTotals[unsigned(backend)].snapshot()
								 : GradientDiagnosticSnapshot{};
}
inline void writeGradientDiagnostics(std::ostream &out, const GradientDiagnosticSnapshot &snapshot)
{
	out << "{\"collected\":" << (snapshot.collected ? "true" : "false")
		<< ",\"exclusive\":true,\"phases\":{";
	for (unsigned i = 0; i < snapshot.phases.size(); ++i)
	{
		const auto &p = snapshot.phases[i];
		out << (i ? "," : "") << '"' << gradientPhaseNames[i] << "\":{\"samples\":" << p.samples
			<< ",\"elapsed_ns\":" << p.elapsedNs << ",\"cpu_ns\":";
		// A mixed valid/invalid aggregate is partial; callers must inspect cpu_invalid.
		if (p.cpuValid)
			out << p.cpuNs;
		else
			out << "null";
		out << ",\"cpu_valid\":" << p.cpuValid << ",\"cpu_invalid\":" << p.cpuInvalid << '}';
	}
	out << "},\"counters\":{";
	for (unsigned i = 0; i < snapshot.counters.size(); ++i)
		out << (i ? "," : "") << '"' << gradientCounterNames[i] << "\":" << snapshot.counters[i];
	out << "}}";
}
} // namespace gradient_kernel
