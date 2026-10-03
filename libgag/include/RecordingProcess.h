// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
namespace GAGCore::Recording
{
// Shell-free child with private stdin and a diagnostic log. All calls on a worker.
class Process
{
  public:
	Process();
	~Process();
	void launch(const std::vector<std::string> &args, const std::string &log, bool input);
	void write(const void *data, std::size_t bytes);
	int finish(int timeoutSeconds = 15);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace GAGCore::Recording
