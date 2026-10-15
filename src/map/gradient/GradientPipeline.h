// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientWorkspace.h"
#include "field/GradientDeviceService.h"
#include "sim/snapshot/WorldSnapshot.h"
#include "SnapshotGradient.h"
#include "ComputeExecutor.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Owner reserves and publishes at fixed ticks. Workers own private outputs and
// immutable inputs until join; physical completion never selects publication.
class GradientPipeline
{
  public:
	struct DeviceJob
	{
		std::shared_ptr<gradient_kernel::OwnedGradientField> field;
		std::shared_ptr<gradient_kernel::OpenCLRequiredControl> control;
		std::atomic<bool> canceled{false};
	};
	struct Job
	{
		std::optional<SimulationSnapshot::Handle> snapshotLease;
		std::uint16_t **slot = nullptr;
		std::unique_ptr<std::uint16_t[]> data;
		int swim = 0;
		std::uint64_t due = 0, executorDue = 0;
		bool superseded = false, done = false;
		std::exception_ptr error;
		gradient_preparation::Request request;
		gradient_preparation::CrowdingScratch *crowding = nullptr;
		ComputeExecutor::Batch batch;
		GradientPipeline *owner = nullptr;
		std::function<void(Job &)> seed;
        std::shared_ptr<void> payload;
        std::function<void(Job&,GradientWorkspace&)> customWork;
        std::function<void(Job&)> customPublish;
		// Allocated only for GPU-configured submissions, never CPU jobs.
		std::unique_ptr<DeviceJob> device;
	};
	struct PendingSnapshot
	{
		std::uint16_t **slot;
		const std::uint16_t *data;
		unsigned remaining;
        bool superseded;
        const Job* job=nullptr;
    };
    struct RestoredSnapshot
	{
		std::uint16_t **slot;
		int swim;
		unsigned remaining;
		bool superseded;
        std::unique_ptr<std::uint16_t[]> data;
        std::shared_ptr<void> payload;
        std::function<void(Job&)> customPublish;
    };
    using Work = std::function<void(Job &, GradientWorkspace &)>;
	using AsyncWork = std::function<std::shared_ptr<gradient_kernel::OwnedGradientField>(
		Job &, gradient_kernel::PlanDecision)>;
	std::function<void(std::uint16_t **)> onPublished;
    std::function<bool(std::uint16_t **)> validDestination;
	std::function<std::uint64_t(unsigned)> deadline;
	static constexpr unsigned MaxDelay = 16;
	static_assert(MaxDelay <= ComputeExecutor::GradientHorizon);
	struct Metrics
	{
		std::uint64_t jobs = 0, published = 0, discarded = 0, maxPending = 0;
	} metrics;

