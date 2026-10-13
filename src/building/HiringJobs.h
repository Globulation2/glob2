// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/EntityRef.h"
namespace Hiring
{
enum class Job : unsigned { Delivery, Fetch, Clear, Explore, Defend };
struct Key { Job job; int material = -1; bool operator==(const Key&) const = default; };
struct Qualified { UnitRef unit; Key key; };
}
