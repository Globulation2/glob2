// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIOrderScheduler.h"
#include "Order.h"
#include "Stream.h"
#include <BinaryStream.h>
#include "Version.h"
#include "GameDiagnostics.h"
#include <algorithm>
#include <limits>
#include <chrono>
#include <set>

namespace AIEngine
{
namespace
{
std::optional<Uint16> targetOf(Order& order)
{
	switch (order.getOrderType())
	{
		case ORDER_DELETE: return static_cast<OrderDelete&>(order).gid;
		case ORDER_CANCEL_DELETE: return static_cast<OrderCancelDelete&>(order).gid;
		case ORDER_CONSTRUCTION: return static_cast<OrderConstruction&>(order).gid;
		case ORDER_CANCEL_CONSTRUCTION: return static_cast<OrderCancelConstruction&>(order).gid;
		case ORDER_CHANGE_PRIORITY: return static_cast<OrderChangePriority&>(order).gid;
		case ORDER_MODIFY_BUILDING: return static_cast<OrderModifyBuilding&>(order).gid;
		case ORDER_MODIFY_EXCHANGE: return static_cast<OrderModifyExchange&>(order).gid;
		case ORDER_MODIFY_SWARM: return static_cast<OrderModifySwarm&>(order).gid;
		case ORDER_MODIFY_FLAG: return static_cast<OrderModifyFlag&>(order).gid;
		case ORDER_MODIFY_CLEARING_FLAG: return static_cast<OrderModifyClearingFlag&>(order).gid;
		case ORDER_MODIFY_MIN_LEVEL_TO_FLAG: return static_cast<OrderModifyMinLevelToFlag&>(order).gid;
		case ORDER_MOVE_FLAG: return static_cast<OrderMoveFlag&>(order).gid;
		default: return {};
	}
}
}
Command Command::capture(Order& order, const AIWorldView& world)
{
	Command command;
	command.bytes.push_back(order.getOrderType());
	if (order.getDataLength())
	{
		const auto* data = order.getData();
		command.bytes.insert(command.bytes.end(), data, data + order.getDataLength());
	}
	if (order.aiSelectedTarget) command.target = order.aiSelectedTarget;
	else if (const auto gid = targetOf(order))
	{
		const auto* building = world.buildingAtSlot(*gid);
		// A missing target must remain invalid even if the slot is filled later.
		command.target = building ? building->identity : BuildingRef{*gid, 0};
	}
	return command;
}
std::optional<Uint16> Command::targetGid(Order& order) { return targetOf(order); }
std::shared_ptr<Order> Command::decode() const
{
	if (bytes.empty()) throw std::runtime_error("Empty AI command");
	auto order = Order::getOrder(bytes.data(), bytes.size(), VERSION_MINOR);
	if (!order) throw std::runtime_error("Invalid AI command");
	return order;
}

Command& OrderScheduler::complete(Pending& entry)
{
	if (entry.failure) std::rethrow_exception(entry.failure);
	if (!entry.completed) throw std::logic_error("AI decision was never executed");
	return *entry.completed;
}
void OrderScheduler::Pending::run(void* context, std::size_t)
{
	auto& entry = *static_cast<Pending*>(context);
	const auto start = std::chrono::steady_clock::now();
	try { entry.completed = entry.decide(*entry.world); }
	catch (...) { entry.failure = std::current_exception(); }
	entry.computationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
	entry.owner->computationNs.fetch_add(entry.computationNs, std::memory_order_relaxed);
	// The observation lease and the decision closure end with the computation.
	entry.world.reset(); entry.decide = nullptr;
}
OrderScheduler::~OrderScheduler()
{
	try { joinAll(); } catch (...) {}
}
bool OrderScheduler::liveWork() const
{
	for (const auto& batch : batches) for (const auto& entry : batch.entries) if (!entry.completed && !entry.failure) return true;
	return false;
}
void OrderScheduler::configure(unsigned delayTicks, ComputeExecutor& target, bool sharedExecution)
{
	if (delayTicks > 8) throw std::invalid_argument("AI order delay must be 0..8 ticks");
	if (liveWork()) throw std::logic_error("Cannot reconfigure a live AI pipeline");
	executor = &target; shared = sharedExecution;
	delay = delayTicks;
}
void OrderScheduler::configureExecution(ComputeExecutor& target, bool sharedExecution)
{
	// Placement changes only between batches: finish what the previous executor
	// configuration was given before switching.
	joinAll();
	executor = &target; shared = sharedExecution;
}
std::size_t OrderScheduler::pendingCount() const
{
	std::size_t count = 0;
	for (const auto& batch : batches) count += batch.entries.size();
	return count;
}
OrderScheduler::TickBatch& OrderScheduler::batchFor(Uint32 observedTick)
{
	if (!batches.empty() && batches.back().observedTick == observedTick)
	{
		if (batches.back().dispatched) throw std::logic_error("AI submission follows its tick's dispatch");
		return batches.back();
	}
	dispatch();
	auto& batch = batches.emplace_back();
	batch.observedTick = observedTick; batch.dueTick = observedTick + delay;
	return batch;
}
void OrderScheduler::dispatch()
{
	if (batches.empty() || batches.back().dispatched) return;
	auto& batch = batches.back();
	batch.dispatched = true;
	if (batch.entries.empty()) return;
	if (!executor) throw std::logic_error("AI scheduler has no executor");
	batch.groups.clear();
	for (auto& entry : batch.entries) batch.groups.push_back({1, {&Pending::run, &entry}, entry.request.player});
	const bool worthSharing = delay > 0 || (batch.entries.size() > 1 && recentWorkNs >= SharedWorkThresholdNs);
	const bool sharedBatch = shared && worthSharing;
	if (sharedBatch) ++metrics.sharedBatches;
	batch.batch = executor->submit(batch.groups, sharedBatch ? ComputeExecutor::Placement::Shared : ComputeExecutor::Placement::OwnerOnly);
}
void OrderScheduler::joinBatch(TickBatch& batch)
{
	if (!batch.dispatched) dispatch();
	if (executor) executor->join(batch.batch);
	batch.batch = {};
}
void OrderScheduler::joinAll()
{
	dispatch();
	for (auto& batch : batches) joinBatch(batch);
}
void OrderScheduler::submit(RequestId request, std::shared_ptr<const AIWorldView> world, Decide decide)
{
	if (!world || world->tick != request.observedTick || !decide)
		throw std::invalid_argument("AI request needs its captured observation tick");
	if (request.observedTick > std::numeric_limits<Uint32>::max() - delay)
		throw std::overflow_error("AI order deadline overflow");
	if (submissionTick && request.observedTick < *submissionTick)
		throw std::logic_error("AI observation ticks must be submitted in order");
	if (deliveryTick && request.observedTick <= *deliveryTick)
		throw std::logic_error("AI submission follows its delivery phase");
	const auto previous = lastSubmitted.find(request.player);
	if (previous != lastSubmitted.end() && previous->second.generation == request.generation
		&& request.observedTick <= previous->second.observedTick)
		throw std::logic_error("AI controller already polled for this tick");
	if (!batches.empty() && request.observedTick - batches.front().observedTick > delay)
		throw std::logic_error("AI pipeline exceeded its delay window");
	if (request.player >= ComputeExecutor::Lanes) throw std::invalid_argument("AI controller index exceeds 31");
	std::size_t controllerPending = 0;
	for (const auto& batch : batches) for (const auto& entry : batch.entries) controllerPending += entry.request.player == request.player;
	if (controllerPending >= delay + 1) throw std::logic_error("AI controller admission exceeds its deadline horizon");
	if (!executor) throw std::logic_error("AI scheduler has no executor");
	const auto [controller, inserted] = lastSubmitted.try_emplace(request.player, request);
	try
	{
		auto& batch = batchFor(request.observedTick);
		batch.entries.push_back({request, request.observedTick + delay, std::move(world), std::move(decide), {}, {}, 0, this});
	}
	catch (...) { if (inserted) lastSubmitted.erase(controller); throw; }
	controller->second = request;
	submissionTick = request.observedTick;
	++metrics.submitted; metrics.maximumPending = std::max<Uint64>(metrics.maximumPending, pendingCount());
}
std::vector<Delivery> OrderScheduler::takeDue(Uint32 tick)
{
	dispatch();
	if (deliveryTick && tick <= *deliveryTick)
		throw std::logic_error("AI delivery ticks must advance");
	std::vector<Delivery> deliveries;
	if (!batches.empty())
	{
		auto& front = batches.front();
		if (front.dueTick < tick) throw std::logic_error("AI order deadline was skipped");
		if (front.dueTick == tick)
		{
			const auto start = std::chrono::steady_clock::now();
			// At delay zero the owner always joins its own tick's batch; a miss
			// is a delayed batch that outlived its horizon.
			const bool missed = delay && executor && !executor->finished(front.batch);
			joinBatch(front);
			if (missed) { ++metrics.deadlineMisses; metrics.deadlineWaitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(); }
			Uint64 work = 0;
			for (const auto& entry : front.entries) work += entry.computationNs;
			recentWorkNs = (recentWorkNs * 3 + work) / 4;
			// The whole batch finished; a failure anywhere publishes nothing.
			for (auto& entry : front.entries) complete(entry);
			for (auto& entry : front.entries) deliveries.push_back({entry.request, entry.dueTick, std::move(*entry.completed)});
			std::stable_sort(deliveries.begin(), deliveries.end(), [](const auto& a, const auto& b) {
				return a.request.player < b.request.player;
			});
			batches.pop_front();
		}
	}
	deliveryTick = tick;
	metrics.delivered += deliveries.size();
	return deliveries;
}
void OrderScheduler::drain()
{
	joinAll();
	for (auto& batch : batches) for (auto& entry : batch.entries) complete(entry);
}
void OrderScheduler::adoptDiagnostics(GameDiagnostics::Session& session)
{
	drain();
	for (auto& batch : batches) for (auto& entry : batch.entries) {
		auto& output = complete(entry);
		if (output.fieldDiagnostics && !session.adoptCapture(*output.fieldDiagnostics))
			output.fieldDiagnostics.reset();
	}
}
std::vector<Delivery> OrderScheduler::cancel(unsigned player, Uint32 generation)
{
	// Join before removing entries: controller state must not be destroyed while
	// its decisions are still running. Other controllers' deadlines do not move.
	joinAll();
	std::vector<Delivery> canceled;
	for (auto& batch : batches)
	{
		for (auto& entry : batch.entries) if (entry.request.player == player && entry.request.generation == generation)
			canceled.push_back({entry.request, entry.dueTick, std::move(complete(entry))});
		std::erase_if(batch.entries, [&](const auto& entry) { return entry.request.player == player && entry.request.generation == generation; });
	}
	std::erase_if(batches, [](const auto& batch) { return batch.entries.empty(); });
	const auto found = lastSubmitted.find(player);
	if (found != lastSubmitted.end() && found->second.generation == generation) lastSubmitted.erase(found);
	return canceled;
}
void OrderScheduler::clear()
{
	joinAll();
	batches.clear(); lastSubmitted.clear();
	submissionTick.reset(); deliveryTick.reset();
}
void OrderScheduler::save(GAGCore::OutputStream* stream)
{
	drain();
	stream->writeEnterSection("AIOrderScheduler");
	stream->writeUint32(delay, "delay");
	stream->writeUint8(submissionTick.has_value(), "hasSubmission");
	if (submissionTick) stream->writeUint32(*submissionTick, "submissionTick");
	stream->writeUint8(deliveryTick.has_value(), "hasDelivery");
	if (deliveryTick) stream->writeUint32(*deliveryTick, "deliveryTick");
	stream->writeUint32(lastSubmitted.size(), "controllers");
	unsigned controllerIndex = 0;
	for (const auto& [player, request] : lastSubmitted)
	{
		stream->writeEnterSection(controllerIndex++);
		stream->writeUint32(player, "player");
		stream->writeUint32(request.generation, "generation");
		stream->writeUint32(request.observedTick, "tick");
		stream->writeUint32(Uint32(request.pollSequence), "sequenceLow");
		stream->writeUint32(Uint32(request.pollSequence >> 32), "sequenceHigh");
		stream->writeUint32(request.actionOrdinal, "ordinal");
		stream->writeLeaveSection();
	}
	stream->writeUint32(pendingCount(), "pending");
	unsigned i = 0;
	for (const auto& batch : batches) for (const auto& entry : batch.entries)
	{
		const auto& command = *entry.completed;
		stream->writeEnterSection(i++);
		stream->writeUint32(entry.request.player, "player");
		stream->writeUint32(entry.request.generation, "generation");
		stream->writeUint32(entry.request.observedTick, "observedTick");
		stream->writeUint32(entry.dueTick, "dueTick");
		stream->writeUint32(Uint32(entry.request.pollSequence), "sequenceLow");
		stream->writeUint32(Uint32(entry.request.pollSequence >> 32), "sequenceHigh");
		stream->writeUint32(entry.request.actionOrdinal, "ordinal");
		stream->writeUint32(command.bytes.size(), "bytes");
		stream->write(command.bytes.data(), command.bytes.size(), "command");
		stream->writeUint8(command.target.has_value(), "hasTarget");
		if (command.target)
		{
			stream->writeUint16(command.target->gid, "gid");
			stream->writeUint32(command.target->generation, "identity");
		}
		stream->writeUint8(command.telemetry.has_value(), "hasTelemetry");
		if (command.telemetry) {
			stream->writeUint32(command.telemetry->tick, "telemetryTick");
			stream->writeUint8(command.telemetry->available, "telemetryAvailable");
			stream->writeUint32(command.telemetry->values.size(), "telemetryValues");
			for (unsigned j = 0; j < command.telemetry->values.size(); ++j) {
				const auto& value = command.telemetry->values[j]; stream->writeEnterSection(j);
				stream->writeUint32(Uint32(value.bits), "low"); stream->writeUint32(Uint32(value.bits >> 32), "high");
				stream->writeUint32(value.updated, "updated"); stream->writeUint8(value.valid, "valid"); stream->writeLeaveSection();
			}
		}
		stream->writeUint32(command.namedTelemetry.size(), "namedTelemetry");
		for (unsigned j = 0; j < command.namedTelemetry.size(); ++j) {
			const auto& named = command.namedTelemetry[j]; stream->writeEnterSection(j);
			stream->writeText(named.name, "name"); stream->writeText(named.value, "value");
			stream->writeText(named.unit, "unit"); stream->writeText(named.meaning, "meaning");
			stream->writeUint32(named.updated, "updated"); stream->writeLeaveSection();
		}
		stream->writeUint8(bool(command.fieldDiagnostics), "hasFieldDiagnostics");
		if (command.fieldDiagnostics) command.fieldDiagnostics->save(stream);
		stream->writeUint32(command.diagnostics.size(), "diagnostics");
		for (unsigned j = 0; j < command.diagnostics.size(); ++j) {
			stream->writeEnterSection(j);
			stream->writeText(command.diagnostics[j].path, "path"); stream->writeText(command.diagnostics[j].header, "header"); stream->writeText(command.diagnostics[j].text, "text");
			stream->writeUint8(command.diagnostics[j].standardOutput, "stdout");
			stream->writeLeaveSection();
		}
		stream->writeUint32(command.resourceEnrollments.size(), "enrollments");
		for (unsigned j = 0; j < command.resourceEnrollments.size(); ++j)
		{
			const auto& enrollment = command.resourceEnrollments[j];
			stream->writeEnterSection(j);
			stream->writeUint32(enrollment.team, "team"); stream->writeUint32(enrollment.resource, "resource");
			stream->writeUint32(enrollment.swim, "swim"); stream->writeUint32(enrollment.observedTick, "tick");
			if (!enrollment.initialField) throw std::logic_error("Missing saved resource enrollment field");
			stream->writeUint32(enrollment.initialField->size(), "cells");
			stream->writeUint16Sections(enrollment.initialField->data(), enrollment.initialField->size(), "field");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
bool OrderScheduler::load(GAGCore::InputStream* stream)
{
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	clear();
	struct Rollback { OrderScheduler& owner; bool success = false; ~Rollback() { if (!success) { owner.clear(); owner.delay = 0; } } } rollback{*this};
	stream->readEnterSection("AIOrderScheduler");
	delay = stream->readUint32("delay");
	if (delay > 8) return false;
	const auto hasSubmission = stream->readUint8("hasSubmission");
	if (hasSubmission > 1) return false;
	if (hasSubmission) submissionTick = stream->readUint32("submissionTick");
	const auto hasDelivery = stream->readUint8("hasDelivery");
	if (hasDelivery > 1) return false;
	if (hasDelivery) deliveryTick = stream->readUint32("deliveryTick");
	const auto controllers = stream->readCount("controllers", 32);
	for (unsigned i = 0; i < controllers; ++i)
	{
		// Section labels are not encoded in binary streams. Text uses sequential
		// indices, matching save, rather than assuming contiguous player numbers.
		stream->readEnterSection(i);
		RequestId request;
		request.player = stream->readUint32("player");
		request.generation = stream->readUint32("generation");
		request.observedTick = stream->readUint32("tick");
		const auto low = stream->readUint32("sequenceLow");
		request.pollSequence = low | (Uint64(stream->readUint32("sequenceHigh")) << 32);
		request.actionOrdinal = stream->readUint32("ordinal");
		if (request.player >= 32 || !request.generation || !submissionTick || request.observedTick > *submissionTick
			|| !lastSubmitted.emplace(request.player, request).second) return false;
		stream->readLeaveSection();
	}
	std::array<std::optional<RequestId>, 32> previousByPlayer;
	std::array<unsigned, 32> pendingByPlayer{};
	const auto count = stream->readCount("pending", 32 * (delay + 1));
    // Several pending polls may carry the same first-observation plane until
    // publication. Restore that shared allocation instead of duplicating it.
    using PlaneKey=std::tuple<unsigned,unsigned,unsigned,Uint32,unsigned>;
    std::map<PlaneKey,std::shared_ptr<const std::vector<Uint16>>> loadedPlanes;
    // Presentation output is optional and shares the established capture budget.
    // Consume oversized output without retaining it; simulation data is intact.
    size_t presentationBytes=0;
    const auto retainPresentation=[&](size_t bytes) {
        if(bytes>GameDiagnostics::CaptureBudget-presentationBytes) return false;
        presentationBytes+=bytes;return true;
    };
	for (unsigned i = 0; i < count; ++i)
	{
		stream->readEnterSection(i);
		Pending entry;
		entry.request.player = stream->readUint32("player");
		entry.request.generation = stream->readUint32("generation");
		entry.request.observedTick = stream->readUint32("observedTick");
		entry.dueTick = stream->readUint32("dueTick");
		const auto low = stream->readUint32("sequenceLow");
		entry.request.pollSequence = low | (Uint64(stream->readUint32("sequenceHigh")) << 32);
		entry.request.actionOrdinal = stream->readUint32("ordinal");
		Command command;
		const auto length = stream->readCount("bytes", 16 * 1024 * 1024);
		if (!length) return false;
		command.bytes.resize(length);
		stream->read(command.bytes.data(), length, "command");
		const auto hasTarget = stream->readUint8("hasTarget");
		if (hasTarget > 1) return false;
		if (hasTarget) command.target = BuildingRef{stream->readUint16("gid"), stream->readUint32("identity")};
		const auto hasTelemetry = stream->readUint8("hasTelemetry");
		if (hasTelemetry > 1) return false;
		if (hasTelemetry) {
			command.telemetry.emplace(); command.telemetry->tick = stream->readUint32("telemetryTick");
			command.telemetry->available = stream->readUint8("telemetryAvailable");
			const auto values = stream->readCount("telemetryValues", AITelemetry::MaximumPresentationValues);
			for (unsigned j = 0; j < values; ++j) {
				stream->readEnterSection(j); const auto low = stream->readUint32("low");
				const Uint64 bits = low | (Uint64(stream->readUint32("high")) << 32);
				const auto updated = stream->readUint32("updated"); const auto valid = stream->readUint8("valid");
				if (valid > 1) return false; command.telemetry->values.push_back({bits, updated, bool(valid)}); stream->readLeaveSection();
			}
		}
		const auto namedCount = stream->readCount("namedTelemetry", 4096);
		for (unsigned j = 0; j < namedCount; ++j) {
			stream->readEnterSection(j);
            decltype(command.namedTelemetry)::value_type named{stream->readText("name"), stream->readText("value"), stream->readText("unit"), stream->readText("meaning"), stream->readUint32("updated")};
            if(retainPresentation(named.name.capacity()+named.value.capacity()+named.unit.capacity()+named.meaning.capacity()+sizeof(named)))
                command.namedTelemetry.push_back(std::move(named));
			stream->readLeaveSection();
		}
		const auto hasFields = stream->readUint8("hasFieldDiagnostics");
		if (hasFields > 1) return false;
		if (hasFields) {
			auto fields = std::make_shared<GameDiagnostics::FieldSink>();
			if (!fields->load(stream)) return false;
            size_t bytes=sizeof(GameDiagnostics::FieldSink);
            for(const auto& field:fields->fields) bytes+=field.values.capacity()*sizeof(std::int64_t);
            if(retainPresentation(bytes)) command.fieldDiagnostics=std::move(fields);
		}
		const auto diagnosticCount = stream->readCount("diagnostics", 65536);
		for (unsigned j = 0; j < diagnosticCount; ++j) {
			stream->readEnterSection(j);
            DiagnosticRecord diagnostic{stream->readText("path"),stream->readText("header"),stream->readText("text")};
            const auto standardOutput=stream->readUint8("stdout");
            if(standardOutput>1 || (standardOutput && !diagnostic.path.empty()))return false;
            diagnostic.standardOutput=standardOutput;
            if(retainPresentation(diagnostic.path.capacity()+diagnostic.header.capacity()+diagnostic.text.capacity()+sizeof(diagnostic)))
                command.diagnostics.push_back(std::move(diagnostic));
			stream->readLeaveSection();
		}
		const auto enrollmentCount = stream->readCount("enrollments", 32 * MaterialSlotCount * 7);
        std::set<SimulationSnapshot::ResourceFieldKey> enrollmentKeys;
		for (unsigned j = 0; j < enrollmentCount; ++j)
		{
			stream->readEnterSection(j);
			ResourceEnrollmentRequest enrollment;
			enrollment.team = stream->readUint32("team"); enrollment.resource = stream->readUint32("resource");
			enrollment.swim = stream->readUint32("swim"); enrollment.observedTick = stream->readUint32("tick");
			const auto cells = stream->readCount("cells", 1024 * 1024);
			if (enrollment.team >= 32 || enrollment.resource >= MaterialSlotCount || enrollment.swim >= 7 || !cells || enrollment.observedTick > entry.request.observedTick
                || !enrollmentKeys.emplace(enrollment.team,enrollment.resource,enrollment.swim,false).second) return false;
            const PlaneKey planeKey{enrollment.team,enrollment.resource,enrollment.swim,enrollment.observedTick,cells};
            const auto existing=loadedPlanes.find(planeKey);
            if(existing!=loadedPlanes.end()) {
                for(unsigned cell=0;cell<cells;++cell) {
                    stream->readEnterSection(cell);const auto value=stream->readUint16("field");stream->readLeaveSection();
                    if(value!=(*existing->second)[cell]) return false;
                }
                enrollment.initialField=existing->second;
            } else {
                auto field=std::make_shared<std::vector<Uint16>>(cells);
                for(unsigned cell=0;cell<cells;++cell) {stream->readEnterSection(cell);(*field)[cell]=stream->readUint16("field");stream->readLeaveSection();}
                enrollment.initialField=std::move(field);
                loadedPlanes.emplace(planeKey,enrollment.initialField);
            }
			command.resourceEnrollments.push_back(std::move(enrollment));
			stream->readLeaveSection();
		}
		const auto controller = lastSubmitted.find(entry.request.player);
		if (controller == lastSubmitted.end() || controller->second.generation != entry.request.generation
			|| entry.request.observedTick > controller->second.observedTick
			|| entry.request.pollSequence > controller->second.pollSequence
			|| (entry.request.observedTick == controller->second.observedTick && entry.request.pollSequence != controller->second.pollSequence)
			|| !submissionTick || *submissionTick - entry.request.observedTick > delay
			|| entry.request.observedTick > std::numeric_limits<Uint32>::max() - delay
			|| entry.dueTick != entry.request.observedTick + delay
			|| (deliveryTick && entry.dueTick <= *deliveryTick)
			|| (!batches.empty() && entry.request.observedTick < batches.back().observedTick)) return false;
		const auto& previous = previousByPlayer[entry.request.player];
		if (++pendingByPlayer[entry.request.player] > delay + 1
			|| (previous && (entry.request.observedTick <= previous->observedTick
				|| entry.request.pollSequence <= previous->pollSequence))) return false;
		previousByPlayer[entry.request.player] = entry.request;
		try {
			auto decoded = command.decode();
			const auto gid = targetOf(*decoded);
			if (gid.has_value() != command.target.has_value()
				|| (gid && *gid != command.target->gid)) return false;
		} catch (const std::exception&) { return false; }
		entry.completed = std::move(command);
		entry.owner = this;
		// Restored decisions are complete: their batch has nothing left to run.
		if (batches.empty() || batches.back().observedTick != entry.request.observedTick)
		{
			auto& batch = batches.emplace_back();
			batch.observedTick = entry.request.observedTick; batch.dueTick = entry.dueTick; batch.dispatched = true;
		}
		batches.back().entries.push_back(std::move(entry));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	rollback.success = true;
	return true;
}
bool OrderScheduler::validateRestoredState(unsigned players, Uint32 tick,
	const std::array<std::pair<Uint32, Uint64>, 32>& actors, int width, int height, int teams) const
{
	if (players > actors.size() || width <= 0 || height <= 0 || teams <= 0 || teams > 32
		|| (submissionTick && *submissionTick > tick) || (deliveryTick && *deliveryTick > tick)) return false;
	const auto validRequest = [&](const RequestId& request) {
		return request.player < players && request.generation == actors[request.player].first
			&& request.pollSequence < actors[request.player].second && request.observedTick <= tick;
	};
	for (const auto& [player, request] : lastSubmitted) if (!validRequest(request)) return false;
	const auto cells = std::size_t(width) * std::size_t(height);
	for (const auto& batch : batches) for (const auto& entry : batch.entries) {
		if (!validRequest(entry.request) || entry.dueTick < tick || !entry.completed) return false;
		for (const auto& enrollment : entry.completed->resourceEnrollments)
			if (enrollment.team >= unsigned(teams) || !enrollment.initialField
				|| enrollment.initialField->size() != cells) return false;
	}
	return true;
}

} // namespace AIEngine
