// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyResourceGradientState.h"
#include "FileFormatVersions.h"
#include "UnitConsts.h"
#include <Stream.h>
#include <stdexcept>

namespace {
std::uint64_t read64(GAGCore::InputStream* stream,const char* name)
{
    stream->readEnterSection(name);
    const std::uint64_t high=stream->readUint32("high"),low=stream->readUint32("low");
    stream->readLeaveSection();
    return (high<<32)|low;
}
LegacyResourceGradientState::Sources loadSources(GAGCore::InputStream* stream)
{
    return {read64(stream,"count"),read64(stream,"added"),read64(stream,"removed")};
}
bool loadMovement(GAGCore::InputStream* stream,LegacyResourceGradientState::MovementCounts& counts)
{
    constexpr std::array<unsigned,3> selectedIds{0,4,5};
    constexpr unsigned requiredMask=(1u<<0)|(1u<<4)|(1u<<5);
    const unsigned mask=stream->readUint8("counterMask");
    if (mask&~63u) throw std::runtime_error("Invalid contradiction counter mask");
    for (unsigned id=0;id<6;++id) if (mask&(1u<<id)) {
        stream->readEnterSection(id);
        const auto value=read64(stream,"count");
        stream->readLeaveSection();
        for (unsigned i=0;i<selectedIds.size();++i)
            if (selectedIds[i]==id) counts[i]=value;
    }
    return (mask&requiredMask)==requiredMask;
}
}

LegacyResourceGradientState::Capture LegacyResourceGradientState::Capture::load(GAGCore::InputStream* stream,unsigned version)
{
    stream->readEnterSection("capture");
    Capture capture;
    capture.tick=stream->readUint32("tick");
    capture.sources=loadSources(stream);
    if (version>=FILE_FORMAT_VERSION_GRADIENT_CONTRADICTIONS) {
        const auto known=stream->readUint8("contradictionKnown");
        if (known>1) throw std::runtime_error("Invalid contradiction capture flag");
        const bool complete=loadMovement(stream,capture.movementBaseline);
        capture.contradictionKnown=known && complete;
    }
    stream->readLeaveSection();
    return capture;
}

bool LegacyResourceGradientState::Field::validMovementCapture(const Capture& capture) const
{
    if (!capture.contradictionKnown) return true;
    for (unsigned i=0;i<movementTotal.size();++i)
        if (capture.movementBaseline[i]>movementTotal[i]) return false;
    const auto queries=movementTotal[2]-capture.movementBaseline[2];
    for (unsigned i=0;i<movementTotal.size();++i)
        if (movementTotal[i]-capture.movementBaseline[i]>queries) return false;
    return true;
}

void LegacyResourceGradientState::load(GAGCore::InputStream* stream,unsigned teams,unsigned version)
{
    *this={};
    stream->readEnterSection("adaptiveResources");
    const auto flag=stream->readUint8("initialized");
    if (flag>1) throw std::runtime_error("Invalid resource policy initialization");
    initialized=flag;
    stream->readEnterSection("sources");
    for (unsigned material=0;material<MaterialCount;++material) {
        stream->readEnterSection(material);
        sources[material]=loadSources(stream);
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
    const unsigned count=stream->readUint16("fieldCount"),limit=teams*MaterialCount*SWIM_CLASS_COUNT;
    stream->readEnterSection("fields");
    if (count>limit || (!initialized && count)) throw std::runtime_error("Invalid resource policy field count");
    for (unsigned i=0;i<count;++i) {
        stream->readEnterSection(i);
        const int key=stream->readUint16("key");
        if (unsigned(key)>=limit || fields.contains(key)) throw std::runtime_error("Invalid resource policy key");
        auto& field=fields[key];
        field.serving=Capture::load(stream,version);
        // These historical admission values are not unified policy state. Read
        // their exact wire layout, retaining only data needed for validation.
        stream->readUint32("submitted");
        stream->readUint32("queryTick");
        const auto requested=stream->readUint8("requested"),known=stream->readUint8("queryKnown");
        if (requested>1 || known>1) throw std::runtime_error("Invalid resource policy flags");
        if (version>=FILE_FORMAT_VERSION_GRADIENT_CONTRADICTIONS) {
            stream->readEnterSection("movementTotals");
            if (!loadMovement(stream,field.movementTotal)) field.serving.contradictionKnown=false;
            stream->readLeaveSection();
        }
        // Lifetime wrap would require over 2^64 queries; corrupt baselines must
        // not become enormous unsigned deltas or pass pending-capture checks.
        if (!field.validMovementCapture(field.serving)) throw std::runtime_error("Invalid contradiction capture baseline");
        const auto& current=sources[(key/SWIM_CLASS_COUNT)%MaterialCount];
        if (field.serving.sources.added>current.added || field.serving.sources.removed>current.removed)
            throw std::runtime_error("Invalid resource policy source epoch");
        for (unsigned bucket=0;bucket<128;++bucket) {
            stream->readEnterSection(bucket);
            const auto queries=stream->readUint32("queries");
            stream->readLeaveSection();
            if (queries>1000000 || (!known && queries)) throw std::runtime_error("Invalid resource query count");
        }
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
    stream->readLeaveSection();
}
