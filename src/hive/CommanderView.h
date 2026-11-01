// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
#include <nlohmann/json.hpp>
#include <map>
#include <vector>
namespace Hive
{
class CommanderView
{
  public:
	virtual ~CommanderView() = default;
	std::vector<std::string> reports;
	std::map<std::string, std::string> controlStatus;
	std::string progress;
	virtual bool available() const = 0;
	virtual bool local() const { return false; }
	virtual Json standingOrders() const = 0;
	virtual void stop() = 0;
	virtual void change(const std::string &id, const std::string &action) = 0;
};
} // namespace Hive
