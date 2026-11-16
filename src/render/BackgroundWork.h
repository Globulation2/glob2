// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>

// A few presentation workers for composition that the graphics thread never
// waits on. Tasks read only immutable snapshots or inputs copied for them and
// write only storage they own; the graphics thread collects finished results
// on a later frame. Hosts without threads run each task when it is submitted.
namespace BackgroundWork
{
// Work for what the view shows goes first; work ahead of the camera (pages
// around and beyond the view) runs only while no visible work waits.
// Small work that everything else waits on (such as the overview's whole-map
// base) goes First, ahead of queued visible work.
enum class Priority
{
	First,
	Visible,
	Ahead
};
// Queues a task; tasks of one priority start in submission order.
void submit(std::function<void()> task, Priority priority = Priority::Visible);
// Worker threads available (zero runs tasks inline).
unsigned workers();
}
