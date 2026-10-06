// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIDecision.h"
#include "AIOrderScheduler.h"
#include "Stream.h"
#include <stdexcept>

namespace AIEngine
{
inline void saveExecutionReceipt(GAGCore::OutputStream& stream, const ExecutionReceipt& receipt)
{
    const auto& id=receipt.request;
    stream.writeUint32(id.player,"player");stream.writeUint32(id.generation,"generation");
    stream.writeUint32(id.observedTick,"observedTick");
    stream.writeUint32(Uint32(id.pollSequence),"sequenceLow");stream.writeUint32(Uint32(id.pollSequence>>32),"sequenceHigh");
    stream.writeUint32(id.actionOrdinal,"ordinal");stream.writeUint8(unsigned(receipt.status),"status");
    stream.writeUint32(receipt.executionTick,"executionTick");stream.writeUint32(receipt.scheduledTick,"scheduledTick");
    stream.writeUint32(receipt.command.size(),"bytes");
    for(unsigned i=0;i<receipt.command.size();++i){stream.writeEnterSection(i);stream.writeUint8(receipt.command[i],"byte");stream.writeLeaveSection();}
    stream.writeUint8(receipt.selectedTarget.has_value(),"hasTarget");
    if(receipt.selectedTarget){stream.writeUint16(receipt.selectedTarget->gid,"gid");stream.writeUint32(receipt.selectedTarget->generation,"targetGeneration");}
}
inline ExecutionReceipt loadExecutionReceipt(GAGCore::InputStream& stream)
{
    ExecutionReceipt receipt;auto& id=receipt.request;
    id.player=stream.readUint32("player");id.generation=stream.readUint32("generation");id.observedTick=stream.readUint32("observedTick");
    const auto low=stream.readUint32("sequenceLow"),high=stream.readUint32("sequenceHigh");id.pollSequence=Uint64(low)|(Uint64(high)<<32);
    id.actionOrdinal=stream.readUint32("ordinal");const auto status=stream.readUint8("status");
    if(status>unsigned(ExecutionStatus::Canceled))throw std::runtime_error("Invalid saved AI receipt status");
    receipt.status=ExecutionStatus(status);receipt.executionTick=stream.readUint32("executionTick");receipt.scheduledTick=stream.readUint32("scheduledTick");
    const auto count=stream.readCount("bytes",16*1024*1024);receipt.command.resize(count);
    for(unsigned i=0;i<count;++i){stream.readEnterSection(i);receipt.command[i]=stream.readUint8("byte");stream.readLeaveSection();}
    const auto target=stream.readUint8("hasTarget");if(target>1)throw std::runtime_error("Invalid saved AI receipt target marker");
    if(target)receipt.selectedTarget=BuildingRef{stream.readUint16("gid"),stream.readUint32("targetGeneration")};
    Command command;command.bytes=receipt.command;auto order=command.decode();
    const auto gid=Command::targetGid(*order);
    if(gid.has_value()!=receipt.selectedTarget.has_value() || (gid && *gid!=receipt.selectedTarget->gid))
        throw std::runtime_error("Invalid saved AI receipt target");
    return receipt;
}
}
