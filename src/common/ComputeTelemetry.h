// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <vector>

enum class ComputeTelemetryMode { Off, Waits, Trace };
enum class ComputeProducer { AI, PeriodicGradient, BuildingGradient, ResourceGrowth, Hiring, Presentation, Other, Mixed, Checksum, Count };
enum class JoinReason { TickDeadline, ImmediateRequired, Save, Reconfigure, Teardown, Verification, Count };
struct BatchDiagnostics {
    ComputeProducer producer = ComputeProducer::Other;
    std::uint64_t observationTick = 0, publicationTick = 0;
};
struct JoinContext {
    JoinReason reason = JoinReason::ImmediateRequired;
    std::uint64_t logicalTick = 0;
};
namespace ComputeTelemetry {
inline constexpr std::uint64_t NoTick = std::numeric_limits<std::uint64_t>::max();
inline constexpr std::size_t DefaultTraceBytes = 16 * 1024 * 1024;
inline constexpr std::array<std::string_view, 3> ModeNames{"off", "waits", "trace"};
inline constexpr std::array<std::string_view, 9> ProducerNames{"ai", "periodic_gradient", "building_gradient", "resource_growth", "hiring", "presentation", "other", "mixed", "checksum"};
inline constexpr std::array<std::string_view, 6> ReasonNames{"tick_deadline", "immediate_required", "save", "reconfigure", "teardown", "verification"};
inline ComputeTelemetryMode parseMode(std::string_view value) {
    for (unsigned i = 0; i < ModeNames.size(); ++i) if (value == ModeNames[i]) return ComputeTelemetryMode(i);
    throw std::invalid_argument("Compute telemetry must be off, waits or trace");
}
inline bool required(JoinReason reason) { return reason == JoinReason::TickDeadline || reason == JoinReason::ImmediateRequired; }
struct Duration {
    std::uint64_t totalNs = 0, maximumNs = 0, samples = 0;
    // Bucket 0 is zero; bucket n>0 covers [2^(n-1), 2^n-1] ns.
    std::array<std::uint64_t, 65> histogram{};
    void add(std::uint64_t ns) noexcept {
        totalNs += ns; maximumNs = std::max(maximumNs, ns); ++samples;
        ++histogram[std::bit_width(ns)];
    }
};
struct JoinSummary {
    std::uint64_t joined = 0, incomplete = 0, backgroundAttempts = 0, ownerExecutions = 0;
    Duration lock, background, owner, total;
};
struct Summary {
    std::array<std::array<JoinSummary, unsigned(JoinReason::Count)>, unsigned(ComputeProducer::Count)> joins{};
    std::array<std::uint64_t, unsigned(ComputeProducer::Count)> affectedTicks{};
    std::uint64_t advancingTicks = 0, backgroundTicks = 0;
    Duration tickBackground;
};
enum class EventKind { Submit, Claim, CallbackStart, CallbackEnd, Suspend, ExternalResolution, Runnable, Complete, JoinInspection, JoinEnd, Retire, Count };
inline constexpr std::array<std::string_view, unsigned(EventKind::Count)> EventNames{"submit", "claim", "callback_start", "callback_end", "suspend", "external_resolution", "runnable", "complete", "join_inspection", "join_end", "retire"};
struct Event {
    std::uint64_t timestampNs = 0, serial = 0, due = 0, laneSequence = 0, logicalTick = NoTick;
    std::uint64_t observationTick = 0, publicationTick = 0;
    std::size_t job = 0;
    unsigned lane = ~0u, thread = 0;
    EventKind kind = EventKind::Submit;
    ComputeProducer producer = ComputeProducer::Other;
    JoinReason reason = JoinReason::ImmediateRequired;
    // Join inspection: 0=complete, 1=worker wait, 2=owner execution.
    unsigned classification = 0;
    // Deferred, blocking fork/join, scene preparation, or graphics batches.
    unsigned executionKind = 0;
};
struct Stream {
    std::vector<Event> storage;
    std::size_t used = 0;
    std::uint64_t lost = 0;
    void append(const Event& event) noexcept {
        if (used < storage.size()) storage[used++] = event; else ++lost;
    }
};
struct Snapshot {
    ComputeTelemetryMode requested = ComputeTelemetryMode::Off, mode = ComputeTelemetryMode::Off;
    bool available = false, traceIncluded = false, traceOpenWork = false, traceCoverageGap = false;
    unsigned threads = 1;
    std::size_t traceBytes = 0;
    Summary summary;
    std::vector<Stream> streams;
};
inline void durationJson(std::ostream& out, const Duration& value) {
    out << "{\"samples\":" << value.samples << ",\"total_ns\":" << value.totalNs << ",\"maximum_ns\":" << value.maximumNs << ",\"histogram\":[";
    for (unsigned i = 0; i < value.histogram.size(); ++i) { if (i) out << ','; out << value.histogram[i]; }
    out << "]}";
}
inline void writeJson(std::ostream& out, const Snapshot& snapshot) {
    out << "{\"schema_version\":1,\"mode\":\"" << ModeNames[unsigned(snapshot.mode)] << "\",\"available\":" << (snapshot.available ? "true" : "false");
    if (snapshot.requested != snapshot.mode) out << ",\"requested_mode\":\"" << ModeNames[unsigned(snapshot.requested)] << '"';
    if (!snapshot.available) { out << '}'; return; }
    out << ",\"threads\":" << snapshot.threads << ",\"worker_execution_available\":" << (snapshot.mode == ComputeTelemetryMode::Trace ? "true" : "false")
        << ",\"advancing_ticks\":" << snapshot.summary.advancingTicks << ",\"background_wait_ticks\":" << snapshot.summary.backgroundTicks << ",\"tick_background_wait\":";
    durationJson(out, snapshot.summary.tickBackground);
    std::uint64_t requiredJoined = 0, requiredIncomplete = 0;
    for (const auto& producer : snapshot.summary.joins) for (unsigned r=0; r<ReasonNames.size(); ++r)
        if (required(JoinReason(r))) { requiredJoined += producer[r].joined; requiredIncomplete += producer[r].incomplete; }
    out << ",\"required_batches_joined\":" << requiredJoined << ",\"required_batches_incomplete\":" << requiredIncomplete;
    out << ",\"producers\":{";
    for (unsigned p = 0; p < ProducerNames.size(); ++p) {
        if (p) out << ',';
        out << '"' << ProducerNames[p] << "\":{\"background_wait_ticks\":" << snapshot.summary.affectedTicks[p] << ",\"joins\":{";
        for (unsigned r = 0; r < ReasonNames.size(); ++r) {
            if (r) out << ',';
            const auto& value = snapshot.summary.joins[p][r];
            out << '"' << ReasonNames[r] << "\":{\"joined\":" << value.joined << ",\"incomplete\":" << value.incomplete
                << ",\"background_wait_attempts\":" << value.backgroundAttempts << ",\"owner_execution_joins\":" << value.ownerExecutions;
            out << ",\"join_lock\":"; durationJson(out, value.lock);
            out << ",\"background_wait\":"; durationJson(out, value.background);
            out << ",\"owner_execution\":"; durationJson(out, value.owner);
            out << ",\"join_total\":"; durationJson(out, value.total); out << '}';
        }
        out << "}}";
    }
    std::uint64_t lost = 0;
    for (const auto& stream : snapshot.streams) lost += stream.lost;
    const bool tracing = snapshot.mode == ComputeTelemetryMode::Trace;
    out << "},\"trace_available\":" << (tracing ? "true" : "false")
        << ",\"trace_included\":" << (snapshot.traceIncluded ? "true" : "false")
        << ",\"trace_incomplete\":";
    if (tracing) out << (lost || snapshot.traceOpenWork || snapshot.traceCoverageGap ? "true" : "false"); else out << "null";
    out << ",\"trace_open_work\":";
    if (tracing) out << (snapshot.traceOpenWork ? "true" : "false"); else out << "null";
    out << ",\"trace_coverage_gap\":";
    if (tracing) out << (snapshot.traceCoverageGap ? "true" : "false"); else out << "null";
    out << ",\"trace_lost_events\":";
    if (tracing) out << lost; else out << "null";
    out << ",\"trace_storage_bytes\":" << snapshot.traceBytes << ",\"trace_streams\":[";
    for (unsigned s = 0; s < snapshot.streams.size(); ++s) {
        if (s) out << ',';
        const auto& stream = snapshot.streams[s];
        out << "{\"stream\":" << s << ",\"lost_events\":" << stream.lost << ",\"events\":[";
        for (std::size_t i = 0; i < stream.used; ++i) {
            if (i) out << ',';
            const auto& e = stream.storage[i];
            out << "{\"event\":\"" << EventNames[unsigned(e.kind)] << "\",\"timestamp_ns\":" << e.timestampNs << ",\"serial\":" << e.serial
                << ",\"due\":" << e.due << ",\"job\":" << e.job << ",\"lane\":" << e.lane << ",\"lane_sequence\":" << e.laneSequence
                << ",\"execution_kind\":" << e.executionKind << ",\"thread\":" << e.thread << ",\"producer\":\"" << ProducerNames[unsigned(e.producer)] << "\",\"observation_tick\":" << e.observationTick
                << ",\"publication_tick\":" << e.publicationTick << ",\"logical_tick\":";
            if (e.logicalTick == NoTick) out << "null"; else out << e.logicalTick;
            out << ",\"join_reason\":\"" << ReasonNames[unsigned(e.reason)] << "\",\"classification\":" << e.classification << '}';
        }
        out << "]}";
    }
    out << "]}";
}
} // namespace ComputeTelemetry
