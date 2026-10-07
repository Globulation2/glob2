// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ai/engine/AIOrderScheduler.h"
#include "Order.h"
#include "FileFormatVersions.h"
#include <Stream.h>
#include <stdexcept>

namespace AIEngine
{
// Selection belongs to the command, so later observations cannot rebind a
// queued GID to a replacement occupying the same slot. Zero stays invalid.
inline void selectTarget(Order& order, const AIWorldView& world)
{
    if(order.aiSelectedTarget) return;
    if(const auto gid=Command::targetGid(order)) {
        const auto* building=world.buildingAtSlot(*gid);
        order.aiSelectedTarget=building ? building->identity : BuildingRef{*gid,0};
    }
}
inline bool selectedTargetExists(const Order& order, const AIWorldView& world)
{
    return !order.aiSelectedTarget || (order.aiSelectedTarget->generation && world.building(*order.aiSelectedTarget));
}
inline void saveSelectedTarget(GAGCore::OutputStream& stream, const Order& order)
{
    stream.writeUint8(order.aiSelectedTarget.has_value(),"hasSelectedTarget");
    if(order.aiSelectedTarget) {
        stream.writeUint16(order.aiSelectedTarget->gid,"selectedTargetGid");
        stream.writeUint32(order.aiSelectedTarget->generation,"selectedTargetGeneration");
    }
}
inline void loadSelectedTarget(GAGCore::InputStream& stream, Order& order, Sint32 versionMinor)
{
    order.aiSelectedTarget.reset();
    if(versionMinor<FILE_FORMAT_VERSION_AI_PIPELINE) return;
    const auto present=stream.readUint8("hasSelectedTarget");
    if(present>1) throw std::runtime_error("Invalid saved AI order selection flag");
    if(!present) return;
    const BuildingRef selected{stream.readUint16("selectedTargetGid"),stream.readUint32("selectedTargetGeneration")};
    const auto gid=Command::targetGid(order);
    if(!gid || *gid!=selected.gid) throw std::runtime_error("Saved AI selection does not match order target");
    order.aiSelectedTarget=selected;
}
}
