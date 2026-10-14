// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientDeviceService.h"
#include "GradientAccelerators.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace gradient_kernel
{

template <class T, std::size_t N> class BoundedEntries
{
	std::array<T, N> entries{};
	std::size_t count = 0;

  public:
	auto begin() noexcept { return entries.begin(); }
	auto end() noexcept { return entries.begin() + count; }
	auto begin() const noexcept { return entries.begin(); }
	auto end() const noexcept { return entries.begin() + count; }
	bool empty() const noexcept { return !count; }
	std::size_t size() const noexcept { return count; }
	bool full() const noexcept { return count == N; }
	T &operator[](std::size_t index) noexcept { return entries[index]; }
	void push_back(T value) noexcept { entries[count++] = std::move(value); }
	auto erase(typename std::array<T, N>::iterator at) noexcept
	{
		for (auto next = at; next + 1 != end(); ++next)
			*next = std::move(*(next + 1));
		entries[--count] = T{};
		return at;
	}
};
struct GradientDeviceState
{
	// First member releases last, after hooks and all queue/control owners die.
	struct Lease
	{
		std::size_t bytes = 0;
		~Lease()
		{
			if (bytes)
				releaseOpenCLHostBytes(bytes);
		}
	} lease;
	struct Queued
	{
		std::shared_ptr<OwnedGradientField> field;
		std::uint64_t serial = 0;
	};
	mutable std::mutex mutex;
	GradientDeviceService::Hooks hooks;
	GradientDeviceService::Metrics totals;
	BoundedEntries<Queued, 64> queue;
	std::array<std::shared_ptr<OpenCLRequiredControl>, 8> activeControls{};
	std::atomic<std::size_t> retained{0};
	std::uint64_t nextSerial = 0;
	Backend backend = Backend::OpenCL;
	bool started = false, initialized = false, canceled = false;
};
namespace
{
std::atomic<unsigned> brokerThreads{0};
class DeviceBroker;
std::atomic<DeviceBroker *> existingBroker{nullptr};

void recoverField(void *context, std::size_t) noexcept
{
	auto &field = *static_cast<OwnedGradientField *>(context);

	CPUSelectionScope selection(field.decision.configuration.backend);
	recordGradientFallback(field.decision.configuration.backend);
	try
	{
		if (!field.cpu)
			throw std::invalid_argument("Missing owned gradient CPU continuation");
		field.cpu(field);
	}
	catch (...)
	{
		field.error = std::current_exception();
	}

	field.inputs.reset();
	field.identity = {};
	field.phase = OwnedGradientField::Phase::Complete;
}
bool beginRecovery(const std::shared_ptr<OwnedGradientField> &field, OwnedGradientField::Phase expected) noexcept
{
	if (!field || !field->completion)
		return false;
	if (!field->phase.compare_exchange_strong(expected, OwnedGradientField::Phase::Recovering))
		return false;
	if (auto state = field->executionState)
	{
		std::lock_guard lock(state->mutex);
		++state->totals.fallbacks;
		++state->totals.fallbackReasons[unsigned(field->fallbackReason)];
	}
	return true;
}
void fallbackField(const std::shared_ptr<OwnedGradientField> &field) noexcept
{
	if (beginRecovery(field, OwnedGradientField::Phase::Running))
		field->completion->resume({&recoverField, field.get()});
}
class DeviceBroker
{
	struct Lease
	{
		std::size_t bytes = 0;
		explicit Lease(std::size_t count)
		{
			if (reserveOpenCLHostBytes(count))
				bytes = count;
		}
		~Lease()
		{
			if (bytes)
				releaseOpenCLHostBytes(bytes);
		}
	} lease{sizeof(DeviceBroker) + 128};
	std::shared_ptr<const void> backendLifetime = retainOpenCLLifetime();
	std::mutex mutex;
	std::condition_variable wake;
	BoundedEntries<std::shared_ptr<GradientDeviceState>, 64> registrations;
	std::thread coordinator;
	bool stopping = false, realInitialized = false, realReady = false;
	bool pending() const
	{
		for (const auto &state : registrations)
		{
			std::lock_guard lock(state->mutex);
			if (state->canceled || !state->initialized || !state->queue.empty())
				return true;
		}
		return false;
	}
	void initialize(const std::shared_ptr<GradientDeviceState> &state) noexcept
	{
		bool ready = false;
		try
		{
			if (state->hooks.initialize)
				ready = state->hooks.initialize();
			else
			{
				if (state->backend == Backend::OpenCL && !realInitialized)
				{
					realReady = initializeOpenCL();
					realInitialized = true;
				}
				ready = state->backend == Backend::OpenCL
							? realReady && (readyPlans.load(std::memory_order_acquire) &
											(1u << unsigned(Plan::Frozen8)))
							: initializeGradientAccelerator(state->backend);
			}
		}
		catch (...)
		{
		}
		std::lock_guard lock(state->mutex);
		state->initialized = true;
		state->started = ready && !state->canceled;
	}
	static bool compatible(const OwnedGradientField &a, const OwnedGradientField &b) noexcept
	{
		return a.session == b.session && a.decision.plan == b.decision.plan &&
			   a.decision.generation == b.decision.generation &&
			   a.decision.configuration == b.decision.configuration && a.family == b.family &&
			   a.grid.width() == b.grid.width() && a.grid.height() == b.grid.height() &&
			   a.limit == b.limit && a.cpuBuckets == b.cpuBuckets &&
			   a.identity.owner == b.identity.owner &&
			   !a.identity.owner.owner_before(b.identity.owner) &&
			   !b.identity.owner.owner_before(a.identity.owner) &&
			   a.identity.variant == b.identity.variant &&
			   a.identity.revision == b.identity.revision;
	}
	void execute(const std::shared_ptr<GradientDeviceState> &state) noexcept
	{
		std::array<std::shared_ptr<OwnedGradientField>, 8> held{};
		std::size_t count = 0;
		bool canceled = false;
		{
			std::lock_guard lock(state->mutex);
			canceled = state->canceled;
			const auto earlier = [](const auto &a, const auto &b)
			{
				return a.field->due != b.field->due ? a.field->due < b.field->due
													: a.serial < b.serial;
			};
			auto earliest = std::min_element(state->queue.begin(), state->queue.end(), earlier);
			if (earliest == state->queue.end())
				return;
			held[count++] = earliest->field;
			state->queue.erase(earliest);
			// Only fields already admitted are eligible. A singleton never waits
			// to fill a batch, and every field retains its own due/ticket/control.
			// Only OpenCL currently shares driver orchestration across fields.
			// The Vulkan/WebGPU adapters execute members sequentially, so grouping
			// them would couple a completed field to a later member's decline.
			while (count < std::min<std::size_t>(held.size(), state->totals.configuredMaxBatch))
			{
				auto next = state->queue.end();
				for (auto candidate = state->queue.begin(); candidate != state->queue.end();
					 ++candidate)
					if (compatible(*held.front(), *candidate->field) &&
						(next == state->queue.end() || earlier(*candidate, *next)))
						next = candidate;
				if (next == state->queue.end())
					break;
				held[count++] = next->field;
				state->queue.erase(next);
			}
			for (std::size_t i = 0; i < count; ++i)
			{
				held[i]->phase = OwnedGradientField::Phase::Running;
				state->activeControls[i] = held[i]->requiredControl;
			}
			++state->totals.batches;
			state->totals.maxBatch = std::max<std::uint64_t>(state->totals.maxBatch, count);
		}
		const bool stale = std::any_of(held.begin(), held.begin() + count,
									   [](const auto &field)
									   {
										   return field->decision.generation !=
													  field->session->currentGeneration() ||
												  field->session->failed.load();
									   });
		bool handled = false;
		if (!canceled && !stale)
			try
			{
				std::array<BackendRequest, 8> requests{
					{{nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr},
					 {nullptr, 0, field::Grid{1, 1}, *held.front()->session, nullptr, nullptr,
					  nullptr}}};
				for (std::size_t i = 0; i < count; ++i)
				{
					auto &field = *held[i];
					requests[i].gradient = field.data.get();
					requests[i].limit = field.limit;
					requests[i].grid = field.grid;
					requests[i].context = &field;
					requests[i].costAt = [](void *value, std::size_t cell)
					{
						auto &field = *static_cast<OwnedGradientField *>(value);
						return field.costAt(field, cell);
					};
					requests[i].identity = field.identity;
					requests[i].family = field.family;
					requests[i].capturedConfiguration = field.decision.configuration;
					requests[i].capturedGeneration = field.decision.generation;
					requests[i].cpuBuckets = field.cpuBuckets;
					requests[i].executedOnDevice = &field.executedGPU;
					requests[i].deviceExecutionObserved = &field.deviceExecutionObserved;
					requests[i].requiredControl = field.requiredControl;
				}

				const std::span batch(requests.data(), count);
				handled = state->hooks.execute
							  ? state->hooks.execute(batch, held.front()->decision.plan)
							  : executeGradientAccelerator(batch, held.front()->decision.plan);
			}

			catch (...)
			{
				handled = false;
			}
		const auto finished = monotonicNs();
		for (std::size_t i = 0; i < count; ++i)
		{
			auto &field = *held[i];
			if (handled || field.executedGPU)
			{
				field.inputs.reset();
				field.identity = {};
			}
			else
				field.fallbackReason =
					canceled ? GradientFallbackReason::Shutdown
					: stale  ? GradientFallbackReason::StaleGeneration
					: field.requiredControl && field.requiredControl->shouldCancel(finished)
						? GradientFallbackReason::Cancellation
					: field.session->failed.load() ? GradientFallbackReason::DriverFailure
												   : GradientFallbackReason::BackendDecline;
		}
		{
			std::lock_guard lock(state->mutex);
			state->activeControls.fill({});
			if (stale)
				state->totals.stale += count;

			for (std::size_t i = 0; i < count; ++i)
			{
				if (held[i]->deviceExecutionObserved)
				{
					++state->totals.deviceCompletedFields;
					if (!handled && !held[i]->executedGPU)
						++state->totals.fallbackAfterDeviceCompletionFields;
				}
				if ((handled || held[i]->executedGPU) && !held[i]->error)
				{
					++state->totals.completed;
					held[i]->executedGPU ? ++state->totals.executed : ++state->totals.trivial;
				}
			}
		}
		for (std::size_t i = 0; i < count; ++i)
		{
			if (handled || held[i]->executedGPU)
			{
				auto phase = OwnedGradientField::Phase::Running;
				if (held[i]->phase.compare_exchange_strong(phase,
														   OwnedGradientField::Phase::Complete))
					held[i]->completion->complete();
			}
			else
				fallbackField(held[i]);
		}
	}
	void run() noexcept
	{
		brokerThreads = 1;
		for (;;)
		{
			std::shared_ptr<GradientDeviceState> selected;
			bool initialization = false;
			{
				std::unique_lock lock(mutex);
				// Browser initialization/completion callbacks do not notify this
				// native condition variable; bounded maintenance observes them.
                if (registrations.empty())
                    wake.wait(lock,[&] {return stopping || !registrations.empty();});
                else
                    wake.wait_for(lock,std::chrono::milliseconds(100),[&] {return stopping || pending();});
				for (auto at = registrations.begin(); at != registrations.end();)
				{
					auto current = *at;
					std::lock_guard stateLock(current->mutex);
					if (current->canceled && current->queue.empty())
						at = registrations.erase(at);
					else
						++at;
				}
				if (stopping)
					for (const auto &state : registrations)
					{
						std::lock_guard stateLock(state->mutex);
						state->canceled = true;
						for (const auto &control : state->activeControls)
							if (control)
								control->requestCancel();
					}
				std::uint64_t due = UINT64_MAX;
				for (const auto &state : registrations)
				{
					std::lock_guard stateLock(state->mutex);
					for (const auto &queued : state->queue)
						if (!selected || queued.field->due < due)
						{
							selected = state;
							due = queued.field->due;
						}
				}
				if (!selected)
					for (const auto &state : registrations)
					{
						std::lock_guard stateLock(state->mutex);
						if (!state->initialized && !state->canceled)
						{
							selected = state;
							initialization = true;
							break;
						}
					}
				if (!selected && stopping)
					break;
			}
			if (selected)
			{
				if (initialization)
					initialize(selected);
				else
					execute(selected);
			}
			if (realInitialized && requiredOpenCLQuarantinePending())
				reapOpenCLRequiredQuarantine();
			maintainGradientAccelerators();
			{
				std::lock_guard lock(mutex);
				for (const auto &state : registrations)
					if (state->backend == Backend::WebGPU)
					{
						const bool ready =
							acceleratorStatus(Backend::WebGPU).availability == Availability::Ready;
						std::lock_guard stateLock(state->mutex);
						if (state->initialized && !state->canceled)
							state->started = ready;
					}
			}
		}
		brokerThreads = 0;
	}

  public:
	DeviceBroker() { existingBroker.store(this, std::memory_order_release); }
	~DeviceBroker()
	{
		shutdown();
		existingBroker.store(nullptr, std::memory_order_release);
	}
	void shutdown() noexcept
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
			for (const auto &state : registrations)
			{
				std::lock_guard stateLock(state->mutex);
				state->canceled = true;
				for (const auto &control : state->activeControls)
					if (control)
						control->requestCancel();
				for (const auto &queued : state->queue)
					if (queued.field->requiredControl)
						queued.field->requiredControl->requestCancel();
			}
		}
		wake.notify_one();
		if (coordinator.joinable())
			coordinator.join();
		// The process owner may create another GlobalContainer in a harness.
		// Old registrations stay canceled; new ones can launch a fresh thread.
		std::lock_guard lock(mutex);
		while (!registrations.empty())
			registrations.erase(registrations.begin());
		stopping = false;
	}
	bool add(const std::shared_ptr<GradientDeviceState> &state) noexcept
	{
		try
		{
			std::lock_guard lock(mutex);
			if (stopping || !lease.bytes || registrations.full())
				return false;
			if (!coordinator.joinable())
				coordinator = GAGCore::ThreadSupport::launch([this] { run(); });
			registrations.push_back(state);
			wake.notify_one();
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
	void notify() noexcept
	{
		std::lock_guard lock(mutex);
		wake.notify_one();
	}
};
DeviceBroker &broker()
{
	static DeviceBroker value;
	return value;
}
} // namespace
void shutdownGradientDeviceCoordinator() noexcept
{
	// No coordinator means no profiled device-service work. Preserve the CPU
	// drain without environment lookups, TLS sessions or diagnostic output.
	auto *coordinator = existingBroker.load(std::memory_order_acquire);
	if (!coordinator)
		return;
	{
		// CPU-only applications must not initialize an otherwise unused backend.
		coordinator->shutdown();
		shutdownGradientAccelerators();
	}
}
OwnedGradientField::~OwnedGradientField()
{
	data.reset();
	inputs.reset();
	identity = {};
	session.reset();
	executionState.reset();
	releaseReservation();
}
void OwnedGradientField::releaseReservation() noexcept
{
	if (reservedHostBytes)
	{
		releaseOpenCLHostBytes(reservedHostBytes);
		if (serviceRetained)
			serviceRetained->fetch_sub(reservedHostBytes);
		reservedHostBytes = 0;
	}
}
std::unique_ptr<std::uint16_t[]> OwnedGradientField::takeData()
{
	releaseReservation();
	return std::move(data);
}
std::shared_ptr<GradientDeviceState> GradientDeviceService::state() const
{
	std::lock_guard lock(mutex);
	return registration;
}
void GradientDeviceService::configure(unsigned computeThreads, Backend mode)
{
	stop();
	{
		std::lock_guard lock(mutex);
		registration.reset();
	}
	if constexpr (!GAGCore::ThreadSupport::available)
		return;
	if (computeThreads < 2 || !isGPU(mode) ||
		(!hooks.initialize &&
		 !acceleratorImplementations[unsigned(mode)].initialize))
		return;
	constexpr auto bytes = sizeof(GradientDeviceState) + 128;
	if (!reserveOpenCLHostBytes(bytes))
		return;
	std::shared_ptr<GradientDeviceState> next;
	try
	{
		next = std::make_shared<GradientDeviceState>();
	}
	catch (...)
	{
		releaseOpenCLHostBytes(bytes);
		throw;
	}
	next->lease.bytes = bytes;
	next->hooks = hooks;
	next->backend = mode;
	next->totals.configuredMaxBatch = mode == Backend::OpenCL ? 8 : 1;
	next->started = true;
	{
		std::lock_guard lock(mutex);
		registration = next;
	}
	if (!broker().add(next))
	{
		std::lock_guard lock(next->mutex);
		next->started = false;
		next->initialized = true;
	}
}
void GradientDeviceService::stop() noexcept
{
	if (auto current = state())
	{
		{
			std::lock_guard lock(current->mutex);
			current->canceled = true;
			for (const auto &control : current->activeControls)
				if (control)
					control->requestCancel();
			for (const auto &queued : current->queue)
				if (queued.field->requiredControl)
					queued.field->requiredControl->requestCancel();
		}
		broker().notify();
	}
}
bool GradientDeviceService::submit(const std::shared_ptr<OwnedGradientField> &field) noexcept
{
	// An admitted request has one owner. A retry cannot change its inputs,
	// recovery reason or completion while the coordinator is using it.
	if (field && field->phase.load(std::memory_order_acquire) != OwnedGradientField::Phase::Private)
		return false;
	try
	{
		auto current = state();
		if (!current)
		{
			if (field)
				field->fallbackReason = GradientFallbackReason::Unavailable;
			return false;
		}
		std::unique_lock lock(current->mutex);
		if (field)
		{
			field->executionState = current;
		}
		if (!field || !field->session || !field->data || !field->completion || !field->cpu ||
			!field->costAt)
		{
			if (field)
				field->fallbackReason = GradientFallbackReason::InvalidRequest;
			++current->totals.declined;
			return false;
		}
		if (!current->started || current->canceled || !current->initialized)
		{
			field->fallbackReason = current->canceled ? GradientFallbackReason::Shutdown
													  : GradientFallbackReason::Unavailable;
			++current->totals.declined;
			return false;
		}
		if (field->requiredControl && field->requiredControl->shouldCancel(monotonicNs()))
		{
			field->fallbackReason = GradientFallbackReason::Cancellation;
			++current->totals.declined;
			return false;
		}
		const auto captured = field->decision.configuration;
		if (!supportsAlgorithm(captured.backend, captured.algorithm) ||
			selectedPlan(captured.backend, captured.algorithm) != field->decision.plan)
		{
			field->fallbackReason = GradientFallbackReason::InvalidRequest;
			++current->totals.declined;
			return false;
		}
		if (field->decision.plan == Plan::CPU || unsigned(field->decision.plan) >= PLANS.size() ||
			PLANS[unsigned(field->decision.plan)].backend != current->backend ||
			field->decision.generation != field->session->currentGeneration())
		{
			field->fallbackReason = GradientFallbackReason::StaleGeneration;
			++current->totals.stale;
			++current->totals.declined;
			return false;
		}
		// Browser-owner shutdown may join this worker only because cancellation
		// makes callback waits relinquish their retained Task without waiting for
		// the browser owner. Every admitted request therefore needs a control.
		if (!field->requiredControl)
			field->requiredControl = std::make_shared<OpenCLRequiredControl>();
		auto phase = OwnedGradientField::Phase::Private;
		if (!field->phase.compare_exchange_strong(phase, OwnedGradientField::Phase::Queued))
		{
			++current->totals.declined;
			return false;
		}
		const auto decline = [&]
		{
			field->phase = OwnedGradientField::Phase::Private;
			field->fallbackReason = GradientFallbackReason::MemoryBudget;
			++current->totals.budgetDeclines;
			++current->totals.declined;
			return false;
		};
		const auto cells = field->grid.cells(),
				   sessionBytes = sizeof(BackendSession) + 128;
		if (current->queue.full() ||
			cells > (OpenCLHostBudget - sizeof(OwnedGradientField) - sessionBytes) /
						sizeof(std::uint16_t))
			return decline();
		const auto base = cells * sizeof(std::uint16_t) + sizeof(OwnedGradientField) + sessionBytes;
		if (field->retainedInputBytes > OpenCLHostBudget - base)
			return decline();
		const auto bytes = base + field->retainedInputBytes;
		if (!reserveOpenCLHostBytes(bytes))
			return decline();
		field->reservedHostBytes = bytes;
		field->serviceRetained =
			std::shared_ptr<std::atomic<std::size_t>>(current, &current->retained);
		current->retained.fetch_add(bytes);
		current->queue.push_back({field, ++current->nextSerial});
		++current->totals.submitted;
		lock.unlock();
		broker().notify();
		return true;
	}
	catch (...)
	{
		return false;
	}
}
void GradientDeviceService::recoverOnWorker(const std::shared_ptr<OwnedGradientField> &field) noexcept
{
	// Admission has not transferred execution. Recover on the submitting worker
	// and settle its existing ticket without another executor dispatch.
	if (beginRecovery(field, OwnedGradientField::Phase::Private))
	{
		recoverField(field.get(), 0);
		field->completion->complete();
	}
}
GradientDeviceService::Metrics GradientDeviceService::metrics() const
{
	Metrics result;
	if (auto current = state())
	{
		std::lock_guard lock(current->mutex);
		result = current->totals;
		result.queued = current->queue.size();
		result.running = current->started && !current->canceled;
		result.ready = result.running && current->initialized;
		result.retainedHostBytes = current->retained.load();
	}
	result.coordinatorThreads = brokerThreads.load();
	return result;
}
} // namespace gradient_kernel