  private:
	struct Workspace
	{
		GradientWorkspace propagation;
		gradient_preparation::CrowdingScratch crowding;
	};
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	std::vector<Workspace> workspaces;
	ComputeExecutor *executor = nullptr;
	std::shared_ptr<gradient_kernel::BackendSession> backendSession;
	std::shared_ptr<gradient_kernel::GradientDeviceService> deviceService;
	gradient_kernel::Configuration selected;
	unsigned delay = 0;
	std::uint64_t tick = 0, lastSubmission = 0;
	std::size_t cells = 0;
	Work work;
	AsyncWork asyncWork;
	ComputeExecutor::Job callback{runCPU, nullptr};
	using Wait = void (GradientPipeline::*)(Job &, JoinContext);
	Wait waitJob = &GradientPipeline::waitCPU;
	static void releaseInputs(Job &job) noexcept
	{
		job.snapshotLease.reset();
		job.seed = {};
		job.crowding = nullptr;
		job.done = true;
	}
	static void runCPU(void *context, std::size_t) noexcept
	{
		auto &job = *static_cast<Job *>(context);
		auto &self = *job.owner;
		auto &scratch = self.workspaces[self.executor->slot()];
		gradient_kernel::CPUSelectionScope choice(self.selected.backend);
		job.crowding = &scratch.crowding;
		try
		{
			if (job.seed)
				job.seed(job);
            if (job.customWork) job.customWork(job,scratch.propagation);
            else self.work(job, scratch.propagation);
		}
		catch (...)
		{
			job.error = std::current_exception();
		}
		releaseInputs(job);
	}
	static void runGPU(void *context, std::size_t) noexcept
	{
		auto &job = *static_cast<Job *>(context);
		auto &self = *job.owner;
		auto &scratch = self.workspaces[self.executor->slot()];
		gradient_kernel::CPUSelectionScope choice(self.selected.backend);
		// Capture before seeding: a readiness change cannot reinterpret this job.
		job.crowding = &scratch.crowding;
		try
		{
            if (job.customWork) {
                if (job.seed) job.seed(job);
                job.customWork(job,scratch.propagation);
                releaseInputs(job);
                return;
            }
			const auto decision = self.backendSession->choose(
				gradient_preparation::backendFamily(job.request.kind), 1, self.selected);
			if (job.seed)
				job.seed(job);
			if (decision.plan != gradient_kernel::Plan::CPU && self.asyncWork)
			{
				auto &device = *job.device;
				device.field = self.asyncWork(job, decision);
				if (device.field)
				{
					std::atomic_store_explicit(&device.control, device.field->requiredControl,
											   std::memory_order_release);
					if (device.canceled.load(std::memory_order_acquire) &&
						device.field->requiredControl)
						device.field->requiredControl->requestCancel();
					device.field->completion = self.executor->defer();
					if (!self.deviceService->submit(device.field))
						self.deviceService->recoverOnWorker(device.field);
				}
				else
					self.work(job, scratch.propagation);
			}
			else
				self.work(job, scratch.propagation);
		}
		catch (...)
		{
			job.error = std::current_exception();
		}
		releaseInputs(job);
	}
	void selectExecution()
	{
		// This is configuration work, never part of a CPU simulation job.
		const bool gpu =
			gradient_kernel::isGPU(selected.backend) && backendSession && deviceService;
		callback = {gpu ? runGPU : runCPU, nullptr};
		waitJob = gpu ? &GradientPipeline::waitGPU : &GradientPipeline::waitCPU;
	}
	void waitCPU(Job &job, JoinContext context)
	{
		if (executor)
			executor->join(job.batch, context);
		if (!job.done)
			throw std::logic_error("Unprepared gradient reservation");
	}
	void waitGPU(Job &job, JoinContext context)
	{
		if (job.device && executor && !executor->finished(job.batch))
		{
			job.device->canceled.store(true, std::memory_order_release);
			if (auto control =
					std::atomic_load_explicit(&job.device->control, std::memory_order_acquire))
				control->requestCancel();
		}
		waitCPU(job, context);
		if (job.device && job.device->field)
		{
			auto field = std::move(job.device->field);
			job.data = field->takeData();
			if (field->error)
				job.error = field->error;
		}
	}

