// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>

// A few presentation workers for composition that the graphics thread never
// waits on. Tasks read only immutable snapshots or inputs copied for them and
// write only storage they own; the graphics thread collects finished results
// on a later frame. Hosts without threads run each task when it is submitted.
namespace BackgroundWork
{
// Queues a task; tasks start in submission order.
void submit(std::function<void()> task);
// Worker threads available (zero runs tasks inline).
unsigned workers();
}
