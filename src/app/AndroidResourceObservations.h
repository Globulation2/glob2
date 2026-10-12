// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourcePolicy.h"
#include <cstddef>
namespace resource_policy {
struct AndroidResourceObservations {
    std::optional<bool> batterySaver;
    std::optional<int> rawThermalStatus, recognizedThermalStatus;
};
// Version 1 packet: version, saver (-1 unknown/0 false/1 true), raw thermal.
AndroidResourceObservations decodeAndroidResourceObservations(const int*, std::size_t);
RuntimeConstraints mergeAndroidResourceObservations(RuntimeConstraints, const AndroidResourceObservations&);
// Setup/end observation only. No listener, profile action or pool reconfiguration.
AndroidResourceObservations discoverAndroidResourceObservations();
}
