// SPDX-License-Identifier: GPL-3.0-or-later
#include "SaveLoadProfile.h"
#include "FileFormatVersions.h"

bool hasAdaptiveGradientState(unsigned version,bool privateAdaptive174)
{
    return privateAdaptive174 || version>=FILE_FORMAT_VERSION_GRADIENT_DEMAND;
}
