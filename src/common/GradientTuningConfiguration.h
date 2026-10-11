// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>

namespace glob2
{
// Flip only after the complete automatic-learning configuration is qualified.
// Policy creation and its CPU-accounting registry must share this one default.
inline constexpr bool QualifiedGradientLearningDefault=false;

inline constexpr bool gradientLearningValueRequested(const char* value,
    bool qualifiedDefault=QualifiedGradientLearningDefault) noexcept {
    if(!value || !*value)return qualifiedDefault;
    // Preserve explicit 1/0 and the existing safe-off malformed-value behavior.
    return value[0]=='1' && value[1]=='\0';
}
inline bool gradientLearningRequested() noexcept {
    return gradientLearningValueRequested(std::getenv("GLOB2_GRADIENT_TUNING"));
}
}
