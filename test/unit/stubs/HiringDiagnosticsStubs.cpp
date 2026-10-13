// SPDX-License-Identifier: GPL-3.0-or-later
// Pure field-kernel tests have no simulation owner or hiring collector.
#include "HiringDiagnostics.h"
namespace HiringDiagnostics
{
thread_local Row* active=nullptr;
std::atomic<bool> collecting{false};
Scope::Scope(Phase value):phase(value) {}
Scope::~Scope()=default;
}
