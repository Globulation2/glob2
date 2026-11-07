// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private adaptive experiments reused the published building draft's number.
// Their binary headers have no unambiguous lineage tag. Only an explicit
// migration operation may select that layout; ordinary loading stays published.
enum class SaveLoadProfile { Published, PrivateAdaptive174 };
// File conversion preserves the encoded map. Normal gameplay performs its
// historical one-time building initialization when that map is opened to play.
enum class MapLoadInitialization { Normal, PreserveEncoded };
// Whether a save of this format carries adaptive gradient state. Defined in
// SaveLoadProfile.cpp so this header stays clear of the format-version list.
bool hasAdaptiveGradientState(unsigned version,bool privateAdaptive174=false);
