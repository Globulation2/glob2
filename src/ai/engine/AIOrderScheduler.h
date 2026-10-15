// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ComputeExecutor.h"
#include <atomic>
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "AIDecision.h"
#include "AIDiagnostics.h"
#include "AITelemetry.h"
#include <cstdint>
#include <optional>

namespace GAGCore { class InputStream; class OutputStream; }
namespace GameDiagnostics { class Session; }
class Order;

namespace AIEngine
{
// Owned wire data cannot alias a controller's internal order queue. Target
// identity travels separately until the simulation owner admits the command.
struct Command
{
	std::vector<Uint8> bytes;
	std::optional<BuildingRef> target;
	std::vector<ResourceEnrollmentRequest> resourceEnrollments;
	std::vector<DiagnosticRecord> diagnostics;
	std::optional<AITelemetry::Sample> telemetry;
	std::vector<AITelemetry::NamedValue> namedTelemetry;
	std::shared_ptr<const GameDiagnostics::FieldSink> fieldDiagnostics;
	std::optional<Uint64> retainedQueryVectorBytes;
	static Command capture(Order& order, const AIWorldView& world);
	static std::optional<Uint16> targetGid(Order& order);
	std::shared_ptr<Order> decode() const;
};

struct Delivery
{
	RequestId request;
	Uint32 dueTick = 0;
	Command command;
};

class OrderScheduler
{
public:
	using Decide = std::function<Command(const AIWorldView&)>;
private:
	// One controller decision admitted at an observation tick. The executor
	// runs it in place; the observation lease is released as soon as it ran.
	struct Pending
	{
		RequestId request;
		Uint32 dueTick = 0;
		std::shared_ptr<const AIWorldView> world;
		Decide decide;
		std::optional<Command> completed;
		std::exception_ptr failure;
		static void run(void* context, std::size_t);
	};
	// The decisions admitted at one observation tick, dispatched to the executor
	// as one batch (one group per controller on that controller's lane) once the
	// tick's polls are all admitted, and joined at its deadline.
	struct TickBatch
	{
		Uint32 observedTick = 0, dueTick = 0;
		std::vector<Pending> entries;
		std::vector<ComputeExecutor::Group> groups;
		ComputeExecutor::Batch batch;
		bool dispatched = false;
	};
	std::deque<TickBatch> batches;
	ComputeExecutor* executor = nullptr;
	std::map<unsigned, RequestId> lastSubmitted;
	unsigned delay = 0;
	std::optional<Uint32> submissionTick;
	std::optional<Uint32> deliveryTick;
	static Command& complete(Pending& entry);
	TickBatch& batchFor(Uint32 observedTick);
	void joinBatch(TickBatch& batch, JoinContext context);
	void joinAll(JoinContext context = {JoinReason::Teardown, ComputeTelemetry::NoTick});
	bool liveWork() const;

public:
	struct Metrics { Uint64 submitted = 0, delivered = 0, deadlineMisses = 0, deadlineWaitNs = 0, maximumPending = 0, sharedBatches = 0; } metrics;
	OrderScheduler() = default;
	OrderScheduler(const OrderScheduler&) = delete;
	// Joins every admitted decision; the executor must outlive the scheduler.
	~OrderScheduler();
	// Shared execution hands batches to the executor's workers and the
	// submitter waits for them at the deadline; otherwise the submitter decides
	// when it dispatches. Either way decisions are delivered at the deadline.
	void configure(unsigned delayTicks, ComputeExecutor& executor);
	void configureExecution(ComputeExecutor& executor);
	unsigned delayTicks() const { return delay; }
	// Configuration zero now means delivery at the next completed observation.
	unsigned effectiveDelay() const { return std::max(1u, delay); }
	bool wasDelivered(Uint32 tick) const { return deliveryTick && *deliveryTick == tick; }
	bool validDeadline(Uint32 observed, Uint32 due) const {
		return due >= observed && (due - observed == delay || (!delay && due - observed == 1));
	}
	bool hasExecutor() const { return executor != nullptr; }
	std::size_t pendingCount() const;
	bool wasSubmitted(unsigned player, Uint32 generation, Uint32 tick) const {
		const auto it=lastSubmitted.find(player);
		return it!=lastSubmitted.end() && it->second.generation==generation && it->second.observedTick==tick;
	}
	void submit(RequestId request, std::shared_ptr<const AIWorldView> world, Decide decide);
	// Hand the newest tick's admitted decisions to the executor. Every other
	// operation dispatches first, so calling this early only starts work sooner.
	void dispatch();
	// Wait only when a command's logical deadline arrives. Finish the whole
	// batch before returning any outputs; errors cannot partially publish it.
	std::vector<Delivery> takeDue(Uint32 tick);
	// Lifecycle barriers finish private work without advancing deadlines.
	void drain(JoinContext context = {JoinReason::Verification, ComputeTelemetry::NoTick});
	void adoptDiagnostics(GameDiagnostics::Session& session);
	std::vector<Delivery> cancel(unsigned player, Uint32 generation);
	void clear();
	void save(GAGCore::OutputStream* stream);
	bool load(GAGCore::InputStream* stream);
	bool validateRestoredState(unsigned players, Uint32 tick,
		const std::array<std::pair<Uint32, Uint64>, 32>& actors, int width, int height, int teams) const;
};
} // namespace AIEngine
