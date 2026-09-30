// SPDX-License-Identifier: GPL-3.0-or-later
// BaseTeam::load constructs a Race only for pre-73 streams, which no unit test writes.
#include "Race.h"
#include <Stream.h>

Race::Race() {}
Race::~Race() {}
bool Race::load(GAGCore::InputStream*, Sint32) { return true; }
