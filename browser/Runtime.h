// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace Glob2Browser {
// Set only on the application thread; scheduled hosts outlive the entry point.
extern thread_local bool hosted;
void completed(int result);
void releaseApplicationThread();
}
