// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/observation/OrderSelection.h"
#include "FileFormatVersions.h"

namespace AIEngine
{
void loadSelectedTarget(GAGCore::InputStream& stream, Order& order, Sint32 versionMinor)
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
