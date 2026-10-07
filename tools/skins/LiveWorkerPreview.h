// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace GAGCore {
class GraphicContext;
}
int verifyLiveWorker(const std::string &assets, const std::string &output);
int previewLiveWorker(GAGCore::GraphicContext &gfx, const std::string &assets,
                      const std::string &output, const std::string &mode);