  public:
	~GradientPipeline() { reset(); }
	bool enabled() const { return delay != 0; }
	unsigned workerCount() const { return executor ? executor->threadCount() - 1 : 0; }
	unsigned delayTicks() const { return delay; }
	std::size_t pendingCount() const { return pending.size(); }
	void finish(JoinContext context = {JoinReason::Verification, ComputeTelemetry::NoTick})
	{
		for (auto &job : pending)
		{
			(this->*waitJob)(*job, context);
			if (job->error)
				std::rethrow_exception(job->error);
		}
	}
	void reset() noexcept
	{
		for (auto &job : pending)
			if (!job->batch.empty())
			{
				try
				{
					(this->*waitJob)(*job, {JoinReason::Teardown, ComputeTelemetry::NoTick});
				}
				catch (...)
				{
				}
			}
		pending.clear();
		spare.clear();
		workspaces.clear();
		delay = 0;
		lastSubmission = tick;
	}
	void configure(ComputeExecutor &target, unsigned ticks, std::size_t size, Work fn)
	{
		if (!ticks || ticks > MaxDelay)
			throw std::invalid_argument("gradient delay exceeds its horizon");
		reset();
		executor = &target;
		cells = size;
		work = std::move(fn);
		metrics = {};
		selected = gradient_kernel::configuration();
		resizeWorkspaces();
		delay = ticks;
		selectExecution();
	}
	void setExecutionBackend(gradient_kernel::Configuration value)
	{
		selected = value;
		if (!gradient_kernel::isGPU(value.backend))
		{
			spare.clear();
			for (auto &job : pending)
				job->device.reset();
		}
		selectExecution();
	}
	void setAsyncWork(AsyncWork fn)
	{
		finish({JoinReason::Reconfigure, ComputeTelemetry::NoTick});
		asyncWork = std::move(fn);
	}
	void setDeviceService(std::shared_ptr<gradient_kernel::GradientDeviceService> value)
	{
		finish({JoinReason::Reconfigure, ComputeTelemetry::NoTick});
		deviceService = std::move(value);
		if (deviceService && gradient_kernel::isGPU(selected.backend) && !backendSession)
			setBackendSession(std::make_shared<gradient_kernel::BackendSession>());
		selectExecution();
	}
	void setBackendSession(std::shared_ptr<gradient_kernel::BackendSession> value)
	{
		backendSession = std::move(value);
		selectExecution();
	}
	std::shared_ptr<gradient_kernel::BackendSession> session() const { return backendSession; }
	void resizeWorkspaces()
	{
		workspaces.resize(executor ? executor->threadCount() : 1);
		for (auto &scratch : workspaces)
		{
			scratch.crowding.materials = {};
			scratch.crowding.materials.budget = 64 * 1024 * 1024 / workspaces.size();
		}
	}
	template <class Visitor> void visitPendingSnapshots(Visitor visitor)
	{
		finish({JoinReason::Save, ComputeTelemetry::NoTick});
		for (const auto &job : pending)
			visitor(PendingSnapshot{job->slot, job->data.get(), unsigned(job->due - tick),
                                    job->superseded,job.get()});
	}
	void restoreCompleted(RestoredSnapshot value)
	{
		if (!enabled() || !value.slot || !value.data || !value.remaining ||
			value.remaining > delay || pending.size() >= delay ||
			(!pending.empty() && pending.back()->due >= tick + value.remaining))
			throw std::runtime_error("Invalid saved gradient deadline or destination");
		auto job = std::make_unique<Job>();
		job->slot = value.slot;
		job->swim = value.swim;
		job->due = tick + value.remaining;
		job->superseded = value.superseded;
		job->done = true;
        job->data = std::move(value.data);
        job->payload=std::move(value.payload);
        job->customPublish=std::move(value.customPublish);
        pending.push_back(std::move(job));
	}
	void advance()
	{
		++tick;
		while (!pending.empty() && pending.front()->due <= tick)
		{
			auto &job = *pending.front();
			(this->*waitJob)(job, {JoinReason::TickDeadline, deadline ? deadline(1) / 2 : tick});
			if (job.error)
				std::rethrow_exception(job.error);
			if (!job.superseded && (!validDestination || validDestination(job.slot)))
			{
                if (job.customPublish) job.customPublish(job);
                else {
				auto *old = *job.slot;
				*job.slot = job.data.release();
				job.data.reset(old);
                if (onPublished) onPublished(job.slot);
                }
                ++metrics.published;
            }
            else
				++metrics.discarded;
            job.customWork={}; job.customPublish={}; job.payload.reset();
            spare.push_back(std::move(pending.front()));
			pending.pop_front();
		}
	}
    template<class Visitor> void visitLogical(Visitor visitor) const
    { for (const auto& job:pending) visitor(job->slot, unsigned(job->due-tick), job->superseded); }
    template<class Visitor> void forEachDestination(Visitor visitor) const
    { for (const auto& job:pending) if (!job->superseded) visitor(job->slot); }
	void invalidate(std::uint16_t **slot)
	{
		for (auto &job : pending)
			if (job->slot == slot)
				job->superseded = true;
	}
	Job *reserve(std::uint16_t **slot, int swim)
	{
		if (!enabled() || tick == lastSubmission || pending.size() >= delay)
			throw std::logic_error("gradient pipeline requires one submission per advanced tick");
		std::unique_ptr<Job> job;
		if (spare.empty())
		{
			job = std::make_unique<Job>();
			// Seeding initializes every cell on the worker. Value initialization
			// here would retain a map-sized clearing pass on the owner.
			job->data.reset(new std::uint16_t[cells]);
		}
		else
		{
			job = std::move(spare.back());
			spare.pop_back();
		}
        if (!job->data) job->data=std::make_unique_for_overwrite<std::uint16_t[]>(cells);
        job->customWork={}; job->customPublish={}; job->payload.reset();
		if (callback.invoke == runGPU)
			job->device = std::make_unique<DeviceJob>();
		job->slot = slot;
		job->swim = swim;
		job->due = tick + delay;
		job->batch = {};
		job->done = false;
		job->superseded = false;
		job->error = nullptr;
		job->owner = this;
		auto *ptr = job.get();
		pending.push_back(std::move(job));
		lastSubmission = tick;
		++metrics.jobs;
		metrics.maxPending = std::max<std::uint64_t>(metrics.maxPending, pending.size());
		return ptr;
	}
	template <class Seed> void prepare(Job *job, Seed &&seed)
	{
		try
		{
			job->seed = std::forward<Seed>(seed);
			const auto remaining = unsigned(job->due - tick);
			job->executorDue =
				deadline ? deadline(remaining) : ComputeExecutor::advanceDue(job->due);
			auto fn = callback;
			fn.context = job;
			const ComputeExecutor::Group group{1, fn, ComputeExecutor::NoLane};
			job->batch = executor->submit(std::span(&group, 1), job->executorDue,
										  {ComputeProducer::PeriodicGradient,
										   deadline ? job->executorDue / 2 - remaining + 1 : tick,
										   job->executorDue / 2});
		}
		catch (...)
		{
			releaseInputs(*job);
			job->error = std::current_exception();
			throw;
		}
	}
	template <class Seed> void submit(std::uint16_t **slot, int swim, Seed &&seed)
	{
		prepare(reserve(slot, swim), std::forward<Seed>(seed));
	}
};
