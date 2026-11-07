// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ai/engine/AIOrderScheduler.h"
#include "Order.h"
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
// Defined in OrderSelection.cpp, which owns the format-version gate.
void loadSelectedTarget(GAGCore::InputStream& stream, Order& order, Sint32 versionMinor);
}
