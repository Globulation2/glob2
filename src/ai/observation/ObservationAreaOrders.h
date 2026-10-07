// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Brush.h"
#include "Order.h"
#include <memory>

namespace AIEngine
{
// Keep the accumulator's wrapped bounding box verbatim. Replacing it with a
// full-map mask changes packet bytes and can exceed the order codec's side cap.
template<class AreaOrder>
std::shared_ptr<Order> observationAreaOrder(Uint8 team,Uint8 mode,BrushAccumulator& accumulator)
{
    Utilities::BitArray mask;
    BrushAccumulator::AreaDimensions dimensions;
    accumulator.getBitmap(&mask,&dimensions);
    auto order=std::make_shared<AreaOrder>(team,mode,dimensions.centerX,dimensions.centerY,
        dimensions.maxX-dimensions.minX,dimensions.maxY-dimensions.minY,mask);
    order->minX=dimensions.minX;order->minY=dimensions.minY;
    order->maxX=dimensions.maxX;order->maxY=dimensions.maxY;
    return order;
}
}
