// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/observation/AIWorldView.h"
#include <vector>

namespace GameDiagnostics { struct FieldSink; }

namespace AIEngine
{
struct RequestId
{
	unsigned player = 0;
	Uint32 generation = 0, observedTick = 0;
	Uint64 pollSequence = 0;
	Uint32 actionOrdinal = 0;
	bool operator==(const RequestId&) const = default;
};

enum class ExecutionStatus { Accepted, Rejected, Canceled };
struct ExecutionReceipt
{
	RequestId request;
	ExecutionStatus status = ExecutionStatus::Rejected;
	Uint32 executionTick = 0, scheduledTick = 0;
	std::vector<Uint8> command;
	std::optional<BuildingRef> selectedTarget;
};

struct ResourceEnrollmentRequest
{
	int team = 0, resource = 0, swim = 0;
	Uint32 observedTick = 0;
	std::shared_ptr<const std::vector<Uint16>> initialField;
};

struct DecisionContext
{
	const AIWorldView& world;
	unsigned player;
	unsigned team;
	const std::vector<ExecutionReceipt>& receipts;
	// Invocation lease only. Incremental queries retain named components, never
	// this complete observation. Direct decision tests may omit this owner.
	std::shared_ptr<const AIWorldView> observation;
	Uint32 scheduledTick = 0;
	Uint64 pollSequence = 0;
	std::vector<ResourceEnrollmentRequest>* resourceEnrollments = nullptr;
	std::shared_ptr<GameDiagnostics::FieldSink> fieldDiagnostics;
    Uint32 controllerGeneration = 0;
};
} // namespace AIEngine
