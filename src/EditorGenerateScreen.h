// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EditorLoadScreen.h"
#include "MapGenerationDescriptor.h"
class EditorGenerateScreen : public EditorLoadScreen
{
public:
    EditorGenerateScreen(MapGenerationDescriptor descriptor, Uint32 seed);
};
