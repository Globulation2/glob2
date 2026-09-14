// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EditorLoadScreen.h"
#include "GenerationRequest.h"
class EditorGenerateScreen : public EditorLoadScreen
{
public:
    void onTimer(Uint32 tick) override;
    EditorGenerateScreen(GenerationRequest descriptor, Uint32 seed, GAGCore::CooperativeSlice slice = GAGCore::CooperativeSlice());
private:
    bool presented = false;
};
