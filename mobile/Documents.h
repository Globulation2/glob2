// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ApplicationHost.h>
#include <cstddef>

namespace MobileDocuments {
using Request = std::uint64_t;
constexpr std::size_t maximumBytes = 64 * 1024 * 1024;
std::unique_ptr<GAGCore::ApplicationHost::FileSelection> select(const std::string& extension);
// Callbacks may arrive on platform worker threads, including after cancellation.
void complete(Request request, GAGCore::ApplicationHost::FileSelectionState state,
              GAGCore::ApplicationHost::SelectedFile file = {});
bool exportFile(const std::string& name, const std::vector<unsigned char>& bytes, const std::string& error);
// Platform implementations own their UI and a copy of export bytes until completion.
bool platformOpen(Request request, const std::string& extension);
void platformCancel(Request request);
void cleanupTemporaryExports();
bool platformExport(const std::string& name, const std::vector<unsigned char>& bytes, const std::string& error);
}
