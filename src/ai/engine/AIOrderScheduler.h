// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "AIExecutor.h"
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
	struct Pending
	{
		RequestId request;
		Uint32 dueTick;
		std::future<Command> work;
		std::optional<Command> completed;
		std::exception_ptr failure;
	};
	Executor executor;
	std::deque<Pending> pending;
	std::map<unsigned, RequestId> lastSubmitted;
	unsigned delay = 0;
	std::optional<Uint32> submissionTick;
	std::optional<Uint32> deliveryTick;
	static Command& complete(Pending& entry);

public:
	struct Metrics { Uint64 submitted = 0, delivered = 0, deadlineMisses = 0, deadlineWaitNs = 0, maximumPending = 0; } metrics;
	using Decide = std::function<Command(const AIWorldView&)>;
	OrderScheduler() = default;
	OrderScheduler(const OrderScheduler&) = delete;
	~OrderScheduler() { executor.drain(); }
	void configure(unsigned delayTicks, unsigned workers);
	void configureWorkers(unsigned workers) { executor.configure(workers); }
	unsigned delayTicks() const { return delay; }
	unsigned workerCount() const { return executor.workerCount(); }
	Uint64 activeNs() const { return executor.activeNs(); }
	std::size_t pendingCount() const { return pending.size(); }
	bool wasSubmitted(unsigned player, Uint32 generation, Uint32 tick) const {
		const auto it=lastSubmitted.find(player);
		return it!=lastSubmitted.end() && it->second.generation==generation && it->second.observedTick==tick;
	}
	void submit(RequestId request, std::shared_ptr<const AIWorldView> world, Decide decide);
	// Wait only when a command's logical deadline arrives. Finish the whole
	// batch before returning any outputs; errors cannot partially publish it.
	std::vector<Delivery> takeDue(Uint32 tick);
	// Lifecycle barriers finish private work without advancing deadlines.
	void drain();
	void adoptDiagnostics(GameDiagnostics::Session& session);
	std::vector<Delivery> cancel(unsigned player, Uint32 generation);
	void clear();
	void save(GAGCore::OutputStream* stream);
	bool load(GAGCore::InputStream* stream);
	bool validateRestoredState(unsigned players, Uint32 tick,
		const std::array<std::pair<Uint32, Uint64>, 32>& actors, int width, int height, int teams) const;
};
} // namespace AIEngine
