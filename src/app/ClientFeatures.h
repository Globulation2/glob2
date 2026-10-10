// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glob2/BuildConfig.h>

namespace ClientFeatures
{
inline constexpr bool Commander = GLOB2_FEATURE_COMMANDER;
inline constexpr bool AuthoringLinks = GLOB2_FEATURE_AUTHORING_LINKS;
inline constexpr bool CommunityAI = GLOB2_FEATURE_COMMUNITY_AI;
inline constexpr bool CommunityGenerators = GLOB2_FEATURE_COMMUNITY_GENERATORS;
}
