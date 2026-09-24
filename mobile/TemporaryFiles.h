// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>
namespace MobileTemporaryFiles {
// Best-effort startup cleanup. Only recognized temporary names owned by dead
// processes are eligible. Never follows symlinks or deletes recovery generations.
std::size_t cleanup(const std::string& profile);
std::size_t cleanupExports(const std::string& temporaryDirectory);
}
