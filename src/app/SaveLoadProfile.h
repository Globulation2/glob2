// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FileFormatVersions.h"

// Private adaptive experiments reused the published building draft's number.
// Their binary headers have no unambiguous lineage tag. Only an explicit
// migration operation may select that layout; ordinary loading stays published.
enum class SaveLoadProfile { Published, PrivateAdaptive174 };
// File conversion preserves the encoded map. Normal gameplay performs its
// historical one-time building initialization when that map is opened to play.
enum class MapLoadInitialization { Normal, PreserveEncoded };
inline constexpr bool hasAdaptiveGradientState(unsigned version,bool privateAdaptive174=false) {
    return privateAdaptive174 || version>=FILE_FORMAT_VERSION_GRADIENT_DEMAND;
}
