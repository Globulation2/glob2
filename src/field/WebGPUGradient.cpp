// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientAccelerators.h"
#include "GradientComputeShaders.h"
#include "OpenCLGradient.h"
#include <mutex>
#include <type_traits>
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
// Browser-specific waiting lives in the platform implementation. Initialization
// remains asynchronous; admission rejects the owner before any callback borrows
// task storage, so owner-side failure cannot release in-flight memory.
extern "C" int glob2_gradient_worker_context();
extern "C" void glob2_gradient_worker_wait(int *);
extern "C" void glob2_gradient_prepare(int *, const char *, const char *, char *);
extern "C" void glob2_gradient_execute(int *, const std::uint32_t *, const std::uint32_t *,
									   std::uint32_t *, int, int, int, int);
extern "C" void glob2_gradient_shutdown();
#endif
namespace gradient_kernel
{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
namespace
{
struct Task
{
	// JS writes done last with Atomics.store, then notifies the coordinator.
	alignas(4) std::array<
		int,
		4> signal{}; // completion, cancellation, observed dispatch, dispatches
	std::array<char, 512> metadata{};
	std::vector<std::uint32_t> seeds, costs, output;
	std::size_t hostBytes = 0, deviceBytes = 0;
	bool accounted = false;
	Task() = default;
	Task(const Task &) = delete;
	Task &operator=(const Task &) = delete;
	Task(Task &&) = delete;
	Task &operator=(Task &&) = delete;
	~Task()
	{
		if (hostBytes)
			releaseOpenCLHostBytes(hostBytes);
		if (deviceBytes)
			releaseGradientDeviceBytes(deviceBytes);
	}
};
static_assert(!std::is_copy_constructible_v<Task> && !std::is_move_constructible_v<Task>);
struct State
{
	std::mutex mutex;
	AcceleratorStatus status{Backend::WebGPU};
	std::vector<std::unique_ptr<Task>> retained;
	std::unique_ptr<Task> initialization;
	unsigned active = 0;
	State() { retained.reserve(64); }
	~State()
	{
		// Callback addresses cannot be recycled by C++ static teardown.
		if (initialization && !completedForTeardown(*initialization))
			initialization.release();
		for (auto &task : retained)
			if (task && !completedForTeardown(*task))
				task.release();
	}
	static bool completedForTeardown(const Task &task)
	{
		return __atomic_load_n(task.signal.data(), __ATOMIC_ACQUIRE) != 0;
	}
};
State &state()
{
	static State value;
	return value;
}
int completed(Task &task)
{
	return __atomic_load_n(task.signal.data(), __ATOMIC_ACQUIRE);
}
// Called once, only after the callback's release-store proves its timing and
// dispatch counters are final. Retained cancellations own no caller pointers.
void accountCompletedTask(State &state, Task &task)
{
	if (task.accounted)
		return;
	task.accounted = true;
	state.status.dispatches += std::max(task.signal[3], 0);
}
bool wait(std::unique_ptr<Task> &task, const BackendRequest *request = nullptr)
{
	while (!completed(*task))
	{
		if (request && request->deviceExecutionObserved &&
			__atomic_load_n(&task->signal[2], __ATOMIC_ACQUIRE))
			*request->deviceExecutionObserved = true;
		if (request && request->requiredControl &&
			request->requiredControl->shouldCancel(monotonicNs()))
		{
			__atomic_store_n(&task->signal[1], 1, __ATOMIC_RELEASE);
			auto &s = state();
			std::lock_guard lock(s.mutex);
			s.retained.push_back(std::move(task));
			return false;
		}
		glob2_gradient_worker_wait(task->signal.data());
	}
	return completed(*task) > 0;
}
} // namespace
bool initializeWebGPUGradient()
{
	maintainWebGPUGradient();
	auto &s = state();
	std::lock_guard lock(s.mutex);
	if (s.status.availability == Availability::Ready)
		return true;
	if (s.status.availability == Availability::Unavailable ||
		s.status.availability == Availability::Failed || s.initialization)
		return false;
	auto task = std::make_unique<Task>();
	if (!reserveOpenCLHostBytes(sizeof(Task) + 128))
		return false;
	task->hostBytes = sizeof(Task) + 128;
	s.status.availability = Availability::Initializing;
	glob2_gradient_prepare(task->signal.data(), gradient_shaders::frozen8WGSL,
						   gradient_shaders::jacobiWGSL, task->metadata.data());
	s.initialization = std::move(task);
	// Initialization callbacks must not make owner-side coordinator shutdown wait
	// on the browser event loop. Readiness is published by worker maintenance.
	return false;
}
bool executeWebGPUGradient(std::span<const BackendRequest> requests, Plan plan)
{
	if (!glob2_gradient_worker_context())
		return false;
	if (plan != Plan::WebGPUFrozen8 && plan != Plan::WebGPUJacobi)
		return false;
	if (requests.size() > BATCH_CATEGORIES)
		return false;
	for (const auto &request : requests)
		if (!request.gradient || !request.costAt || request.limit < 0 ||
			request.operation != Operation::CompleteField)
			return false;
	maintainWebGPUGradient();
	{
		auto &s = state();
		std::lock_guard lock(s.mutex);
		if (s.retained.size() >= 64)
			return false;
	}
	// Shutdown must see the entire batch, including gaps between fields and
	// completed private outputs awaiting atomic batch commit.
	auto &service = state();
	{
		std::lock_guard lock(service.mutex);
		if (service.status.availability != Availability::Ready ||
			service.retained.size() + service.active >= 64)
			return false;
		++service.active;
	}
	struct ActiveLease
	{
		State &state;
		~ActiveLease()
		{
			std::lock_guard lock(state.mutex);
			--state.active;
		}
	} activeLease{service};
	// Keep all output private until the whole selected group succeeds.
	std::vector<std::unique_ptr<Task>> tasks;
	for (const auto &request : requests)
	{
		const auto n = request.grid.cells(), tiles = std::size_t((request.grid.width() + 15) / 16) *
													 ((request.grid.height() + 15) / 16);
		if (n > OpenCLHostBudget / 20)
			return false;
		auto task = std::make_unique<Task>();
		const auto hostBytes = sizeof(Task) + 128 + n * 20 + tiles * 4 + 20,
				   deviceBytes = n * 16 + 20 + tiles * 8;
		if (!reserveOpenCLHostBytes(hostBytes))
			return false;
		task->hostBytes = hostBytes;
		if (!reserveGradientDeviceBytes(deviceBytes))
			return false;
		task->deviceBytes = deviceBytes;
		task->seeds.resize(n);
		task->costs.resize(n);
		task->output.resize(n);
		for (std::size_t i = 0; i < n; ++i)
		{
			task->seeds[i] = request.gradient[i];
			const auto c = request.gradient[i] || request.identity.allCells
							   ? request.costAt(request.context, i)
							   : LAND_STEPS;
			if (!c.cardinal || !c.diagonal || c.cardinal > 65535 || c.diagonal > 65535)
				return false;
			task->costs[i] = c.cardinal | (c.diagonal << 16);
		}
		glob2_gradient_execute(task->signal.data(), task->seeds.data(), task->costs.data(),
							   task->output.data(), request.grid.width(), request.grid.height(),
							   std::min(request.limit, COST_LIMIT), plan == Plan::WebGPUFrozen8);
		const bool ready = wait(task, &request);
		if (task && task->signal[2] && request.deviceExecutionObserved)
			*request.deviceExecutionObserved = true;
		if (task && completed(*task))
		{
			std::lock_guard lock(service.mutex);
			accountCompletedTask(service, *task);
		}
		if (!ready)
		{
			if (task && completed(*task) == -1)
			{
				auto &s = state();
				std::lock_guard lock(s.mutex);
				s.status.availability = Availability::Failed;
				s.status.error = "WebGPU gradient device or submission failed";
				readyPlans.fetch_and(~((1u << unsigned(Plan::WebGPUFrozen8)) |
									   (1u << unsigned(Plan::WebGPUJacobi))));
			}
			return false;
		}		tasks.push_back(std::move(task));
	}
	for (std::size_t i = 0; i < tasks.size(); ++i)
	{
		std::copy(tasks[i]->output.begin(), tasks[i]->output.end(), requests[i].gradient);
		if (requests[i].executedOnDevice)
			*requests[i].executedOnDevice = true;
	}
	{
		auto &s = state();
		std::lock_guard lock(s.mutex);
		s.status.fields += tasks.size();
	}
	return true;
}
AcceleratorStatus webGPUGradientStatus()
{
	auto &s = state();
	std::lock_guard lock(s.mutex);
	return s.status;
}
void maintainWebGPUGradient() noexcept
{
	auto &s = state();
	std::lock_guard lock(s.mutex);
	if (s.initialization && completed(*s.initialization))
	{
		const bool ready = completed(*s.initialization) > 0;
		s.status.availability = ready ? Availability::Ready : Availability::Unavailable;
		s.status.device = ready ? s.initialization->metadata.data() : "";
		s.status.driver = "WebGPU (driver version not exposed)";
		s.status.shaderIdentity = gradient_shaders::identity;
		if (!ready)
			s.status.error = "WebGPU compute initialization failed or is unavailable";
		if (ready)
			readyPlans.fetch_or((1u << unsigned(Plan::WebGPUFrozen8)) |
									(1u << unsigned(Plan::WebGPUJacobi)),
								std::memory_order_release);
		s.initialization.reset();
	}
	for (auto at = s.retained.begin(); at != s.retained.end();)
	{
		const auto result = completed(**at);
		if (!result)
		{
			++at;
			continue;
		}
		// Cancellation does not erase a later device-loss report. A callback
		// still owns the task until it publishes completion after retirement.
		if (result == -1)
		{
			s.status.availability = Availability::Failed;
			s.status.error = "WebGPU retained submission failed";
			readyPlans.fetch_and(
				~((1u << unsigned(Plan::WebGPUFrozen8)) | (1u << unsigned(Plan::WebGPUJacobi))));
		}
		accountCompletedTask(s, **at);
		at = s.retained.erase(at);
	}
}
void shutdownWebGPUGradient() noexcept
{
	maintainWebGPUGradient();
	auto &s = state();
	std::lock_guard lock(s.mutex);
	// Outstanding callbacks still own their Wasm completion addresses.
	if (!s.active && s.retained.empty() && !s.initialization)
	{
		glob2_gradient_shutdown();
		s.status = {Backend::WebGPU};
		readyPlans.fetch_and(
			~((1u << unsigned(Plan::WebGPUFrozen8)) | (1u << unsigned(Plan::WebGPUJacobi))));
	}
}
#else
bool initializeWebGPUGradient()
{
	return false;
}
bool executeWebGPUGradient(std::span<const BackendRequest>, Plan)
{
	return false;
}
AcceleratorStatus webGPUGradientStatus()
{
	return {Backend::WebGPU, Availability::Unavailable, "",
			"WebGPU compute requires a threaded browser build"};
}
void shutdownWebGPUGradient() noexcept {}
void maintainWebGPUGradient() noexcept {}
#endif
} // namespace gradient_kernel
