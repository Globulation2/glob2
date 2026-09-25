// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HostViewport.h>
#include <InterfacePresentation.h>
namespace GAGCore {
inline SafeInsets presentationSafeInsets(GraphicContext* gfx) {
    auto insets=mobileSafeInsets(gfx);const double scale=gfx->getUiScale();
    insets.left/=scale;insets.top/=scale;insets.right/=scale;insets.bottom/=scale;
    return insets;
}
inline ViewRect mobileDialogSafe(GraphicContext* gfx) {
    const double unit=gfx->logicalUnitsPerPoint();const auto i=presentationSafeInsets(gfx);
    ViewRect safe{i.left*unit,i.top*unit,std::max(0.0,gfx->getW()-(i.left+i.right)*unit),std::max(0.0,gfx->getH()-(i.top+i.bottom)*unit)};
    safe.h=std::max(0.0,std::min(safe.y+safe.h,gfx->getH()-mobileKeyboardInset(gfx)*unit/gfx->getUiScale())-safe.y);
    return safe;
}
}
